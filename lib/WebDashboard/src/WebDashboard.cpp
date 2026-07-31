#include "WebDashboard.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <esp_system.h>
#include <WiFi.h>
#include <Update.h>
#include <cctype>

namespace {
struct RequestBody {
    String json;
    bool overflow = false;
};

void collectJsonBody(AsyncWebServerRequest* request, uint8_t* data, size_t len,
                     size_t index, size_t total) {
    if (index == 0) request->_tempObject = new RequestBody();
    auto* body = static_cast<RequestBody*>(request->_tempObject);
    if (!body) return;
    if (total > 4096 || body->json.length() + len > 4096) {
        body->overflow = true;
        return;
    }
    body->json.concat(reinterpret_cast<const char*>(data), len);
}
}

WebDashboard::WebDashboard() {}

bool WebDashboard::initLittleFS() {
    if (!LittleFS.begin(true)) {
        log_e("LittleFS mount failed");
        return false;
    }
    return true;
}

void WebDashboard::begin(AsyncWebServer& server, const char* username, const char* password) {
    _server = &server;
    _username = username ? username : "admin";
    _password = password ? password : "";
    if (!initLittleFS()) return;
    _ws = new AsyncWebSocket("/ws");
    _ws->setAuthentication(_username, _password);
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
    if (_otaRestartPending && millis() >= _otaRestartAt) ESP.restart();
}

bool WebDashboard::authorize(AsyncWebServerRequest* request) const {
    if (_password.length() && !request->authenticate(_username.c_str(), _password.c_str())) {
        request->requestAuthentication();
        return false;
    }
    return true;
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
                        bool validHex = psk_str && strlen(psk_str) == 32;
                        for (size_t i = 0; validHex && i < 32; ++i) validHex = isxdigit((unsigned char)psk_str[i]);
                        bool validType = type && (strcmp(type, "sensor") == 0 || strcmp(type, "actuator") == 0);
                        bool validAlias = alias && strlen(alias) <= 23;
                        if (nid != 0 && validHex && validType && validAlias && _provision_cb) {
                            uint8_t psk[16];
                            for (int i = 0; i < 16; i++) {
                                char byte_str[3] = {psk_str[i*2], psk_str[i*2+1], 0};
                                psk[i] = strtol(byte_str, NULL, 16);
                            }
                            _provision_cb(nid, psk, type, alias);
                        }
                    } else if (action && strcmp(action, "set_actuator_config") == 0) {
                        uint16_t nid = doc["node_id"] | 0;
                        bool autoMode = doc["auto_mode"] | false;
                        uint8_t threshold = doc["threshold"] | 50;
                        uint16_t sensorId = doc["sensor_id"] | 0;
                        if (_actuator_cfg_cb) _actuator_cfg_cb(nid, autoMode, threshold, sensorId);
                    }
                }
            }
            break;
        }
        default: break;
    }
}

void WebDashboard::pushSensorTelemetry(const SensorTelemetryData& d) {
    if (!_ws) return;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        R"({"type":"sensor","id":%u,"moisture":%.1f,"temp":%.2f,)"
        R"("batt":%.2f,"seq":%u,"rssi":%u,"ts":%u})",
        (unsigned)d.node_id, d.moisture_percent, d.temperature_c,
        d.battery_v, d.sequence, (unsigned)d.rssi, d.timestamp);
    _ws->textAll(buf);
}
void WebDashboard::pushWeatherTelemetry(const WeatherTelemetryData& d) {
    if (!_ws) return;
    char buf[256];
    auto val = [](float v) { return isnan(v) || isinf(v) ? 0.0f : v; };
    std::snprintf(buf, sizeof(buf),
        R"({"type":"weather","rain":%.2f,"wind":%.2f,"dir":%u,"temp":%.1f,"hum":%.1f,"pres":%.1f,"lux":%.0f,"bat":%.0f,"ts":%u})",
        val(d.rain_mm), val(d.wind_speed_ms), d.wind_dir_deg, val(d.temperature_c),
        val(d.humidity_pct), val(d.pressure_hpa), val(d.luminosity_lux), val(d.battery_mv),
        (unsigned)millis());
    _ws->textAll(buf);
}
void WebDashboard::pushActuatorState(const ActuatorStateData& d) {
    if (!_ws) return;
    char buf[192];
    std::snprintf(buf, sizeof(buf),
        R"({"type":"actuator","id":%u,"valve":%s,"batt":%.2f,"ts":%u})",
        d.node_id, d.valve_open ? "true" : "false", d.battery_v, d.timestamp);
    _ws->textAll(buf);
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

void WebDashboard::serveStaticFiles() {
    _server->serveStatic("/", LittleFS, "/").setDefaultFile("index.html")
        .setAuthentication(_username, _password);
    _server->onNotFound([this](AsyncWebServerRequest* request) {
        if (!authorize(request)) return;
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
    }).setAuthentication(_username, _password);
    _server->on("/api/nodes", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (_nodesProvider) {
            request->send(200, "application/json", _nodesProvider().c_str());
        } else {
            request->send(200, "application/json", "[]");
        }
    }).setAuthentication(_username, _password);
    _server->on("/api/cloud-config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (_cloudCfgProvider) {
            request->send(200, "application/json", _cloudCfgProvider().c_str());
        } else {
            request->send(200, "application/json", "{}");
        }
    }).setAuthentication(_username, _password);
    _server->on("/api/cloud-config", HTTP_POST,
        [this](AsyncWebServerRequest* request) {
            auto* body = static_cast<RequestBody*>(request->_tempObject);
            if (!body || body->overflow) {
                delete body; request->_tempObject = nullptr;
                request->send(413, "application/json", R"({"error":"body_too_large"})"); return;
            }
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, body->json);
            delete body; request->_tempObject = nullptr;
            if (err) { request->send(400, "application/json", R"({"error":"bad_json"})"); return; }
            const char* url = doc["url"] | "";
            const char* apiKey = doc["apiKey"] | "";
            const char* caCert = doc["caCert"] | "";
            if (strlen(url) >= 128 || strlen(apiKey) >= 64 || strlen(caCert) >= 2048) {
                request->send(400, "application/json", R"({"error":"invalid_length"})"); return;
            }
            if (url[0] && strncmp(url, "https://", 8) != 0) {
                request->send(400, "application/json", R"({"error":"https_required"})"); return;
            }
            if (_cloudCfgUpdate_cb) _cloudCfgUpdate_cb(url, apiKey, caCert);
            request->send(200, "application/json", R"({"status":"ok"})");
        },
        nullptr,
        collectJsonBody).setAuthentication(_username, _password);
    _server->on("/api/restart", HTTP_POST, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", R"({"status":"restarting"})");
        delay(100);
        ESP.restart();
    }).setAuthentication(_username, _password);
    _server->on("/api/control", HTTP_POST,
        [this](AsyncWebServerRequest* request) {
            auto* body = static_cast<RequestBody*>(request->_tempObject);
            if (!body || body->overflow) {
                delete body; request->_tempObject = nullptr;
                request->send(413, "application/json", R"({"error":"body_too_large"})"); return;
            }
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, body->json);
            delete body; request->_tempObject = nullptr;
            if (err) { request->send(400, "application/json", R"({"error":"bad_json"})"); return; }
            uint16_t nid = doc["node_id"] | 0;
            if (nid == 0 || !doc["value"].is<bool>()) {
                request->send(400, "application/json", R"({"error":"invalid_control"})"); return;
            }
            if (_control_cb) _control_cb(nid, doc["value"].as<bool>());
            request->send(200, "application/json", R"({"status":"ok"})");
        },
        nullptr,
        collectJsonBody).setAuthentication(_username, _password);
}

void WebDashboard::registerOtaHandler() {
    _server->on("/api/ota/upload", HTTP_POST,
        [this](AsyncWebServerRequest* request) {
            if (_otaOk) {
                request->send(200, "application/json", R"({"status":"ok","msg":"firmware verified; restarting"})");
                _otaRestartAt = millis() + 750;
                _otaRestartPending = true;
            } else {
                request->send(500, "application/json", R"({"status":"error","msg":"firmware update failed"})");
            }
        },
        [this](AsyncWebServerRequest* request, const String& filename,
            size_t index, uint8_t* data, size_t len, bool final) {
            if (!index) {
                log_i("OTA: %s (%u bytes)", filename.c_str(), request->contentLength());
                _otaOk = Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
                if (!_otaOk) log_e("OTA begin failed");
            }
            if (_otaOk && Update.write(data, len) != len) {
                _otaOk = false;
                log_e("OTA write failed");
            }
            if (final) {
                _otaOk = _otaOk && Update.end(true) && !Update.hasError();
                if (_otaOk) log_i("OTA success");
                else log_e("OTA end failed: %s", Update.errorString());
            }
        }).setAuthentication(_username, _password);
}
