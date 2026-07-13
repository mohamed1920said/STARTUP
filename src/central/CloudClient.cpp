#include "CloudClient.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

CloudClient::CloudClient() : _lastPollMs(0), _cmdCallback(nullptr) {}

void CloudClient::begin(const String& defaultUrl, const String& defaultApiKey) {
    loadConfig();
    
    // Fallback to defaults if NVS is empty
    if (_url.isEmpty()) _url = defaultUrl;
    if (_apiKey.isEmpty()) _apiKey = defaultApiKey;
    
    // Remove trailing slash if present
    if (_url.endsWith("/")) {
        _url = _url.substring(0, _url.length() - 1);
    }
}

void CloudClient::loadConfig() {
    Preferences prefs;
    prefs.begin("cloud_cfg", true);
    _url = prefs.getString("url", "");
    _apiKey = prefs.getString("apiKey", "");
    prefs.end();
}

void CloudClient::saveConfig(const String& url, const String& apiKey) {
    _url = url;
    if (_url.endsWith("/")) _url = _url.substring(0, _url.length() - 1);
    _apiKey = apiKey;
    
    Preferences prefs;
    prefs.begin("cloud_cfg", false);
    prefs.putString("url", _url);
    prefs.putString("apiKey", _apiKey);
    prefs.end();
}

void CloudClient::setCommandCallback(void (*cb)(uint32_t, uint16_t, bool)) {
    _cmdCallback = cb;
}

void CloudClient::loop() {
    if (_url.isEmpty() || _apiKey.isEmpty()) return;
    
    uint32_t now = millis();
    if (now - _lastPollMs >= 10000) { // Poll every 10 seconds
        _lastPollMs = now;
        pollCommands();
    }
}

bool CloudClient::postJson(const String& endpoint, const String& payload) {
    if (_url.isEmpty() || _apiKey.isEmpty() || WiFi.status() != WL_CONNECTED) return false;
    
    HTTPClient http;
    http.begin(_url + endpoint);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-API-Key", _apiKey);
    
    int httpCode = http.POST(payload);
    http.end();
    
    return (httpCode >= 200 && httpCode < 300);
}

bool CloudClient::sendTelemetry(uint16_t nodeId, float moisture, float tempC, float batteryV, uint32_t seq, uint8_t errorFlags, uint8_t rssi) {
    JsonDocument doc;
    doc["node_id"] = nodeId;
    doc["moisture"] = moisture;
    doc["temperature"] = tempC;
    doc["battery"] = batteryV;
    doc["sequence"] = seq;
    doc["error_flags"] = errorFlags;
    doc["rssi"] = rssi;
    
    String payload;
    serializeJson(doc, payload);
    return postJson("/api/ingest/telemetry", payload);
}

bool CloudClient::sendHeartbeat(uint16_t nodeId, bool valveOpen, float batteryV) {
    JsonDocument doc;
    doc["node_id"] = nodeId;
    doc["valve_open"] = valveOpen;
    doc["battery"] = batteryV;
    
    String payload;
    serializeJson(doc, payload);
    return postJson("/api/ingest/heartbeat", payload);
}

bool CloudClient::sendWeather(float rainMm, float windSpeed, int windDir, float tempC) {
    JsonDocument doc;
    doc["rain_mm"] = rainMm;
    doc["wind_speed"] = windSpeed;
    doc["wind_dir"] = windDir;
    doc["temperature"] = tempC;
    
    String payload;
    serializeJson(doc, payload);
    return postJson("/api/ingest/weather", payload);
}

bool CloudClient::registerNode(uint16_t nodeId, const String& type, const String& alias) {
    JsonDocument doc;
    doc["node_id"] = nodeId;
    doc["type"] = type;
    doc["alias"] = alias;
    
    String payload;
    serializeJson(doc, payload);
    return postJson("/api/ingest/register-node", payload);
}

bool CloudClient::sendAck(uint32_t commandId, bool ok, float batteryV) {
    JsonDocument doc;
    doc["command_id"] = commandId;
    doc["ok"] = ok;
    doc["battery"] = batteryV;
    
    String payload;
    serializeJson(doc, payload);
    return postJson("/api/ingest/ack", payload);
}

void CloudClient::pollCommands() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    HTTPClient http;
    http.begin(_url + "/api/commands/pending");
    http.addHeader("X-API-Key", _apiKey);
    
    int httpCode = http.GET();
    if (httpCode == 200) {
        String response = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, response);
        
        if (!error && doc.is<JsonArray>()) {
            JsonArray arr = doc.as<JsonArray>();
            for (JsonObject cmd : arr) {
                uint32_t cmdId = cmd["command_id"];
                uint16_t nodeId = cmd["node_id"];
                String action = cmd["action"];
                
                if (_cmdCallback) {
                    bool on = (action == "VALVE_ON");
                    _cmdCallback(cmdId, nodeId, on);
                }
            }
        }
    }
    http.end();
}
