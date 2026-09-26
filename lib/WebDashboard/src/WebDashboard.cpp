#include "WebDashboard.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <esp_system.h>
#include <WiFi.h>
#include <Update.h>
#include <cctype>
#include <cstring>
#include <new>

#ifndef PILOT_ENABLE_WEB_OTA
#define PILOT_ENABLE_WEB_OTA 0
#endif

namespace {
struct RequestBody {
    String json;
    bool overflow = false;
};

void collectJsonBody(AsyncWebServerRequest* request, uint8_t* data, size_t len,
                     size_t index, size_t total) {
    if (index == 0) {
        request->_tempObject = new (std::nothrow) RequestBody();
        // The server otherwise free()s _tempObject without running String's
        // destructor when a mobile client abandons a partially uploaded POST.
        request->onDisconnect([request]() {
            delete static_cast<RequestBody*>(request->_tempObject);
            request->_tempObject = nullptr;
        });
    }
    auto* body = static_cast<RequestBody*>(request->_tempObject);
    if (!body) return;
    if (total > 4096 || body->json.length() + len > 4096) {
        body->overflow = true;
        return;
    }
    if (!body->json.concat(reinterpret_cast<const char*>(data), len)) body->overflow = true;
}

void jsonFloat(char* dst, size_t size, float value, unsigned decimals) {
    if (isnan(value) || isinf(value)) strlcpy(dst, "null", size);
    else snprintf(dst, size, "%.*f", static_cast<int>(decimals), value);
}

bool keyAllowed(const uint8_t psk[16]) {
    bool allZero = true;
    bool allFF = true;
    for (size_t i = 0; i < 16; ++i) {
        allZero = allZero && psk[i] == 0;
        allFF = allFF && psk[i] == 0xFF;
    }
    return !allZero && !allFF;
}
}

WebDashboard::WebDashboard() {}

bool WebDashboard::initLittleFS() {
    if (!LittleFS.begin(false)) {
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
#if PILOT_ENABLE_WEB_OTA
    registerOtaHandler();
#endif
    _server->begin();
}

void WebDashboard::loop() {
    if (_ws && millis() - _lastCleanupMs >= 1000) {
        _lastCleanupMs = millis();
        _ws->cleanupClients(3);
    }
    if (_otaRestartPending && static_cast<int32_t>(millis() - _otaRestartAt) >= 0) ESP.restart();
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
            client->setCloseClientOnQueueFull(false);
            client->keepAlivePeriod(30);
            log_i("WS[%u] connected", client->id());
            break;
        case WS_EVT_DISCONNECT:
            log_i("WS[%u] disconnected", client->id());
            break;
        case WS_EVT_DATA: {
            AwsFrameInfo* info = (AwsFrameInfo*)arg;
            if (info && info->opcode == WS_TEXT && info->final && info->index == 0 && info->len == len && len <= 1024) {
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, data, len);
                if (!err) {
                    const char* action = doc["action"];
                    if (action && strcmp(action, "toggle_valve") == 0) {
                        uint16_t nid = doc["node_id"] | 0;
                        bool on = doc["value"] | false;
                        const bool queued = _control_cb && _control_cb(nid, on);
                        client->text(queued
                            ? R"({"type":"command_result","status":"queued"})"
                            : R"({"type":"command_result","status":"rejected"})");
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
                        if (nid != 0 && nid != 0xFFFF && validHex && validType && validAlias && _provision_cb) {
                            uint8_t psk[16];
                            for (int i = 0; i < 16; i++) {
                                char byte_str[3] = {psk_str[i*2], psk_str[i*2+1], 0};
                                psk[i] = strtol(byte_str, NULL, 16);
                            }
                            if (keyAllowed(psk)) {
                                _provision_cb(nid, psk, type, alias);
                            } else {
                                client->text(R"({"type":"provisioned","status":"weak_key"})");
                            }
                            memset(psk, 0, sizeof(psk));
                        } else {
                            client->text(R"({"type":"provisioned","status":"invalid_request"})");
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

static void formatSensor(char* buf, size_t size, const SensorTelemetryData& d, uint32_t now) {
    char moisture[20], temp[20], battery[20];
    jsonFloat(moisture, sizeof(moisture), d.moisture_percent, 1);
    jsonFloat(temp, sizeof(temp), d.temperature_c, 2);
    jsonFloat(battery, sizeof(battery), d.battery_v, 2);
    std::snprintf(buf, size,
        R"({"type":"sensor","id":%u,"moisture_raw":%u,"moisture":%s,"temp":%s,)"
        R"("batt":%s,"seq":%u,"rssi":%d,"errors":%u,"ts":%u,"epoch_ms":%llu,"age_ms":%u})",
        (unsigned)d.node_id, (unsigned)d.moisture_raw, moisture, temp,
        battery, d.sequence, (int)d.rssi, d.error_flags, d.timestamp,
        static_cast<unsigned long long>(d.epoch_ms), now - d.timestamp);
}
static void formatWeather(char* buf, size_t size, const WeatherTelemetryData& d, uint32_t now) {
    char rain[20], wind[20], temp[20], hum[20], pres[20], lux[20], bat[20], dir[12];
    jsonFloat(rain, sizeof(rain), d.rain_mm, 2);
    jsonFloat(wind, sizeof(wind), d.wind_speed_ms, 2);
    jsonFloat(temp, sizeof(temp), d.temperature_c, 1);
    jsonFloat(hum, sizeof(hum), d.humidity_pct, 1);
    jsonFloat(pres, sizeof(pres), d.pressure_hpa, 1);
    jsonFloat(lux, sizeof(lux), d.luminosity_lux, 0);
    jsonFloat(bat, sizeof(bat), d.battery_mv, 0);
    if (d.wind_dir_deg < 0) strlcpy(dir, "null", sizeof(dir));
    else snprintf(dir, sizeof(dir), "%d", d.wind_dir_deg);
    std::snprintf(buf, size,
        R"({"type":"weather","rain":%s,"wind":%s,"dir":%s,"temp":%s,"hum":%s,"pres":%s,"lux":%s,"bat":%s,"ts":%u,"epoch_ms":%llu,"age_ms":%u})",
        rain, wind, dir, temp, hum, pres, lux, bat,
        (unsigned)d.timestamp, static_cast<unsigned long long>(d.epoch_ms), now - d.timestamp);
}
static void formatActuator(char* buf, size_t size, const ActuatorStateData& d, uint32_t now) {
    auto val = [](float value) { return isnan(value) || isinf(value) ? -1.0f : value; };
    std::snprintf(buf, size,
        R"({"type":"actuator","id":%u,"valve":%s,"commanded":%s,"feedback_valid":%s,)"
        R"("batt":%.2f,"flow_lpm":%.2f,"pressure_bar":%.2f,"tank_pct":%.1f,)"
        R"("pump_current_a":%.2f,"errors":%u,"ts":%u,"epoch_ms":%llu,"age_ms":%u})",
        d.node_id, d.valve_open ? "true" : "false",
        d.valve_commanded_open ? "true" : "false", d.feedback_valid ? "true" : "false",
        d.battery_v, val(d.flow_lpm), val(d.line_pressure_bar), val(d.tank_pct), val(d.pump_current_a),
        d.error_flags, d.timestamp, static_cast<unsigned long long>(d.epoch_ms), now - d.timestamp);
}

void WebDashboard::pushSensorTelemetry(const SensorTelemetryData& d) {
    {
        std::lock_guard<std::mutex> lock(_telemetryMutex);
        _sensors.put(d, millis());
    }
    char buf[384];
    formatSensor(buf, sizeof(buf), d, millis());
    broadcast(buf);
}
void WebDashboard::pushWeatherTelemetry(const WeatherTelemetryData& d) {
    {
        std::lock_guard<std::mutex> lock(_telemetryMutex);
        _weather = d;
        _hasWeather = true;
    }
    char buf[448];
    formatWeather(buf, sizeof(buf), d, millis());
    broadcast(buf);
}
void WebDashboard::pushActuatorState(const ActuatorStateData& d) {
    {
        std::lock_guard<std::mutex> lock(_telemetryMutex);
        _actuators.put(d, millis());
    }
    char buf[448];
    formatActuator(buf, sizeof(buf), d, millis());
    broadcast(buf);
}
void WebDashboard::forgetNode(uint16_t nodeId) {
    std::lock_guard<std::mutex> lock(_telemetryMutex);
    _sensors.remove(nodeId);
    _actuators.remove(nodeId);
}
std::string WebDashboard::telemetryJson() {
    std::lock_guard<std::mutex> lock(_telemetryMutex);
    const uint32_t now = millis();
    std::string json = R"({"weather":)";
    char buf[448];
    if (_hasWeather) { formatWeather(buf, sizeof(buf), _weather, now); json += buf; }
    else json += "null";
    json += R"(,"sensors":[)";
    bool first = true;
    for (const auto& entry : _sensors.entries) {
        if (!entry.used) continue;
        if (!first) json += ',';
        first = false;
        formatSensor(buf, sizeof(buf), entry.value, now);
        json += buf;
    }
    json += R"(],"actuators":[)";
    first = true;
    for (const auto& entry : _actuators.entries) {
        if (!entry.used) continue;
        if (!first) json += ',';
        first = false;
        formatActuator(buf, sizeof(buf), entry.value, now);
        json += buf;
    }
    json += "]}";
    return json;
}
void WebDashboard::broadcast(const char* json) {
    if (!_ws || !json || !_ws->count()) return;
    // A slow/background phone must not cause a reconnect/queue-exhaustion loop.
    // Current readings and recommendations can be recovered through HTTP.
    if (ESP.getFreeHeap() < 20000 || !_ws->availableForWriteAll()) { ++_wsDropped; return; }
    _ws->textAll(json);
}
void WebDashboard::pushRaw(const char* json) {
    broadcast(json);
}

void WebDashboard::pushLog(const char* level, const char* message) {
    if (!_ws) return;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        R"({"type":"log","level":"%s","msg":"%s","ts":%u})",
        level, message, (unsigned)millis());
    broadcast(buf);
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
    _server->on("/api/health", HTTP_GET, [this](AsyncWebServerRequest* request) {
        String json = "{";
        json += "\"uptime\":" + String(millis() / 1000);
        json += ",\"free_heap\":" + String(ESP.getFreeHeap());
        json += ",\"min_free_heap\":" + String(ESP.getMinFreeHeap());
        json += ",\"ws_dropped\":" + String(_wsDropped.load());
        json += ",\"rssi\":" + String(WiFi.RSSI());
        if (_healthProvider) { json += ','; json += _healthProvider().c_str(); }
        json += "}";
        request->send(200, "application/json", json);
    }).setAuthentication(_username, _password);
    _server->on("/api/telemetry", HTTP_GET, [this](AsyncWebServerRequest* request) {
        request->send(200, "application/json", telemetryJson().c_str());
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
    _server->on("/api/dataset/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        const std::string status = _datasetStatusProvider ? _datasetStatusProvider() : "{}";
        request->send(200, "application/json", status.c_str());
    }).setAuthentication(_username, _password);
    _server->on("/api/ai/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        const std::string status = _aiStatusProvider ? _aiStatusProvider() : "{}";
        request->send(200, "application/json", status.c_str());
    }).setAuthentication(_username, _password);
    _server->on("/api/dataset/export", HTTP_GET, [this](AsyncWebServerRequest* request) {
        const bool archive = request->hasParam("archive") && request->getParam("archive")->value() == "1";
        const String& path = archive ? _datasetArchivePath : _datasetPath;
        if (path.isEmpty() || !LittleFS.exists(path)) {
            request->send(404, "application/json", R"({"error":"dataset_not_found"})");
            return;
        }
        request->send(LittleFS, path, "text/csv", true);
    }).setAuthentication(_username, _password);
    _server->on("/api/dataset/label", HTTP_POST,
        [this](AsyncWebServerRequest* request) {
            auto* body = static_cast<RequestBody*>(request->_tempObject);
            if (!body || body->overflow) {
                delete body; request->_tempObject = nullptr;
                request->send(413, "application/json", R"({"error":"body_too_large"})"); return;
            }
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, body->json);
            delete body; request->_tempObject = nullptr;
            const char* label = doc["label"] | "";
            const char* notes = doc["notes"] | "";
            if (err || !label[0] || strlen(label) >= 24 || strlen(notes) >= 80 ||
                !_datasetLabel_cb || !_datasetLabel_cb(label, notes)) {
                request->send(400, "application/json", R"({"error":"invalid_label"})"); return;
            }
            request->send(200, "application/json", R"({"status":"ok"})");
        }, nullptr, collectJsonBody).setAuthentication(_username, _password);
    _server->on("/api/dataset", HTTP_DELETE, [this](AsyncWebServerRequest* request) {
        if (!_datasetClear_cb || !_datasetClear_cb()) {
            request->send(500, "application/json", R"({"error":"clear_failed"})"); return;
        }
        request->send(200, "application/json", R"({"status":"cleared"})");
    }).setAuthentication(_username, _password);
    _server->on("/api/field-config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        const std::string config = _fieldCfgProvider ? _fieldCfgProvider() : "[]";
        request->send(200, "application/json", config.c_str());
    }).setAuthentication(_username, _password);
    _server->on("/api/field-config", HTTP_POST,
        [this](AsyncWebServerRequest* request) {
            auto* body = static_cast<RequestBody*>(request->_tempObject);
            if (!body || body->overflow) {
                delete body; request->_tempObject = nullptr;
                request->send(413, "application/json", R"({"error":"body_too_large"})"); return;
            }
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, body->json);
            delete body; request->_tempObject = nullptr;
            const uint16_t nodeId = doc["node_id"] | 0;
            const uint16_t dryRaw = doc["dry_raw"] | 2500;
            const uint16_t wetRaw = doc["wet_raw"] | 400;
            const char* crop = doc["crop"] | "unknown";
            const char* stage = doc["growth_stage"] | "unknown";
            const char* soil = doc["soil_type"] | "unknown";
            const float area = doc["zone_area_m2"] | 0.0f;
            const float emitter = doc["emitter_flow_lph"] | 0.0f;
            if (err || !_fieldCfgUpdate_cb ||
                !_fieldCfgUpdate_cb(nodeId, dryRaw, wetRaw, crop, stage, soil, area, emitter)) {
                request->send(400, "application/json", R"({"error":"invalid_field_config"})"); return;
            }
            request->send(200, "application/json", R"({"status":"ok"})");
        }, nullptr, collectJsonBody).setAuthentication(_username, _password);
    _server->on("/api/restart", HTTP_POST, [this](AsyncWebServerRequest* request) {
        request->send(200, "application/json", R"({"status":"restarting"})");
        _otaRestartAt = millis() + 750;
        _otaRestartPending = true;
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
            if (!_control_cb || !_control_cb(nid, doc["value"].as<bool>())) {
                request->send(409, "application/json", R"({"error":"command_rejected"})");
                return;
            }
            request->send(202, "application/json", R"({"status":"queued"})");
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
