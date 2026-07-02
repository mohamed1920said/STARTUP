#pragma once
#include <stdint.h>
#include <stddef.h>

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

enum class PacketType : uint8_t {
    REGISTER_REQ       = 0x01,
    REGISTER_ACK       = 0x02,
    SENSOR_TELEMETRY   = 0x10,
    ACTUATOR_COMMAND   = 0x20,
    ACTUATOR_STATUS    = 0x21,
    ACK                = 0x30,
    HEARTBEAT          = 0xF0,
    UNKNOWN            = 0xFF
};

struct LoraFrame {
    uint8_t  node_id[NODE_ID_SIZE];
    uint8_t  iv[IV_NONCE_SIZE];
    uint8_t  pkt_type;
    uint8_t  ciphertext[LORA_MAX_CIPHERTEXT];
    uint16_t ciphertext_len;
    uint8_t  mic[MIC_SIZE];
};

struct SensorTelemetry {
    uint32_t sequence;
    uint16_t moisture_raw;
    int16_t  temperature_c;
    uint16_t battery_mv;
    uint8_t  error_flags;
} __attribute__((packed));

struct ActuatorCommand {
    uint32_t sequence;
    uint8_t  command;
    uint8_t  value;
    uint16_t timeout_s;
} __attribute__((packed));

struct AckPayload {
    uint32_t ack_seq;
    uint8_t  result;
    uint16_t battery_mv;
} __attribute__((packed));
