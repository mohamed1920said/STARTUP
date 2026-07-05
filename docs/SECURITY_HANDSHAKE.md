# Security — PSK Provisioning & AES-128-GCM Encryption

## Overview

Nodes are provisioned with a 16-byte Pre-Shared Key (PSK) via the
gateway's WebDashboard. All LoRa packets are encrypted and authenticated
with **AES-128-GCM** using mbedtls. There is no over-the-air registration
handshake — PSK is assigned out-of-band and stored in NVS.

## Provisioning Flow

```
Admin                           Gateway
  │                                │
  ├─ Open dashboard http://<ip>    │
  ├─ Fill provision form:          │
  │   Node ID: 0x0001              │
  │   PSK: 32 hex characters       │
  │   Type: Sensor                 │
  │   Alias: Garden Soil           │
  ├─ WebSocket action:"add_node" ──►│
  │                                ├─ NodeManager::provision()
  │                                ├─ nvs_set_blob("node_psk", ...)
  │                                ├─ nvs_set_u16("node_id", ...)
  │                                └─ Respond: success/fail
```

## Per-Packet Encryption

### Parameters

| Parameter      | Value                          |
|----------------|--------------------------------|
| Algorithm      | AES-128-GCM                    |
| Key            | 16-byte PSK (per node)         |
| Nonce/IV       | 12 bytes                       |
| AAD            | Node_ID (2) + Type (1) + Seq (4) = 7 bytes |
| Tag (MIC)      | 4 bytes (truncated)            |
| Implementation | mbedtls_gcm_*                  |

### Nonce Structure

```
[0x00, 0x00, 0x00, 0x00 | node_id_hi, node_id_lo | seq_bytes...]
  4 bytes zero padding     2 bytes node ID         4 bytes counter
```

The 4-byte zero prefix ensures uniqueness even if node_id overlaps
with the sequence counter space. The 32-bit sequence counter
guarantees a unique nonce for every packet from a given node.

### Encryption (Sensor)

```cpp
CryptoEngine crypto;
crypto.begin(psk, 16);
LoraFrame frame;
crypto.encrypt(plaintext, len, pkt_type, seq_counter, node_id, frame);
// frame.iv, frame.ciphertext, frame.mic populated
```

### Decryption (Gateway)

```cpp
const uint8_t* psk = nodeMgr.getPsk(nodeId);
if (!psk) { /* unknown node */ return; }
CryptoEngine crypto;
crypto.begin(psk, 16);
crypto.decrypt(ciphertext, len, pkt_type, seq, nodeId, iv, mic);
```

## Critical Implementation Detail

The gateway's `CryptoEngine::decrypt()` originally passed `GCM_TAG_SIZE` (16)
as the `tag_len` parameter to `mbedtls_gcm_auth_decrypt`, but only 4 bytes
(the truncated MIC) are stored and transmitted. This caused mbedtls to
compare the full 16-byte expected tag against 4 real bytes + 12 zero bytes,
making authentication always fail.

**Fix:** Use `GCM_TAG_TRUNCATED` (4) as `tag_len`.

## Replay Protection

- Sensor maintains a monotonic 32-bit sequence counter (increments per
  `sendTelemetry()` call, persisted in RAM).
- Gateway's `NodeManager` tracks the last sequence per node.
- Packets with `seq ≤ last_seq` are dropped.
- Counter wraps after ~4 billion packets (negligible risk at 5s interval).

## Security Properties

| Property           | Mechanism                                      |
|--------------------|------------------------------------------------|
| Node Identity      | Node_ID (2 bytes) — unique per device          |
| Pre-Shared Secret  | PSK (16 bytes) — never transmitted OTA         |
| Per-Packet Auth    | AES-128-GCM with 4-byte truncated MIC          |
| Replay Protection  | Monotonic 32-bit sequence counter              |
| Key-agility        | Each node has independent PSK                  |
