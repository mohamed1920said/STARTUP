# AMR System Architecture

## Scope

The maintained product contains one ESP32-S3 central gateway and TTGO LoRa32 sensor/actuator nodes. Alternative gateway and radio-test targets are intentionally excluded.

## Topology

```text
Soil sensor nodes -- encrypted LoRa telemetry --> ESP32-S3 gateway
ESP32-S3 gateway -- encrypted LoRa commands --> actuator nodes
Actuator nodes -- encrypted ACK/heartbeat --> ESP32-S3 gateway
Weather sensors --> ESP32-S3 gateway
ESP32-S3 gateway --> local dashboard + rolling CSV + MQTT + optional HTTPS cloud
```

## Central gateway runtime

The Arduino framework runs on ESP-IDF/FreeRTOS. The gateway creates:

- LoRa task: core 1, priority 4, 4096-byte stack.
- Cloud task: core 0, priority 1, 8192-byte stack.
- Dataset task: core 0, priority 1, 6144-byte stack.
- LoRa TX queue: 8 entries.
- Cloud request queue: 64 entries.
- Dataset queue: 48 typed records.
- State mutex for persistent downlink sequences and pending commands.

The Arduino loop services Wi-Fi management, the local MQTT broker, dashboard maintenance, and weather collection. LoRa, automation, actuator safety and CSV logging start even when the farm Wi-Fi connection is unavailable. The open commissioning AP exposes only setup routes; dashboard, MQTT and cloud services start on the configured farm network after reboot.

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
| Ciphertext | 8-17 bytes |
| Truncated authentication tag | 4 bytes |

Packet types are telemetry `0x10` (12 bytes), actuator command `0x20` (8
bytes), ACK `0x30` (10 bytes), and heartbeat `0x40` (17 bytes). The heartbeat
includes commanded and actual valve state, feedback validity, error flags,
flow, line pressure, tank level and pump current. Missing optional hydraulic
measurements use explicit sentinels rather than zero.

## Data flow

Sensor telemetry is authenticated, decrypted, checked against per-stream replay state, centrally calibrated from raw ADC values, and then published to the dashboard, MQTT, automation engine, CSV logger and optional cloud queue. Valve commands are associated with a pending sequence; the displayed state follows feedback reported by the actuator rather than assuming a command succeeded.

Automatic irrigation uses a per-zone threshold, 3% hysteresis and a 10-second
per-actuator command throttle. Every open command carries a maximum runtime;
the actuator independently forces a close when its local deadline expires.

The dataset schema is versioned and records UTC epoch milliseconds when NTP is
available, plus uptime and a random boot ID so pre-sync samples remain
traceable. The local CSV is authoritative when cloud delivery fails. See
`AI_DATASET.md` for the schema and collection workflow.

## Storage

- `wifi_cfg`: Wi-Fi and dashboard credentials.
- `node_db`: provisioned nodes, PSKs, automation and replay state.
- `lora_seq`: persistent transmit sequence reservations.
- `cloud_cfg`: HTTPS URL, API key and trusted CA certificate.
- `field_cfg`: moisture calibration and agronomic metadata per sensor zone.
- LittleFS: dashboard assets plus rolling `dataset.csv` and
  `dataset_previous.csv` files.

The ESP32-S3 uses `partitions_8MB.csv`: two 2.25 MiB OTA application slots,
a 3.375 MiB LittleFS partition and a coredump partition. Each CSV segment is
limited to 1.25 MiB to preserve filesystem headroom.

See `WIRING.md` and `SECURITY_HANDSHAKE.md` for the hardware and protocol details.
