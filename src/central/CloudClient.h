#pragma once
#include <Arduino.h>
#include <vector>

struct PendingCommand {
    uint32_t command_id;
    uint16_t node_id;
    String action;
};

class CloudClient {
public:
    CloudClient();
    
    void begin(const String& defaultUrl = "", const String& defaultApiKey = "");
    void loadConfig();
    void saveConfig(const String& url, const String& apiKey);
    
    String getUrl() const { return _url; }
    String getApiKey() const { return _apiKey; }
    
    void loop();
    
    bool sendTelemetry(uint16_t nodeId, float moisture, float tempC, float batteryV, uint32_t seq, uint8_t errorFlags, uint8_t rssi);
    bool sendHeartbeat(uint16_t nodeId, bool valveOpen, float batteryV);
    bool sendWeather(float rainMm, float windSpeed, int windDir, float tempC);
    bool registerNode(uint16_t nodeId, const String& type, const String& alias);
    bool sendAck(uint32_t commandId, bool ok, float batteryV);
    
    void setCommandCallback(void (*cb)(uint32_t cmdId, uint16_t nodeId, bool on));
    
private:
    String _url;
    String _apiKey;
    uint32_t _lastPollMs;
    void (*_cmdCallback)(uint32_t, uint16_t, bool);
    
    bool postJson(const String& endpoint, const String& payload);
    void pollCommands();
};
