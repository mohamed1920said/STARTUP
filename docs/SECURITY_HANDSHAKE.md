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
[packet_type | 0x00 | node_id_hi, node_id_lo | sequence (64-bit field)]
    1 byte     1 byte          2 bytes                 8 bytes
```

Packet type separates ACK, heartbeat, telemetry, and command nonce spaces.
Each transmitter reserves persistent sequence ranges in NVS so a reboot skips
unused values rather than reusing a nonce. A packet stream must stop and be
re-keyed before its 32-bit counter is exhausted.

### Encryption (Sensor)

```cpp
LoraCrypto crypto;
crypto.setKey(psk, 16);
crypto.encrypt(plaintext, len, pkt_type, seq_counter, node_id, iv, mic);
// plaintext now contains ciphertext; iv and truncated mic are populated
```

### Decryption (Gateway)

```cpp
const uint8_t* psk = nodeMgr.getPsk(nodeId);
if (!psk) { /* unknown node */ return; }
LoraCrypto crypto;
crypto.setKey(psk, 16);
crypto.decrypt(ciphertext, len, pkt_type, seq, nodeId, iv, mic);
```

## Authentication-tag implementation detail

The gateway's `CryptoEngine::decrypt()` originally passed `GCM_TAG_SIZE` (16)
as the `tag_len` parameter to `mbedtls_gcm_auth_decrypt`, but only 4 bytes
(the truncated MIC) are stored and transmitted. This caused mbedtls to
compare the full 16-byte expected tag against 4 real bytes + 12 zero bytes,
making authentication always fail.

**Fix:** Use `GCM_TAG_TRUNCATED` (4) as `tag_len`.

## Replay Protection

- Sensor, actuator, and gateway reserve monotonic sequence ranges in NVS.
- Gateway tracks telemetry, heartbeat, and ACK sequences independently per node.
- Packets with `seq ≤ last_seq` for that node and packet stream are dropped.
- Actuator persists the last accepted command sequence and drops older commands after reboot.
- Counter wraps after ~4 billion packets (negligible risk at 5s interval).

## Security Properties

| Property           | Mechanism                                      |
|--------------------|------------------------------------------------|
| Node Identity      | Node_ID (2 bytes) — unique per device          |
| Pre-Shared Secret  | PSK (16 bytes) — never transmitted OTA         |
| Per-Packet Auth    | AES-128-GCM with 4-byte truncated MIC          |
| Replay Protection  | Monotonic 32-bit sequence counter              |
| Key-agility        | Each node has independent PSK                  |
