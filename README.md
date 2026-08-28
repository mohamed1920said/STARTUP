# AMR LoRa Irrigation System

Production-focused firmware for three device roles:

- ESP32-S3 central gateway
- TTGO LoRa32 soil sensor node
- TTGO LoRa32 actuator/valve node

The gateway receives AES-128-GCM encrypted LoRa telemetry at 868 MHz, serves a local dashboard, runs automatic irrigation rules and an offline ESP32-S3 Edge-AI advisor, controls actuators, records a versioned AI-ready dataset, publishes local MQTT data, and optionally connects to an HTTPS cloud API.

## Firmware environments

| Environment | Hardware | Purpose |
|---|---|---|
| `central_gateway` | ESP32-S3-DevKitC-1-N8 | Gateway, dashboard, weather, MQTT and cloud bridge |
| `sensor_node` | TTGO LoRa32 v2.1 | Soil moisture, DS18B20 temperature and battery telemetry |
| `actuator_node` | TTGO LoRa32 v2.1 | Fail-safe latching-valve control, verified feedback and hydraulic telemetry |

## Wi-Fi setup

1. Power the central gateway.
2. Connect to the open `STARTUP-Gateway-xxxxxx` Wi-Fi network; no AP password is required.
3. Open `http://192.168.4.1`.
4. Use the branded setup page to select farm Wi-Fi and create an 8-32 character dashboard administrator password.
5. After connection, sign in to the dashboard with username `admin` and the password created during setup.

The dashboard, REST API, WebSocket and OTA routes remain authenticated even though the commissioning AP is open.

LoRa reception, local automation, actuator safety and CSV logging continue when
farm Wi-Fi or internet access is unavailable. Dashboard, MQTT and cloud access
resume after the gateway can boot on the configured farm network.

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

This release changes the gateway partition table. The first installation must
upload both firmware and filesystem; uploading only one will leave the
dashboard or partition layout inconsistent.

## Important protocol rule

Gateway, sensor and actuator firmware must be upgraded together. ACK is 10 bytes and actuator heartbeat is 17 bytes in schema v2; mixed firmware releases cannot communicate correctly. The devices also share the same nonce layout, persistent sequence rules and replay-protection model.

## Prototype data collection

The authenticated dashboard's **AI Dataset** panel provides:

- raw and centrally calibrated moisture values;
- UTC epoch time, boot ID, uptime and packet sequence for traceability;
- weather, valve commands, verified valve state, ACKs and optional hydraulics;
- crop, growth-stage and soil metadata per sensor zone;
- field event labels such as rain, leak, blockage and valve fault;
- current and previous CSV downloads.

Configure each sensor's real dry/wet ADC endpoints before collecting training
data. The gateway keeps two rolling 1.25 MiB CSV segments as an outage buffer.
Download both segments regularly or configure the HTTPS cloud connection for a
multi-week dataset. See [AI dataset guide](docs/AI_DATASET.md).

Hydraulic inputs are intentionally disabled by default. Wire and calibrate the
real sensors, then build the actuator with `-DENABLE_HYDRAULIC_SENSORS=1`.

## Edge AI

The central gateway now contains a compact trained model bank for irrigation
need, water/runtime recommendation, rain-delay scheduling, soil dry-down,
watering-effect verification, fault detection and next-day weather prediction.
The dashboard **Edge AI** panel explains each zone recommendation.

AI control is deliberately compiled off with `EDGE_AI_ALLOW_CONTROL=0`. The
model runs in shadow mode while the normal threshold controller and actuator
safety limits remain independent. See [Edge-AI model and deployment guide](docs/EDGE_AI.md).

## Project layout

```text
data/                 Dashboard HTML, JavaScript and CSS
docs/                 Current architecture, wiring and security references
hardware/central_gateway/ KiCad 10 carrier PCB, BOM, clean DRC and fabrication files
lib/                  Required Wi-Fi, dashboard, LoRa protocol and crypto libraries
src/central/          ESP32-S3 gateway firmware
src/sensor/           Soil sensor firmware
src/actuator/         Fail-safe valve actuator firmware
platformio.ini        Three retained PlatformIO environments
partitions_8MB.csv     Gateway OTA + 3.375 MiB LittleFS partition map
flash_central.bat     Windows gateway upload helper
upload_guide.txt      Upload commands and recovery notes
```

## Production note

This is suitable for controlled prototype and pilot testing. It contains a trained prototype model, but simulated device/control labels do not authorize unattended irrigation. Collect representative labeled field data, retrain, validate and complete shadow-mode acceptance before permitting AI control. Commercial release still requires per-device secret provisioning, signed firmware/secure boot, watchdog and brownout validation, enclosure and power testing, fail-safe hydraulic hardware, radio/regulatory certification, and documented installation/support procedures.
