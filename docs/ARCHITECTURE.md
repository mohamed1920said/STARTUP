# AMR System Architecture

## Scope

The maintained product contains one ESP32-S3 central gateway and TTGO LoRa32 sensor/actuator nodes. Alternative gateway and radio-test targets are intentionally excluded.

## Topology

```text
Soil sensor nodes -- encrypted LoRa telemetry --> ESP32-S3 gateway
ESP32-S3 gateway -- encrypted LoRa commands --> actuator nodes
Actuator nodes -- encrypted ACK/heartbeat --> ESP32-S3 gateway
Weather sensors --> ESP32-S3 gateway
ESP32-S3 gateway --> local dashboard + MQTT + optional HTTPS cloud
```

## Central gateway runtime

The Arduino framework runs on ESP-IDF/FreeRTOS. The gateway creates:

- LoRa task: core 1, priority 4, 4096-byte stack.
- Cloud task: core 0, priority 1, 8192-byte stack.
- LoRa TX queue: 8 entries.
- Cloud request queue: 16 entries.
- State mutex for persistent downlink sequences and pending commands.

The Arduino loop services Wi-Fi management, the local MQTT broker, dashboard maintenance, and weather collection.

## Radio configuration

| Parameter | Value |
|---|---|
| Frequency | 868.0 MHz |
| Bandwidth | 125 kHz |
| Spreading factor | SF9 |
| Coding rate | 4/5 |
| Sync word | 0x12 |
| TX power | 10 dBm |

## Packet format

| Field | Size |
|---|---|
| Node ID | 2 bytes |
| AES-GCM nonce | 12 bytes |
| Packet type | 1 byte |
| Ciphertext | 7-12 bytes |
| Truncated authentication tag | 4 bytes |

Packet types are telemetry `0x10`, actuator command `0x20`, ACK `0x30`, and heartbeat `0x40`.

## Data flow

Sensor telemetry is authenticated, decrypted, checked against per-stream replay state, and then published to the dashboard, MQTT, automation engine and optional cloud queue. Valve commands are associated with a pending sequence; the displayed valve state changes only after a successful ACK or a heartbeat reports the actual state.

## Storage

- `wifi_cfg`: Wi-Fi and dashboard credentials.
- `node_db`: provisioned nodes, PSKs, automation and replay state.
- `lora_seq`: persistent transmit sequence reservations.
- `cloud_cfg`: HTTPS URL, API key and trusted CA certificate.
- LittleFS: dashboard assets.

See `WIRING.md` and `SECURITY_HANDSHAKE.md` for the hardware and protocol details.
