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

class WebDashboard {
public:
    WebDashboard();

    void begin(AsyncWebServer& server);

    void pushSensorTelemetry(const SensorTelemetryData& data);
    void pushWeatherTelemetry(const WeatherTelemetryData& data);
    void pushActuatorState(const ActuatorStateData& data);
    void pushLog(const char* level, const char* message);

    void onActuatorToggle(ControlCallback cb) { _control_cb = cb; }
    void loop();

private:
    AsyncWebServer* _server = nullptr;
    AsyncWebSocket* _ws = nullptr;
    ControlCallback _control_cb;

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
