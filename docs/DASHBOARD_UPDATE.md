# Central reliability and dashboard update

## Screens and design intent

The dashboard is a local web interface served by the ESP32-S3. It follows familiar iOS layout conventions using the device's system font, neutral grouped backgrounds, restrained green actions, and generous spacing. It is not a native iOS application.

| Screen | Layout and purpose | Typography and interaction | iOS reasoning |
| --- | --- | --- | --- |
| Overview | Current weather observation, irrigation next step, then supporting station measurements. | Large title; large, light temperature; smaller secondary labels. A single link opens irrigation details. | A clear hierarchy helps the farmer understand the immediate situation without scanning technical panels. |
| Irrigation | Field recommendations, experimental local weather estimate, then valve controls. | Grouped answers with short status labels. Opening a valve requires confirmation; commands remain unverified without physical feedback. | Keep related decisions together and distinguish a request from its physical result. |
| Devices | Soil readings and per-session moisture history. | System text and simple measurement rows. Device management is a separate link. | Familiar grouped information keeps routine observation separate from setup. |
| Settings | Gateway diagnostics and expandable device, field, cloud, update and log groups. | Standard form controls; disclosure sections; clear primary and destructive actions. | Progressive disclosure keeps infrequent administration out of the daily view. |

Mobile navigation uses four persistent bottom tabs, with safe-area padding. Desktop uses a sidebar with the same destinations. Controls have a minimum 44px touch target; inputs use 16px text. System dark mode is supported. Motion is limited to brief state transitions and toast entry, and respects reduced-motion preferences. Native browser confirmation dialogs and disclosure controls provide predictable keyboard behavior. No external fonts, icon scripts, maps or third-party dashboard libraries are required.

Design references: [Apple layout guidance](https://developer.apple.com/design/human-interface-guidelines/layout), [typography](https://developer.apple.com/design/human-interface-guidelines/typography), [tab bars](https://developer.apple.com/design/human-interface-guidelines/tab-bars).

## Reliability changes

- Radio receive work is queued. Decryption, recommendations, dataset records and optional MQTT publication run in the 16 KiB Arduino loop task instead of inside the 4 KiB radio task. Local MQTT is disabled in the supervised-pilot build. Task allocation failures are reported.
- Shared node/field/AI state is synchronized and node snapshots avoid using changing array pointers across tasks.
- Wi-Fi retries after a router or connection interruption. WebSocket output applies backpressure; background browser tabs release their connection and reconnect on return.
- An authenticated `/api/telemetry` snapshot restores weather, sensor and actuator cards after refresh. Snapshot age preserves staleness instead of making an old reading look newly received.
- Dashboard polling is sequential, bounded by timeouts and paused when the page is hidden. Gateway restart detection clears old readings. Connection loss is visible separately from missing sensor data.
- Invalid soil readings remain visible with a reason. Missing or stale weather produces unavailable values rather than a fictitious zero-rain forecast. Model-driven runtime and rain delay require the appropriate inputs.
- The no-feedback actuator's capability flags no longer falsely imply a stuck valve. Missing hydraulic/position sensors limit fault checks; inferred valve state is not a measured position.

## Verification and practical limits

Host tests compile the production C++ engine and relevant central methods against hardware stubs. Browser tests use deterministic, explicitly labeled demo data. They cover missing readings, unavailable forecasts, view navigation, mobile overflow, failed control requests and disconnection states. These tests do not establish field accuracy or physically verify valve movement.

Run from the project directory:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/test_ai_readiness.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/test_central_reliability.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" edge_ai/preview_dashboard.py --port 8766
# In another terminal, with Playwright available:
node tests/test_dashboard.cjs
```

Preview: `http://127.0.0.1:8766`. Demo values are never sent to the central or its LoRa nodes.

Firmware and dashboard must be deployed together. PlatformIO environment: `central_gateway`; filesystem: LittleFS. The current partition layout uses the first 8 MiB even when the detected module has 16 MiB flash. Do not change the partition layout as part of a routine update.

Uploading a filesystem image replaces files in that partition, including stored datasets. The 2026-09-11 COM5 update was preceded by a full 16 MiB backup in `tmp/device_backups/central_COM5_20260911_e072a1d5386c/`. That ignored directory may contain private device configuration and must not be committed or published.
