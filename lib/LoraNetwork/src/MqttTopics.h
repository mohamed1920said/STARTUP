#pragma once
#include <string>
#include <cstdio>

struct MqttTopics {
    static constexpr const char* GW_TELEMETRY  = "gateway/weather";
    static constexpr const char* GW_LOG        = "gateway/log";
    static constexpr const char* GW_STATUS     = "gateway/status";
    static constexpr const char* GW_DOWNLINK   = "gateway/downlink";
    static constexpr const char* GW_BROKER_CLI = "gateway/clients";

    static std::string sensorTelemetry(uint16_t nodeId) {
        char buf[32]; std::snprintf(buf, sizeof(buf), "node/%04X/telemetry", nodeId);
        return buf;
    }
    static std::string actuatorAck(uint16_t nodeId) {
        char buf[32]; std::snprintf(buf, sizeof(buf), "node/%04X/ack", nodeId);
        return buf;
    }
    static std::string actuatorCmd(uint16_t nodeId) {
        char buf[32]; std::snprintf(buf, sizeof(buf), "node/%04X/cmd", nodeId);
        return buf;
    }
    static std::string nodeStatus(uint16_t nodeId) {
        char buf[32]; std::snprintf(buf, sizeof(buf), "node/%04X/status", nodeId);
        return buf;
    }
    static std::string jsonTelemetry(uint32_t seq, float moisture,
                                      float temp, float batt, uint8_t err) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            R"({"seq":%u,"moisture":%.1f,"temp_c":%.2f,"batt_v":%.2f,"err":%u})",
            seq, moisture, temp, batt, err);
        return buf;
    }
    static std::string jsonAck(uint32_t ackSeq, bool ok, float batt) {
        char buf[96];
        std::snprintf(buf, sizeof(buf),
            R"({"ack_seq":%u,"ok":%s,"batt_v":%.2f})", ackSeq, ok ? "true" : "false", batt);
        return buf;
    }
};
