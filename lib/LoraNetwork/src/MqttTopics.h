#pragma once
#include <cstdio>

struct MqttTopics {
    static constexpr const char* GW_TELEMETRY  = "gateway/weather";
    static constexpr const char* GW_LOG        = "gateway/log";
    static constexpr const char* GW_STATUS     = "gateway/status";
    static constexpr const char* GW_DOWNLINK   = "gateway/downlink";
    static constexpr const char* GW_BROKER_CLI = "gateway/clients";

    static const char* sensorTelemetry(uint16_t nodeId, char* buf, size_t sz) {
        snprintf(buf, sz, "node/%04X/telemetry", nodeId);
        return buf;
    }
    static const char* actuatorAck(uint16_t nodeId, char* buf, size_t sz) {
        snprintf(buf, sz, "node/%04X/ack", nodeId);
        return buf;
    }
    static const char* actuatorCmd(uint16_t nodeId, char* buf, size_t sz) {
        snprintf(buf, sz, "node/%04X/cmd", nodeId);
        return buf;
    }
    static const char* nodeStatus(uint16_t nodeId, char* buf, size_t sz) {
        snprintf(buf, sz, "node/%04X/status", nodeId);
        return buf;
    }
    static void formatTelemetry(char* buf, size_t sz, uint32_t seq, float moisture,
                                 float temp, float batt, uint8_t err) {
        snprintf(buf, sz,
            R"({"seq":%u,"moisture":%.1f,"temp_c":%.2f,"batt_v":%.2f,"err":%u})",
            seq, moisture, temp, batt, err);
    }
    static void formatAck(char* buf, size_t sz, uint32_t ackSeq, bool ok, float batt) {
        snprintf(buf, sz,
            R"({"ack_seq":%u,"ok":%s,"batt_v":%.2f})", ackSeq, ok ? "true" : "false", batt);
    }
};
