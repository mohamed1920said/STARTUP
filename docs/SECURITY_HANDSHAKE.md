# Security Handshake Protocol — PSK Node Registration

## Overview

Before a node can exchange encrypted telemetry or commands, it must
complete a one-time registration handshake with the Central Gateway.
The handshake uses the provisioned **Pre-Shared Key (PSK)** to
authenticate the node without exposing secret material over the air.

## Flow

```
Node                                      Gateway
  │                                          │
  │ [Provisioned out-of-band:                │
  │  Node_ID=0x0001, PSK=16 bytes]           │
  │                                          │
  │ ── REGISTER_REQ ──────────────────────►  │
  │    Node_ID[2] | Node_Type[1] |           │
  │    Challenge[4]                          │
  │                                          │
  │    Challenge = Truncated_CMAC(           │
  │       PSK, "lora-reg-2026" || Node_ID    │
  │    )[0:4]                                │
  │                                          │
  │    ── Gateway validates:                 │
  │    1. Lookup PSK by Node_ID              │
  │    2. Compute expected_CMAC              │
  │    3. Compare (timing-safe)              │
  │    4. Store session_key in node table    │
  │                                          │
  │ ◄── REGISTER_ACCEPT ──────────────────── │
  │    Node_ID[2] | Status[1]                │
  │    Status: 0x00=OK, 0x01=Rejected        │
  │                                          │
  │ ── (Optional) SESSION_REKEY ──────────►  │
  │    New session key derived from PSK       │
```

## Key Derivation

```text
Session_Key = AES_128_ECB_encrypt(PSK, Node_ID || 0x00...0)
             = mbedtls_aes_crypt_ecb(PSK, input_block)

Where input_block[0..1] = Node_ID (big-endian)
      input_block[2..15] = 0x00
```

## Code Snippet — Gateway-Side Registration Handler

```cpp
#include <mbedtls/cmac.h>
#include <cstring>

// ─── AES-CMAC Truncation (RFC 4493) ─────────────────────────────────
bool computeChallenge(const uint8_t psk[16], uint16_t node_id,
                       uint8_t challenge_out[4]) {
    mbedtls_cipher_context_t ctx;
    mbedtls_cipher_init(&ctx);

    const mbedtls_cipher_info_t* info =
        mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_ECB);
    if (!info) return false;

    mbedtls_cipher_setup(&ctx, info);
    mbedtls_cipher_cmac_starts(&ctx, psk, 128);

    // Message: "lora-reg-2026" || Node_ID (big-endian)
    uint8_t msg[16];
    memcpy(msg, "lora-reg-2026", 13);
    msg[13] = (node_id >> 8) & 0xFF;
    msg[14] = node_id & 0xFF;
    msg[15] = 0x00; // padding block for CMAC

    uint8_t full_cmac[16];
    mbedtls_cipher_cmac_update(&ctx, msg, 15); // 13 + 2 bytes
    mbedtls_cipher_cmac_finish(&ctx, full_cmac);

    mbedtls_cipher_free(&ctx);

    // Truncate to 4 bytes
    memcpy(challenge_out, full_cmac, 4);
    return true;
}

// ─── Registration Handler (called on REGISTER_REQ) ──────────────────
void handleRegistrationRequest(uint16_t req_node_id,
                                uint8_t req_node_type,
                                const uint8_t challenge[4]) {
    // 1. Lookup PSK by Node_ID
    uint8_t psk[16];
    if (!loadPSKfromNVS(req_node_id, psk)) {
        sendReject(req_node_id);  // Unknown node
        return;
    }

    // 2. Compute expected challenge
    uint8_t expected[4];
    if (!computeChallenge(psk, req_node_id, expected)) {
        sendReject(req_node_id);
        return;
    }

    // 3. Timing-safe comparison
    volatile uint8_t diff = 0;
    for (int i = 0; i < 4; i++) {
        diff |= challenge[i] ^ expected[i];
    }

    if (diff != 0) {
        sendReject(req_node_id);  // Authentication failed
        return;
    }

    // 4. Authentication OK — derive session key
    uint8_t session_key[16];
    CryptoEngine::deriveSessionKey(psk, req_node_id, session_key);

    // 5. Store in node table + NVS
    NodeInfo info;
    info.id = req_node_id;
    info.type = req_node_type;
    memcpy(info.psk, psk, 16);
    memcpy(info.session_key, session_key, 16);
    info.registered = true;
    info.last_seen = millis();
    saveNodeInfo(info);

    // 6. Register with protocol layer
    protocolManager->registerNodeKey(req_node_id, session_key);

    // 7. Send accept
    sendAccept(req_node_id);
}
```

## Security Properties

| Property           | Mechanism                                      |
|--------------------|------------------------------------------------|
| Node Identity      | Node_ID (2 bytes) — unique per device          |
| Pre-Shared Secret  | PSK (16 bytes) — never transmitted OTA         |
| Replay Protection  | Monotonic 32-bit sequence in every packet      |
| Per-Packet Auth    | AES-128-GCM with 4-byte truncated MIC          |
| Key Separation     | Session key derived from PSK + Node_ID via AES |
| Challenge Auth     | AES-CMAC with domain-specific label            |

## Provisioning (Out-of-Band)

Nodes are provisioned before deployment via serial CLI:

```cpp
void provisionNodeSerial() {
    uint16_t node_id = 0x0001;
    uint8_t  psk[16];

    // Generate random PSK using ESP32 hardware RNG
    esp_fill_random(psk, 16);

    // Store on node
    nvs_set_blob(node_handle, "psk", psk, 16);
    nvs_set_u16(node_handle, "node_id", node_id);
    nvs_set_u8(node_handle, "node_type", 0x01); // sensor
    nvs_commit(node_handle);

    // Store on gateway
    nodeManager->provisionNode(node_id, 0x01, psk, "Garden_Sensor_1");

    Serial.printf("Provisioned node %04X\n", node_id);
    // Print PSK for QR code / documentation
}
```
