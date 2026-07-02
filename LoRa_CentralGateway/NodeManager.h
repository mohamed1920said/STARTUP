#pragma once
#include <Arduino.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <LoraNetwork.h>

// LoraNetwork.h is a convenience header — in practice include the individual headers:
// PacketTypes.h, CryptoEngine.h, LoraProtocol.h, MqttTopics.h

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
private:
    static constexpr int MAX_NODES = 16;
    NodeInfo _nodes[MAX_NODES];
    int _count;
    NodeCmdCb _cmdCb;
    NodeInfo* find(uint16_t id);
    void saveNVS(const NodeInfo& n);
    void loadNVS();
};
