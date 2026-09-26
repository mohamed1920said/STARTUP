#include "NodeManager.h"
#include <LoraProtocol.h>
#include <cstring>

#ifndef PILOT_REQUIRE_MANUAL_REARM
#define PILOT_REQUIRE_MANUAL_REARM 1
#endif

static bool keyAllowed(const uint8_t psk[16]) {
    if (!psk) return false;
    bool allZero = true;
    bool allFF = true;
    for (size_t i = 0; i < 16; ++i) {
        allZero = allZero && psk[i] == 0;
        allFF = allFF && psk[i] == 0xFF;
    }
    return !allZero && !allFF;
}

// Escape JSON-special characters in a string (for user-supplied alias)
static void jsonEscape(const char* src, char* dst, size_t dstSz) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dstSz - 1; i++) {
        char c = src[i];
        if (c == '"' || c == '\\') {
            if (j + 2 >= dstSz) break;
            dst[j++] = '\\'; dst[j++] = c;
        } else if (c == '\n' || c == '\r' || c == '\t' || static_cast<unsigned char>(c) < 0x20) {
            if (j + 6 >= dstSz) break;
            snprintf(dst + j, dstSz - j, "\\u%04X", static_cast<unsigned char>(c));
            j += 6;
        } else {
            dst[j++] = c;
        }
    }
    dst[j] = '\0';
}

NodeManager::NodeManager() : _count(0) {
    for (auto& n : _nodes) n.registered = false;
}

void NodeManager::begin() { std::lock_guard<std::recursive_mutex> lock(_mutex); loadNVS(); }

bool NodeManager::provision(uint16_t id, uint8_t type,
                             const uint8_t psk[16], const char* alias) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (!alias || id == 0 || id == 0xFFFF || !keyAllowed(psk) ||
        (type != 0x01 && type != 0x02)) return false;
    // Updating an existing ID would reset its replay counters. Operators must
    // remove it explicitly and use a newly generated PSK for reprovisioning.
    if (find(id)) return false;
    uint8_t derived[16];
    if (!CryptoEngine::deriveSessionKey(psk, id, derived)) return false;
    if (_count >= MAX_NODES) { memset(derived, 0, sizeof(derived)); return false; }
    NodeInfo* n = &_nodes[_count++];
    memset(n, 0, sizeof(*n));
    n->id = id;
    n->type = type;
    memcpy(n->psk, psk, 16);
    memcpy(n->sessionKey, derived, sizeof(derived));
    n->registered = true;
    n->lastSeq = 0;
    // Provisioning is not evidence that the physical node is online. OPEN
    // remains blocked until an authenticated actuator packet arrives.
    n->lastSeen = 0;
    n->autoMode = false;
    n->threshold = 50;
    n->sensorId = 0;
    n->valveOpen = false;
    n->lastTelemetrySeq = 0;
    n->lastAckSeq = 0;
    n->lastHeartbeatSeq = 0;
    strncpy(n->alias, alias, 23); n->alias[23] = 0;
    memset(derived, 0, sizeof(derived));
    if (!saveNVS(*n)) {
        memset(n, 0, sizeof(*n));
        --_count;
        return false;
    }
    return true;
}

bool NodeManager::handlePacket(uint16_t nodeId, PacketType type,
                               const uint8_t* pt, size_t len, uint32_t seq) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    NodeInfo* n = find(nodeId);
    if (!n || !n->registered || seq == 0) return false;
    uint32_t* streamLast = nullptr;
    switch (type) {
        case PacketType::SENSOR_TELEMETRY: {
            if (n->type != 0x01 || !pt || len != TELEMETRY_WIRE_SIZE) return false;
            SensorTelemetry telemetry;
            deserializeTelemetry(pt, telemetry);
            if (telemetry.sequence != seq) return false;
            streamLast = &n->lastTelemetrySeq;
            break;
        }
        case PacketType::ACK:
            if (n->type != 0x02 || !pt || len != ACK_WIRE_SIZE) return false;
            streamLast = &n->lastAckSeq;
            break;
        case PacketType::HEARTBEAT: {
            if (n->type != 0x02 || !pt || len != HEARTBEAT_WIRE_SIZE) return false;
            HeartbeatPayload heartbeat;
            deserializeHeartbeat(pt, heartbeat);
            if (heartbeat.sequence != seq) return false;
            streamLast = &n->lastHeartbeatSeq;
            break;
        }
        default: return false;
    }
    if (seq <= *streamLast) return false;
    const NodeInfo previous = *n;
    *streamLast = seq;
    n->lastSeen = millis();
    n->lastSeq = seq;
    if (!saveNVS(*n)) {
        *n = previous;
        return false;
    }
    return true;
}

bool NodeManager::remove(uint16_t id) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    nvs_handle_t h;
    if (nvs_open("node_db", NVS_READWRITE, &h) != ESP_OK) return false;
    char key[16]; snprintf(key, sizeof(key), "n_%04X", id);
    const esp_err_t erased = nvs_erase_key(h, key);
    const esp_err_t committed = erased == ESP_OK ? nvs_commit(h) : erased;
    nvs_close(h);
    if (erased != ESP_OK || committed != ESP_OK) return false;
    for (int i = 0; i < _count; i++) {
        if (_nodes[i].id == id) {
            for (int j = i; j < _count - 1; j++) _nodes[j] = _nodes[j + 1];
            _count--;
            _nodes[_count].registered = false;
            return true;
        }
    }
    return false;
}

bool NodeManager::setActuatorConfig(uint16_t id, bool autoMode, uint8_t threshold, uint16_t sensorId) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    NodeInfo* n = find(id);
    if (!n || !n->registered || n->type != 0x02) return false;
    if (threshold < 5 || threshold > 95) return false;
    if (autoMode) {
        NodeInfo* sensor = find(sensorId);
        if (!sensor || sensor->type != 0x01) return false;
    }
    const NodeInfo previous = *n;
    n->autoMode = autoMode;
    n->threshold = threshold;
    n->sensorId = sensorId;
    if (!saveNVS(*n)) {
        *n = previous;
        return false;
    }
    return true;
}

void NodeManager::setValveState(uint16_t id, bool open) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    NodeInfo* n = find(id);
    if (n) n->valveOpen = open;
}

NodeInfo* NodeManager::find(uint16_t id) {
    for (int i = 0; i < _count; i++)
        if (_nodes[i].registered && _nodes[i].id == id) return &_nodes[i];
    return nullptr;
}

bool NodeManager::saveNVS(const NodeInfo& info) {
    nvs_handle_t h;
    if (nvs_open("node_db", NVS_READWRITE, &h) != ESP_OK) return false;
    char key[16]; snprintf(key, sizeof(key), "n_%04X", info.id);
    bool saved = nvs_set_blob(h, key, &info, sizeof(NodeInfo)) == ESP_OK &&
        nvs_commit(h) == ESP_OK;
    NodeInfo verify{};
    size_t verifySize = sizeof(verify);
    saved = saved && nvs_get_blob(h, key, &verify, &verifySize) == ESP_OK &&
        verifySize == sizeof(verify) && memcmp(&verify, &info, sizeof(info)) == 0;
    memset(&verify, 0, sizeof(verify));
    nvs_close(h);
    return saved;
}

std::string NodeManager::toJson() const {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    // Pre-calculate approximate size to avoid reallocation
    size_t approx = 2 + _count * 180 + 1;
    std::string json;
    json.reserve(approx);
    json = '[';
    for (int i = 0; i < _count; i++) {
        if (i > 0) json += ',';
        char aliasEsc[144];
        jsonEscape(_nodes[i].alias, aliasEsc, sizeof(aliasEsc));
        char buf[384];
        snprintf(buf, sizeof(buf),
            R"({"id":%u,"type":%u,"alias":"%s","registered":%s,"lastSeq":%u,"lastSeen":%u,"autoMode":%s,"threshold":%u,"sensorId":%u})",
            _nodes[i].id, _nodes[i].type, aliasEsc,
            _nodes[i].registered ? "true" : "false",
            _nodes[i].lastSeq, _nodes[i].lastSeen,
            _nodes[i].autoMode ? "true" : "false",
            _nodes[i].threshold, _nodes[i].sensorId);
        json += buf;
    }
    json += ']';
    return json;
}

void NodeManager::loadNVS() {
    nvs_handle_t h;
    if (nvs_open("node_db", NVS_READONLY, &h) != ESP_OK) return;
    nvs_iterator_t it = nvs_entry_find("nvs", "node_db", NVS_TYPE_BLOB);
    while (it != NULL) {
        nvs_entry_info_t ei; nvs_entry_info(it, &ei);
        if (_count < MAX_NODES) {
            size_t sz = sizeof(NodeInfo);
            NodeInfo& ni = _nodes[_count];
            memset(&ni, 0, sizeof(ni));
            if (nvs_get_blob(h, ei.key, &ni, &sz) == ESP_OK && sz == sizeof(NodeInfo)) {
                ni.alias[sizeof(ni.alias) - 1] = 0;
                uint8_t derived[16];
                char expectedKey[16];
                snprintf(expectedKey, sizeof(expectedKey), "n_%04X", ni.id);
                const bool validRecord = ni.registered && ni.id != 0 && ni.id != 0xFFFF &&
                    (ni.type == 0x01 || ni.type == 0x02) && keyAllowed(ni.psk) &&
                    ni.threshold >= 5 && ni.threshold <= 95 &&
                    strcmp(ei.key, expectedKey) == 0 &&
                    find(ni.id) == nullptr &&
                    CryptoEngine::deriveSessionKey(ni.psk, ni.id, derived) &&
                    memcmp(derived, ni.sessionKey, sizeof(derived)) == 0;
                memset(derived, 0, sizeof(derived));
                if (!validRecord) {
                    memset(&ni, 0, sizeof(ni));
                    it = nvs_entry_next(it);
                    continue;
                }
                // Uptime from a previous boot is not evidence of a live node.
                ni.lastSeen = 0;
#if PILOT_REQUIRE_MANUAL_REARM
                // A supervised pilot must explicitly re-arm automation after
                // every gateway restart and observe fresh node telemetry first.
                ni.autoMode = false;
#endif
                _count++;
            }
        }
        it = nvs_entry_next(it);
    }
    nvs_close(h);
}
