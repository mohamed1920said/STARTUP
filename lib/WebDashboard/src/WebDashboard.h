#pragma once
#include <ESPAsyncWebServer.h>
#include <functional>
#include <string>

struct SensorTelemetryData {
    uint16_t node_id;
    float    moisture_percent;
    float    temperature_c;
    float    battery_v;
    uint32_t sequence;
    uint32_t rssi;
    uint32_t timestamp;
};

struct WeatherTelemetryData {
    float rain_mm;
    float wind_speed_ms;
    uint16_t wind_dir_deg;
    float temperature_c;
};

struct ActuatorStateData {
    uint16_t node_id;
    bool     valve_open;
    float    battery_v;
    uint32_t timestamp;
};

using ControlCallback = std::function<void(uint16_t node_id, bool on)>;
using ProvisionCallback = std::function<void(uint16_t node_id, const uint8_t psk[16], const char* type, const char* alias)>;
using RemoveCallback = std::function<void(uint16_t node_id)>;
using ActuatorCfgCallback = std::function<void(uint16_t node_id, bool autoMode, uint8_t threshold, uint16_t sensorId)>;
using NodesProvider = std::function<std::string()>;

class WebDashboard {
public:
    WebDashboard();
    void begin(AsyncWebServer& server);
    void pushSensorTelemetry(const SensorTelemetryData& data);
    void pushWeatherTelemetry(const WeatherTelemetryData& data);
    void pushActuatorState(const ActuatorStateData& data);
    void pushLog(const char* level, const char* message);
    void pushRaw(const char* json);
    void onActuatorToggle(ControlCallback cb) { _control_cb = cb; }
    void onNodeProvision(ProvisionCallback cb) { _provision_cb = cb; }
    void onNodeRemove(RemoveCallback cb) { _remove_cb = cb; }
    void onActuatorConfig(ActuatorCfgCallback cb) { _actuator_cfg_cb = cb; }
    void setNodesProvider(NodesProvider cb) { _nodesProvider = cb; }
    void loop();
private:
    AsyncWebServer* _server = nullptr;
    AsyncWebSocket* _ws = nullptr;
    ControlCallback _control_cb;
    ProvisionCallback _provision_cb;
    RemoveCallback _remove_cb;
    ActuatorCfgCallback _actuator_cfg_cb;
    NodesProvider _nodesProvider;
    void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                   AwsEventType type, void* arg, uint8_t* data, size_t len);
    std::string serializeSensor(const SensorTelemetryData& d);
    std::string serializeWeather(const WeatherTelemetryData& d);
    std::string serializeActuator(const ActuatorStateData& d);
    void serveStaticFiles();
    void registerApiHandlers();
    void registerOtaHandler();
    bool initLittleFS();
};
