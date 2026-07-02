#include "NodeManager.h"
#include <cstring>

NodeManager::NodeManager() : _count(0), _cmdCb(nullptr) {
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
    if (_cmdCb) _cmdCb(nodeId, pt, len);
}

bool NodeManager::sendCmd(uint16_t nodeId, const uint8_t* data, size_t len, uint32_t seq) {
    // Downlink — protocol layer handles encryption
    // This method would call LoraProtocolManager::sendTo
    // For now, placeholder (integrated in main sketch)
    (void)nodeId; (void)data; (void)len; (void)seq;
    return true;
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

void NodeManager::loadNVS() {
    nvs_handle_t h;
    if (nvs_open("node_db", NVS_READONLY, &h) != ESP_OK) return;
    nvs_iterator_t it = nullptr;
    esp_err_t res = nvs_entry_find("nvs", "node_db", NVS_TYPE_BLOB, &it);
    while (res == ESP_OK) {
        nvs_entry_info_t ei; nvs_entry_info(it, &ei);
        if (_count < MAX_NODES) {
            size_t sz = sizeof(NodeInfo);
            if (nvs_get_blob(h, ei.key, &_nodes[_count], &sz) == ESP_OK)
                _count++;
        }
        res = nvs_entry_next(&it);
    }
    nvs_close(h);
}
