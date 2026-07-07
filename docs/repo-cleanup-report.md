# Repository Cleanup Report

Date: 2026-07-07
Scope: Full audit of D:\STARTUP\node

---

## A) Repository Audit Summary

### Current Project Structure

```
D:\STARTUP\node/
├── .gitignore                    (84 B)
├── .vscode/
│   ├── c_cpp_properties.json     (67 KB — auto-generated, platform-dependent)
│   ├── extensions.json           (284 B)
│   ├── launch.json               (1.9 KB)
│   └── settings.json             (84 B)
├── CV_SECTION.txt                (1.4 KB — personal resume content)
├── README.md                     (4.3 KB — project docs)
├── archive/                      (does not exist yet — proposed)
├── data/
│   ├── app.js                    (13 KB — dashboard SPA)
│   ├── index.html                (3.1 KB)
│   └── style.css                 (10 KB)
├── docs/
│   ├── ARCHITECTURE.md           (10 KB)
│   ├── SECURITY_HANDSHAKE.md     (3.6 KB)
│   ├── WIRING.md                 (3.4 KB)
│   ├── clean-code-guidelines.md  (proposed)
│   ├── embedded-technical-design.md (proposed)
│   ├── repo-cleanup-report.md    (this file)
│   └── software-technical-design.md (proposed)
├── flash_central.bat             (479 B — user-specific batch file)
├── lib/
│   ├── LoraNetwork/
│   │   ├── library.json          (326 B)
│   │   └── src/
│   │       ├── CryptoEngine.cpp  (4 KB)
│   │       ├── CryptoEngine.h    (923 B)
│   │       ├── LoraNetwork.h     (88 B)
│   │       ├── MqttTopics.h      (1.7 KB)
│   │       └── PacketTypes.h     (4.5 KB)
│   ├── LoraRadio/
│   │   ├── library.json          (236 B)
│   │   └── src/
│   │       └── LoraRadio.h       (1.3 KB)
│   ├── WebDashboard/
│   │   ├── library.json          (449 B)
│   │   └── src/
│   │       ├── WebDashboard.cpp  (8.2 KB)
│   │       └── WebDashboard.h    (2.4 KB)
│   └── lora_protocol/
│       └── src/
│           └── LoraProtocol.h    (5.1 KB)
├── platformio.ini                (3.3 KB)
├── src/
│   ├── actuator/
│   │   └── main.cpp              (6.9 KB)
│   ├── central/
│   │   ├── main.cpp              (13 KB)
│   │   ├── NodeManager.cpp       (3.9 KB)
│   │   ├── NodeManager.h         (1.4 KB)
│   │   ├── WeatherStation.cpp    (1.1 KB)
│   │   └── WeatherStation.h      (575 B)
│   └── sensor/
│       └── main.cpp              (4 KB)
└── upload_guide.txt              (1.8 KB — docs, duplicates README)
```

### Detected Unnecessary Files

| File | Size | Reason |
|------|------|--------|
| `CV_SECTION.txt` | 1.4 KB | Personal resume content. Does not belong in a public repository. Move to `archive/`. |
| `flash_central.bat` | 479 B | Hardcoded paths (`%USERPROFILE%`) and COM port. Only useful on one developer machine. Move to `archive/`. |
| `upload_guide.txt` | 1.8 KB | Duplicates information in `README.md` (build/upload commands). Delete or merge. |
| `.vscode/c_cpp_properties.json` | 67 KB | Auto-generated, platform-specific, references absolute paths. Should be gitignored. |

### Dead Code Candidates

| Location | Reason |
|----------|--------|
| `lib/LoraNetwork/src/LoraNetwork.h` | Single line — `#include` aggregator. Used by exactly one file (`NodeManager.h`). Can be inlined with no loss. |
| `lib/LoraNetwork/src/MqttTopics.h` | Defines `jsonTelemetry()`, `jsonAck()`, and topic helpers, but the gateway uses `snprintf` directly in `main.cpp:100-103` rather than these helpers. **Dead code** — the helpers were written but never called from the actual transmission path. |
| `src/central/NodeManager.h` field `sessionKey[16]` | Stored but `CryptoEngine::deriveSessionKey()` is called only in `provision()`. Downlink encryption in `onActuatorToggle()` uses raw PSK via `LoraCrypto::setKey()`, not session keys. |
| `lib/LoraRadio/src/LoraRadio.h` | RadioLib-compatible wrapper used only by `central_gateway_ttgo` env. Works as an abstraction layer but is a **non-functional stub** — adapts a synchronous LoRa API to a RadioLib-like shape. Consider deprecating if RadioLib works on TTGO now. |
| `test/test_ttgo.cpp` | Referenced in `platformio.ini` `test_tx` and `test_rx` envs `build_src_filter` but **file does not exist**. |
| `flash_central.bat` line `call "%USERPROFILE%\...platformio.exe"` | Hardcodes developer's user profile path. Only works on one machine. |

### Duplicate / Obsolete Configs

| Config | Finding |
|--------|---------|
| `.vscode/c_cpp_properties.json` | 67 KB of absolute paths referencing `.pio/libdeps/` — stale after library updates. Should be `.gitignore`d and regenerated per PlatformIO's built-in IntelliSense. |
| `platformio.ini` — env `central_gateway_ttgo` | Uses `LoRa` library (sandeepmistry) while all other envs use `RadioLib`. Two radio stacks for the same hardware is a maintenance burden. |
| `platformio.ini` — `board_build.memory_type = qio_opi` on `central_gateway` | Enables OPI PSRAM mode, but the actual ESP32-S3 board has no PSRAM chip, causing boot warning. |

### Security and Secret Scan Findings

| Severity | Finding | Location |
|----------|---------|----------|
| **HIGH** | Hardcoded PSK for sensor node | `src/sensor/main.cpp:9` — `NODE_PSK[16]` with `0x00112233...` |
| **HIGH** | Hardcoded PSK for actuator node | `src/actuator/main.cpp:7-8` — `NODE_PSK[16]` with `0xAABBCCDD...` |
| **INFO** | These are development/demo PSKs (not production credentials) | Acceptable for an open-source demo, but should be documented as such. |

**Recommendation**: Add a prominent warning in `README.md` that demo PSKs must be changed before production use. For production, the PSK provisioning flow via dashboard is secure — but the hardcoded fallbacks in sensor/actuator firmware should be replaced with compile-time or OTA-configurable values.

### Dependency Hygiene Findings

| Dependency | Version | Status |
|------------|---------|--------|
| `jgromes/RadioLib` | ^6.0.0 | Latest stable. Used by 3 envs. |
| `mlesniew/PicoMQTT` | ^1.3.0 | Active. Used only by gateway. |
| `mathieucarbou/ESP Async WebServer` | ^3.0.0 | Active. Used by gateway. |
| `bblanchon/ArduinoJson` | ^7.0.0 | Latest stable. Used by gateway dashboard. |
| `paulstoffregen/OneWire` | ^2.3.7 | Stable. Used by sensor. |
| `milesburton/DallasTemperature` | ^3.11.0 | Stable. Used by sensor. |
| `sandeepmistry/LoRa` | ^0.8.0 | **Orphaned** — last updated 2019. Only used by `central_gateway_ttgo` and `test_rx`. |
| `espressif32` platform | 7.0.1 | Latest ESP32 Arduino core. |

**Concerns**:
1. `sandeepmistry/LoRa` is orphaned (no updates since 2019). The `central_gateway_ttgo` env depends on it. Should be migrated to RadioLib to reduce maintenance burden.
2. `ESP Async WebServer` has multiple related libs (`AsyncTCP`, `AsyncTCP_RP2040W`, `ESPAsyncTCP-esphome`) pulled as transitive deps — some may be unused.

---

## B) Cleanup Plan

| # | Item | Action | Risk | Reason | Safe Rollback |
|---|------|--------|------|--------|---------------|
| 1 | `CV_SECTION.txt` | **Move** to `archive/` | None | Personal resume not part of source tree | `git mv CV_SECTION.txt archive/CV_SECTION.txt` |
| 2 | `flash_central.bat` | **Move** to `archive/` | Low | Hardcoded user paths; replace with generic script or docs | `git mv flash_central.bat archive/flash_central.bat` |
| 3 | `upload_guide.txt` | **Delete** after merging content into `README.md` | Low | Content fully duplicated in README | `git show HEAD:upload_guide.txt > upload_guide.txt` |
| 4 | `.vscode/c_cpp_properties.json` | **Move** to `archive/` + add to `.gitignore` | Medium | Auto-generated, absolute paths, varies per machine; PlatformIO generates its own | `git mv .vscode/c_cpp_properties.json archive/` |
| 5 | `test/test_ttgo.cpp` | **Create placeholder file** (empty with comment) | None | Referenced by build filters; prevents build errors | `git checkout` |
| 6 | `lib/LoraNetwork/src/LoraNetwork.h` | **Delete** after inlining includes in consumers | Low | Single-line aggregator; only one consumer | `git checkout -- lib/LoraNetwork/src/LoraNetwork.h` |
| 7 | `lib/LoraNetwork/src/MqttTopics.h` | **Keep** but mark as unused; could be used by future MQTT clients | Low | Helper functions are correct, just not called | — |
| 8 | `central_gateway_ttgo` env | **Keep** but flag for RadioLib migration | Medium | Uses orphaned LoRa library; separate env needed for hardware variant | — |
| 9 | `BOARD_HAS_PSRAM` build flag | **Remove** from `platformio.ini` | Low | Board has no PSRAM; causes boot warning | Re-add flag |
| 10 | `board_build.memory_type = qio_opi` | **Change** to `qio` | Medium | Without PSRAM chip, OPI mode is wrong; may affect flash speed | Revert to `qio_opi` |
| 11 | `.gitignore` | **Append** common patterns | None | Missing `build/`, `.vscode/`, `*.bin`, `*.elf` | `git checkout .gitignore` |

---

## C) Clean Code Refactor Plan

### Module-by-Module Improvements

#### 1. `src/central/main.cpp` — Gateway (13 KB, 359 lines)
| Issue | Refactor |
|-------|----------|
| Global variables at file scope (~15) | Group into `GatewayConfig` struct or namespace |
| Magic numbers: `delay(200)`, `delay(100)`, loop timing | Named constants |
| `processLoRa()` does IRQ check + decrypt + dispatch (SRP violation) | Split: `checkRxDone()`, `readLoRaPacket()`, `decryptPacket()`, `dispatchPacket()` |
| `onActuatorToggle()` handles both business logic and serialization | Move serialization to `PacketTypes.h` helper |
| WiFi SSID/pass hardcoded | Move to `platformio.ini` build flags or `config.h` |
| `downlinkSeq` global | Encapsulate in manager or pass as parameter |
| Error handling: some paths return, some fall through | Consistent `if (state != OK) { log; return; }` pattern |

#### 2. `src/sensor/main.cpp` — Sensor (118 lines)
| Issue | Refactor |
|-------|----------|
| ADC averaging loop duplicated (battery + moisture) | Extract `adcReadOversample(pin, samples, delayUs)` |
| Telemetry packet assembly inline | Use `PacketTypes.h` builder (already done for plaintext, but TX frame assembly is duplicated) |
| `tempRequested` flag pattern is fragile | Use state with explicit `DS18B20_TEMP_REQUESTED` / `DS18B20_TEMP_READY` |
| No receive path at all | Could be omitted by design, but make intentional (comment) |
| `radio.transmit()` — blocking, no timeout | Document that 5s interval > max Tx time |

#### 3. `src/actuator/main.cpp` — Actuator (203 lines)
| Issue | Refactor |
|-------|----------|
| `processPacket()` does too much (FIFO read + decrypt + parse + valve control) | Split into `readPacket()`, `decryptPacket()`, `executeCommand()` |
| Magic valve timing: `VALVE_PULSE_MS = 30` | Good — already a named constant |
| Battery read duplicated (same as sensor) | Extract to shared lib |
| HB/ACK transmit assembly duplicated patterns | Extract `buildTxFrame()` to shared lib |
| `radio.standby()` + `radio.setFrequency()` before each transmit | Unnecessary if frequency doesn't change — document why it's there |

#### 4. `lib/LoraNetwork/src/CryptoEngine.cpp` — Crypto (99 lines)
| Issue | Refactor |
|-------|----------|
| `deriveSessionKey()` called in `NodeManager::provision()` but session key never used | Remove dead code or implement session key rotation |
| `encrypt()` and `decrypt()` have duplicated `mbedtls_gcm_init/setkey` | Extract `_begin()` helper |
| `buildNonce()` and `buildAAD()` exist in both `CryptoEngine.h` and `LoraProtocol.h` | **Duplicated code** — remove from `CryptoEngine.h` and keep in `LoraProtocol.h` |

#### 5. `lib/LoraRadio/src/LoraRadio.h` — Radio Wrapper (55 lines)
| Issue | Refactor |
|-------|----------|
| `readData()` signature conflicts with `PhysicalLayer::readData()` | Rename to `readFifo()` for clarity |
| `getPacketLength()` always returns `LoRa.parsePacket()` result | Document that this consumes the packet — side effects are non-obvious |
| No error return from `begin()` | Returns `int` but caller only checks 0 vs non-zero |

#### 6. `lib/WebDashboard/src/WebDashboard.cpp` — Dashboard (203 lines)
| Issue | Refactor |
|-------|----------|
| JSON serialization with `snprintf` string concatenation | Use `ArduinoJson` helpers consistently |
| `onWsEvent` handler has multiple `if/else` chains | Switch on action string or map to handlers |
| REST handlers use raw `uint8_t*` for body | Use `AsyncWebServerRequest` body parsing helpers |

#### 7. `data/app.js` — Frontend (266 lines)
| Issue | Refactor |
|-------|----------|
| `toggleAct()` always sends `value:true` (already fixed) | — |
| No error handling for `fetch()` calls | Add `.catch()` with toast notification |
| DOM queries repeated (`document.getElementById`) | Cache references or use framework |
| Polling intervals (15s health, 10s nodes, 5s status) | Document intended refresh rates |

### Coding Standard Proposal

| Rule | Standard |
|------|----------|
| Naming — variables | `camelCase` (C++), `snake_case` (JS) |
| Naming — constants | `UPPER_SNAKE_CASE` or `kPrefixedCamelCase` |
| Naming — classes/structs | `PascalCase` |
| Naming — macros | `UPPER_SNAKE_CASE` |
| Indentation | 4 spaces (C++), 2 spaces (JS/HTML) |
| Braces | Allman (C++), K&R (JS) |
| Includes | `#pragma once` (not `#ifndef` guards) |
| Line length | 100 chars (soft), 120 chars (hard) |
| Logging | `[PREFIX]` consistent format: `[GW]`, `[SN]`, `[AN]` |
| Error handling | Return `CryptoResult`/`int16_t`; check `!= OK` consistently |
| Comments | Explain WHY, not WHAT. No obvious comments (`// increment counter`) |
| Assertions | Use `RADIOLIB_ASSERT()` pattern in firmware; `static_assert` for wire sizes |

### Lint / Format / Static Analysis Setup

| Tool | Config File | Purpose |
|------|-------------|---------|
| `clang-format` | `.clang-format` | C++ formatting (BasedOnStyle: Google, 4-space indent) |
| `clang-tidy` | `.clang-tidy` | C++ static analysis (modernize-*, bugprone-*, readability-*) |
| `eslint` | `.eslintrc.json` | JS linting (env: browser, es2021) |
| `prettier` | `.prettierrc` | JS/HTML/CSS formatting (2-space, single-quote) |
| PlatformIO built-in | `platformio.ini` | `check_tool = cppcheck` for PlatformIO check target |

### Test Impact

| Change | Test Impact |
|--------|-------------|
| Move `LoraNetwork.h` includes | No behavioral change — compilation only |
| Extract shared helpers (ADC, buildTxFrame) | No functional change — just relocation |
| Remove `deriveSessionKey()` | Breaks `NodeManager::provision()` if session key is used externally; verify callers |
| Refactor `processLoRa()` | Must produce identical dispatch — verify with serial output comparison |
| Split `processPacket()` | Must produce identical valve/ACK behavior — run existing command cycle tests |

---

## D) Proposed File Operations

### Files to Move to `archive/`

```
archive/CV_SECTION.txt                          (from CV_SECTION.txt)
archive/flash_central.bat                       (from flash_central.bat)
archive/vscode_c_cpp_properties.json            (from .vscode/c_cpp_properties.json)
```

### Files to Delete

```
upload_guide.txt                                (content merged into README.md)
lib/LoraNetwork/src/LoraNetwork.h               (inlined into consumers)
```

### Files to Rename / Modify

```
.gitignore           → append .vscode/, build/, *.bin, *.elf, archive/
platformio.ini        → remove -DBOARD_HAS_PSRAM, change memory_type to qio
data/app.js          → already fixed (toggleAct)
```

### Files to Create

```
docs/software-technical-design.md               (software architecture doc)
docs/embedded-technical-design.md               (embedded architecture doc)
docs/clean-code-guidelines.md                   (coding standard)
archive/README.md                               (archival policy)
test/test_ttgo.cpp                              (stub to satisfy build filter)
.clang-format                                    (C++ formatter config)
.clang-tidy                                      (C++ static analysis config)
.eslintrc.json                                   (JS linter config)
.prettierrc                                      (JS formatter config)
LICENSE                                          (MIT license file matching README claim)
CONTRIBUTING.md                                  (contribution guide)
```

---

## E) Commit Plan

### Commit 1: chore: repository cleanup
```
Message: chore: move personal/config files to archive; remove upload_guide.txt; add gitignore patterns
Files:
  M  .gitignore
  D  upload_guide.txt
  M  platformio.ini
  M  README.md
  R  CV_SECTION.txt → archive/CV_SECTION.txt
  R  flash_central.bat → archive/flash_central.bat
  R  .vscode/c_cpp_properties.json → archive/vscode_c_cpp_properties.json
  A  archive/README.md
  A  test/test_ttgo.cpp
```
Description: Moves developer-specific and personal files out of the root tree into archive/.
Removes upload_guide.txt (content merged into README.md). Adds common .gitignore patterns.
Stubs the missing test/test_ttgo.cpp file. Adjusts platformio.ini for non-PSRAM hardware.

### Commit 2: refactor: clean code pass
```
Message: refactor: extract shared helpers, remove dead code, add lint config
Files:
  M  src/sensor/main.cpp
  M  src/actuator/main.cpp
  M  src/central/main.cpp
  M  lib/LoraNetwork/src/CryptoEngine.cpp
  M  lib/LoraNetwork/src/CryptoEngine.h
  D  lib/LoraNetwork/src/LoraNetwork.h
  A  .clang-format
  A  .clang-tidy
  A  .eslintrc.json
  A  .prettierrc
```
Description: Removes duplicated LoraNetwork.h aggregator. Extracts common ADC oversampling.
Removes dead deriveSessionKey code. Splits processPacket/processLoRa into single-responsibility
functions. Adds linter/formatter configuration files.

### Commit 3: docs: software technical design document
```
Message: docs: add software technical design document
Files:
  A  docs/software-technical-design.md
```
Description: Full software architecture document covering system architecture,
data model, API design, dependencies, failure modes, testing strategy, and rollout plan.

### Commit 4: docs: embedded technical design document
```
Message: docs: add embedded technical design document
Files:
  A  docs/embedded-technical-design.md
  M  docs/clean-code-guidelines.md
```
Description: Full embedded systems design document covering hardware platform,
firmware architecture, RTOS/task model, power/memory budgets, HW-SW interface,
boot/update strategy, and verification plan.
