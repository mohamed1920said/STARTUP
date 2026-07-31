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
    float humidity_pct;
    float pressure_hpa;
    float luminosity_lux;
    float battery_mv;
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
using CloudCfgProvider = std::function<std::string()>;
using CloudCfgUpdateCallback = std::function<void(const char* url, const char* apiKey, const char* caCert)>;

class WebDashboard {
public:
    WebDashboard();
    void begin(AsyncWebServer& server, const char* username, const char* password);
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
    void setCloudCfgProvider(CloudCfgProvider cb) { _cloudCfgProvider = cb; }
    void onCloudCfgUpdate(CloudCfgUpdateCallback cb) { _cloudCfgUpdate_cb = cb; }
    void loop();
private:
    AsyncWebServer* _server = nullptr;
    AsyncWebSocket* _ws = nullptr;
    String _username;
    String _password;
    bool _otaOk = false;
    bool _otaRestartPending = false;
    uint32_t _otaRestartAt = 0;
    ControlCallback _control_cb;
    ProvisionCallback _provision_cb;
    RemoveCallback _remove_cb;
    ActuatorCfgCallback _actuator_cfg_cb;
    NodesProvider _nodesProvider;
    CloudCfgProvider _cloudCfgProvider;
    CloudCfgUpdateCallback _cloudCfgUpdate_cb;
    void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                   AwsEventType type, void* arg, uint8_t* data, size_t len);
    void serveStaticFiles();
    void registerApiHandlers();
    void registerOtaHandler();
    bool initLittleFS();
    bool authorize(AsyncWebServerRequest* request) const;
};
