# AI Dataset Collection Guide

## Objective

The prototype records trustworthy field observations for training irrigation,
leak/blockage detection and equipment-health models. It does not fabricate a
dataset or run an unvalidated model. First collect real data across different
soil conditions, weather, crop stages and irrigation events.

## Before collection

1. Flash the matching gateway, sensor and actuator builds.
2. Upload the gateway LittleFS dashboard after changing the partition table.
3. Provision each node with its unique ID and PSK.
4. In **AI Dataset**, select each sensor node and enter crop, growth stage,
   soil type, zone area and emitter flow.
5. Measure and save the sensor's dry and saturated/wet raw ADC endpoints.
6. Confirm the actuator dashboard reports **Feedback: verified** before using
   automatic irrigation.
7. If hydraulic sensors are installed, calibrate their constants and explicitly
   enable `ENABLE_HYDRAULIC_SENSORS` before flashing the actuator.

## CSV schema version 3

Each row contains `schema_version`, `record_type`, UTC `epoch_ms`, `uptime_ms`,
`boot_id`, `node_id`, `linked_node_id`, packet `sequence`, raw/calibrated sensor values, weather,
battery and RSSI, commanded/actual valve state, command outcome/source,
hydraulic measurements, error flags, agronomic metadata (including zone area
and emitter flow), human labels, and traceable Edge-AI predictions. AI columns
include the model version, shadow/control state, irrigation probability and
decision, water/runtime schedule, weather forecast, dry-down rate,
watering-effect result and equipment fault score. Actuator rows use `linked_node_id` for the
sensor zone selected in actuator automation configuration.

On first boot after a schema-2 upgrade, the gateway preserves the old current
file as the downloadable previous segment and creates a fresh schema-3 file.
This avoids mixing rows with different column counts.

Record types:

| Type | Meaning |
|---|---|
| `sensor` | Raw ADC, calibrated moisture, soil temperature, node battery and RSSI |
| `weather` | Rain, wind, direction, air temperature, humidity, pressure, light and gateway battery |
| `command` | Requested valve state, source and sequence |
| `command_ack` | Actuator result, actual valve state, feedback validity and errors |
| `actuator` | Periodic actual/commanded state and optional hydraulic telemetry |
| `label` | Farmer/operator observation entered in the dashboard |

Blank measurement fields are written as `nan`; do not replace them with zero.
A zero flow reading is a real observation, while `nan` means the sensor was not
connected or the measurement was unavailable.

## Labels

Add an event marker as soon as a known event happens. Available dashboard labels
include normal observation, irrigation start/end, rain, leak, blocked pipe,
empty tank, sensor fault and valve fault. Use notes for details such as the zone,
maintenance action, measured water quantity or independent reference reading.

Labels are observations, not proof. Review them before training and keep a log
of how each label was established.

## Retention and export

The gateway keeps two rolling 1.25 MiB segments:

- `/api/dataset/export` downloads the current segment.
- `/api/dataset/export?archive=1` downloads the previous segment.

Both routes require dashboard authentication. When the current segment fills,
the previous segment is replaced. Download both files regularly and merge them
off-device, or configure HTTPS cloud ingestion. Local storage is an outage
buffer, not a multi-week data warehouse.

Do not clear the dataset until both segments have been backed up and opened
successfully. The dashboard clear action deletes both local segments.

## Minimum collection campaign

Collect at least four to eight weeks spanning dry-down, irrigation and rainfall,
with multiple representative zones. Record independent soil-moisture reference
measurements and actual water volumes when possible. Intentionally document
safe fault tests such as a closed supply or disconnected sensor; never create a
real leak solely to generate data.

## Model deployment gate

1. Join rows by time and the configured sensor-to-actuator zone relationship.
2. Split training, validation and test data by date or field, not random rows,
   to reduce leakage from adjacent time samples.
3. Report false-negative rates for leak, blockage and dry-soil events.
4. Convert and test the selected small model on the ESP32-S3.
5. Run it in **shadow mode**: log recommendations but do not actuate.
6. Compare recommendations with farmer decisions and existing threshold rules.
7. Permit control only after field acceptance tests; retain maximum runtime,
   valve feedback and hydraulic fail-safes outside the AI model.
