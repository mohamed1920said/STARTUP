#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include "PacketTypes.h"
#include "CryptoEngine.h"

class LoRadio {
public:
    virtual void begin(float frequency_mhz) = 0;
    virtual void send(const uint8_t* buf, size_t len) = 0;
    virtual void startReceive() = 0;
    virtual void setRxTimeout(uint32_t ms) = 0;
    virtual void sleep() = 0;
    virtual void onReceive(std::function<void(const uint8_t*, size_t)> callback) = 0;
};

using OnPacketDecrypted = std::function<void(NodeId node_id, PacketType type,
                                              const uint8_t* plaintext,
                                              size_t len, uint32_t seq)>;
using OnPacketFailed = std::function<void(NodeId node_id, PacketType type,
                                           uint32_t seq, const char* reason)>;

class LoraProtocolManager {
public:
    LoraProtocolManager(LoRadio& radio, CryptoEngine& crypto);

    void begin(float frequency_mhz = 868.0f);
    void process();

    bool sendTo(NodeId target, PacketType type,
                const uint8_t* plaintext, size_t plaintext_len,
                uint32_t sequence);
    bool sendRaw(NodeId target, PacketType type,
                 const uint8_t* data, size_t data_len);

    void onPacketDecrypted(OnPacketDecrypted cb) { _decrypted_cb = cb; }
    void onPacketFailed(OnPacketFailed cb)        { _failed_cb = cb; }

    void registerNodeKey(NodeId node_id, const uint8_t* session_key);
    void removeNodeKey(NodeId node_id);

private:
    LoRadio&          _radio;
    CryptoEngine&     _crypto;
    OnPacketDecrypted _decrypted_cb;
    OnPacketFailed    _failed_cb;

    static constexpr size_t MAX_NODES = 16;
    struct NodeKeyEntry {
        NodeId  id;
        uint8_t key[AES128_KEY_SIZE];
        bool    active = false;
        uint32_t last_seq;
    };
    NodeKeyEntry _node_keys[MAX_NODES];

    size_t nodeIndex(NodeId id) const { return (id & 0x0F) % MAX_NODES; }
    NodeKeyEntry* findNode(NodeId id);
    const NodeKeyEntry* findNode(NodeId id) const;

    uint8_t _rx_buf[LORA_MAX_PAYLOAD];
    size_t  _rx_len = 0;

    void handleRxFrame(const uint8_t* buf, size_t len);
};
