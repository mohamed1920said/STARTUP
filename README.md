# AMR LoRa Irrigation System

Supervised-pilot firmware for three device roles:

- ESP32-S3 central gateway
- TTGO LoRa32 soil sensor node
- TTGO LoRa32 actuator/valve node

The gateway receives AES-128-GCM encrypted LoRa telemetry at 868 MHz, serves a local dashboard, runs guarded irrigation rules and an offline ESP32-S3 Edge-AI advisor, controls actuators, records a versioned AI-ready dataset, and optionally connects to an HTTPS cloud API. Cloud control is CLOSE-only, while local MQTT and browser firmware upload are disabled by default for the supervised pilot.

## Firmware environments

| Environment | Hardware | Purpose |
|---|---|---|
| `central_gateway` | ESP32-S3-DevKitC-1-N8 | Gateway, dashboard, weather and optional cloud bridge |
| `sensor_node` | TTGO LoRa32 v2.1 | Soil moisture, DS18B20 temperature and battery telemetry |
| `actuator_node` | TTGO LoRa32 v2.1 | Fail-safe latching-valve control, verified feedback and hydraulic telemetry |

## Wi-Fi setup

1. Power the central gateway.
2. Read the generated commissioning password from the physical 115200-baud serial console.
3. Connect to the WPA2-protected `STARTUP-Gateway-xxxxxx` Wi-Fi network using that password.
4. Open `http://192.168.4.1`.
5. Select farm Wi-Fi and create an 8–32 character dashboard administrator password.
6. After restart, sign in to the dashboard with username `admin` and the configured password.

Use an isolated pilot network. The dashboard, REST API and WebSocket use HTTP Basic authentication. Gateway firmware updates use physical USB in this build.

LoRa reception, local automation, actuator safety and CSV logging continue when
farm Wi-Fi or internet access is unavailable. Dashboard and cloud access
resume after the gateway can boot on the configured farm network.

## Build

```powershell
pio run -e central_gateway -e sensor_node -e actuator_node
pio run -e central_gateway -t buildfs
```

`platformio.ini` pins Espressif32 platform `7.0.1` and exact tested library versions. Do not loosen those constraints for a pilot image; rebuild and repeat the deployment checklist after any dependency change.

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

Gateway, sensor and actuator firmware must be upgraded together. This revision sends the full 16-byte AES-GCM authentication tag; firmware using the former 4-byte tag cannot communicate with it. ACK is 10 bytes and actuator heartbeat is 17 bytes. Persistent sequence reservations and replay checkpoints are written and read back before use; storage failure suppresses transmission or rejects the command/packet instead of continuing without replay protection.

## Node commissioning

Sensor and actuator images contain no default node ID or PSK. Provision each node over its physical USB serial connection, then add the exact same ID, type and unique 16-byte PSK in the gateway dashboard. Duplicate gateway IDs are refused. Reprovisioning requires explicit removal and a newly generated PSK. An unprovisioned or corrupt node keeps its radio disabled, and gateway OPEN remains blocked until it receives a fresh authenticated packet after provisioning. Never clear or clone the preserved `lora_seq` state while reusing a PSK; generate a new PSK after a full NVS erase.

See the [supervised pilot deployment guide](docs/PILOT_DEPLOYMENT.md) for provisioning commands, cloud endpoints and the required bench and field validation.

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

## Pilot safety limits

Automatic mode can be enabled only after valid valve feedback received within 90 seconds. Invalid sensor data cancels an automatic OPEN that may still be in flight; invalid or stale data retries CLOSE until fresh feedback verifies commanded and physical state are closed. A failed safety enqueue remains eligible at the next pass without starting its throttle; a normal threshold-driven automatic CLOSE retries after five seconds. Pending tracking rejects duplicate live OPENs and never replaces one to make room. The actuator rejects OPEN below 3400 mV or above an implausible 5000 mV reading; CLOSE remains allowed. OPEN is capped at 300 seconds, the build rejects any higher configured cap, and repeated OPEN commands do not extend the active deadline. Failed OPEN feedback verification immediately sends CLOSE and retries it if necessary.

This does not authorize unattended irrigation. A latching valve needs energy to close, software cannot prove water stopped without independent feedback, and the cloud queue is RAM-only with no automatic CSV backlog replay. Keep a physical master shutoff accessible and complete `docs/PILOT_DEPLOYMENT.md` before any wet pilot.
