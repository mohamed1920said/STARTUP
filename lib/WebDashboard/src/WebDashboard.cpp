#include "WebDashboard.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <esp_system.h>
#include <WiFi.h>
#include <Update.h>

WebDashboard::WebDashboard() {}

bool WebDashboard::initLittleFS() {
    if (!LittleFS.begin(true)) {
        log_e("LittleFS mount failed");
        return false;
    }
    return true;
}

void WebDashboard::begin(AsyncWebServer& server) {
    _server = &server;
    if (!initLittleFS()) return;
    _ws = new AsyncWebSocket("/ws");
    _ws->onEvent([this](AsyncWebSocket* srv, AsyncWebSocketClient* client,
                         AwsEventType type, void* arg,
                         uint8_t* data, size_t len) {
        this->onWsEvent(srv, client, type, arg, data, len);
    });
    _server->addHandler(_ws);
    registerApiHandlers();
    serveStaticFiles();
    registerOtaHandler();
    _server->begin();
}

void WebDashboard::loop() {
    if (_ws) _ws->cleanupClients(3);
}

void WebDashboard::onWsEvent(AsyncWebSocket* server,
                              AsyncWebSocketClient* client,
                              AwsEventType type, void* arg,
                              uint8_t* data, size_t len) {
    (void)server;
    switch (type) {
        case WS_EVT_CONNECT:
            log_i("WS[%u] connected", client->id());
            break;
        case WS_EVT_DISCONNECT:
            log_i("WS[%u] disconnected", client->id());
            break;
        case WS_EVT_DATA: {
            AwsFrameInfo* info = (AwsFrameInfo*)arg;
            if (info->final && info->index == 0 && info->len == len) {
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, data, len);
                if (!err) {
                    const char* action = doc["action"];
                    if (action && strcmp(action, "toggle_valve") == 0) {
                        uint16_t nid = doc["node_id"] | 0;
                        bool on = doc["value"] | false;
                        if (_control_cb) _control_cb(nid, on);
                    } else if (action && strcmp(action, "remove_node") == 0) {
                        uint16_t nid = doc["node_id"] | 0;
                        if (_remove_cb) _remove_cb(nid);
                    } else if (action && strcmp(action, "add_node") == 0) {
                        uint16_t nid = doc["node_id"] | 0;
                        const char* psk_str = doc["psk"];
                        const char* type = doc["node_type"];
                        const char* alias = doc["alias"] | "";
                        if (psk_str && strlen(psk_str) == 32 && _provision_cb) {
                            uint8_t psk[16];
                            for (int i = 0; i < 16; i++) {
                                char byte_str[3] = {psk_str[i*2], psk_str[i*2+1], 0};
                                psk[i] = strtol(byte_str, NULL, 16);
                            }
                            _provision_cb(nid, psk, type, alias);
                        }
                    }
                }
            }
            break;
        }
        default: break;
    }
}

void WebDashboard::pushSensorTelemetry(const SensorTelemetryData& d) {
    if (_ws) { std::string s = serializeSensor(d); _ws->textAll(s.c_str()); }
}
void WebDashboard::pushWeatherTelemetry(const WeatherTelemetryData& d) {
    if (_ws) { std::string s = serializeWeather(d); _ws->textAll(s.c_str()); }
}
void WebDashboard::pushActuatorState(const ActuatorStateData& d) {
    if (_ws) { std::string s = serializeActuator(d); _ws->textAll(s.c_str()); }
}
void WebDashboard::pushRaw(const char* json) {
    if (_ws) _ws->textAll(json);
}

void WebDashboard::pushLog(const char* level, const char* message) {
    if (!_ws) return;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        R"({"type":"log","level":"%s","msg":"%s","ts":%u})",
        level, message, (unsigned)millis());
    _ws->textAll(buf);
}

std::string WebDashboard::serializeSensor(const SensorTelemetryData& d) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        R"({"type":"sensor","id":%u,"moisture":%.1f,"temp":%.2f,)"
        R"("batt":%.2f,"seq":%u,"rssi":%ld,"ts":%u})",
        d.node_id, d.moisture_percent, d.temperature_c,
        d.battery_v, d.sequence, d.rssi, d.timestamp);
    return buf;
}
std::string WebDashboard::serializeWeather(const WeatherTelemetryData& d) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        R"({"type":"weather","rain":%.2f,"wind":%.2f,"dir":%u,"temp":%.2f,"ts":%u})",
        d.rain_mm, d.wind_speed_ms, d.wind_dir_deg, d.temperature_c, (unsigned)millis());
    return buf;
}
std::string WebDashboard::serializeActuator(const ActuatorStateData& d) {
    char buf[196];
    std::snprintf(buf, sizeof(buf),
        R"({"type":"actuator","id":%u,"valve":%s,"batt":%.2f,"ts":%u})",
        d.node_id, d.valve_open ? "true" : "false", d.battery_v, d.timestamp);
    return buf;
}

void WebDashboard::serveStaticFiles() {
    _server->serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    _server->onNotFound([](AsyncWebServerRequest* request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
        } else {
            request->send(LittleFS, "/index.html", "text/html");
        }
    });
}

void WebDashboard::registerApiHandlers() {
    _server->on("/api/health", HTTP_GET, [](AsyncWebServerRequest* request) {
        String json = "{";
        json += "\"uptime\":" + String(millis() / 1000);
        json += ",\"free_heap\":" + String(ESP.getFreeHeap());
        json += ",\"rssi\":" + String(WiFi.RSSI());
        json += "}";
        request->send(200, "application/json", json);
    });
    _server->on("/api/nodes", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (_nodesProvider) {
            request->send(200, "application/json", _nodesProvider().c_str());
        } else {
            request->send(200, "application/json", "[]");
        }
    });
    _server->on("/api/control", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data,
                size_t len, size_t index, size_t total) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, data, len);
            if (err) { request->send(400, "application/json", R"({"error":"bad_json"})"); return; }
            uint16_t nid = doc["node_id"] | 0;
            bool on = doc["value"] | false;
            if (_control_cb) _control_cb(nid, on);
            request->send(200, "application/json", R"({"status":"ok"})");
        });
}

void WebDashboard::registerOtaHandler() {
    _server->on("/api/ota/upload", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            request->send(200, "application/json", R"({"status":"ok","msg":"upload started"})");
        },
        [](AsyncWebServerRequest* request, const String& filename,
            size_t index, uint8_t* data, size_t len, bool final) {
            if (!index) {
                log_i("OTA: %s (%u bytes)", filename.c_str(), request->contentLength());
                if (!Update.begin(request->contentLength(), U_FLASH)) log_e("OTA begin failed");
            }
            if (Update.write(data, len) != len) log_e("OTA write failed");
            if (final) {
                if (Update.end(true)) { log_i("OTA success"); delay(1000); ESP.restart(); }
                else { log_e("OTA end failed: %s", Update.errorString()); request->send(500, "text/plain", "OTA failed"); }
            }
        });
}
