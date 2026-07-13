#pragma once
#include <Arduino.h>

class CloudClient {
public:
    CloudClient();
    
    void begin(const char* defaultUrl = "", const char* defaultApiKey = "");
    void loadConfig();
    void saveConfig(const char* url, const char* apiKey);
    
    const char* getUrl() const { return _url; }
    const char* getApiKey() const { return _apiKey; }
    
    void loop();
    
    bool sendTelemetry(uint16_t nodeId, float moisture, float tempC, float batteryV, uint32_t seq, uint8_t errorFlags, uint8_t rssi);
    bool sendHeartbeat(uint16_t nodeId, bool valveOpen, float batteryV);
    bool sendWeather(float rainMm, float windSpeed, int windDir, float tempC);
    bool registerNode(uint16_t nodeId, const char* type, const char* alias);
    bool sendAck(uint32_t commandId, bool ok, float batteryV);
    
    void setCommandCallback(void (*cb)(uint32_t cmdId, uint16_t nodeId, bool on));
    
private:
    char _url[128];
    char _apiKey[64];
    uint32_t _lastPollMs;
    void (*_cmdCallback)(uint32_t, uint16_t, bool);
    
    bool postJson(const char* endpoint, const char* payload);
    void pollCommands();
};
