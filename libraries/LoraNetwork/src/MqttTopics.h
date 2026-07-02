#pragma once

#include <cstdio>
#include <string>

namespace MqttTopics {

inline std::string sensorTelemetry(uint16_t node_id) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "nodes/sensor_%u/telemetry", node_id);
    return buf;
}

inline std::string sensorStatus(uint16_t node_id) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "nodes/sensor_%u/status", node_id);
    return buf;
}

inline std::string sensorConfig(uint16_t node_id) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "nodes/sensor_%u/config", node_id);
    return buf;
}

inline std::string actuatorControl(uint16_t node_id) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "nodes/actuator_%u/control", node_id);
    return buf;
}

inline std::string actuatorAck(uint16_t node_id) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "nodes/actuator_%u/ack", node_id);
    return buf;
}

inline std::string actuatorStatus(uint16_t node_id) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "nodes/actuator_%u/status", node_id);
    return buf;
}

inline constexpr const char* GW_TELEMETRY  = "nodes/gateway/telemetry";
inline constexpr const char* GW_NODES      = "nodes/gateway/nodes";
inline constexpr const char* OTA_STATUS    = "nodes/gateway/ota/status";
inline constexpr const char* OTA_TRIGGER   = "nodes/gateway/ota/trigger";
inline constexpr const char* SYS_HEARTBEAT = "sys/heartbeat";
inline constexpr const char* SYS_ALARM     = "sys/alarm";
inline constexpr const char* SYS_LOG       = "sys/log";

inline std::string jsonTelemetry(uint16_t seq, float moisture_percent,
                                 float temp_c, float batt_v, uint8_t errors) {
    char buf[192];
    std::snprintf(buf, sizeof(buf),
        R"({"seq":%u,"moisture":%.1f,"temp":%.2f,"batt":%.2f,"err":%u})",
        seq, moisture_percent, temp_c, batt_v, errors);
    return buf;
}

inline std::string jsonAck(uint32_t ack_seq, bool ok, float batt_v) {
    char buf[96];
    std::snprintf(buf, sizeof(buf),
        R"({"ack_seq":%u,"result":"%s","batt":%.2f})",
        ack_seq, ok ? "ok" : "err", batt_v);
    return buf;
}

inline std::string jsonActuatorCmd(const char* cmd, int val) {
    char buf[64];
    std::snprintf(buf, sizeof(buf),
        R"({"cmd":"%s","val":%d})", cmd, val);
    return buf;
}

} // namespace MqttTopics
