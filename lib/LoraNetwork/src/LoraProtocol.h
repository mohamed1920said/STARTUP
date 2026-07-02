#pragma once
#include "PacketTypes.h"
#include "CryptoEngine.h"
#include <functional>

class LoRadio {
public:
    virtual void begin(float frequency_mhz) = 0;
    virtual void setRxTimeout(uint32_t ms) = 0;
    virtual void startReceive() = 0;
    virtual bool send(const uint8_t* data, size_t len) = 0;
    using ReceiveCallback = std::function<void(const uint8_t*, size_t)>;
    virtual void onReceive(ReceiveCallback cb) = 0;
    virtual ~LoRadio() {}
};

struct NodeKeyEntry {
    NodeId   id;
    uint8_t  key[AES128_KEY_SIZE];
    bool     active;
    uint32_t last_seq;
};

class LoraProtocolManager {
public:
    using DecryptedCallback = std::function<void(NodeId, PacketType,
                                                  const uint8_t*, size_t, uint32_t)>;
    using FailedCallback = std::function<void(NodeId, PacketType,
                                               uint32_t, const char*)>;
    LoraProtocolManager(LoRadio& radio, CryptoEngine& crypto);
    void begin(float frequency_mhz);
    void process();
    bool sendTo(NodeId target, PacketType type,
                const uint8_t* plaintext, size_t len, uint32_t sequence);
    bool sendRaw(NodeId target, PacketType type,
                 const uint8_t* data, size_t data_len);
    void registerNodeKey(NodeId node_id, const uint8_t* key);
    void removeNodeKey(NodeId node_id);
    void onDecrypted(DecryptedCallback cb) { _decrypted_cb = cb; }
    void onFailed(FailedCallback cb) { _failed_cb = cb; }
private:
    static constexpr int MAX_NODES = 32;
    LoRadio& _radio;
    CryptoEngine& _crypto;
    NodeKeyEntry _node_keys[MAX_NODES];
    DecryptedCallback _decrypted_cb;
    FailedCallback _failed_cb;
    uint8_t _rx_buf[LORA_MAX_PAYLOAD];
    size_t _rx_len = 0;
    void handleRxFrame(const uint8_t* buf, size_t len);
    NodeKeyEntry* findNode(NodeId id);
};
