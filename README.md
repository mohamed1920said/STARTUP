# AMR LoRa Irrigation System

Production-focused firmware for three device roles:

- ESP32-S3 central gateway
- TTGO LoRa32 soil sensor node
- TTGO LoRa32 actuator/valve node

The gateway receives AES-128-GCM encrypted LoRa telemetry at 868 MHz, serves a local dashboard, runs automatic irrigation rules, controls actuators, publishes local MQTT data, and optionally connects to an HTTPS cloud API.

## Firmware environments

| Environment | Hardware | Purpose |
|---|---|---|
| `central_gateway` | ESP32-S3-DevKitC-1-N8 | Gateway, dashboard, weather, MQTT and cloud bridge |
| `sensor_node` | TTGO LoRa32 v2.1 | Soil moisture, DS18B20 temperature and battery telemetry |
| `actuator_node` | TTGO LoRa32 v2.1 | Latching-valve control, ACK and heartbeat |

## Wi-Fi setup

1. Power the central gateway.
2. Connect to the open `STARTUP-Gateway-xxxxxx` Wi-Fi network; no AP password is required.
3. Open `http://192.168.4.1`.
4. Use the branded setup page to select farm Wi-Fi and create an 8-32 character dashboard administrator password.
5. After connection, sign in to the dashboard with username `admin` and the password created during setup.

The dashboard, REST API, WebSocket and OTA routes remain authenticated even though the commissioning AP is open.

## Build

```powershell
pio run -e central_gateway -e sensor_node -e actuator_node
```

## Upload the ESP32-S3 gateway

```powershell
pio run -e central_gateway -t upload --upload-port COM4
pio run -e central_gateway -t uploadfs --upload-port COM4
```

`flash_central.bat COM4` performs both gateway uploads.

## Important protocol rule

Gateway, sensor and actuator firmware must be upgraded together. They share the same nonce layout, persistent sequence rules and replay-protection model.

## Project layout

```text
data/                 Dashboard HTML, JavaScript and CSS
docs/                 Current architecture, wiring and security references
lib/                  Required Wi-Fi, dashboard, LoRa protocol and crypto libraries
src/central/          ESP32-S3 gateway firmware
src/sensor/           Soil sensor firmware
src/actuator/         Valve actuator firmware
platformio.ini        Three retained PlatformIO environments
flash_central.bat     Windows gateway upload helper
upload_guide.txt      Upload commands and recovery notes
```

## Production note

This is suitable for controlled pilot testing. Commercial release still requires field validation, per-device secret provisioning, signed firmware/secure boot, enclosure and power testing, regulatory certification, and documented installation/support procedures.
