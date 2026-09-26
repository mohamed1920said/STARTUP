#include "CloudClient.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <cstring>

static void jsonEscape(const char* src, char* dst, size_t sz) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < sz - 1; i++) {
        char c = src[i];
        if (c == '"' || c == '\\') {
            if (j + 2 >= sz) break;
            dst[j++] = '\\'; dst[j++] = c;
        } else if (c == '\n' || c == '\r' || c == '\t' || static_cast<unsigned char>(c) < 0x20) {
            if (j + 6 >= sz) break;
            snprintf(dst + j, sz - j, "\\u%04X", static_cast<unsigned char>(c));
            j += 6;
        } else {
            dst[j++] = c;
        }
    }
    dst[j] = '\0';
}

static float finiteOrMissing(float value) {
    return isnan(value) || isinf(value) ? -1.0f : value;
}

CloudClient::CloudClient() : _lastPollMs(0), _cmdCallback(nullptr) {
    _url[0] = '\0';
    _apiKey[0] = '\0';
    _caCert[0] = '\0';
}

void CloudClient::begin(const char* defaultUrl, const char* defaultApiKey) {
    loadConfig();
    if (_url[0] == '\0' && defaultUrl[0]) strlcpy(_url, defaultUrl, sizeof(_url));
    if (_apiKey[0] == '\0' && defaultApiKey[0]) strlcpy(_apiKey, defaultApiKey, sizeof(_apiKey));
    size_t ulen = strlen(_url);
    if (ulen > 0 && _url[ulen - 1] == '/') _url[ulen - 1] = '\0';
}

void CloudClient::loadConfig() {
    Preferences prefs;
    prefs.begin("cloud_cfg", true);
    String u = prefs.getString("url", "");
    String k = prefs.getString("apiKey", "");
    String ca = prefs.getString("caCert", "");
    strlcpy(_url, u.c_str(), sizeof(_url));
    strlcpy(_apiKey, k.c_str(), sizeof(_apiKey));
    strlcpy(_caCert, ca.c_str(), sizeof(_caCert));
    prefs.end();
}

void CloudClient::saveConfig(const char* url, const char* apiKey, const char* caCert,
                             bool clearApiKey, bool clearCaCert) {
    strlcpy(_url, url ? url : "", sizeof(_url));
    size_t ulen = strlen(_url);
    if (ulen > 0 && _url[ulen - 1] == '/') _url[ulen - 1] = '\0';
    if (clearApiKey) _apiKey[0] = '\0';
    else if (apiKey && apiKey[0]) strlcpy(_apiKey, apiKey, sizeof(_apiKey));
    if (clearCaCert) _caCert[0] = '\0';
    else if (caCert && caCert[0]) strlcpy(_caCert, caCert, sizeof(_caCert));

    Preferences prefs;
    prefs.begin("cloud_cfg", false);
    prefs.putString("url", _url);
    prefs.putString("apiKey", _apiKey);
    prefs.putString("caCert", _caCert);
    prefs.end();
}

void CloudClient::setCommandCallback(void (*cb)(uint32_t, uint16_t, bool)) {
    _cmdCallback = cb;
}

void CloudClient::loop() {
    if (_url[0] == '\0' || _apiKey[0] == '\0') return;
    uint32_t now = millis();
    if (now - _lastPollMs >= 10000) {
        _lastPollMs = now;
        pollCommands();
    }
}

bool CloudClient::postJson(const char* endpoint, const char* payload) {
    if (_url[0] == '\0' || _apiKey[0] == '\0' || _caCert[0] == '\0' || WiFi.status() != WL_CONNECTED) return false;
    if (strncmp(_url, "https://", 8) != 0) return false;

    char fullUrl[256];
    snprintf(fullUrl, sizeof(fullUrl), "%s%s", _url, endpoint);

    HTTPClient http;
    if (!http.begin(fullUrl, _caCert)) return false;
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-API-Key", _apiKey);
    http.setTimeout(3000);

    int httpCode = http.POST((uint8_t*)payload, strlen(payload));
    http.end();
    return (httpCode >= 200 && httpCode < 300);
}

bool CloudClient::sendTelemetry(uint16_t nodeId, uint16_t moistureRaw, float moisture,
                                float tempC, float batteryV, uint32_t seq,
                                uint8_t errorFlags, int16_t rssi, uint64_t epochMs) {
    char payload[384];
    snprintf(payload, sizeof(payload),
        R"({"node_id":%u,"moisture_raw":%u,"moisture":%.2f,"temperature":%.2f,"battery":%.3f,"sequence":%u,"error_flags":%u,"rssi_dbm":%d,"epoch_ms":%llu})",
        nodeId, moistureRaw, finiteOrMissing(moisture), finiteOrMissing(tempC),
        finiteOrMissing(batteryV), seq, errorFlags, rssi,
        static_cast<unsigned long long>(epochMs));
    return postJson("/api/ingest/telemetry", payload);
}

bool CloudClient::sendHeartbeat(uint16_t nodeId, bool valveCommanded, bool valveActual,
                                bool feedbackValid, float batteryV, float flowLpm,
                                float pressureBar, float tankPct, float pumpCurrentA,
                                uint8_t errorFlags, uint64_t epochMs) {
    char payload[384];
    snprintf(payload, sizeof(payload),
        R"({"node_id":%u,"valve_commanded":%s,"valve_actual":%s,"feedback_valid":%s,"battery":%.3f,"flow_lpm":%.3f,"pressure_bar":%.3f,"tank_pct":%.1f,"pump_current_a":%.3f,"error_flags":%u,"epoch_ms":%llu})",
        nodeId, valveCommanded ? "true" : "false", valveActual ? "true" : "false",
        feedbackValid ? "true" : "false", finiteOrMissing(batteryV), finiteOrMissing(flowLpm),
        finiteOrMissing(pressureBar), finiteOrMissing(tankPct), finiteOrMissing(pumpCurrentA),
        errorFlags, static_cast<unsigned long long>(epochMs));
    return postJson("/api/ingest/heartbeat", payload);
}

bool CloudClient::sendWeather(float rainMm, float windSpeed, int windDir, float tempC,
                              float humidityPct, float pressureHpa, float luminosityLux,
                              float batteryMv, uint64_t epochMs) {
    char payload[384];
    snprintf(payload, sizeof(payload),
        R"({"rain_mm":%.3f,"wind_speed":%.3f,"wind_dir":%d,"temperature":%.2f,"humidity":%.2f,"pressure_hpa":%.2f,"luminosity_lux":%.1f,"battery_mv":%.1f,"epoch_ms":%llu})",
        finiteOrMissing(rainMm), finiteOrMissing(windSpeed), windDir,
        finiteOrMissing(tempC), finiteOrMissing(humidityPct), finiteOrMissing(pressureHpa),
        finiteOrMissing(luminosityLux), finiteOrMissing(batteryMv),
        static_cast<unsigned long long>(epochMs));
    return postJson("/api/ingest/weather", payload);
}

bool CloudClient::registerNode(uint16_t nodeId, const char* type, const char* alias) {
    char aliasEsc[144];
    jsonEscape(alias, aliasEsc, sizeof(aliasEsc));
    char payload[256];
    snprintf(payload, sizeof(payload),
        R"({"node_id":%u,"type":"%s","alias":"%s"})",
        nodeId, type, aliasEsc);
    return postJson("/api/ingest/register-node", payload);
}

bool CloudClient::sendAck(uint32_t commandId, bool ok, float batteryV, bool valveActual,
                          bool feedbackValid, uint8_t errorFlags, uint64_t epochMs) {
    char payload[256];
    snprintf(payload, sizeof(payload),
        R"({"command_id":%u,"ok":%s,"battery":%.3f,"valve_actual":%s,"feedback_valid":%s,"error_flags":%u,"epoch_ms":%llu})",
        commandId, ok ? "true" : "false", finiteOrMissing(batteryV), valveActual ? "true" : "false",
        feedbackValid ? "true" : "false", errorFlags,
        static_cast<unsigned long long>(epochMs));
    return postJson("/api/ingest/ack", payload);
}

void CloudClient::pollCommands() {
    if (WiFi.status() != WL_CONNECTED || _caCert[0] == '\0') return;
    if (strncmp(_url, "https://", 8) != 0) return;

    char fullUrl[256];
    snprintf(fullUrl, sizeof(fullUrl), "%s/api/commands/pending", _url);

    HTTPClient http;
    if (!http.begin(fullUrl, _caCert)) return;
    http.addHeader("X-API-Key", _apiKey);
    http.setTimeout(3000);

    int httpCode = http.GET();
    if (httpCode == 200) {
        String response = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, response);
        if (!error && doc.is<JsonArray>()) {
            JsonArray arr = doc.as<JsonArray>();
            for (JsonObject cmd : arr) {
                const JsonVariantConst cmdIdValue = cmd["command_id"];
                const JsonVariantConst nodeIdValue = cmd["node_id"];
                const char* action = cmd["action"];
                const bool validAction = action &&
                    (strcmp(action, "VALVE_ON") == 0 || strcmp(action, "VALVE_OFF") == 0);
                if (!cmdIdValue.is<uint64_t>() || !nodeIdValue.is<uint64_t>() || !validAction)
                    continue;
                const uint64_t rawCmdId = cmdIdValue.as<uint64_t>();
                const uint64_t rawNodeId = nodeIdValue.as<uint64_t>();
                if (rawCmdId == 0 || rawCmdId > UINT32_MAX ||
                    rawNodeId == 0 || rawNodeId >= UINT16_MAX)
                    continue;
                const uint32_t cmdId = static_cast<uint32_t>(rawCmdId);
                const uint16_t nodeId = static_cast<uint16_t>(rawNodeId);
                if (_cmdCallback) {
                    const bool on = strcmp(action, "VALVE_ON") == 0;
                    _cmdCallback(cmdId, nodeId, on);
                }
            }
        }
    }
    http.end();
}
