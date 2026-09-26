# AMR System Architecture

## Pilot scope

The maintained build contains one ESP32-S3 central gateway and TTGO LoRa32
sensor/actuator nodes. It is configured for a supervised pilot: Edge AI remains in
shadow mode, local MQTT and browser OTA are disabled, automatic OPEN requires
verified valve-position feedback, cloud control is CLOSE-only, and one OPEN period
is capped at 300 seconds. Compile-time checks prevent a higher configured cap.

```text
Soil sensor nodes -- authenticated LoRa telemetry --> ESP32-S3 gateway
ESP32-S3 gateway -- authenticated LoRa commands --> actuator nodes
Actuator nodes -- authenticated ACK/heartbeat --> ESP32-S3 gateway
Weather sensors --> ESP32-S3 gateway
ESP32-S3 gateway --> authenticated local dashboard + rolling CSV
ESP32-S3 gateway --> optional CA-validated HTTPS cloud API
```

## Central gateway runtime

The Arduino framework runs on ESP-IDF/FreeRTOS. The gateway creates:

- LoRa task: core 1, priority 4, 4096-byte stack.
- Cloud task: core 0, priority 1, 8192-byte stack.
- Dataset task: core 0, priority 1, 6144-byte stack.
- LoRa TX queue: 8 entries.
- Pending command correlation: 32 slots for up to 16 provisioned nodes.
- Cloud request queue: 64 volatile entries.
- Dataset queue: 48 typed records.
- State mutex for persistent downlink sequences and pending commands.

The Arduino loop services Wi-Fi, dashboard maintenance, weather collection, Edge AI
and the automatic controller. LoRa reception, safety logic, and rolling CSV logging
continue without farm Wi-Fi or internet. The setup AP is WPA2 protected with the
generated password printed only on the physical serial console. The farm-network
dashboard uses HTTP Basic authentication; deploy it on an isolated pilot network.

The HTTPS queue retries briefly but is held in RAM. Restart discards queued requests,
and the gateway does not replay CSV records to the cloud. Current and previous CSV
segments remain the outage evidence and must be exported separately.

Cloud `VALVE_ON` commands receive a negative ACK without radio transmission.
`VALVE_OFF` remains enabled. The gateway caches 32 tuples of command ID, target node,
and requested state in RAM. Exact completed duplicates return the cached ACK;
reusing a numeric ID for another node/state is a separate request. Completion also
requires the same tuple, so a delayed old actuator result cannot overwrite a newer
request. A fast ACK that completes the tuple before cloud-pending bookkeeping finishes
cannot downgrade `COMPLETE` back to `PENDING`. The parser validates identifiers before
narrowing them: `command_id` is an integer in 1..`UINT32_MAX`, and `node_id` is an
integer in 1..65534. Invalid values are rejected rather than wrapping to another node.
Rejected commands return a negative ACK, and a pending CLOSE can retry after 30
seconds. The cloud service must still guarantee unique command IDs because this cache
does not survive restart.

## Radio configuration and frame

| Parameter | Value |
|---|---|
| Frequency | 868.0 MHz |
| Bandwidth | 125 kHz |
| Spreading factor | SF9 |
| Coding rate | 4/5 |
| Sync word | `0x12` |
| TX power | 10 dBm |

| Frame field | Size |
|---|---:|
| Node ID | 2 bytes |
| AES-GCM nonce | 12 bytes |
| Packet type | 1 byte |
| Ciphertext | 8–17 bytes for current packet types |
| Authentication tag | 16 bytes, full AES-GCM tag |

Packet types are sensor telemetry `0x10` (12-byte ciphertext), actuator command
`0x20` (8), ACK `0x30` (10), and heartbeat `0x40` (17). Every role must be flashed
from the same revision because the change from the former 4-byte tag changes every
frame length.

## Identity, replay protection, and data flow

Every sensor and actuator is physically provisioned with a role, unique node ID and
16-byte PSK in the `dev_ident` NVS namespace. A missing, corrupt, or wrong-role
identity keeps its radio disabled. The same ID, type, and PSK are entered in the
gateway's authenticated dashboard. There is no radio registration handshake.
Duplicate gateway node IDs are refused. Reprovisioning requires explicit removal
and a fresh PSK so replay history is not silently reset.

Sensor, actuator, and gateway transmitters reserve monotonic sequence ranges in the
`lora_seq` NVS namespace. Receivers track replay state by node and packet stream.
Identity removal preserves sequence state. Erasing or cloning sequence state while
reusing a PSK can repeat an AES-GCM nonce and is prohibited.

Every sequence reservation and replay update that controls acceptance is committed
and read back. Gateway startup stops before command services when the initial
downlink block cannot be verified. Node uplinks stop when no sequence block can be
verified. The actuator leaves radio disabled if replay state cannot load and rejects
a command before movement if its new replay checkpoint cannot be saved. The gateway
rolls back and rejects a received packet when its replay update cannot be persisted.

Authenticated sensor telemetry is checked for replay, centrally calibrated, and
sent to the dashboard, automatic controller, CSV logger, Edge AI shadow advisor,
and optional cloud queue. A moisture error flag, raw ADC outside 10–4000, or a
non-finite/out-of-range calibrated value makes the reading unusable for automatic
control. An invalid reading queues CLOSE even if the cached actuator state still
says closed, cancelling an automatic OPEN that may be in flight. The 30-second retry
clock advances only after CLOSE enters the TX queue; queue failure remains eligible
at the next safety pass. Retries stop only after fresh feedback verifies both
commanded and actual state closed. A verified CLOSE acknowledgement clears older
pending OPEN records by radio sequence but preserves any newer OPEN.

OPEN requires an actuator heartbeat no older than 90 seconds. Automatic OPEN also
requires a valid prediction associated with the actuator, usable moisture data,
verified position feedback, and no actuator fault. Each automatic zone uses a 3%
hysteresis and a 10-second command throttle. Enabling automatic mode itself requires
valid valve feedback no older than 90 seconds. Automatic mode is cleared on gateway
restart and must be explicitly re-armed.

While an automatic zone is believed open, actuator state older than 90 seconds or
linked sensor state older than five minutes triggers CLOSE; the gateway retries that
safety request every 30 seconds. CLOSE remains available when OPEN is rejected. The
actuator independently caps OPEN at 300 seconds and repeated OPEN commands do not
move the original deadline.

Provisioning sets node freshness to unknown. OPEN remains blocked until a fresh
authenticated actuator packet is accepted. The actuator independently rejects OPEN
below 3400 mV and above an implausible 5000 mV reading, while accepting CLOSE. If
OPEN feedback does not verify, it immediately pulses CLOSE and keeps the close
safety timer armed until closure verifies.

The actuator checks its armed local CLOSE timer before testing whether identity or
radio is available. Physical CLOSE retries therefore continue when commissioning
state, replay-storage startup, or LoRa prevents communication; heartbeat reporting
resumes only when identity and radio are ready.

## Command queue integrity

The gateway reserves a pending-correlation slot before inserting an encrypted frame
into the eight-entry LoRa TX queue. The reservation and queue insertion run under the
same state lock. If insertion fails, an unused reservation is cleared; when a newer
CLOSE had selected the oldest CLOSE for replacement, the exact displaced record is
restored. This prevents an ACK from being correlated to a command that was never
queued and prevents a queued command from becoming untracked.

The 32-slot capacity is twice the maximum configured node count of 16; slots are a
shared pool rather than a per-node quota. A second live OPEN for the same node is
rejected. No live OPEN is overwritten when the tracker is full. A new CLOSE may
replace only the oldest live CLOSE, preserving shutdown priority without losing OPEN
correlation. A verified CLOSE clears only older OPEN records by radio sequence and
leaves any newer OPEN intact.

If a normal threshold/hysteresis automatic CLOSE cannot be queued, the gateway
schedules a dedicated retry five seconds later and continues rescheduling at that
interval while queueing fails. Invalid/stale-sensor safety CLOSE uses its separate
safety retry rules described above.

ACK and heartbeat report commanded state, measured state, feedback validity, error
flags, battery, and optional hydraulics. Without position feedback, the state is an
estimate and the actuator is manual-only for this pilot.

## Storage

- `wifi_cfg`: Wi-Fi and dashboard credentials.
- `node_db`: gateway node records, PSKs, automation and replay state.
- `dev_ident`: field-node role, ID, raw PSK and record CRC.
- `lora_seq`: persistent transmit reservations and actuator command replay state.
- `cloud_cfg`: HTTPS URL, API key and trusted CA certificate.
- `field_cfg`: calibration and agronomic metadata per sensor zone.
- LittleFS: dashboard assets plus rolling `dataset.csv` and
  `dataset_previous.csv` files.

The ESP32-S3 uses `partitions_8MB.csv`: two 2.25 MiB application slots, a
3.375 MiB LittleFS partition, and a coredump partition. Each CSV segment is limited
to 1.25 MiB to preserve filesystem headroom.

See `PILOT_DEPLOYMENT.md`, `WIRING.md`, and `SECURITY_HANDSHAKE.md` before field use.

## Reproducible build

All environments pin the Espressif32 PlatformIO platform to `7.0.1`. Library
dependencies are exact, including RadioLib `6.6.0`, PicoMQTT `1.3.0`, ESP Async
WebServer `3.0.6`, ArduinoJson `7.4.3`, DHT `1.4.7`, BMP280 `2.6.8`, Unified Sensor
`1.1.15`, OneWire `2.3.8`, and DallasTemperature `3.11.0`. Update them only as a
coordinated release followed by full rebuild and pilot validation.
