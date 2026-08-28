#include "CryptoEngine.h"
#include <cstring>
#include <algorithm>
#include <mbedtls/gcm.h>
#include <mbedtls/aes.h>

CryptoEngine::CryptoEngine() {
    std::memset(_key, 0, sizeof(_key));
}

bool CryptoEngine::begin(const uint8_t* psk, size_t len) {
    if (len != AES128_KEY_SIZE) return false;
    std::memcpy(_key, psk, AES128_KEY_SIZE);
    _initialized = true;
    return true;
}

void CryptoEngine::buildNonce(NodeId node_id, uint8_t pkt_type, uint32_t sequence,
                               uint8_t* nonce, size_t nonce_len) {
    std::memset(nonce, 0, nonce_len);
    nonce[0] = pkt_type;
    nonce[2] = static_cast<uint8_t>(node_id >> 8);
    nonce[3] = static_cast<uint8_t>(node_id & 0xFF);
    for (int i = 0; i < 8; ++i) {
        nonce[nonce_len - 1 - i] = static_cast<uint8_t>(sequence & 0xFF);
        sequence >>= 8;
    }
}

void CryptoEngine::buildAAD(NodeId node_id, uint8_t pkt_type,
                             uint32_t sequence, uint8_t* aad) {
    aad[0] = static_cast<uint8_t>(node_id >> 8);
    aad[1] = static_cast<uint8_t>(node_id & 0xFF);
    aad[2] = pkt_type;
    aad[3] = static_cast<uint8_t>(sequence >> 24);
    aad[4] = static_cast<uint8_t>(sequence >> 16);
    aad[5] = static_cast<uint8_t>(sequence >> 8);
    aad[6] = static_cast<uint8_t>(sequence & 0xFF);
}

bool CryptoEngine::encrypt(uint8_t* plaintext, size_t plaintext_len,
                            uint8_t pkt_type, uint32_t sequence,
                            NodeId node_id, LoraFrame& frame) {
    if (!_initialized || plaintext_len > LORA_MAX_CIPHERTEXT) return false;
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES,
                                  _key, AES128_KEY_SIZE * 8);
    if (ret != 0) { mbedtls_gcm_free(&gcm); return false; }
    uint8_t nonce[GCM_IV_SIZE];
    buildNonce(node_id, pkt_type, sequence, nonce, GCM_IV_SIZE);
    std::memcpy(frame.iv, nonce, GCM_IV_SIZE);
    uint8_t aad[7];
    buildAAD(node_id, pkt_type, sequence, aad);
    uint8_t tag[GCM_TAG_SIZE];
    ret = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT,
                                     plaintext_len, nonce, GCM_IV_SIZE,
                                     aad, sizeof(aad), plaintext, plaintext,
                                     GCM_TAG_SIZE, tag);
    if (ret != 0) { mbedtls_gcm_free(&gcm); return false; }
    std::memcpy(frame.mic, tag, GCM_TAG_TRUNCATED);
    frame.ciphertext_len = plaintext_len;
    mbedtls_gcm_free(&gcm);
    return true;
}

bool CryptoEngine::decrypt(uint8_t* ciphertext, size_t ciphertext_len,
                            uint8_t pkt_type, uint32_t sequence,
                            NodeId node_id, const LoraFrame& frame) {
    if (!_initialized) return false;
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES,
                                  _key, AES128_KEY_SIZE * 8);
    if (ret != 0) { mbedtls_gcm_free(&gcm); return false; }
    uint8_t aad[7];
    buildAAD(node_id, pkt_type, sequence, aad);
    ret = mbedtls_gcm_auth_decrypt(&gcm, ciphertext_len,
                                     frame.iv, GCM_IV_SIZE,
                                     aad, sizeof(aad),
                                     frame.mic, GCM_TAG_TRUNCATED,
                                     ciphertext, ciphertext);
    mbedtls_gcm_free(&gcm);
    return (ret == 0);
}

bool CryptoEngine::deriveSessionKey(const uint8_t* psk, NodeId node_id,
                                     uint8_t* session_key_out) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    int ret = mbedtls_aes_setkey_enc(&aes, psk, AES128_KEY_SIZE * 8);
    if (ret != 0) { mbedtls_aes_free(&aes); return false; }
    uint8_t input[AES128_BLOCK_SIZE];
    std::memset(input, 0, AES128_BLOCK_SIZE);
    input[0] = static_cast<uint8_t>(node_id >> 8);
    input[1] = static_cast<uint8_t>(node_id & 0xFF);
    ret = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, input, session_key_out);
    mbedtls_aes_free(&aes);
    return (ret == 0);
}
