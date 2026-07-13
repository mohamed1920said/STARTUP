# Software Technical Design Document

| Field | Value |
|-------|-------|
| **Project** | LoRa Star Network — IoT Gateway & Sensor/Actuator Nodes |
| **Document Version** | 1.0 |
| **Date** | 2026-07-07 |
| **Author** | Repository Maintainer |

---

## 1. Summary

A production-grade, star-topology LoRa network at 868 MHz consisting of a Central Gateway (ESP32-S3 / TTGO LoRa32 v2.1), a Sensor Node, and an Actuator Node. The gateway provides WiFi connectivity, an embedded MQTT broker, a real-time WebSocket dashboard, and AES-128-GCM encrypted LoRa uplink/downlink. All three firmware variants share a common crypto protocol library with mbedtls.

---

## 2. Background / Problem Statement

**Problem**: Existing commercial IoT LoRa solutions either require proprietary gateways (The Things Network, Helium) or cloud subscriptions (Cayenne, Ubidots). There is a need for a fully self-contained, LAN-based LoRa system with:
- No cloud dependency
- End-to-end encryption (not just link-layer)
- Real-time browser dashboard with zero install
- Field-provisionable nodes with per-device keys

**Prior art in this repo**: The project evolved from a simple LoRa sensor → serial monitor setup into a multi-node encrypted network with MQTT and a web dashboard. The architecture reflects organic growth rather than top-down design.

---

## 3. Goals / Non-goals

### Goals
- End-to-end AES-128-GCM encryption between every node and the gateway
- Real-time browser dashboard served from the gateway (no cloud)
- Sub-second downlink command → actuator response
- Node provisioning via dashboard (key, alias, type)
- MQTT bridge for external client integration

### Non-goals
- Multi-gateway handover or roaming
- Over-the-air firmware updates for sensor/actuator nodes
- Deep sleep on sensor/actuator (nodes are mains-powered for this design)
- LoRaWAN compliance
- Cellular backhaul

---

## 4. Scope

**In scope**:
- Gateway firmware (`src/central/main.cpp`)
- Sensor firmware (`src/sensor/main.cpp`)
- Actuator firmware (`src/actuator/main.cpp`)
- Shared libraries (`lib/LoraNetwork/`, `lib/lora_protocol/`, `lib/LoraRadio/`, `lib/WebDashboard/`)
- Dashboard SPA (`data/`)
- Build system (`platformio.ini`)

**Out of scope**:
- Third-party library source (`.pio/libdeps/`)
- Hardware design files (schematic, PCB)
- Mobile application

---

## 5. Functional Requirements

| ID | Requirement | Priority | Source |
|----|-------------|----------|--------|
| FR-01 | Gateway shall receive and decrypt LoRa packets from provisioned nodes | P0 | Core |
| FR-02 | Gateway shall serve a web dashboard on port 80 | P0 | Core |
| FR-03 | Gateway shall run a local MQTT broker | P1 | Core |
| FR-04 | Gateway shall provide REST API for node control | P0 | Core |
| FR-05 | Sensor shall measure moisture, temperature, battery and transmit encrypted telemetry every 5 seconds | P0 | Core |
| FR-06 | Actuator shall receive encrypted downlink commands and control a latching valve | P0 | Core |
| FR-07 | Actuator shall send encrypted heartbeat every 30 seconds | P1 | Core |
| FR-08 | Actuator shall reply to commands with encrypted ACK | P0 | Core |
| FR-09 | Gateway shall reject packets with invalid authentication tags | P0 | Security |
| FR-10 | Gateway shall enforce replay protection (seq > lastSeq) | P1 | Security |
| FR-11 | Dashboard shall support node provisioning with 128-bit PSK | P0 | Usability |
| FR-12 | Gateway OTA firmware update via dashboard | P2 | Maintenance |

---

## 6. Non-Functional Requirements

| ID | Requirement | Target | Measure |
|----|-------------|--------|---------|
| NFR-01 | Downlink latency | < 2 s (Tx + Rx + decrypt) | Serial timestamp |
| NFR-02 | Gateway memory | < 80% heap (320 KB) | Free heap logging |
| NFR-03 | Code portability | Single PlatformIO project | Same build for 3 node types |
| NFR-04 | Crypto auditability | Wire format is struct + static_assert | Binary identical across compilers |
| NFR-05 | Dashboard SPA size | < 100 KB total | File sizes |

---

## 7. Current Architecture

### 7.1 System Context

```
┌──────────────┐    LoRa 868 MHz     ┌──────────────┐
│  Sensor Node │ ──────────────────▶  │              │
│  (ESP32 +    │    AES-128-GCM      │  Gateway     │
│   SX1276)    │                      │  (ESP32-S3)  │
│              │ ◀──────────────────  │              │
└──────────────┘    Downlink (ACK)    │  ─ MQTT      │
                                      │  ─ WebSocket │
┌──────────────┐                      │  ─ REST API  │
│ Actuator     │ ◀──────────────────  │  ─ SPA       │
│ Node (ESP32  │    Command pkt       │              │
│  + SX1276)   │ ──────────────────▶  └──────┬───────┘
│              │    Heartbeat / ACK          │ WiFi
└──────────────┘                             │
                                      ┌──────┴───────┐
                                      │  Browser /   │
                                      │  MQTT Client  │
                                      └──────────────┘
```

### 7.2 Gateway Software Stack

```
┌────────────────────────────────────────────┐
│          WebDashboard (SPA)                │
│  ┌──────────┐ ┌──────────┐ ┌───────────┐  │
│  │ index.html│ │ app.js   │ │ style.css │  │
│  │ (LittleFS)│ │ (WS+FETCH)│ │  (Dark)   │  │
│  └──────────┘ └──────────┘ └───────────┘  │
├────────────────────────────────────────────┤
│        ESP Async WebServer                  │
│  ┌──────────┐ ┌──────────┐ ┌───────────┐  │
│  │ WebSocket │ │ REST API │ │ OTA       │  │
│  │ /ws       │ │ /api/*   │ │ /api/ota  │  │
│  └──────────┘ └──────────┘ └───────────┘  │
├────────────────────────────────────────────┤
│  ┌──────────────┐ ┌────────────────────┐  │
│  │ PicoMQTT     │ │ NodeManager (NVS)  │  │
│  │ (Local MQTT) │ │ NodeInfo[16]       │  │
│  └──────────────┘ └────────────────────┘  │
├────────────────────────────────────────────┤
│  ┌──────────────────────────────────────┐  │
│  │ LoRa Rx Path                         │  │
│  │ processLoRa() → decrypt → dispatch  │  │
│  │ onDecryptedPkt() → MQTT + Dashboard  │  │
│  └──────────────────────────────────────┘  │
│  ┌──────────────────────────────────────┐  │
│  │ LoRa Tx Path                         │  │
│  │ manualTransmit() → IRQ flag poll     │  │
│  │ (bypasses DIO0/PSRAM conflict)       │  │
│  └──────────────────────────────────────┘  │
├────────────────────────────────────────────┤
│  ┌──────────────────────────────────────┐  │
│  │ WeatherStation (ISR)                 │  │
│  │ rain_cnt, wind_cnt → read() delta   │  │
│  └──────────────────────────────────────┘  │
├────────────────────────────────────────────┤
│  Arduino Framework / ESP-IDF (ESP32-S3)    │
│  WiFi STA, LWIP, SPI, NVS, LittleFS       │
└────────────────────────────────────────────┘
```

### 7.3 Current Design Weaknesses

1. **NodeManager duplication**: `NodeManager.h/.cpp` is in `src/central/` rather than a shared library, making it impossible to use from other node types.
2. **Radio stack duality**: Two radio libraries (RadioLib for ESP32-S3, sandeepmistry/LoRa for TTGO) with different APIs, requiring `LoraRadio` wrapper.
3. **MQTT topic helpers unused**: `MqttTopics.h` defines structured topic helpers but `main.cpp` uses hardcoded `snprintf` strings.
4. **Session key dead code**: `CryptoEngine::deriveSessionKey()` is called in `provision()` but never used for actual encryption — raw PSK is used instead via `LoraCrypto::setKey()`.
5. **Crypto API duplication**: `buildNonce()` and `buildAAD()` exist in both `CryptoEngine.h` and `LoraProtocol.h`.

---

## 8. Proposed Architecture

[ASSUMPTION] The proposed architecture preserves the existing single-binary-per-node approach. No RTOS or multi-threading changes are proposed.

### 8.1 Extracted Shared Library Layer

```
lib/
├── LoraCrypto/          (was LoraNetwork + lora_protocol)
│   ├── CryptoEngine.h/.cpp   — mbedtls wrapper (unchanged)
│   ├── PacketTypes.h         — wire format (unchanged)
│   ├── LoraProtocol.h        — high-level encrypt/decrypt (unchanged)
│   └── LoraCrypto.h          — unified include (new)
├── NodeManager/          (from src/central/ — promoted)
│   └── NodeManager.h/.cpp   — shared node DB + NVS
├── LoraRadio/            (unchanged)
└── WebDashboard/         (unchanged)
```

### 8.2 Clean Module Boundaries

| Module | Responsibility | Consumes |
|--------|---------------|----------|
| `main.cpp` (per node) | Initialization, loop, wiring | All libs |
| `LoraCrypto` | AES-128-GCM encrypt/decrypt, nonce/AAD | mbedtls |
| `PacketTypes` | Wire format structs, serialize/deserialize | None |
| `NodeManager` | Node DB, NVS persistence, PSK lookup | LoraCrypto |
| `WebDashboard` | HTTP/WS server, SPA, API handlers | ArduinoJson, AsyncWebServer |
| `LoraRadio` | Hardware SPI abstraction (TTGO only) | LoRa library |

---

## 9. Data Model / Storage Impact

### 9.1 NVS Schema (`node_db` namespace)

| Key | Type | Content |
|-----|------|---------|
| `n_XXXX` (e.g. `n_0001`) | Blob | `NodeInfo` struct (serialized) |

### 9.2 NodeInfo Struct (unchanged)

```cpp
struct NodeInfo {
    uint16_t id;         // 2B — unique node identifier
    uint8_t  type;       // 1B — 0x01 sensor, 0x02 actuator
    uint8_t  sessionKey[16]; // [DEAD] not used for encryption
    uint8_t  psk[16];    // 16B — pre-shared key
    bool     registered; // 1B
    uint32_t lastSeq;    // 4B — replay protection
    uint32_t lastSeen;   // 4B — millis() timestamp
    char     alias[24];  // 24B — human-readable name
    bool     autoMode;   // 1B
    uint8_t  threshold;  // 1B — 0-100 moisture %
    uint16_t sensorId;   // 2B — linked sensor node ID
    bool     valveOpen;  // 1B
    // Total: 60 bytes
};
```

**Proposed change**: Remove `sessionKey[16]` field to save 16 bytes per node (256 bytes total for 16 nodes).

### 9.3 Database Impact

| Operation | NVS Write | NVS Read |
|-----------|-----------|----------|
| Provision node | 1 blob write | — |
| Handle packet | — | 0 (RAM-only update) |
| Update actuator config | 1 blob write | — |
| Remove node | 1 key erase | — |
| Boot (load all) | — | Iterate all blobs |

---

## 10. API / Interface Changes

### 10.1 Current REST API (unchanged)

| Method | Endpoint | Body | Response |
|--------|----------|------|----------|
| GET | `/api/health` | — | `{"uptime":..., "heap":..., "rssi":...}` |
| GET | `/api/nodes` | — | `[{"id":..., "type":..., ...}]` |
| POST | `/api/restart` | — | `{"status":"restarting"}` |
| POST | `/api/control` | `{"node_id":..., "value":bool}` | `{"status":"ok"}` |
| POST | `/api/ota/upload` | multipart firmware | `{"status":"ok"}` |

### 10.2 WebSocket Actions (unchanged)

| Action | Payload | Effect |
|--------|---------|--------|
| `toggle_valve` | `{node_id, value}` | Send downlink command |
| `add_node` | `{node_id, psk, node_type, alias}` | Provision node |
| `remove_node` | `{node_id}` | Remove from NVS |
| `set_actuator_config` | `{node_id, auto_mode, threshold, sensor_id}` | Update auto config |

### 10.3 No Breaking Changes Proposed

All existing API contracts remain unchanged. The refactoring is internal (function split, variable renaming, dead code removal).

---

## 11. Dependencies

| Library | Version | License | Purpose | Used By | Risk |
|---------|---------|---------|---------|---------|------|
| RadioLib | ^6.0.0 | MIT | LoRa SX1276 driver | central_gateway, sensor, actuator | Low — active |
| PicoMQTT | ^1.3.0 | MIT | Embedded MQTT broker | central_gateway | Low |
| ESP Async WebServer | ^3.0.0 | LGPL-3.0 | HTTP/WS server | central_gateway | Low — active |
| ArduinoJson | ^7.0.0 | MIT | JSON parsing/serialization | central_gateway | Low |
| OneWire | ^2.3.7 | LGPL-2.1 | DS18B20 protocol | sensor_node | Low — stable |
| DallasTemperature | ^3.11.0 | LGPL-2.1 | Temperature sensor | sensor_node | Low |
| LoRa (sandeepmistry) | ^0.8.0 | MIT | Alternative SX1276 driver | central_gateway_ttgo | **Medium — orphaned** |
| mbedtls | (ESP-IDF built-in) | Apache-2.0 | AES-128-GCM | All | Low — platform-provided |

---

## 12. Failure Modes / Error Handling

| Failure Mode | Detection | Recovery | Current Handling |
|--------------|-----------|----------|-----------------|
| LoRa CRC error | `readData()` returns `RADIOLIB_ERR_CRC_MISMATCH` | Drop packet, re-arm Rx | `radio.startReceive()` |
| Decrypt auth fail | `CryptoResult::ERR_AUTH` | Drop packet, log node_id + seq | Return early |
| Unknown node (no PSK) | `getPsk()` returns nullptr | Drop packet | `return;` with log |
| Replay attack | `seq <= lastSeq` | Drop packet | Return early |
| WiFi disconnect | `WiFi.status()` check | Auto-reconnect (ESP-IDF) | Implicit |
| NVS full | `nvs_set_blob()` returns error | Log failure | Return false (provision fails) |
| OOM | `new` returns null / heap logging | Dashboard shows free heap | Passive monitoring |
| DIO0 IRQ conflict | TX_TIMEOUT on ESP32-S3 | `manualTransmit()` polling | Workaround in place |

### Proposed Error Handling Convention

```cpp
// Pattern applied consistently:
int16_t result = someOperation();
if (result != RADIOLIB_ERR_NONE) {
    Serial.printf("[GW] operation failed: %d\n", result);
    return;  // or clean up
}
```

For the crypto layer, the existing `CryptoResult` enum is the correct approach — continue using it.

---

## 13. Observability

### 13.1 Logging Conventions

| Prefix | Used By | Format |
|--------|---------|--------|
| `[GW]` | Gateway | `[GW] <event>: <details>` |
| `[SN]` | Sensor | `[SN] <event>: <details>` |
| `[AN]` | Actuator | `[AN] <event>: <details>` |
| `[DBG]` | All (debug) | `[DBG] <key>=<value>` — gated by `CORE_DEBUG_LEVEL` |

### 13.2 Dashboard Observability

| Metric | Source | Update Rate |
|--------|--------|-------------|
| Free heap | `ESP.getFreeHeap()` | 15s (health poll) |
| WiFi RSSI | `WiFi.RSSI()` | 15s |
| Uptime | `millis()` | 15s |
| Node lastSeen | `NodeManager` | Per-packet |
| Node battery | In telemetry | Per-packet (5-30s) |
| Valve state | In heartbeat | Per-packet (30s) |

### 13.3 Missing Observability (Proposed)

- LoRa RSSI per packet (currently logged but not persisted)
- Downlink success rate (Tx count vs ACK count)
- NVS free space monitoring

---

## 14. Testing Strategy

### 14.1 Current State

No automated tests exist. Testing is manual via:
- Serial monitor observation
- Dashboard interaction
- Physical valve actuation verification

### 14.2 Proposed Test Strategy

| Level | Scope | Tool | Priority |
|-------|-------|------|----------|
| Compile | All 4 production envs + 2 test envs | PlatformIO build | P0 (already in CI) |
| Static analysis | C++ (clang-tidy), JS (eslint) | `.clang-tidy`, `.eslintrc` | P1 |
| Unit — crypto | `CryptoEngine` encrypt/decrypt round-trip | PlatformIO test framework | P1 |
| Unit — packet | Serialize/deserialize round-trip | PlatformIO test framework | P1 |
| Integration | Gateway inject → actuator receive → ACK | Two-node physical test | P2 |
| End-to-end | Dashboard toggle → valve cycle | Manual | P2 |

### 14.3 Test File Locations (Proposed)

```
test/
├── test_crypto/
│   └── test_crypto.cpp       — CryptoEngine round-trip tests
├── test_packet/
│   └── test_packet.cpp       — serialize/deserialize round-trip
└── test_ttgo.cpp             — (existing stub)
```

---

## 15. Deployment / Rollout

### 15.1 Firmware Build

```bash
# All environments
pio run

# Single environment
pio run -e central_gateway

# Upload + filesystem
pio run -e central_gateway -t upload --upload-port COM_N
pio run -e central_gateway -t uploadfs --upload-port COM_N
```

### 15.2 First-Time Provisioning

1. Flash gateway firmware + filesystem
2. Power gateway — connects to WiFi, prints IP
3. Open `http://<ip>` in browser
4. Fill Node Provisioning form (ID, 32-char hex PSK, type, alias)
5. Flash sensor/actuator firmware with matching PSK
6. Sensor telemetry auto-appears on dashboard

### 15.3 Rollback

- Firmware: re-flash previous `.bin` via USB or OTA
- Filesystem: re-upload previous `data/` via `uploadfs`
- NVS: `nvs_flash_erase()` via serial monitor command or re-flash

---

## 16. Backward Compatibility / Migration

| Change | Compatibility | Migration |
|--------|--------------|-----------|
| Wire format (`PacketTypes.h`) | **Backward compatible** — no field layout changes | None needed |
| Crypto protocol (nonce, AAD) | **Backward compatible** — unchanged | None needed |
| NVS schema (`NodeInfo`) | **Breaking if `sessionKey` removed** — old blobs have different size | Clear NVS (re-provision nodes) |
| REST API | **Backward compatible** — no endpoint changes | None needed |
| WebSocket actions | **Backward compatible** — no action changes | None needed |
| RadioLib API calls | **Backward compatible** — wrapper methods unchanged | Recompile |

---

## 17. Risks / Mitigations

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| ESP32-S3 PSRAM-less board crashes on OPI boot | High (current) | Medium | Remove `BOARD_HAS_PSRAM`, change to `qio` memory type |
| DIO0=2 conflicts with PSRAM on S3 | Certain (current hw) | High | `manualTransmit()` IRQ polling — already deployed |
| Orphaned LoRa library breaks future PlatformIO | Low | Medium | Migrate `central_gateway_ttgo` to RadioLib |
| DS18B20 sensor disconnect causes -127°C reading | Medium | Low | Error flag in telemetry (`err |= 0x02`) |
| GCM nonce reuse if sequence counter wraps | Very low (4B packets) | High | Document 4B counter → 4B packets before wrap |

---

## 18. Open Questions

1. **Session key**: Should `deriveSessionKey()` be removed, or should downlink encryption actually use session keys for forward secrecy?
2. **RadioLib on TTGO gateway**: Does RadioLib work correctly on TTGO LoRa32 v2.1 now, or is `LoraRadio` wrapper still required?
3. **MQTT topic helpers**: Should `MqttTopics.h` be integrated into the gateway's publish path, or kept as optional helpers?
4. **OTA for sensor/actuator**: Is wireless firmware update desired for leaf nodes, or is USB flashing sufficient?
5. **DS18B20 timing**: The sensor requests temperature, then immediately reads it on the next loop cycle — is the 100ms `delay()` sufficient for conversion?

---

## 19. Task Breakdown

| # | Task | Owner | Estimate | Dependencies |
|---|------|-------|----------|--------------|
| 1 | Remove `sessionKey` from `NodeInfo`, clean NVS | [TO_FILL] | 2h | None |
| 2 | Remove dead `LoraNetwork.h`, inline includes | [TO_FILL] | 30m | None |
| 3 | Extract common `adcReadOversample()` to shared header | [TO_FILL] | 1h | None |
| 4 | Split `processLoRa()` into single-responsibility functions | [TO_FILL] | 3h | None |
| 5 | Split `processPacket()` in actuator | [TO_FILL] | 2h | None |
| 6 | Add `.clang-format`, `.clang-tidy`, lint configs | [TO_FILL] | 1h | None |
| 7 | Fix `platformio.ini` PSRAM config | [TO_FILL] | 30m | None |
| 8 | Write crypto unit tests | [TO_FILL] | 4h | 1 |
| 9 | Create `LICENSE` (MIT) file | [TO_FILL] | 15m | None |
| 10 | Migrate `central_gateway_ttgo` to RadioLib | [TO_FILL] | 4h | None |

---

## 20. Definition of Done

- [ ] All 4 production environments compile without errors
- [ ] Gateway boots, connects to WiFi, serves dashboard
- [ ] Sensor telemetry received, decrypted, displayed on dashboard
- [ ] Actuator command → valve toggle → ACK cycle works end-to-end
- [ ] All `static_assert` checks pass for wire format sizes
- [ ] NVS node provisioning works (add + remove)
- [ ] Linter/formatter configs pass on all touched files
- [ ] Dead code removed (`sessionKey`, `LoraNetwork.h`, `upload_guide.txt`)
- [ ] `CV_SECTION.txt` and developer-specific files moved to `archive/`
- [ ] Technical design documents reviewed and committed

---

## 21. Appendix

### A. Wire Format Reference

```
┌──────┬──────────┬──────┬────────────┬─────┐
│NodeID│ IV/Nonce │ Type │ Ciphertext │ MIC │
│ 2B   │  12B     │ 1B   │  11-27B   │ 4B  │
└──────┴──────────┴──────┴────────────┴─────┘
Total: 30-46 bytes

Nonce:  [0x00 x4 | NodeID x2 | Seq x4]  = 12 bytes
AAD:    [NodeID x2 | PktType x1 | Seq x4] = 7 bytes
```

### B. Packet Types

| Type | Value | Payload Size | Direction |
|------|-------|-------------|-----------|
| SENSOR_TELEMETRY | 0x10 | 12 bytes | Sensor → Gateway |
| ACTUATOR_COMMAND | 0x20 | 8 bytes | Gateway → Actuator |
| ACK | 0x30 | 7 bytes | Actuator → Gateway |
| HEARTBEAT | 0x40 | 7 bytes | Actuator → Gateway |
