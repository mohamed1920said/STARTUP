# LoRa Star Network — IoT Gateway & Sensor/Actuator Nodes

A production-grade, low-power LoRa star network operating at **868 MHz** with a Central Gateway, Sensor Node, and Actuator Node, all built on ESP32 (TTGO LoRa32 v2.1 / ESP32-S3) with **SX1276** radios.

## Architecture

```
┌─────────────────┐       LoRa (868 MHz)       ┌──────────────────┐
│   Sensor Node   │ ──────────────────────────▶ │                  │
│  (ESP32 + SX1276)│    AES-128-GCM encrypted   │  Central Gateway  │
│  moisture, temp,  │                            │  (TTGO or S3)     │
│  battery         │ ◀────────────────────────── │  ─ MQTT broker   │
└─────────────────┘       Downlink (ACK/cmd)    │  ─ WebDashboard   │
                                                │  ─ LittleFS SPA   │
┌─────────────────┐       LoRa (868 MHz)       │                  │
│  Actuator Node  │ ◀────────────────────────── │                  │
│  (ESP32 + SX1276)│                            └──────────────────┘
│  valve control   │
│  CAD + listen    │
└─────────────────┘
```

## Hardware

| Component | Pin |
|-----------|-----|
| NSS/CS    | 18  |
| SCK       | 5   |
| MOSI      | 27  |
| MISO      | 19  |
| RST       | 14  |
| DIO0/IRQ  | 26  |
| DIO1      | NC  |
| Sensor moisture | GPIO34 |
| DS18B20        | GPIO4  |
| Battery ADC    | GPIO35 |
| Actuator valve | GPIO2  |
| Valve feedback | GPIO3  |

## Security

- **AES-128-GCM** encryption via mbedtls
- 12-byte random nonce per packet
- 4-byte truncated MIC (GCM authentication tag)
- Each node provisioned with unique 128-bit PSK

## Packet Format

| Field | Size |
|-------|------|
| Node ID | 2 bytes |
| IV/Nonce | 12 bytes |
| Packet Type | 1 byte |
| Ciphertext | 11-27 bytes |
| MIC (truncated) | 4 bytes |
| **Total** | **30-46 bytes** |

## Firmware

### PlatformIO Environments

| Env | Board | Radio Library |
|-----|-------|---------------|
| `central_gateway` | ESP32-S3 (ESP32S3DevModule) | RadioLib |
| `central_gateway_ttgo` | TTGO LoRa32 v2.1 | LoRa (sandeepmistry) |
| `sensor_node` | TTGO LoRa32 v2.1 | RadioLib |
| `actuator_node` | TTGO LoRa32 v2.1 | RadioLib |

### Gateway
- WiFi station + MQTT broker + WebDashboard
- REST API + WebSocket for real-time telemetry
- Node provisioning via NVS
- WeatherStation (S3 only — disabled on TTGO due to GPIO12 strapping)

### Sensor Node
- 30-second telemetry loop
- Capacitive moisture sensor (GPIO34)
- DS18B20 temperature (GPIO4)
- Battery voltage (GPIO35)
- No deep sleep

### Actuator Node
- CAD (Channel Activity Detection) + listen mode
- Valve control (GPIO2) with feedback (GPIO3)
- Responds to downlink commands

## Quick Start

```bash
# Build & upload gateway (TTGO)
pio run -e central_gateway_ttgo -t upload

# Build & upload sensor
pio run -e sensor_node -t upload

# Build dashboard SPIFFS filesystem
pio run -e central_gateway_ttgo -t uploadfs
```

1. Power the gateway — connects to WiFi (`msi` SSID)
2. Find its IP from serial output
3. Open `http://<gateway-ip>` in a browser
4. Provision sensor node (ID `0001`, 32-char hex PSK)
5. Sensor telemetry auto-appears on dashboard

## Project Structure

```
├── src/
│   ├── central/         # Gateway firmware
│   ├── sensor/          # Sensor node firmware
│   └── actuator/        # Actuator node firmware
├── lib/
│   ├── LoraRadio/       # LoRa library wrapper (RadioLib-compatible)
│   ├── LoraNetwork/     # CryptoEngine, PacketTypes, LoraProtocol
│   └── WebDashboard/    # REST API + WebSocket + SPA
├── data/                # Dashboard SPA (HTML/JS/CSS)
├── docs/                # Architecture & security docs
└── platformio.ini       # Build configuration
```

## License

MIT
