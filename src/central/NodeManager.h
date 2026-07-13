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
    bool     autoMode;
    uint8_t  threshold;
    uint16_t sensorId;
    bool     valveOpen;
};

class NodeManager {
public:
    NodeManager();
    void begin();
    bool provision(uint16_t id, uint8_t type, const uint8_t psk[16], const char* alias);
    bool remove(uint16_t id);
    bool setActuatorConfig(uint16_t id, bool autoMode, uint8_t threshold, uint16_t sensorId);
    void setValveState(uint16_t id, bool open);
    void handlePacket(uint16_t nodeId, PacketType type,
                      const uint8_t* plaintext, size_t len, uint32_t seq);
    int count() const { return _count; }
    const NodeInfo* getNode(int i) const { return (i < _count) ? &_nodes[i] : nullptr; }
    const uint8_t* getPsk(uint16_t id) const {
        for (int i = 0; i < _count; i++)
            if (_nodes[i].registered && _nodes[i].id == id) return _nodes[i].psk;
        return nullptr;
    }
    std::string toJson() const;
    static constexpr int MAX_NODES = 16;

private:
    NodeInfo _nodes[MAX_NODES];
    int _count;
    NodeInfo* find(uint16_t id);
    void saveNVS(const NodeInfo& n);
    void loadNVS();
};
