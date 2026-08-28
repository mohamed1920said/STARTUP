#pragma once
#include <ESPAsyncWebServer.h>
#include <functional>
#include <string>

struct SensorTelemetryData {
    uint16_t node_id;
    uint16_t moisture_raw;
    float    moisture_percent;
    float    temperature_c;
    float    battery_v;
    uint32_t sequence;
    int16_t  rssi;
    uint8_t  error_flags;
    uint32_t timestamp;
    uint64_t epoch_ms;
};

struct WeatherTelemetryData {
    float rain_mm;
    float wind_speed_ms;
    int16_t wind_dir_deg;
    float temperature_c;
    float humidity_pct;
    float pressure_hpa;
    float luminosity_lux;
    float battery_mv;
    uint32_t timestamp;
    uint64_t epoch_ms;
};

struct ActuatorStateData {
    uint16_t node_id;
    bool     valve_open;
    bool     valve_commanded_open;
    bool     feedback_valid;
    float    battery_v;
    float    flow_lpm;
    float    line_pressure_bar;
    float    tank_pct;
    float    pump_current_a;
    uint8_t  error_flags;
    uint32_t timestamp;
    uint64_t epoch_ms;
};

using ControlCallback = std::function<void(uint16_t node_id, bool on)>;
using ProvisionCallback = std::function<void(uint16_t node_id, const uint8_t psk[16], const char* type, const char* alias)>;
using RemoveCallback = std::function<void(uint16_t node_id)>;
using ActuatorCfgCallback = std::function<void(uint16_t node_id, bool autoMode, uint8_t threshold, uint16_t sensorId)>;
using NodesProvider = std::function<std::string()>;
using CloudCfgProvider = std::function<std::string()>;
using CloudCfgUpdateCallback = std::function<void(const char* url, const char* apiKey, const char* caCert)>;
using DatasetStatusProvider = std::function<std::string()>;
using AIStatusProvider = std::function<std::string()>;
using DatasetLabelCallback = std::function<bool(const char* label, const char* notes)>;
using DatasetClearCallback = std::function<bool()>;
using FieldCfgProvider = std::function<std::string()>;
using FieldCfgUpdateCallback = std::function<bool(uint16_t nodeId, uint16_t dryRaw, uint16_t wetRaw,
                                                   const char* crop, const char* growthStage,
                                                   const char* soilType, float zoneAreaM2,
                                                   float emitterFlowLph)>;

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
    void setDatasetPath(const char* path) { _datasetPath = path ? path : ""; }
    void setDatasetArchivePath(const char* path) { _datasetArchivePath = path ? path : ""; }
    void setDatasetStatusProvider(DatasetStatusProvider cb) { _datasetStatusProvider = cb; }
    void setAIStatusProvider(AIStatusProvider cb) { _aiStatusProvider = cb; }
    void onDatasetLabel(DatasetLabelCallback cb) { _datasetLabel_cb = cb; }
    void onDatasetClear(DatasetClearCallback cb) { _datasetClear_cb = cb; }
    void setFieldCfgProvider(FieldCfgProvider cb) { _fieldCfgProvider = cb; }
    void onFieldCfgUpdate(FieldCfgUpdateCallback cb) { _fieldCfgUpdate_cb = cb; }
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
    DatasetStatusProvider _datasetStatusProvider;
    AIStatusProvider _aiStatusProvider;
    DatasetLabelCallback _datasetLabel_cb;
    DatasetClearCallback _datasetClear_cb;
    FieldCfgProvider _fieldCfgProvider;
    FieldCfgUpdateCallback _fieldCfgUpdate_cb;
    String _datasetPath;
    String _datasetArchivePath;
    void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                   AwsEventType type, void* arg, uint8_t* data, size_t len);
    void serveStaticFiles();
    void registerApiHandlers();
    void registerOtaHandler();
    bool initLittleFS();
    bool authorize(AsyncWebServerRequest* request) const;
};
