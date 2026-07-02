# LoRaWAN-esque Secure Mesh/Star Network — Architecture Document

## 1. System Topology

```
                                    ┌─────────────────────────┐
                                    │   Internet / Cloud      │
                                    │   (Optional MQTT Bridge) │
                                    └──────────┬──────────────┘
                                               │ Wi-Fi
                                               │
┌──────────────────────────────────────────────────────────────────┐
│                    CENTRAL GATEWAY (ESP32-S3)                     │
│                                                                   │
│  ┌──────────┐  ┌──────────────┐  ┌──────────┐  ┌──────────────┐ │
│  │  LoRa    │  │  PicoMQTT    │  │  Web     │  │  Weather     │ │
│  │  P2P Rx  │◄─┤  Local       │◄─┤  Dash-   │  │  Station     │ │
│  │  + Crypto│  │  Broker      │  │  board   │  │  (I2C/GPIO)  │ │
│  └──────────┘  └──────────────┘  └──────────┘  └──────────────┘ │
│       │                                                           │
│       │ LoRa (868 MHz)                                           │
│       │                                                           │
│  ┌────┴─────────────────────────────────────────────────┐        │
│  │  P2P Link Layer:  Spreading Factor 7-12, BW 125-250 │        │
│  │  Encryption:       AES-128-GCM (mbedtls)            │        │
│  │  Auth:             PSK + Node_ID handshake           │        │
│  └──────────────────────────────────────────────────────┘        │
└──────────────────────────────────────────────────────────────────┘
         ▲                          ▲                  ▲
         │ LoRa                     │ LoRa             │ LoRa
         │                          │                  │
┌────────┴───────┐    ┌─────────────┴──────┐   ┌──────┴──────────┐
│  SENSOR NODE   │    │   ACTUATOR NODE    │   │  FUTURE NODES   │
│  (ESP32-S3)    │    │   (ESP32-S3)       │   │  (...n)         │
│                │    │                    │   │                 │
│  Soil Moisture │    │  Solenoid Valve    │   │  Expandable     │
│  DS18B20 Temp  │    │  (relay/MOSFET)    │   │  via PSK reg.   │
│                │    │                    │   │                 │
│  Deep Sleep    │    │  Solar + 18650     │   │                 │
│  18650 Li-ion  │    │  Duty-cycle Rx     │   │                 │
└────────────────┘    └────────────────────┘   └─────────────────┘
```

## 2. Data Flow (End-to-End)

### Sensor Node → Gateway → Dashboard

```
Sensor Node                        Central Gateway                    Web Browser
    │                                    │                              │
    ├─ Wake from deep sleep              │                              │
    ├─ Read sensor (moisture, temp)      │                              │
    ├─ Build plaintext payload:           │                              │
    │   {seq, moisture, temp, batt, err} │                              │
    ├─ AES-128-GCM encrypt with          │                              │
    │   session key derived from PSK     │                              │
    ├─ Construct LoRa packet:            │                              │
    │   [Node_ID | IV | Ciphertext | MIC]│                              │
    ├─ Transmit on 868 MHz ───────────►  │                              │
    │                                    ├─ Receive LoRa packet          │
    │                                    ├─ Lookup Node_ID → PSK        │
    │                                    ├─ Derive session key           │
    │                                    ├─ Verify MIC, decrypt payload  │
    │                                    ├─ Validate seq ≥ last_seq      │
    │                                    ├─ Publish to MQTT topic:       │
    │                                    │   nodes/sensor_1/telemetry    │
    │                                    ├─ Internal PubSub triggers     │
    │                                    │   WebSocket update            │
    │                                    │                              │
    │                                    │                              ├─ WS msg received
    │                                    │                              ├─ Update dashboard
    │                                    │                              ├─ Render chart/tables
    │                                    │                              │
    ├─ Re-enter deep sleep               │                              │
```

### Gateway → Actuator Node (Downlink)

```
Web Browser                       Central Gateway                 Actuator Node
    │                                    │                              │
    ├─ User toggles valve ON             │                              │
    ├─ POST /api/actuator/1/on           │                              │
    ├─ Dashboard sends MQTT publish:     │                              │
    │   nodes/actuator_1/control ← "ON"  │                              │
    │                                    ├─ MQTT broker fires callback  │
    │                                    ├─ Build plaintext command:     │
    │                                    │   {"cmd":"VALVE_SET","val":1} │
    │                                    ├─ Encrypt with node's key     │
    │                                    ├─ Transmit LoRa downlink ────►│
    │                                    │                              ├─ Rx window open
    │                                    │                              ├─ Decrypt + verify MIC
    │                                    │                              ├─ Validate command
    │                                    │                              ├─ Toggle GPIO → relay
    │                                    │                              ├─ Build ACK payload
    │                                    │                              ├─ Encrypt + send ACK ──►
    │                                    ├─ Receive ACK                  │
    │                                    ├─ Publish nodes/actuator_1/ack │
    │                                    │                              │
    │                              WebSocket update ◄────────────────────┘
```

## 3. Security Architecture

### 3.1 Node Provisioning & PSK

Each node is provisioned *out-of-band* (e.g., via serial CLI, BLE, or QR code scan):

| Parameter       | Size     | Description                                      |
|-----------------|----------|--------------------------------------------------|
| `Node_ID`       | 2 bytes  | Unique node identifier (0x0001–0xFFFE)           |
| `PSK`           | 16 bytes | Pre-Shared Key (AES-128 key material)             |
| `Node_Type`     | 1 byte   | 0x01=Sensor, 0x02=Actuator, 0x03=Repeater        |

Both the Central Gateway's secure NVS and the node's NVS store:
```c
nvs_set_blob(handle, "node_psk", psk, 16);
nvs_set_u16(handle, "node_id", node_id);
nvs_set_u8(handle, "node_type", node_type);
```

### 3.2 Session Key Derivation (HKDF-like)

A static session key is derived from the PSK using AES-128-CMAC:
```
Session_Key = AES_CMAC(PSK, Node_ID || "lora-net-2026" || Nonce_GW)
```

For simplicity and low overhead, we use a deterministic scheme:
```
Session_Key = AES_128_ECB_encrypt(PSK, Node_ID || 0x00...0)
```

This is computed once at provisioning time and stored. Re-keying can be triggered
by the Gateway via a `REKEY` command downlink encrypted with the current key.

### 3.3 Per-Packet Encryption

- **Algorithm:** AES-128-GCM (mbedtls on ESP32)
- **IV/Nonce:** 12 bytes — 4 bytes of Node_ID + 8 bytes of sequence counter (big-endian)
- **AAD (Additional Auth Data):** Node_ID (2 bytes) + Packet_Type (1 byte) + Sequence (4 bytes)
- **Ciphertext:** Variable (plaintext payload encrypted in-place)
- **MIC/Tag:** 4 bytes (truncated GCM tag for minimal overhead)

### 3.4 Replay Protection

- Each node maintains a monotonic 32-bit sequence counter (stored in RTC RAM across deep sleep).
- The Gateway tracks `last_seq[Node_ID]` in a hash table.
- Packets with `seq ≤ last_seq` are silently dropped.
- On wrap-around (~4B packets), the node must re-register with the Gateway.

## 4. OTA Update Strategy

### 4.1 Central Gateway OTA (Wi-Fi)

Standard ESP32 HTTPS OTA via ESPAsyncWebServer upload handler:
1. User selects firmware binary in Dashboard OTA form.
2. File is streamed to `Update.write()` with CRC-32 verification.
3. On success, `ESP.restart()`; on failure, rollback to previous partition.

### 4.2 Remote Node OTA (LoRa — Fragmented)

Since LoRa data rate is low (0.3–5 kbps), a reliable fragmentation protocol is used:

```
Gateway                              Remote Node
  │                                        │
  ├─ OTA_META: {crc32, size=128K,         │
  │   chunks=1024, fw_ver=2.1.0} ──────►  │
  │                                        ├─ Allocate buffer (or write to flash)
  │                                        ├─ Send ACK
  │                                        │
  ├─ OTA_DATA: {chunk_id=0, data[128]} ──►│
  │◄── ACK {chunk_id}                     │
  ├─ OTA_DATA: {chunk_id=1, data[128]} ──►│
  │◄── NACK {chunk_id} (CRC fail)         │
  ├─ OTA_DATA: {chunk_id=1, data[128]} ──►│ (retransmit)
  │◄── ACK {chunk_id}                     │
  │   ...                                  │
  │                                        │
  ├─ OTA_FINAL: {crc32_full} ───────────► │
  │                                        ├─ Verify full CRC-32
  │                                        ├─ If ok: set boot partition, restart
  │                                        ├─ If fail: send NACK, request retry
```

Each chunk: `[Node_ID | IV | Ciphertext | MIC]` — same encrypted container.

## 5. MQTT Topic Hierarchy

```
lora-network/
├── nodes/
│   ├── sensor_<ID>/
│   │   ├── telemetry        # Pub: Sensor → Broker (JSON: moisture, temp, batt_v, seq)
│   │   ├── status           # Pub: hb, sleep, error codes
│   │   └── config           # Sub: sample_interval, sleep_threshold
│   ├── actuator_<ID>/
│   │   ├── control          # Sub: {"cmd":"VALVE_SET","val":0|1}
│   │   ├── ack              # Pub: {"seq":N,"result":"ok|err","batt_v":3.7}
│   │   └── status           # Pub: hb, valve_state, solar_v
│   └── gateway/
│       ├── telemetry        # Pub: weather data, uptime
│       ├── ota/
│       │   ├── status       # Pub: OTA progress
│       │   └── trigger      # Sub: OTA command for remote node
│       └── nodes/           # Pub: node discovery events
└── sys/
    ├── heartbeat            # Pub: all nodes (periodic liveness)
    ├── alarm                # Pub: critical alerts (battery low, sensor fault)
    └── log                  # Pub: debug/diagnostic messages
```

## 6. Binary LoRa Packet Structure

```
┌──────────────────────────────────────────────────────────────┐
│ LoRa PHY Payload (max 255 bytes @ SF12/BW125)                │
├────────────┬──────────┬──────────┬────────────┬──────────────┤
│  Node_ID   │  IV/Nonce│ Packet   │ Ciphertext │  MIC/Tag     │
│  (2 bytes) │ (12 B)   │ Type (1) │ (variable) │  (4 bytes)   │
├────────────┼──────────┼──────────┼────────────┼──────────────┤
│ 0x0001     │ Nonce    │ 0x01     │ Encrypted  │ Truncated    │
│ (MSB first)│          │ (sensor) │ JSON/CBOR  │ GCM tag      │
└────────────┴──────────┴──────────┴────────────┴──────────────┘

Nonce structure:
  ┌────────────┬──────────────────────────────────────┐
  │ Node_ID    │  Sequence Counter (8 bytes, big-end) │
  │ (4 bytes)  │                                      │
  └────────────┴──────────────────────────────────────┘

Packet Types:
  0x01 = Sensor telemetry (uplink)
  0x02 = Actuator command (downlink)
  0x03 = ACK (uplink)
  0x04 = Registration request (uplink)
  0x05 = Registration accept (downlink)
  0x06 = OTA metadata (downlink)
  0x07 = OTA chunk (downlink)
  0x08 = OTA ack (uplink)
  0x09 = Heartbeat (uplink)
```

## 7. Power Management Profiles

### Sensor Node (Aggressive Deep Sleep)

| State       | Current | Duration      | Notes                              |
|-------------|---------|---------------|------------------------------------|
| Active      | ~80 mA  | ~2–3 seconds  | Wake, read sensors, encrypt, Tx    |
| Deep Sleep  | ~10 µA  | 15–60 minutes | RTC timer wake, ULP sensor option  |
| **Avg.**    | ~0.3 µA | —             | At 15 min interval, 3s active      |

### Actuator Node (Duty-Cycle Rx)

| State          | Current  | Duration       | Notes                            |
|----------------|----------|----------------|----------------------------------|
| Active Tx      | ~80 mA   | ~100 ms        | Send ACK after command           |
| Rx Listening   | ~12 mA   | 1–5 seconds    | Wake + CAD + Rx window           |
| Light Sleep    | ~150 µA  | 5–30 seconds   | Timer wake + CAD sniff           |
| **Avg.**       | ~500 µA  | —              | 5s Rx every 30s cycle            |

## 8. GPIO Mapping Reference

### Central Gateway
| Peripherals       | GPIO  | Notes                    |
|-------------------|-------|--------------------------|
| LoRa NSS          | 5     | SPI CS                   |
| LoRa SCK          | 6     | SPI Clock                |
| LoRa MOSI         | 7     | SPI MOSI                 |
| LoRa MISO         | 8     | SPI MISO                 |
| LoRa DIO0         | 9     | Rx/Tx done IRQ           |
| LoRa RST          | 10    | Reset                    |
| Rain Gauge        | 11    | Interrupt (rising edge)  |
| Anemometer        | 12    | Interrupt (rising edge)  |
| Wind Vane         | 13    | ADC (analog voltage)     |
| Built-in LED      | 2     | Status                   |

### Sensor Node
| Peripherals       | GPIO  | Notes                    |
|-------------------|-------|--------------------------|
| LoRa (same SPI)   | 5–10  | Same mapping             |
| Soil Moisture     | 1     | ADC1_CH0 (12-bit)        |
| DS18B20           | 2     | 1-Wire (OneWire library) |
| Status LED        | 3     | Optional                 |

### Actuator Node
| Peripherals       | GPIO  | Notes                    |
|-------------------|-------|--------------------------|
| LoRa (same SPI)   | 5–10  | Same mapping             |
| Valve Relay       | 2     | Active HIGH (MOSFET)     |
| Valve Feedback    | 3     | Limit switch/current sense |
| Solar Charger IRQ | 4     | Optional                 |
