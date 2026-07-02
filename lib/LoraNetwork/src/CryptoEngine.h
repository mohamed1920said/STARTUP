#pragma once
#include "PacketTypes.h"

class CryptoEngine {
public:
    CryptoEngine();
    bool begin(const uint8_t* key, size_t len);
    bool encrypt(uint8_t* plaintext, size_t plaintext_len,
                 uint8_t pkt_type, uint32_t sequence,
                 NodeId node_id, LoraFrame& frame);
    bool decrypt(uint8_t* ciphertext, size_t ciphertext_len,
                 uint8_t pkt_type, uint32_t sequence,
                 NodeId node_id, const LoraFrame& frame);
    static bool deriveSessionKey(const uint8_t* psk, NodeId node_id,
                                 uint8_t* session_key_out);
    static void buildNonce(NodeId node_id, uint32_t sequence,
                           uint8_t* nonce, size_t nonce_len);
    static void buildAAD(NodeId node_id, uint8_t pkt_type,
                         uint32_t sequence, uint8_t* aad);
private:
    uint8_t _key[AES128_KEY_SIZE];
    bool _initialized = false;
};
