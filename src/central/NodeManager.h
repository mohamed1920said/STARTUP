#pragma once
#include <Arduino.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <LoraNetwork.h>
#include <mutex>

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
    uint32_t lastTelemetrySeq;
    uint32_t lastAckSeq;
    uint32_t lastHeartbeatSeq;
};

class NodeManager {
public:
    NodeManager();
    void begin();
    bool provision(uint16_t id, uint8_t type, const uint8_t psk[16], const char* alias);
    bool remove(uint16_t id);
    bool setActuatorConfig(uint16_t id, bool autoMode, uint8_t threshold, uint16_t sensorId);
    void setValveState(uint16_t id, bool open);
    bool handlePacket(uint16_t nodeId, PacketType type,
                      const uint8_t* plaintext, size_t len, uint32_t seq);
    int count() const { std::lock_guard<std::recursive_mutex> lock(_mutex); return _count; }
    bool copyNode(int i, NodeInfo& out) const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (i < 0 || i >= _count) return false;
        out = _nodes[i];
        return true;
    }
    bool copyPsk(uint16_t id, uint8_t out[16]) const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        for (int i = 0; i < _count; i++)
            if (_nodes[i].registered && _nodes[i].id == id) {
                memcpy(out, _nodes[i].psk, 16);
                return true;
            }
        return false;
    }
    std::string toJson() const;
    static constexpr int MAX_NODES = 16;

private:
    mutable std::recursive_mutex _mutex;
    NodeInfo _nodes[MAX_NODES];
    int _count;
    NodeInfo* find(uint16_t id);
    bool saveNVS(const NodeInfo& n);
    void loadNVS();
};
