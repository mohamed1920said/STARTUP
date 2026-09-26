# Supervised Pilot Deployment

## Scope

This build is for a supervised pilot. Keep a trained operator at the installation,
keep a physical master shutoff accessible, and verify every safety action on a dry
bench before connecting the valve to irrigation water. The firmware limits an
OPEN command to 300 seconds, blocks automatic OPEN without verified valve-position
feedback, rejects OPEN when the actuator heartbeat is older than 90 seconds, and
requests CLOSE when an automatic zone loses actuator state or its sensor becomes
stale. These controls reduce risk; they do not prove that a latching valve moved or
that water stopped.

The actuator also rejects OPEN when its measured battery is below 3400 mV or above
an implausible 5000 mV; CLOSE remains accepted. Remote cloud control is CLOSE-only
in this build. Local dashboard OPEN remains an attended action subject to the same
actuator freshness, battery, feedback, and timeout protections.

The actuator's local timeout/CLOSE loop runs before the identity/radio availability
check. Once armed, it continues physical CLOSE attempts even if NVS identity or LoRa
is unavailable; only its status transmission depends on radio.

An actuator built without position feedback is available for supervised manual
testing only. Its ACK confirms that an electrical pulse was sent. It cannot confirm
valve movement. Do not enable automatic irrigation for that actuator.

## Hardware gates

Complete these checks before any wet test:

- Fit an accessible manual upstream shutoff and confirm the operator can isolate
  the zone without the gateway, actuator, network, or battery.
- Confirm valve type, pulse polarity, 4.5 V coil requirements, driver current,
  supply capacity, flyback/current paths, and every GPIO against the actual parts.
- Fit and test independent valve-position feedback for automatic operation.
- Dry-cycle OPEN and CLOSE, remove radio/Wi-Fi/cloud connectivity, reset each
  controller, and interrupt power at each stage.
- Validate brownout behavior, actuator battery limits, weatherproof enclosure,
  cable strain relief, fusing, moisture ingress protection, and antenna clearance.
- Keep `EDGE_AI_ALLOW_CONTROL=0`. Edge AI is a shadow advisor during this pilot.
- Do not leave the installation unattended until the validation checklist below
  passes on the installed hardware and the pilot owner accepts the results.

## Build all coordinated firmware

Install PlatformIO, open a PowerShell terminal in the repository root, and run:

```powershell
pio run -e central_gateway -e sensor_node -e actuator_node
pio run -e central_gateway -t buildfs
```

The tested dependency set is pinned in `platformio.ini`:

| Component | Pinned version |
|---|---|
| Espressif32 PlatformIO platform | `7.0.1` |
| RadioLib | `6.6.0` |
| PicoMQTT | `1.3.0` |
| ESP Async WebServer | `3.0.6` |
| ArduinoJson | `7.4.3` |
| DHT sensor library | `1.4.7` |
| Adafruit BMP280 Library | `2.6.8` |
| Adafruit Unified Sensor | `1.1.15` |
| OneWire, sensor node only | `2.3.8` |
| DallasTemperature, sensor node only | `3.11.0` |

Keep these exact constraints for the pilot. Any platform or dependency update needs
a clean rebuild of all roles and repetition of the full validation checklist. Both
gateway and actuator contain compile-time checks that reject a configured pilot OPEN
cap above 300 seconds.

The gateway, sensors, and actuators use a full 16-byte AES-GCM authentication tag.
Flash all roles from this same revision. Firmware with the former 4-byte tag cannot
communicate with this revision.

Upload the gateway firmware and LittleFS dashboard, replacing `COM_GATEWAY` with
the confirmed port:

```powershell
pio run -e central_gateway -t upload --upload-port COM_GATEWAY
pio run -e central_gateway -t uploadfs --upload-port COM_GATEWAY
pio device monitor --port COM_GATEWAY --baud 115200
```

Upload node firmware with each board's confirmed port:

```powershell
pio run -e sensor_node -t upload --upload-port COM_SENSOR
pio run -e actuator_node -t upload --upload-port COM_ACTUATOR
```

Do not infer a COM port from an earlier installation. Close any serial monitor using
the port before uploading.

## Provision each field node over physical USB

Sensor and actuator binaries contain no shared default identity. An empty, corrupt,
or role-mismatched identity keeps the node radio disabled. The actuator first puts
its outputs into a safe state and attempts a CLOSE pulse.

Generate a different random 16-byte PSK for every node and store it in the pilot
asset register:

```powershell
py -c "import secrets; print(secrets.token_hex(16).upper())"
```

Connect the node by USB and open a 115200-baud serial monitor. Commands are
uppercase and end with Enter:

```text
STATUS
PROVISION 0001 <32_HEX_SENSOR_PSK>
```

Use a unique four-digit hexadecimal ID other than `0000` or `FFFF`, and replace the
example PSK. Sensor and actuator IDs must also be unique from one another. The node
prints its role, state, ID, and chip identity but never prints the PSK. It restarts
after successful provisioning.

To replace a valid identity, connect by physical USB and run:

```text
UNPROVISION 0001 CONFIRM
```

If `STATUS` reports `corrupt` or `role_mismatch`, recover with:

```text
ERASE CORRUPT CONFIRM
```

Identity removal deliberately preserves the `lora_seq` NVS namespace. Never clear,
restore, or clone sequence state while keeping the same PSK: doing so can reuse an
AES-GCM nonce. After a full flash erase or NVS replacement, provision a newly
generated PSK at both ends.

Sequence and replay writes are verified by reading NVS back. Treat any serial
`sequence`, `replay storage`, or `storage error` message as a no-go condition:

- gateway startup stops before command services if it cannot reserve downlink
  sequence space;
- the actuator keeps radio disabled if replay state cannot be loaded;
- an actuator command is rejected without actuation if its replay checkpoint cannot
  be saved; ACK result `0x05` means replay-storage/NVS persistence failure;
- sensor/actuator uplinks are not transmitted without a verified sequence block;
- the gateway rejects a received packet if its updated replay checkpoint cannot be
  committed and verified.

The deep-sleep sensor exposes a short serial maintenance window after a physical
reset. Start the serial monitor before reset. A timer wake sends telemetry and
returns to sleep without opening this window. An unprovisioned sensor remains awake
with its radio disabled so it can be commissioned.

## Configure the gateway

On first boot, keep the gateway connected to a physical serial monitor. The serial
log prints a unique `STARTUP-Gateway-xxxxxx` SSID and its generated commissioning
password. Connect to that WPA2-protected AP, open `http://192.168.4.1`, select the
farm network, and create an 8–32 character dashboard administrator password. After
restart, sign in as `admin` on the gateway's farm-network address.

Use an isolated pilot network. The local dashboard uses HTTP with Basic
authentication. Local MQTT and browser firmware upload are disabled by default in
the supervised pilot build. Perform firmware updates over physical USB.

In **Settings → Devices**, add each node using the exact ID, type, and PSK recorded
during USB provisioning. Keys consisting entirely of `00` or `FF` are rejected.
The gateway refuses an ID that is already registered. To reprovision, first remove
the gateway record, physically `UNPROVISION` the node, generate a new PSK, and then
provision both ends again. Do not overwrite an existing record because doing so
would reset its replay history.

Provisioning alone does not mark a node online: its gateway `lastSeen` starts empty.
Wait for a fresh authenticated packet from that physical node before any command
test. Link each actuator to its sensor. The moisture threshold range is 5–95%.
Automatic mode is cleared on every gateway restart and must be re-armed by the
operator only after fresh sensor telemetry and valid actuator feedback have been
observed. Enabling automatic mode is rejected unless valid valve feedback was
received within the previous 90 seconds.

For an invalid or stale linked sensor, the gateway keeps requesting CLOSE while an
OPEN is pending or closure is not verified. It records the 30-second retry time only
after the CLOSE was successfully queued, so a full/unavailable TX queue remains
eligible at the next safety pass. Retries stop after fresh feedback reports both
commanded and actual state closed. When that verified CLOSE ACK arrives, the gateway
removes only pending OPENs with an older radio sequence; it preserves a newer OPEN
so an earlier CLOSE response cannot erase a later command.

The gateway has 32 pending command slots for at most 16 nodes. It reserves the
pending record before placing a frame in the eight-entry LoRa TX queue. Reservation
and queue insertion are one locked transaction: if queue insertion fails, an unused
slot is cleared or the exact oldest CLOSE displaced by a newer CLOSE is restored.
A duplicate live OPEN for the same actuator is rejected, and a live OPEN is never
overwritten to free a slot. When all slots are occupied, only a newer CLOSE may
replace the oldest CLOSE. This keeps OPEN/ACK correlation intact while giving
shutdown commands priority.

A normal moisture-threshold transition from OPEN to CLOSE has a dedicated recovery
path: if its first enqueue fails, the gateway schedules another attempt after five
seconds and repeats that schedule while queueing fails. This is separate from the
invalid/stale sensor safety CLOSE throttle above.

## Optional HTTPS cloud connection

In **Settings → Cloud**, enter:

- an `https://` base URL without a trailing slash;
- the API key sent as `X-API-Key`;
- the PEM CA certificate used to validate the server.

Plain HTTP, a missing API key, or a missing CA certificate disables cloud transfer.
The server contract is:

| Method | Endpoint |
|---|---|
| `POST` | `/api/ingest/telemetry` |
| `POST` | `/api/ingest/heartbeat` |
| `POST` | `/api/ingest/weather` |
| `POST` | `/api/ingest/register-node` |
| `POST` | `/api/ingest/ack` |
| `GET` | `/api/commands/pending` |

For example, the telemetry request body contains the centrally calibrated reading
and its trace fields:

```json
{
  "node_id": 1,
  "moisture_raw": 2134,
  "moisture": 47.25,
  "temperature": 23.80,
  "battery": 3.921,
  "sequence": 1057,
  "error_flags": 0,
  "rssi_dbm": -91,
  "epoch_ms": 1780000000000
}
```

The gateway polls commands about every 10 seconds. Each command object needs
`command_id`, `node_id`, and an action. Return a JSON array such as:

```json
[
  {"command_id": 42, "node_id": 2, "action": "VALVE_OFF"}
]
```

The cloud parser accepts only `VALVE_ON` and `VALVE_OFF`, but this supervised-pilot
build rejects every `VALVE_ON` without transmitting an OPEN and posts a negative ACK.
`VALVE_OFF` remains enabled and is the only remote control action the pilot server
should send.

The parser validates identifiers before converting them to radio-sized values.
`command_id` must be an integer from 1 through 4294967295 (`UINT32_MAX`), and
`node_id` must be an integer from 1 through 65534. Non-integer, negative, zero, and
out-of-range identifiers are rejected, so an oversized value cannot wrap and target
another node.

The gateway keys idempotency by the tuple `(command_id, node_id, requested_state)`.
Repeating the same completed tuple does not actuate again; it returns the cached
positive or negative ACK. Reusing a numeric command ID for a different node or state
does not suppress that new request. A delayed actuator completion is accepted only
when all three fields still match, so an old ACK cannot complete or acknowledge the
newer tuple. A pending CLOSE may retry after 30 seconds, while the same pending tuple
is otherwise ignored. This bounded cache is volatile across gateway restart, so the
cloud service must still issue unique command IDs and retain outcomes. If a fast
actuator ACK completes an exact tuple before its cloud-pending bookkeeping finishes,
the completed state remains terminal and is never downgraded to pending.
The ACK `ok` field is true only for actuator result `0x00`, meaning position was
verified. A no-feedback CLOSE pulse therefore returns `ok:false` even though the
electrical pulse was sent; an operator must verify physical closure.

The cloud request queue contains 64 entries in RAM and retries a failed request a
limited number of times. It is lost on restart and does not replay the LittleFS CSV
backlog. Rolling local CSV is the outage record; export both current and previous
segments regularly or implement server-side ingestion of exported files.

## Validation checklist

Record the result, firmware commit, device IDs, operator, date, and measured valve
behavior for every item:

1. Confirm all three roles were built and flashed from the same revision and that
   packets authenticate with the 16-byte tag.
   Force an NVS write/readback failure in a test build or fault harness and verify
   sequence transmission/command processing stops rather than using an unreserved
   or uncheckpointed counter.
2. Confirm the commissioning AP requires the serial-displayed password and the
   dashboard requires the configured administrator password.
3. Confirm an unprovisioned, corrupt, or wrong-role node keeps its radio disabled.
   Confirm a duplicate gateway ID is refused. Remove both identity records, use a
   new PSK, and confirm clean reprovisioning.
4. Confirm sensor and actuator IDs/PSKs match the gateway. Verify OPEN is blocked
   immediately after registration, then confirm a fresh authenticated packet and
   30-second actuator heartbeat make the node current.
5. Issue an automatic OPEN and, before its ACK, disconnect or short the moisture
   probe so it reports an error or raw ADC outside 10–4000. Verify the in-flight
   OPEN is cancelled by CLOSE. Confirm later invalid packets can retry CLOSE after
   the 30-second safety throttle.
6. Verify automatic mode cannot be enabled without valid position feedback received
   within 90 seconds. Let feedback age beyond 90 seconds, verify re-arm is rejected,
   and confirm automatic mode starts disabled after every gateway restart.
7. Attempt OPEN after the actuator heartbeat has been absent for more than 90
   seconds. Verify rejection. Confirm CLOSE remains available.
8. While an automatic zone is open, stop sensor telemetry for more than five
   minutes and verify CLOSE requests repeat no faster than every 30 seconds after
   successful queueing. Fill/disable the TX queue and confirm a failed enqueue does
   not start the throttle. Restore feedback and verify retries stop only after both
   commanded and actual state are verified closed. Repeat with the actuator
   heartbeat absent for more than 90 seconds.
   Separately force the normal threshold-driven CLOSE enqueue to fail and verify a
   retry is attempted after five seconds.
9. Request a runtime above 300 seconds and verify rejection. Confirm a build with
   `PILOT_MAX_OPEN_S` above 300 also fails. Verify the actuator closes at no more
   than 300 seconds and repeated OPEN does not extend the original deadline.
10. Supply simulated battery readings below 3400 mV and above 5000 mV. Verify OPEN
    returns the low/invalid-battery result without pulsing, then verify CLOSE remains
    accepted at both readings.
11. Make OPEN position verification fail. Verify the actuator immediately pulses
    CLOSE and continues retrying if CLOSE is not verified, including with identity
    or radio unavailable. Check physical position.
12. Send cloud `VALVE_ON` and verify no radio OPEN plus a negative cloud ACK. Send
    a unique `VALVE_OFF`, repeat the same ID/node/state tuple, and verify the cached
    ACK without repeated actuation. Reuse that numeric ID with a different node or
    state and verify it is treated as a separate tuple; confirm a delayed old
    completion cannot overwrite the newer result. Inject a fast actuator ACK before
    pending bookkeeping finishes and confirm `COMPLETE` is not changed back to
    `PENDING`. Submit fractional, negative, zero, and out-of-range command/node IDs;
    verify each is rejected before radio transmission and cannot wrap to another node.
    Fill the pending tracker in a harness: verify duplicate live OPEN rejection,
    confirm live OPEN records are never evicted, and confirm only a newer CLOSE can
    replace the oldest CLOSE. Force LoRa TX insertion failure and verify an unused
    reservation is cleared and an oldest CLOSE selected for replacement is restored
    exactly.
13. Remove farm Wi-Fi and cloud access. Verify LoRa control and CSV logging continue,
    then confirm no claim is made that missed cloud records were replayed.
14. Remove power from the actuator while the physical valve is open. Use the manual
    shutoff and document the result; a latching valve cannot close without energy.

Keep the physical shutoff available throughout the pilot even after every item
passes.
