# Clean Code Guidelines — LoRa Star Network

## Naming Conventions

| Category | Convention | Example |
|----------|-----------|---------|
| Variables | `camelCase` | `packetLength`, `lastHbMs` |
| Constants | `kPrefixedCamelCase` or `UPPER_SNAKE` | `kMaxNodes`, `TX_INTERVAL_MS` |
| Macros | `UPPER_SNAKE_CASE` | `LORA_PROTO_DEBUG`, `LORAGW` |
| Classes / Structs | `PascalCase` | `CryptoEngine`, `NodeInfo` |
| Functions | `camelCase` | `sendTelemetry()`, `processPacket()` |
| Files | `PascalCase` for classes, `kebab-case` for app files | `CryptoEngine.h`, `main.cpp` |
| Node prefixes | `[GW]`, `[SN]`, `[AN]` | Log tags |

## Layout and Formatting

**C++** (proposed `.clang-format`):
- Indent: 4 spaces (no tabs)
- Brace placement: Allman style (braces on new line)
- Line length: 100 soft, 120 hard
- Pointer alignment: `*` next to type: `uint8_t* buf`
- Column limit for function parameters: align or indent

**JavaScript / HTML** (proposed `.prettierrc`):
- Indent: 2 spaces
- Quotes: single (JS)
- Trailing commas: es5
- Semicolons: always

## Single Responsibility Principle

Each function should do ONE thing:

```cpp
// Bad: does Rx check, read, decrypt, AND dispatch
void processLoRa() { ... }

// Good: composed of single-responsibility steps
bool packetAvailable();
bool readPacket(uint8_t* buf, size_t& len);
CryptoResult decryptPacket(...);
void dispatchPacket(...);
```

## Error Handling

Return explicit error codes:

```cpp
// Good
CryptoResult cr = crypto.decrypt(...);
if (cr != CryptoResult::OK) {
    Serial.printf("[GW] decrypt fail: %d\n", (int)cr);
    return;
}
```

- Use `CryptoResult` enum for crypto errors
- Use `int16_t` return values with `RADIOLIB_ERR_NONE` for radio errors
- NEVER silently ignore errors
- Always log the error code

## Logging Convention

```
[PREFIX] <event>: <details>
```

| Prefix | Node | Example |
|--------|------|---------|
| `[GW]` | Gateway | `[GW] Toggle actuator 0002 -> ON` |
| `[SN]` | Sensor | `[SN] Seq=5 Moist=1200 Temp=22.5` |
| `[AN]` | Actuator | `[AN] pktType=0x20 seq=3 ctLen=8` |
| `[DBG]` | Any (debug) | `[DBG] Free heap: 278488` |

- Production logs: human-readable events (packets, errors, commands)
- Debug logs: key=value pairs, gated by `CORE_DEBUG_LEVEL` or `LORA_PROTO_DEBUG`

## Configuration Centralization

**DO NOT** scatter magic numbers across files:
- Frequency, SF, BW → `platformio.ini` build flags or `config.h`
- Timing constants → top of `main.cpp` with descriptive names
- GPIO pins → platform defines (`#ifdef TTGO_GATEWAY` / `#else`)

## Code Duplication

Extract shared patterns:
- ADC oversampling (sensor battery + moisture, actuator battery)
- TX frame assembly (sensor telemetry, actuator HB, actuator ACK)
- Crypto setup (`setKey` + `encrypt`/`decrypt` with error check)

## Avoiding Dead Code

- If a function is never called, remove it (git history preserves)
- If a field is never read, remove it (with NVS migration plan)
- Comment-out is NOT documentation — delete with commit message rationale

## Constants and Enums

Prefer `enum class` over `#define` for scoped constants:

```cpp
// Good
enum class PacketType : uint8_t {
    SENSOR_TELEMETRY = 0x10,
    ACTUATOR_COMMAND = 0x20,
    ACK = 0x30,
    HEARTBEAT = 0x40
};

// Avoid
#define PKT_TYPE_SENSOR 0x10
```

Wire-size constants use `constexpr size_t` with `static_assert`:

```cpp
constexpr size_t COMMAND_WIRE_SIZE = 8;
static_assert(sizeof(ActuatorCommand) == COMMAND_WIRE_SIZE);
```

## Comments

- Explain WHY, not WHAT
- No obvious comments: `// increment counter`
- TODO: use `// TODO(username): reason` format
- FIXME: use `// FIXME: reason` for known bugs

---

## Lint / Format Setup

| File | Tool | Purpose |
|------|------|---------|
| `.clang-format` | clang-format | Auto-format C++ |
| `.clang-tidy` | clang-tidy | C++ static analysis |
| `.eslintrc.json` | ESLint | JavaScript linting |
| `.prettierrc` | Prettier | JS/HTML/CSS formatting |

### Running Checks

```bash
# Format all C++ files
find src/ lib/ -name "*.cpp" -o -name "*.h" | xargs clang-format -i -style=file

# Check C++ lint
find src/ lib/ -name "*.cpp" -o -name "*.h" | xargs clang-tidy

# Format JS
npx prettier --write data/app.js

# PlatformIO check (if configured)
pio check
```

---

## Review Checklist

Before committing, verify:
- [ ] No magic numbers (all named constants)
- [ ] No commented-out code
- [ ] Error return values checked
- [ ] Log messages use correct prefix
- [ ] `static_assert` for wire format sizes
- [ ] No duplicate includes
- [ ] No unused variables or dead code
- [ ] One functional change per commit
