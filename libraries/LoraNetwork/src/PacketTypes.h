#pragma once

#include <cstdint>
#include <cstddef>

constexpr size_t NODE_ID_SIZE       = 2;
constexpr size_t IV_NONCE_SIZE      = 12;
constexpr size_t PKT_TYPE_SIZE      = 1;
constexpr size_t MIC_SIZE           = 4;
constexpr size_t LORA_HEADER_SIZE   = NODE_ID_SIZE + IV_NONCE_SIZE + PKT_TYPE_SIZE;
constexpr size_t LORA_OVERHEAD      = LORA_HEADER_SIZE + MIC_SIZE;
constexpr size_t LORA_MAX_PAYLOAD   = 255;
constexpr size_t LORA_MAX_CIPHERTEXT = LORA_MAX_PAYLOAD - LORA_OVERHEAD;

using NodeId = uint16_t;

enum class PacketType : uint8_t {
    SENSOR_TELEMETRY     = 0x01,
    ACTUATOR_COMMAND     = 0x02,
    ACK                  = 0x03,
    REGISTER_REQ         = 0x04,
    REGISTER_ACCEPT      = 0x05,
    OTA_META             = 0x06,
    OTA_CHUNK            = 0x07,
    OTA_ACK              = 0x08,
    HEARTBEAT            = 0x09,
    REKEY                = 0x0A,
};

struct __attribute__((packed)) LoraFrame {
    uint8_t  node_id[NODE_ID_SIZE];
    uint8_t  iv[IV_NONCE_SIZE];
    uint8_t  pkt_type;
    uint8_t  ciphertext[LORA_MAX_CIPHERTEXT];
    uint8_t  mic[MIC_SIZE];
    size_t ciphertext_len;
};

struct __attribute__((packed)) SensorTelemetry {
    uint32_t sequence;
    uint16_t moisture_raw;
    int16_t  temperature_c;
    uint16_t battery_mv;
    uint8_t  error_flags;
};

struct __attribute__((packed)) ActuatorCommand {
    uint32_t sequence;
    uint8_t  command;
    uint8_t  value;
    uint16_t timeout_s;
};

struct __attribute__((packed)) AckPayload {
    uint32_t ack_seq;
    uint8_t  result;
    uint16_t battery_mv;
};

struct __attribute__((packed)) OtaMeta {
    uint32_t firmware_crc32;
    uint32_t firmware_size;
    uint16_t total_chunks;
    uint8_t  fw_major;
    uint8_t  fw_minor;
    uint8_t  fw_patch;
};

struct __attribute__((packed)) OtaChunk {
    uint16_t chunk_id;
    uint32_t chunk_crc32;
    uint8_t  data[128];
};

struct __attribute__((packed)) OtaChunkAck {
    uint16_t chunk_id;
    uint8_t  status;
};
