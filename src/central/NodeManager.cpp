#include "NodeManager.h"
#include <cstring>

NodeManager::NodeManager() : _count(0) {
    for (auto& n : _nodes) n.registered = false;
}

void NodeManager::begin() { loadNVS(); }

bool NodeManager::provision(uint16_t id, uint8_t type,
                             const uint8_t psk[16], const char* alias) {
    NodeInfo* n = find(id);
    if (!n) {
        if (_count >= MAX_NODES) return false;
        n = &_nodes[_count++];
    }
    n->id = id;
    n->type = type;
    memcpy(n->psk, psk, 16);
    CryptoEngine::deriveSessionKey(psk, id, n->sessionKey);
    n->registered = true;
    n->lastSeq = 0;
    n->lastSeen = millis();
    n->autoMode = false;
    n->threshold = 50;
    n->sensorId = 0;
    n->valveOpen = false;
    strncpy(n->alias, alias, 23); n->alias[23] = 0;
    saveNVS(*n);
    return true;
}

void NodeManager::handlePacket(uint16_t nodeId, PacketType type,
                                const uint8_t* pt, size_t len, uint32_t seq) {
    NodeInfo* n = find(nodeId);
    if (!n || !n->registered) return;
    n->lastSeen = millis();
    n->lastSeq = seq;
}

bool NodeManager::remove(uint16_t id) {
    nvs_handle_t h;
    if (nvs_open("node_db", NVS_READWRITE, &h) != ESP_OK) return false;
    char key[16]; snprintf(key, sizeof(key), "n_%04X", id);
    nvs_erase_key(h, key); nvs_commit(h); nvs_close(h);
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
    NodeInfo* n = find(id);
    if (!n || !n->registered) return false;
    n->autoMode = autoMode;
    n->threshold = constrain(threshold, 0, 100);
    n->sensorId = sensorId;
    saveNVS(*n);
    return true;
}

void NodeManager::setValveState(uint16_t id, bool open) {
    NodeInfo* n = find(id);
    if (n) n->valveOpen = open;
}

NodeInfo* NodeManager::find(uint16_t id) {
    for (int i = 0; i < _count; i++)
        if (_nodes[i].registered && _nodes[i].id == id) return &_nodes[i];
    return nullptr;
}

void NodeManager::saveNVS(const NodeInfo& info) {
    nvs_handle_t h;
    if (nvs_open("node_db", NVS_READWRITE, &h) != ESP_OK) return;
    char key[16]; snprintf(key, sizeof(key), "n_%04X", info.id);
    nvs_set_blob(h, key, &info, sizeof(NodeInfo));
    nvs_commit(h); nvs_close(h);
}

std::string NodeManager::toJson() const {
    std::string json = "[";
    for (int i = 0; i < _count; i++) {
        if (i > 0) json += ",";
        char buf[256];
        snprintf(buf, sizeof(buf),
            R"({"id":%u,"type":%u,"alias":"%s","registered":%s,"lastSeq":%u,"lastSeen":%u,"autoMode":%s,"threshold":%u,"sensorId":%u})",
            _nodes[i].id, _nodes[i].type, _nodes[i].alias,
            _nodes[i].registered ? "true" : "false",
            _nodes[i].lastSeq, _nodes[i].lastSeen,
            _nodes[i].autoMode ? "true" : "false",
            _nodes[i].threshold, _nodes[i].sensorId);
        json += buf;
    }
    json += "]";
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
            if (nvs_get_blob(h, ei.key, &ni, &sz) == ESP_OK) {
                _nodes[_count].autoMode = false;
                _nodes[_count].threshold = 50;
                _nodes[_count].sensorId = 0;
                _nodes[_count].valveOpen = false;
                _count++;
            }
        }
        it = nvs_entry_next(it);
    }
    nvs_close(h);
}
