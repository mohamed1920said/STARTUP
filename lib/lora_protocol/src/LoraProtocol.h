#pragma once
#include <PacketTypes.h>
#include <CryptoEngine.h>

/* ---------- wire-size aliases (canonical, never sizeof) ---------- */
constexpr size_t TELEMETRY_WIRE_SIZE = 12;
constexpr size_t COMMAND_WIRE_SIZE   = 8;
constexpr size_t ACK_WIRE_SIZE       = 7;
constexpr size_t HEARTBEAT_WIRE_SIZE = 7;

/* ---------- error codes ---------- */
enum class CryptoResult {
    OK       = 0,
    ERR_KEY  = -1,    // key not set
    ERR_LEN  = -2,    // invalid input length
    ERR_AUTH = -3,    // MIC/authentication failure
    ERR_CRYPTO = -4,  // mbedtls internal error
};

/* ---------- diagnostic macros (enable with -DLORA_PROTO_DEBUG) ---------- */
#ifdef LORA_PROTO_DEBUG
#include <Arduino.h>
static inline void loraProtoHex(const char* tag, const uint8_t* data, size_t len) {
    Serial.printf("[LP] %s: ", tag);
    for (size_t i = 0; i < len; ++i) { Serial.printf("%02X", data[i]); }
    Serial.println();
}
#define LP_LOG(fmt, ...)  Serial.printf("[LP] " fmt "\n", ##__VA_ARGS__)
#define LP_HEX(tag,d,l)   loraProtoHex(tag,d,l)
#else
#define LP_LOG(...)
#define LP_HEX(...)
#endif

/* ---------- LoraCrypto : wraps CryptoEngine with error codes ---------- */
class LoraCrypto {
public:
    LoraCrypto() : _keySet(false) {}

    void setKey(const uint8_t* key, size_t len) {
        if (len == AES128_KEY_SIZE) {
            memcpy(_key, key, AES128_KEY_SIZE);
            _keySet = true;
        }
    }

    bool isKeySet() const { return _keySet; }

    /** Build 12-byte nonce: [0,0,node_id_big16,seq_big64]. */
    static void buildNonce(uint8_t* nonce, uint16_t nodeId, uint64_t sequence) {
        memset(nonce, 0, GCM_IV_SIZE);
        nonce[2] = uint8_t(nodeId >> 8);
        nonce[3] = uint8_t(nodeId & 0xFF);
        for (int i = 0; i < 8; ++i) {
            nonce[GCM_IV_SIZE - 1 - i] = uint8_t(sequence & 0xFF);
            sequence >>= 8;
        }
    }

    /** Build 7-byte AAD: [node_id_big16, pkt_type, seq_big32]. */
    static void buildAAD(uint8_t* aad, uint16_t nodeId, uint8_t pktType, uint32_t sequence) {
        aad[0] = uint8_t(nodeId >> 8);
        aad[1] = uint8_t(nodeId & 0xFF);
        aad[2] = pktType;
        aad[3] = uint8_t(sequence >> 24);
        aad[4] = uint8_t(sequence >> 16);
        aad[5] = uint8_t(sequence >> 8);
        aad[6] = uint8_t(sequence & 0xFF);
    }

    /**
     * Encrypt plaintext in-place. Uses LoraFrame internally (CryptoEngine API).
     * @param pt          [in/out] plaintext buffer; ciphertext written in-place
     * @param ptLen       plaintext / ciphertext length
     * @param pktType     packet type byte (for AAD)
     * @param sequence    64-bit sequence number (for nonce)
     * @param nodeId      16-bit node identifier
     * @param outIv       output 12-byte IV/nonce
     * @param outMic      output 4-byte truncated MIC
     */
    CryptoResult encrypt(uint8_t* pt, size_t ptLen,
                         uint8_t pktType, uint64_t sequence, uint16_t nodeId,
                         uint8_t* outIv, uint8_t* outMic) {
        if (!_keySet) return CryptoResult::ERR_KEY;
        if (ptLen > LORA_MAX_CIPHERTEXT) return CryptoResult::ERR_LEN;

        LoraFrame frame;
        CryptoEngine engine;
        if (!engine.begin(_key, AES128_KEY_SIZE)) return CryptoResult::ERR_CRYPTO;
        if (!engine.encrypt(pt, ptLen, pktType, uint32_t(sequence & 0xFFFFFFFF), nodeId, frame))
            return CryptoResult::ERR_CRYPTO;

        memcpy(outIv, frame.iv, GCM_IV_SIZE);
        memcpy(outMic, frame.mic, MIC_SIZE);

        LP_HEX("enc-iv", outIv, GCM_IV_SIZE);
        LP_HEX("enc-mic", outMic, MIC_SIZE);
        return CryptoResult::OK;
    }

    /**
     * Decrypt ciphertext in-place. Uses LoraFrame internally (CryptoEngine API).
     * @param ct          [in/out] ciphertext buffer; plaintext written in-place
     * @param ctLen       ciphertext / plaintext length
     * @param pktType     packet type byte (for AAD)
     * @param sequence    64-bit sequence number (for nonce)
     * @param nodeId      16-bit node identifier
     * @param iv          12-byte IV from received frame
     * @param mic         4-byte MIC from received frame
     */
    CryptoResult decrypt(uint8_t* ct, size_t ctLen,
                         uint8_t pktType, uint64_t sequence, uint16_t nodeId,
                         const uint8_t* iv, const uint8_t* mic) {
        if (!_keySet) return CryptoResult::ERR_KEY;
        if (ctLen > LORA_MAX_CIPHERTEXT || ctLen == 0) return CryptoResult::ERR_LEN;

        LoraFrame frame;
        memcpy(frame.iv, iv, GCM_IV_SIZE);
        memcpy(frame.mic, mic, MIC_SIZE);
        frame.ciphertext_len = ctLen;

        LP_HEX("dec-iv", iv, GCM_IV_SIZE);
        LP_HEX("dec-mic", mic, MIC_SIZE);

        CryptoEngine engine;
        if (!engine.begin(_key, AES128_KEY_SIZE)) return CryptoResult::ERR_CRYPTO;
        if (!engine.decrypt(ct, ctLen, pktType, uint32_t(sequence & 0xFFFFFFFF), nodeId, frame))
            return CryptoResult::ERR_AUTH;

        return CryptoResult::OK;
    }

private:
    uint8_t _key[AES128_KEY_SIZE];
    bool    _keySet;
};
