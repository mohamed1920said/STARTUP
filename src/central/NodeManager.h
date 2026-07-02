#pragma once
#include <Arduino.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <LoraNetwork.h>

struct NodeInfo {
    uint16_t id;
    uint8_t  type;
    uint8_t  sessionKey[16];
    uint8_t  psk[16];
    bool     registered;
    uint32_t lastSeq;
    uint32_t lastSeen;
    char     alias[24];
};

using NodeCmdCb = void(*)(uint16_t nodeId, const uint8_t* data, size_t len);

class NodeManager {
public:
    NodeManager();
    void begin();
    bool provision(uint16_t id, uint8_t type, const uint8_t psk[16], const char* alias);
    void handlePacket(uint16_t nodeId, PacketType type,
                      const uint8_t* plaintext, size_t len, uint32_t seq);
    bool sendCmd(uint16_t nodeId, const uint8_t* data, size_t len, uint32_t seq);
    void setCmdCallback(NodeCmdCb cb) { _cmdCb = cb; }
    int count() const { return _count; }
    const NodeInfo* getNode(int i) const { return (i < _count) ? &_nodes[i] : nullptr; }
    const uint8_t* getPsk(uint16_t id) const {
        for (int i = 0; i < _count; i++)
            if (_nodes[i].registered && _nodes[i].id == id) return _nodes[i].psk;
        return nullptr;
    }
    std::string toJson() const;

private:
    static constexpr int MAX_NODES = 16;
    NodeInfo _nodes[MAX_NODES];
    int _count;
    NodeCmdCb _cmdCb;
    NodeInfo* find(uint16_t id);
    void saveNVS(const NodeInfo& n);
    void loadNVS();
};
