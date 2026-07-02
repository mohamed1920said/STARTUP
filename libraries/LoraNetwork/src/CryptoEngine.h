#pragma once

#include <cstdint>
#include <cstddef>
#include "PacketTypes.h"

constexpr size_t AES128_KEY_SIZE     = 16;
constexpr size_t AES128_BLOCK_SIZE   = 16;
constexpr size_t GCM_IV_SIZE         = 12;
constexpr size_t GCM_TAG_SIZE        = 16;
constexpr size_t GCM_TAG_TRUNCATED   = 4;

class CryptoEngine {
public:
    CryptoEngine();

    bool begin(const uint8_t* psk, size_t len);

    bool encrypt(uint8_t* plaintext, size_t plaintext_len,
                 uint8_t pkt_type, uint32_t sequence,
                 NodeId node_id, LoraFrame& frame);

    bool decrypt(uint8_t* ciphertext, size_t ciphertext_len,
                 uint8_t pkt_type, uint32_t sequence,
                 NodeId node_id, const LoraFrame& frame);

    static bool deriveSessionKey(const uint8_t* psk, NodeId node_id,
                                 uint8_t* session_key_out);
private:
    uint8_t _key[AES128_KEY_SIZE];
    bool    _initialized = false;

    static void buildNonce(NodeId node_id, uint32_t sequence,
                           uint8_t* nonce_out, size_t nonce_len);
    static void buildAAD(NodeId node_id, uint8_t pkt_type,
                         uint32_t sequence, uint8_t* aad_out);
};
