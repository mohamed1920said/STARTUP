# LoRa Star Network — Architecture Document

## 1. System Topology

```
                                      ┌──────────────────────────┐
                                      │    MQTT Clients / Cloud  │
                                      │    (Optional bridge)     │
                                      └───────────┬──────────────┘
                                                  │ Wi-Fi
                                                  │
 ┌──────────────────────────────────────────────────────────────────┐
 │                    CENTRAL GATEWAY                                │
 │      TTGO LoRa32 v2.1 (LoRa lib)  or  ESP32-S3 (RadioLib)       │
 │                                                                  │
 │  ┌──────────┐  ┌──────────────┐  ┌──────────┐                   │
 │  │ LoRa Rx  │  │  PicoMQTT    │  │  Web     │                   │
 │  │ + Crypto │◄─┤  Local       │◄─┤  Dash-   │                   │
 │  │          │  │  Broker      │  │  board   │                   │
 │  └──────────┘  └──────────────┘  └──────────┘                   │
 │       │                                                          │
 │       │ LoRa (868 MHz, SF9, BW125, CR5, 10 dBm)                 │
 │       │                                                          │
 │  ┌────┴────────────────────────────────────────────────┐         │
 │  │  Encryption: AES-128-GCM (mbedtls)                  │         │
 │  │  Auth: 4-byte truncated GCM tag                    │         │
 │  │  Nonce: 12 bytes (4 zero + 4 node_id + 4 seq)     │         │
 │  └─────────────────────────────────────────────────────┘         │
 └──────────────────────────────────────────────────────────────────┘
          ▲                          ▲
          │ LoRa                     │ LoRa
          │                          │
 ┌────────┴───────┐    ┌─────────────┴──────┐
 │  SENSOR NODE   │    │   ACTUATOR NODE    │
 │  TTGO LoRa32   │    │   TTGO LoRa32      │
 │  v2.1 (ESP32)  │    │   v2.1 (ESP32)     │
 │                │    │                    │
 │  Capacitive    │    │  Valve (GPIO2)     │
 │  moisture (34) │    │  Feedback (GPIO3)  │
 │  DS18B20 (4)   │    │  CAD + listen      │
 │  Battery (35)  │    │  Battery (35)      │
 │  No deep sleep │    │                    │
 └────────────────┘    └────────────────────┘
```

## 2. Hardware

| Component     | Pin | Node                |
|---------------|-----|---------------------|
| LoRa NSS/CS   | 18  | All (SPI)           |
| LoRa SCK      | 5   | All (SPI)           |
| LoRa MOSI     | 27  | All (SPI)           |
| LoRa MISO     | 19  | All (SPI)           |
| LoRa RST      | 14  | All (wired, not 23) |
| LoRa DIO0/IRQ | 26  | All                 |
| Moisture      | 34  | Sensor              |
| DS18B20       | 4   | Sensor              |
| Battery ADC   | 35  | Sensor / Actuator   |
| Valve control | 2   | Actuator            |
| Valve feedback| 3   | Actuator            |

## 3. Data Flow

### Sensor → Gateway → Dashboard

```
Sensor                                Gateway                        Browser
  │                                      │                             │
  ├─ Read moisture (GPIO34), map ADC     │                             │
  │  to 0-100% (dry=2500, wet=400)      │                             │
  ├─ Read DS18B20 (GPIO4) temperature   │                             │
  ├─ Read battery (GPIO35)              │                             │
  ├─ Build SensorTelemetry binary struct│                             │
  ├─ AES-128-GCM encrypt (PSK, nonce)   │                             │
  ├─ Transmit: [NodeID | IV | Cipher | MIC] ──────────────────►       │
  │                                      ├─ radio.startReceive()      │
  │                                      ├─ Decrypt with node's PSK   │
  │                                      ├─ Publish to MQTT topic     │
  │                                      ├─ Push to WebSocket         │
  │                                      │                          ├─ Update card
  │                                      │                          ├─ Show moisture%,
  │   (loops every 5s)                   │                            temp, batt, RSSI
```

### Gateway → Actuator (Downlink)

```
Browser                              Gateway                      Actuator
  │                                      │                             │
  ├─ Click toggle in dashboard           │                             │
  ├─ WebSocket action:"toggle"           │                             │
  │                                      ├─ Build ActuatorCommand      │
  │                                      ├─ Encrypt with node's PSK    │
  │                                      ├─ radio.transmit() ────────► │
  │                                      │                             ├─ Receive, decrypt
  │                                      │                             ├─ Toggle GPIO2
  │                                      │                             ├─ Send ACK ──────►
  │                                      ├─ Receive ACK                │
  │                                      ├─ Update dashboard           │
  │                                      ├─ radio.startReceive() re-arm│
```

## 4. Firmware Environments

| Env             | Board          | Radio Library      |
|-----------------|----------------|--------------------|
| central_gateway | ESP32-S3       | RadioLib           |
| central_gateway_ttgo | TTGO v2.1 | LoRa (sandeepmistry) |
| sensor_node     | TTGO v2.1      | RadioLib           |
| actuator_node   | TTGO v2.1      | RadioLib           |

RTL and WeatherStation are excluded on TTGO gateway (GPIO12 strapping conflict).

## 5. Packet Format

```
┌───────┬──────────┬──────┬──────────────┬─────┐
│Node_ID│ IV/Nonce │ Type │  Ciphertext  │ MIC │
│2 bytes│ 12 bytes │1 byte│   variable   │4 B  │
└───────┴──────────┴──────┴──────────────┴─────┘

Nonce layout:
  4 bytes zero padding | 4 bytes Node_ID | 4 bytes sequence counter

SensorTelemetry payload (12 bytes):
  sequence (4) | moisture_raw (2) | moisture_pct (1) | temp_c (2) | batt_mv (2) | err (1)
```

## 6. Security

- **Algorithm:** AES-128-GCM via mbedtls
- **PSK:** 16 bytes per node, provisioned via dashboard WebSocket
- **Nonce:** 12 bytes (never reused — sequence counter increments per packet)
- **MIC:** 4-byte truncated GCM tag (`GCM_TAG_TRUNCATED` — critical fix: must match what's transmitted)
- **AAD:** Node_ID (2 bytes) + Packet_Type (1 byte) + Sequence (4 bytes)

## 7. LoraRadio Wrapper

The `lib/LoraRadio/src/LoraRadio.h` adapter wraps the Arduino LoRa library
(sandeepmistry) with a RadioLib-compatible API, used on TTGO gateway where
RadioLib's SX1276 receive path does not latch IRQ flags in continuous mode.

```cpp
LoraRadio radio;
radio.begin(freq, bw, sf, cr, syncWord, power, preambleLen);
radio.startReceive();     // enters continuous Rx
radio.available();        // true when packet received
radio.getPacketLength();  // byte count
radio.readData(buf, len); // copy payload
radio.transmit(buf, len); // blocking Tx
```

## 8. MQTT Topics

| Topic pattern                          | Direction     |
|----------------------------------------|---------------|
| `nodes/<id>/telemetry`                 | Sensor → MQTT |
| `nodes/<id>/control`                   | MQTT → Actuator |
| `nodes/<id>/ack`                       | Actuator → MQTT |

## 9. GPIO Mapping Reference

### Sensor Node (TTGO LoRa32 v2.1)
| Peripheral     | GPIO | Notes                        |
|----------------|------|------------------------------|
| LoRa NSS       | 18   | SPI CS                       |
| LoRa SCK       | 5    | SPI Clock                    |
| LoRa MOSI      | 27   | SPI MOSI                     |
| LoRa MISO      | 19   | SPI MISO                     |
| LoRa RST       | 14   | Reset                        |
| LoRa DIO0      | 26   | IRQ                          |
| Moisture       | 34   | ADC (dry=2500, wet=400)      |
| DS18B20        | 4    | 1-Wire                       |
| Battery        | 35   | ADC (via voltage divider)    |

### Actuator Node (TTGO LoRa32 v2.1)
| Peripheral     | GPIO | Notes                        |
|----------------|------|------------------------------|
| LoRa (same SPI)| 5,18,19,27,14,26 | same pins          |
| Valve relay    | 2    | Active HIGH                  |
| Valve feedback | 3    | Limit switch                 |
| Battery        | 35   | ADC                          |
