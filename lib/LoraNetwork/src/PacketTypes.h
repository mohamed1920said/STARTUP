#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstring>

/* ---------- constants ---------- */

constexpr size_t AES128_KEY_SIZE     = 16;
constexpr size_t AES128_BLOCK_SIZE   = 16;
constexpr size_t GCM_IV_SIZE         = 12;
constexpr size_t GCM_TAG_SIZE        = 16;
constexpr size_t GCM_TAG_TRUNCATED   = 4;
constexpr size_t LORA_HEADER_SIZE    = 15;    // 2 node_id + 12 iv + 1 pkt_type
constexpr size_t MIC_SIZE            = GCM_TAG_TRUNCATED;
constexpr size_t NODE_ID_SIZE        = 2;
constexpr size_t IV_NONCE_SIZE       = GCM_IV_SIZE;
constexpr size_t LORA_MAX_CIPHERTEXT = 64;
constexpr size_t LORA_MAX_PAYLOAD    = LORA_HEADER_SIZE + LORA_MAX_CIPHERTEXT + MIC_SIZE;

using NodeId = uint16_t;

/* ---------- wire protocol helpers ---------- */

inline void write16be(uint8_t* dst, uint16_t v) { dst[0]=uint8_t(v>>8); dst[1]=uint8_t(v); }
inline void write32be(uint8_t* dst, uint32_t v) { dst[0]=uint8_t(v>>24); dst[1]=uint8_t(v>>16); dst[2]=uint8_t(v>>8); dst[3]=uint8_t(v); }
inline uint16_t read16be(const uint8_t* src) { return uint16_t(src[0]<<8)|src[1]; }
inline uint32_t read32be(const uint8_t* src) {
    return (uint32_t(src[0]) << 24) | (uint32_t(src[1]) << 16) |
           (uint32_t(src[2]) << 8) | uint32_t(src[3]);
}

/* ---------- packet type enum ---------- */

enum class PacketType : uint8_t {
    SENSOR_TELEMETRY   = 0x10,
    ACTUATOR_COMMAND   = 0x20,
    ACK                = 0x30,
    HEARTBEAT          = 0x40,
    UNKNOWN            = 0xFF
};

/* ---------- in-memory helpers (for convenience, NOT transmitted raw) ---------- */

struct LoraFrame {
    uint8_t  node_id[2];
    uint8_t  iv[GCM_IV_SIZE];
    uint8_t  pkt_type;
    uint8_t  ciphertext[LORA_MAX_CIPHERTEXT];
    uint16_t ciphertext_len;
    uint8_t  mic[MIC_SIZE];
};

#define CHECK_SIZE(s, e) static_assert(sizeof(s) == (e), "sizeof(" #s ") mismatch")

/* ---------- SensorTelemetry: wire = 12 bytes ---------- */
struct SensorTelemetry {
    uint32_t sequence;
    uint16_t moisture_raw;
    uint8_t  moisture_pct;
    int16_t  temperature_c;
    uint16_t battery_mv;
    uint8_t  error_flags;
} __attribute__((packed));
CHECK_SIZE(SensorTelemetry, 12);

inline size_t serializeTelemetry(uint8_t* dst, const SensorTelemetry& s) {
    write32be(dst+0,  s.sequence);
    write16be(dst+4,  s.moisture_raw);
    dst[6]  = s.moisture_pct;
    write16be((uint8_t*)(dst+7), uint16_t(s.temperature_c));
    write16be(dst+9,  s.battery_mv);
    dst[11] = s.error_flags;
    return 12;
}
inline void deserializeTelemetry(const uint8_t* src, SensorTelemetry& s) {
    s.sequence      = read32be(src+0);
    s.moisture_raw  = read16be(src+4);
    s.moisture_pct  = src[6];
    s.temperature_c = int16_t(read16be(src+7));
    s.battery_mv    = read16be(src+9);
    s.error_flags   = src[11];
}

/* ---------- ActuatorCommand: wire = 8 bytes ---------- */
struct ActuatorCommand {
    uint32_t sequence;
    uint8_t  command;
    uint8_t  value;
    uint16_t timeout_s;
} __attribute__((packed));
CHECK_SIZE(ActuatorCommand, 8);

inline size_t serializeCommand(uint8_t* dst, const ActuatorCommand& c) {
    write32be(dst+0, c.sequence);
    dst[4] = c.command;
    dst[5] = c.value;
    write16be(dst+6, c.timeout_s);
    return 8;
}
inline void deserializeCommand(uint8_t* src, ActuatorCommand& c) {
    c.sequence  = read32be(src+0);
    c.command   = src[4];
    c.value     = src[5];
    c.timeout_s = read16be(src+6);
}

/* ---------- AckPayload: wire = 10 bytes ---------- */
struct AckPayload {
    uint32_t ack_seq;
    uint8_t  result;
    uint16_t battery_mv;
    uint8_t  valve_state;
    uint8_t  feedback_valid;
    uint8_t  error_flags;
} __attribute__((packed));
CHECK_SIZE(AckPayload, 10);

inline size_t serializeAck(uint8_t* dst, const AckPayload& a) {
    write32be(dst+0, a.ack_seq);
    dst[4] = a.result;
    write16be(dst+5, a.battery_mv);
    dst[7] = a.valve_state;
    dst[8] = a.feedback_valid;
    dst[9] = a.error_flags;
    return 10;
}
inline void deserializeAck(const uint8_t* src, AckPayload& a) {
    a.ack_seq    = read32be(src+0);
    a.result     = src[4];
    a.battery_mv = read16be(src+5);
    a.valve_state = src[7];
    a.feedback_valid = src[8];
    a.error_flags = src[9];
}

/* ---------- HeartbeatPayload: wire = 17 bytes ---------- */
struct HeartbeatPayload {
    uint32_t sequence;
    uint16_t battery_mv;
    uint8_t  valve_state;
    uint8_t  valve_commanded;
    uint8_t  feedback_valid;
    uint8_t  error_flags;
    uint16_t flow_centi_lpm;
    uint16_t pressure_kpa;
    uint8_t  tank_pct;
    uint16_t pump_current_ma;
} __attribute__((packed));
CHECK_SIZE(HeartbeatPayload, 17);

inline size_t serializeHeartbeat(uint8_t* dst, const HeartbeatPayload& h) {
    write32be(dst+0, h.sequence);
    write16be(dst+4, h.battery_mv);
    dst[6] = h.valve_state;
    dst[7] = h.valve_commanded;
    dst[8] = h.feedback_valid;
    dst[9] = h.error_flags;
    write16be(dst+10, h.flow_centi_lpm);
    write16be(dst+12, h.pressure_kpa);
    dst[14] = h.tank_pct;
    write16be(dst+15, h.pump_current_ma);
    return 17;
}
inline void deserializeHeartbeat(const uint8_t* src, HeartbeatPayload& h) {
    h.sequence    = read32be(src+0);
    h.battery_mv  = read16be(src+4);
    h.valve_state = src[6];
    h.valve_commanded = src[7];
    h.feedback_valid = src[8];
    h.error_flags = src[9];
    h.flow_centi_lpm = read16be(src+10);
    h.pressure_kpa = read16be(src+12);
    h.tank_pct = src[14];
    h.pump_current_ma = read16be(src+15);
}

#undef CHECK_SIZE
