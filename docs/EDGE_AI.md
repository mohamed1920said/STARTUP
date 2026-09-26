# ESP32-S3 Edge-AI Irrigation Model

## Status

The gateway contains a trained compact model bank and runs inference locally,
without internet access. The current model is intentionally compiled in
**shadow mode** because weather inputs are ERA5 reanalysis and device/control
labels are deterministic prototype simulations, not measurements from a
calibrated farm installation.

Shadow mode displays and logs recommendations but does not let the AI issue a
valve command. Existing threshold automation remains available independently.

## Questions answered

| Farmer question | Gateway output |
|---|---|
| Does this field need irrigation now? | `irrigation_needed`, `irrigation_now`, probability and explanation |
| How long should watering run? | water depth, total duration, 300-second pilot command cycles and cycle count |
| Can irrigation wait? | rain-delay flag, slow-drying flag and wait hours |
| Did watering increase soil moisture? | pending, moisture increased, or no response, with measured percentage-point change |
| Is equipment unhealthy? | leak, blocked pipe, empty tank, stuck valve, sensor fault or weak battery |
| What is the schedule? | now, cooler start window, 12-hour slow-drying delay, or 24-hour rain delay |
| What weather is expected? | next-day temperature, rain amount/probability and ET₀ |

## Architecture

The model is a hybrid design:

1. Small standardized logistic and ridge models generate irrigation, water,
   drying and weather estimates.
2. A compact multi-class model scores hydraulic/device faults.
3. Deterministic safety rules override model output for valve disagreement,
   impossible sensor values, low batteries, empty tanks and abnormal flow.
4. Runtime is calculated from predicted millimetres, configured zone area and
   total emitter flow. The supervised-pilot actuator caps a command at 300 seconds.
5. The actuator's own timeout and feedback protections remain authoritative.

Model code:

- `src/central/EdgeAIEngine.h`
- `src/central/EdgeAIEngine.cpp`
- generated coefficients in `src/central/EdgeAIModel.h`

## Model inputs

Irrigation inference uses calibrated soil moisture and trend, moisture
threshold, soil temperature, local temperature/humidity/pressure/rain/wind,
estimated VPD and ET₀, crop coefficient, recent rain and seasonality.

Runtime also requires valid values for:

- zone area in square metres;
- combined emitter flow in litres per hour.

The gateway refuses AI runtime control when either value is zero.

Fault inference combines raw/calibrated moisture, batteries, RSSI, valve
command and actual state, feedback validity, flow, pressure, tank level, pump
current and firmware error flags. Missing optional hydraulic sensors remain
`NAN` and do not become false zero readings.

## Scheduling policy

- Critically dry soil, defined as at least 12 percentage points below the zone
  threshold, bypasses weather and slow-drying delay logic.
- Otherwise, predicted rain of at least 1.5 mm with rain probability above the
  trained threshold delays irrigation for 24 hours.
- A drying rate above -0.7 percentage points/day with ET₀ below 4 mm/day delays
  irrigation for 12 hours.
- Non-critical watering is scheduled in a cooler 05:00-09:00 or 19:00 window.
- Long applications must be split into 300-second pilot cycles. Hydraulic hardware must
  provide the spacing and pressure capacity required for multiple cycles.

## Watering-effect verification

When verified actuator feedback changes from closed to open, the gateway saves
the current soil moisture. After the valve closes:

- an increase of at least 1.5 percentage points is reported as
  `moisture_increased`;
- the result stays `pending` for up to two hours;
- no sufficient increase after two hours is reported as `no_response` and
  raises a diagnostic warning.

This check is useful for detecting ineffective irrigation, but it does not
replace independent flow metering or manual agronomic verification.

## Weather prediction

The embedded forecast uses a compact next-day model trained on daily Tunisia
ERA5 records. Local 30-second weather readings are aggregated to daily means
and rainfall totals before inference. The model predicts:

- next-day mean air temperature;
- next-day rain amount;
- probability of at least 1 mm rain;
- next-day reference evapotranspiration (ET₀).

This is a local fallback forecast, not a replacement for a high-resolution
numerical weather service. A production scheduler should combine it with a
trusted forecast API when connectivity is available and retain the local model
for offline operation.

## Validation results

Training uses 2023-2024 records. The complete year 2025 is held out, preventing
random leakage from adjacent days.

| Output | 2025 holdout result |
|---|---|
| Irrigation decision | 86.16% accuracy, 84.24% recall, 15.76% false-negative rate |
| Recommended water | 1.71 mm MAE across 203 irrigation events |
| Next-day temperature | 0.98 °C MAE |
| Next-day rain amount | 1.21 mm MAE |
| Next-day ET₀ | 0.55 mm MAE |
| Rain occurrence | 72.48% recall, 30.50% precision |

Fault scores are high on injected synthetic conditions, but the 2025 holdout
does not contain every fault class. Fault metrics therefore do not qualify the
model for unattended commercial control.

Machine-readable results are in `edge_ai/metrics.json`; model coefficients,
features, hashes and governance metadata are in
`edge_ai/model_manifest.json`.

## Train and test

Use the bundled Python environment or any Python installation with NumPy and
pandas:

```powershell
python edge_ai/train_edge_ai.py
python -m unittest -v edge_ai/test_edge_ai.py
pio run -e central_gateway
```

Training is deterministic. It regenerates the model header, manifest and
metrics from the CSV. The firmware embeds the dataset SHA-256 hash so a deployed
model can be traced to its training artifact.

## Dashboard and API

The dashboard **Edge AI** panel shows forecasts and one recommendation card per
sensor zone. Current state is available from authenticated endpoint:

```text
GET /api/ai/status
```

Live messages use WebSocket types `ai_weather` and `ai_prediction`.

AI outputs are also appended to dataset schema version 3. When upgrading from
schema 2, the gateway moves the existing current CSV to the downloadable
previous segment before creating a schema-3 file; this prevents mixed-width
CSV rows.

## Enabling AI control

Current `platformio.ini` explicitly sets:

```ini
-DEDGE_AI_ALLOW_CONTROL=0
```

Do not change it until all of the following are complete:

1. Replace or augment synthetic device columns with calibrated prototype data.
2. Collect farmer-verified irrigation and fault labels across representative
   Tunisian farms, crops, soils and seasons.
3. Retrain and repeat temporal and farm-level holdout tests.
4. Run shadow mode and compare recommendations with agronomist/farmer actions.
5. Verify leak/blockage false-negative rates and watering-effect behaviour.
6. Test valve feedback, maximum runtime, brownout, empty-tank, pump and offline
   fail-safe behaviour on the actual hydraulic installation.
7. Approve the model version and dataset hash in a documented release process.

Keep `EDGE_AI_ALLOW_CONTROL=0` throughout this supervised pilot. A later control
trial requires a separately reviewed release after every gate above passes. Never
remove actuator timeout, feedback, flow, pressure or battery protection.
