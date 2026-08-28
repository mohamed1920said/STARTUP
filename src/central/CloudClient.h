#pragma once
#include <Arduino.h>

class CloudClient {
public:
    CloudClient();

    void begin(const char* defaultUrl = "", const char* defaultApiKey = "");
    void loadConfig();
    void saveConfig(const char* url, const char* apiKey, const char* caCert,
                    bool clearApiKey = false, bool clearCaCert = false);

    const char* getUrl() const { return _url; }
    const char* getApiKey() const { return _apiKey; }
    bool hasCaCert() const { return _caCert[0] != '\0'; }
    bool isConfigured() const { return _url[0] != '\0' && _apiKey[0] != '\0' && hasCaCert(); }

    void loop();

    bool sendTelemetry(uint16_t nodeId, uint16_t moistureRaw, float moisture, float tempC,
                       float batteryV, uint32_t seq, uint8_t errorFlags, int16_t rssi,
                       uint64_t epochMs);
    bool sendHeartbeat(uint16_t nodeId, bool valveCommanded, bool valveActual,
                       bool feedbackValid, float batteryV, float flowLpm,
                       float pressureBar, float tankPct, float pumpCurrentA,
                       uint8_t errorFlags, uint64_t epochMs);
    bool sendWeather(float rainMm, float windSpeed, int windDir, float tempC,
                     float humidityPct, float pressureHpa, float luminosityLux,
                     float batteryMv, uint64_t epochMs);
    bool registerNode(uint16_t nodeId, const char* type, const char* alias);
    bool sendAck(uint32_t commandId, bool ok, float batteryV, bool valveActual,
                 bool feedbackValid, uint8_t errorFlags, uint64_t epochMs);

    void setCommandCallback(void (*cb)(uint32_t cmdId, uint16_t nodeId, bool on));

private:
    char _url[128];
    char _apiKey[64];
    char _caCert[2048];
    uint32_t _lastPollMs;
    void (*_cmdCallback)(uint32_t, uint16_t, bool);

    bool postJson(const char* endpoint, const char* payload);
    void pollCommands();
};
