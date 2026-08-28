#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <SPI.h>
#include <RadioLib.h>
#include <PicoMQTT.h>
#include <Preferences.h>
#include <LoraProtocol.h>
#include <MqttTopics.h>
#include <WebDashboard.h>
#include "WiFiManager.h"
#include "WeatherStation.h"
#include "NodeManager.h"
#include "CloudClient.h"
#include "DataLogger.h"
#include "FieldConfigManager.h"
#include "TimeService.h"
#include "EdgeAIEngine.h"
#include "EdgeAIModel.h"

const float    LORA_FREQ  = 868.0f;
const uint32_t WEATHER_INTERVAL_MS = 30000;
const uint16_t ACTUATOR_MAX_RUNTIME_S = 1800;
const float AUTO_HYSTERESIS_PCT = 3.0f;
const uint8_t  LORA_NSS = 18, LORA_DIO0 = 2, LORA_RST = 14;
const uint8_t  RAIN_PIN = 6, WIND_PIN = 3, VANE_PIN = 10;
const uint8_t  DHT_PIN  = 7, LDR_PIN  = 9, BATT_PIN = 8;
const uint8_t  I2C_SDA  = 4, I2C_SCL  = 5;

Module loraMod(LORA_NSS, LORA_DIO0, LORA_RST, RADIOLIB_NC);
SX1276 radio(&loraMod);
PicoMQTT::Server mqttBroker(1883);
AsyncWebServer webServer(80);
WebDashboard dashboard;
WeatherStation weather(RAIN_PIN, WIND_PIN, VANE_PIN, DHT_PIN, LDR_PIN, BATT_PIN, I2C_SDA, I2C_SCL);
NodeManager nodeMgr;
CloudClient cloud;
WiFiManager wifiManager;
DataLogger dataLogger;
FieldConfigManager fieldConfig;
TimeService timeService;
EdgeAIEngine edgeAI;

uint32_t downlinkSeq = 0;
uint32_t downlinkSeqLimit = 0;
QueueHandle_t loraTxQueue = NULL;
QueueHandle_t cloudReqQueue = NULL;
SemaphoreHandle_t stateMutex = NULL;
volatile bool loraReady = false;
bool dashboardReady = false;
bool mqttReady = false;

struct LoraTxItem {
    uint8_t data[LORA_MAX_PAYLOAD];
    size_t len;
};

struct PendingCommand {
    uint32_t radioSeq;
    uint32_t cloudCmdId;
    uint16_t nodeId;
    bool desiredState;
    uint16_t timeoutS;
    uint32_t createdMs;
    char source[16];
    bool valid;
};

PendingCommand pendingCommands[16] = {};
size_t pendingCursor = 0;

uint32_t nextDownlinkSequence() {
    if (!stateMutex || xSemaphoreTake(stateMutex, pdMS_TO_TICKS(500)) != pdTRUE) return 0;
    if (downlinkSeq >= downlinkSeqLimit) {
        Preferences prefs;
        if (!prefs.begin("lora_seq", false)) { xSemaphoreGive(stateMutex); return 0; }
        uint32_t start = prefs.getULong("next_tx", 0x80000000UL);
        if (start < 0x80000000UL || start > UINT32_MAX - 1024) {
            prefs.end(); xSemaphoreGive(stateMutex); return 0;
        }
        downlinkSeq = start - 1;
        downlinkSeqLimit = start + 1023;
        prefs.putULong("next_tx", downlinkSeqLimit + 1);
        prefs.end();
    }
    uint32_t result = ++downlinkSeq;
    xSemaphoreGive(stateMutex);
    return result;
}

void rememberPending(uint32_t radioSeq, uint32_t cloudCmdId, uint16_t nodeId, bool desired,
                     uint16_t timeoutS, const char* source) {
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(500)) != pdTRUE) return;
    PendingCommand& pending = pendingCommands[pendingCursor];
    pending = {};
    pending.radioSeq = radioSeq;
    pending.cloudCmdId = cloudCmdId;
    pending.nodeId = nodeId;
    pending.desiredState = desired;
    pending.timeoutS = timeoutS;
    pending.createdMs = millis();
    strlcpy(pending.source, source ? source : "unknown", sizeof(pending.source));
    pending.valid = true;
    pendingCursor = (pendingCursor + 1) % 16;
    xSemaphoreGive(stateMutex);
}

bool consumePending(uint32_t radioSeq, uint16_t nodeId, PendingCommand& out) {
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;
    bool found = false;
    for (auto& p : pendingCommands) {
        if (p.valid && p.radioSeq == radioSeq && p.nodeId == nodeId) {
            out = p; p.valid = false; found = true; break;
        }
    }
    xSemaphoreGive(stateMutex);
    return found;
}

bool hasRecentCloudCommand(uint32_t cloudCmdId, uint16_t nodeId) {
    if (cloudCmdId == 0 || !stateMutex ||
        xSemaphoreTake(stateMutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;
    const uint32_t now = millis();
    bool found = false;
    for (auto& pending : pendingCommands) {
        if (!pending.valid || pending.cloudCmdId != cloudCmdId || pending.nodeId != nodeId) continue;
        if (now - pending.createdMs < 30000UL) found = true;
        else pending.valid = false;
        break;
    }
    xSemaphoreGive(stateMutex);
    return found;
}

enum class CloudReqType { TELEMETRY, WEATHER, HEARTBEAT, REGISTER, ACK };

struct alignas(4) CloudReq {
    CloudReqType type;
    uint16_t nodeId;
    float f1, f2, f3, f4, f5, f6, f7, f8;
    uint64_t epochMs;
    uint32_t seq;
    uint16_t u1;
    int16_t i1;
    uint8_t flags;
    uint8_t flags2;
    uint8_t attempts;
    char str1[24];
    char str2[24];
};

void initLoRa();
void processLoRaPacket(const uint8_t* buf, size_t len);
void onDecryptedPkt(uint16_t node, PacketType type,
                     const uint8_t* pt, size_t len, uint32_t seq);
void enqueueLoraTx(uint16_t nodeId, bool on, uint32_t cloudCmdId,
                   const char* source, uint16_t timeoutS = ACTUATOR_MAX_RUNTIME_S);
void onNodeProvision(uint16_t nodeId, const uint8_t psk[16], const char* type, const char* alias);
void onActuatorConfig(uint16_t nodeId, bool autoMode, uint8_t threshold, uint16_t sensorId);
void queueCloudTelemetry(uint16_t nodeId, uint16_t raw, float mp, float tc, float bv,
                         uint32_t seq, uint8_t err, int16_t rssi, uint64_t epochMs);
void queueCloudHeartbeat(uint16_t nodeId, bool commanded, bool actual, bool feedbackValid,
                         float bv, float flow, float pressure, float tank, float pumpCurrent,
                         uint8_t err, uint64_t epochMs);
void queueCloudWeather(const WeatherData& weatherData, uint64_t epochMs);
void queueCloudNode(uint16_t nodeId, const char* type, const char* alias);
void queueCloudAck(uint32_t cmdId, bool ok, float bv, bool valveActual,
                   bool feedbackValid, uint8_t err, uint64_t epochMs);
void loraTask(void* pv);
void cloudTask(void* pv);
static void jsonEscape(const char* src, char* dst, size_t sz);

static int transmitLoRa(uint8_t* data, size_t len) {
    int st = radio.startTransmit(data, len);
    if (st != 0) return st;
    uint32_t start = millis();
    while (millis() - start < 2000) {
        if (radio.getIRQFlags() & RADIOLIB_SX127X_CLEAR_IRQ_FLAG_TX_DONE) {
            radio.standby();
            return 0;
        }
        delay(0);
    }
    radio.standby();
    return -5;
}

void loraTask(void* pv) {
    LoraTxItem txItem;
    uint8_t buf[LORA_MAX_PAYLOAD];
    size_t len;

    while (1) {
        if (xQueueReceive(loraTxQueue, &txItem, 0) == pdTRUE) {
            if (loraReady) {
                radio.standby();
                int tr = transmitLoRa(txItem.data, txItem.len);
                Serial.printf("[GW] LoraTask Tx: ret=%d\n", tr);
                radio.startReceive();
            }
            continue;
        }

        if (loraReady) {
            uint16_t flags = radio.getIRQFlags();
            if (flags & RADIOLIB_SX127X_CLEAR_IRQ_FLAG_RX_DONE) {
                len = radio.getPacketLength();
                if (len >= LORA_HEADER_SIZE + MIC_SIZE && len <= sizeof(buf)) {
                    if (radio.readData(buf, len) == 0) {
                        processLoRaPacket(buf, len);
                    }
                }
                radio.startReceive();
                continue;
            }
        }

        delay(1);
    }
}

void processLoRaPacket(const uint8_t* buf, size_t len) {
    Serial.printf("[GW] Raw pkt: len=%u\n", len);

    uint16_t nodeId = (buf[0] << 8) | buf[1];
    uint8_t  pktType = buf[NODE_ID_SIZE + IV_NONCE_SIZE];
    uint32_t seq = 0;
    for (int i = 0; i < 8; i++) seq = (seq << 8) | buf[NODE_ID_SIZE + 4 + i];

    size_t ctLen = len - LORA_HEADER_SIZE - MIC_SIZE;
    if (ctLen > LORA_MAX_CIPHERTEXT) return;

    const uint8_t* iv = buf + NODE_ID_SIZE;
    const uint8_t* ct = buf + LORA_HEADER_SIZE;
    const uint8_t* mic = ct + ctLen;

    const uint8_t* nodePsk = nodeMgr.getPsk(nodeId);
    if (!nodePsk) { Serial.printf("[GW] Unknown node %04X\n", nodeId); return; }

    uint8_t pt[LORA_MAX_CIPHERTEXT];
    memcpy(pt, ct, ctLen);

    LoraCrypto nodeCrypto;
    nodeCrypto.setKey(nodePsk, 16);
    CryptoResult cr = nodeCrypto.decrypt(pt, ctLen, pktType, seq, nodeId, iv, mic);
    if (cr != CryptoResult::OK) {
        Serial.printf("[GW] Decrypt fail node=%04X seq=%u err=%d\n", nodeId, seq, (int)cr);
        return;
    }
    if (!nodeMgr.handlePacket(nodeId, (PacketType)pktType, pt, ctLen, seq)) {
        Serial.printf("[GW] Replay/invalid sequence node=%04X type=%02X seq=%u\n", nodeId, pktType, seq);
        return;
    }
    onDecryptedPkt(nodeId, (PacketType)pktType, pt, ctLen, seq);
}

void cloudTask(void* pv) {
    CloudReq req;
    TickType_t wait = pdMS_TO_TICKS(5000);

    while (1) {
        if (xQueueReceive(cloudReqQueue, &req, wait) == pdTRUE) {
            bool sent = false;
            switch (req.type) {
                case CloudReqType::TELEMETRY:
                    sent = cloud.sendTelemetry(req.nodeId, req.u1, req.f1, req.f2, req.f3,
                                               req.seq, req.flags, req.i1, req.epochMs);
                    break;
                case CloudReqType::WEATHER:
                    sent = cloud.sendWeather(req.f1, req.f2, (int)req.f3, req.f4,
                                             req.f5, req.f6, req.f7, req.f8, req.epochMs);
                    break;
                case CloudReqType::HEARTBEAT:
                    sent = cloud.sendHeartbeat(req.nodeId, (req.flags & 0x01) != 0,
                                               (req.flags2 & 0x01) != 0,
                                               (req.flags2 & 0x02) != 0,
                                               req.f1, req.f2, req.f3, req.f4, req.f5,
                                               static_cast<uint8_t>(req.u1), req.epochMs);
                    break;
                case CloudReqType::REGISTER:
                    sent = cloud.registerNode(req.nodeId, req.str1, req.str2);
                    break;
                case CloudReqType::ACK:
                    sent = cloud.sendAck(req.seq, req.flags != 0, req.f1,
                                         (req.flags2 & 0x01) != 0,
                                         (req.flags2 & 0x02) != 0,
                                         static_cast<uint8_t>(req.u1), req.epochMs);
                    break;
            }
            if (!sent && req.attempts < 2) {
                ++req.attempts;
                vTaskDelay(pdMS_TO_TICKS(500U << req.attempts));
                if (xQueueSendToFront(cloudReqQueue, &req, 0) != pdTRUE) {
                    Serial.println(F("[GW] Cloud retry queue full; local dataset remains authoritative"));
                }
            }
        }
        cloud.loop();
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println(F("[GW] Starting Central Gateway v3.1 (Optimized)"));

    wifiManager.begin(webServer);
    wifiManager.waitForConnection(15000);
    const bool networkReady = wifiManager.isConnected();

    if (networkReady) {
        if (MDNS.begin("startup-gateway")) {
            MDNS.addService("http", "tcp", 80);
        }
    }

    Serial.printf("[DBG] Free heap: %u\n", ESP.getFreeHeap());
    timeService.begin();
    fieldConfig.begin();
    edgeAI.begin();
    initLoRa();

    nodeMgr.begin();
    for (int i = 0; i < nodeMgr.count(); ++i) {
        const NodeInfo* node = nodeMgr.getNode(i);
        if (node && node->type == 0x01) fieldConfig.get(node->id);
    }

    stateMutex = xSemaphoreCreateMutex();
    loraTxQueue = xQueueCreate(8, sizeof(LoraTxItem));
    cloudReqQueue = xQueueCreate(64, sizeof(CloudReq));
    if (!stateMutex || !loraTxQueue || !cloudReqQueue || nextDownlinkSequence() == 0) {
        Serial.println(F("[GW] Fatal: queue/mutex/sequence initialization failed"));
        return;
    }

    dashboard.setDatasetPath(DataLogger::DATASET_PATH);
    dashboard.setDatasetArchivePath(DataLogger::ARCHIVE_PATH);
    dashboard.setDatasetStatusProvider([]() -> std::string { return dataLogger.statusJson(); });
    dashboard.setAIStatusProvider([]() -> std::string { return edgeAI.statusJson(); });
    dashboard.onDatasetLabel([](const char* label, const char* notes) {
        return dataLogger.addLabel(label, notes, timeService.epochMillis(),
                                   millis(), timeService.bootId());
    });
    dashboard.onDatasetClear([]() { return dataLogger.clear(); });
    dashboard.setFieldCfgProvider([]() -> std::string { return fieldConfig.toJson(); });
    dashboard.onFieldCfgUpdate([](uint16_t nodeId, uint16_t dryRaw, uint16_t wetRaw,
                                  const char* crop, const char* stage, const char* soil,
                                  float area, float emitter) {
        bool isProvisionedSensor = false;
        for (int i = 0; i < nodeMgr.count(); ++i) {
            const NodeInfo* node = nodeMgr.getNode(i);
            if (node && node->id == nodeId && node->type == 0x01) {
                isProvisionedSensor = true;
                break;
            }
        }
        if (!isProvisionedSensor) return false;
        return fieldConfig.update(nodeId, dryRaw, wetRaw, crop, stage, soil, area, emitter);
    });
    dashboard.onActuatorToggle([](uint16_t nodeId, bool on) {
        enqueueLoraTx(nodeId, on, 0, "manual");
    });
    dashboard.onNodeProvision(onNodeProvision);
    dashboard.onNodeRemove([](uint16_t id) {
        nodeMgr.remove(id);
        char buf[64];
        snprintf(buf, sizeof(buf), "Node 0x%04X removed", id);
        dashboard.pushLog("info", buf);
    });
    dashboard.onActuatorConfig(onActuatorConfig);
    dashboard.setNodesProvider([]() -> std::string { return nodeMgr.toJson(); });

    cloud.begin("", "");
    dashboard.setCloudCfgProvider([]() -> std::string {
        char urlEsc[192];
        jsonEscape(cloud.getUrl(), urlEsc, sizeof(urlEsc));
        char buf[256];
        snprintf(buf, sizeof(buf), R"({"url":"%s","apiKeySet":%s,"caCertSet":%s})",
                 urlEsc, cloud.getApiKey()[0] ? "true" : "false",
                 cloud.hasCaCert() ? "true" : "false");
        return std::string(buf);
    });
    dashboard.onCloudCfgUpdate([](const char* url, const char* apiKey, const char* caCert) {
        cloud.saveConfig(url, apiKey, caCert);
        Serial.printf("[GW] Cloud config updated: %s\n", url);
    });
    cloud.setCommandCallback([](uint32_t cmdId, uint16_t nodeId, bool on) {
        enqueueLoraTx(nodeId, on, cmdId, "cloud");
    });

    if (!dataLogger.begin()) {
        Serial.println(F("[GW] Warning: dataset logger initialization failed"));
    }
    weather.begin();

    if (networkReady) {
        dashboard.begin(webServer, wifiManager.getAdminUser(), wifiManager.getAdminPassword());
        dashboardReady = true;
        mqttBroker.begin();
        mqttReady = true;
        Serial.print(F("[GW] Dashboard: http://")); Serial.println(wifiManager.getLocalIP());
    } else {
        webServer.begin();
        Serial.println(F("[GW] Setup mode active; LoRa automation and dataset logging remain online"));
        Serial.println(F("[GW] Connect to the AP and visit http://192.168.4.1"));
    }

    xTaskCreatePinnedToCore(loraTask, "lora", 4096, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(cloudTask, "cloud", 8192, NULL, 1, NULL, 0);
}

void loop() {
    wifiManager.loop();
    static uint32_t lastWeatherMs = 0;
    uint32_t now = millis();
    if (mqttReady) mqttBroker.loop();
    if (dashboardReady) dashboard.loop();

    if (now - lastWeatherMs >= WEATHER_INTERVAL_MS) {
        lastWeatherMs = now;
        WeatherData wd = weather.read();
        const uint64_t epochMs = timeService.epochMillis();
        EdgeAIWeatherReading aiWeather;
        aiWeather.temperatureC = wd.temperature_c;
        aiWeather.humidityPct = wd.humidity_pct;
        aiWeather.pressureHpa = wd.pressure_hpa;
        aiWeather.rainMm = wd.rain_mm;
        aiWeather.windMs = wd.wind_speed_ms;
        aiWeather.luminosityLux = wd.luminosity_lux;
        aiWeather.epochMs = epochMs;
        edgeAI.updateWeather(aiWeather);
        char buf[256];
        snprintf(buf, sizeof(buf),
            R"({"rain":%.2f,"wind":%.2f,"wind_adc":%d,"wind_deg":%.0f,"temp":%.1f,"hum":%.1f,"pres":%.1f,"lux":%.0f,"bat":%.0f,"ts":%u})",
            wd.rain_mm, wd.wind_speed_ms, wd.wind_vane_adc, wd.wind_dir_deg,
            wd.temperature_c, wd.humidity_pct, wd.pressure_hpa,
            wd.luminosity_lux, wd.battery_mv, now);
        if (mqttReady) mqttBroker.publish(MqttTopics::GW_TELEMETRY, buf);

        WeatherTelemetryData wt;
        wt.rain_mm = wd.rain_mm; wt.wind_speed_ms = wd.wind_speed_ms;
        wt.wind_dir_deg = isfinite(wd.wind_dir_deg) ? static_cast<int16_t>(wd.wind_dir_deg) : -1;
        wt.temperature_c = wd.temperature_c;
        wt.humidity_pct = wd.humidity_pct; wt.pressure_hpa = wd.pressure_hpa;
        wt.luminosity_lux = wd.luminosity_lux; wt.battery_mv = wd.battery_mv;
        wt.timestamp = now; wt.epoch_ms = epochMs;
        dashboard.pushWeatherTelemetry(wt);

        DatasetRecord record = dataLogger.makeRecord("weather", epochMs, now, timeService.bootId());
        record.airTempC = wd.temperature_c;
        record.humidityPct = wd.humidity_pct;
        record.pressureHpa = wd.pressure_hpa;
        record.rainMm = wd.rain_mm;
        record.windMs = wd.wind_speed_ms;
        record.windDirDeg = wd.wind_dir_deg;
        record.luminosityLux = wd.luminosity_lux;
        record.gatewayBatteryMv = wd.battery_mv;
        const EdgeAIWeatherForecast forecast = edgeAI.weatherForecast();
        record.aiModelVersion = EdgeAIModel::MODEL_VERSION;
        record.aiShadowMode = edgeAI.controlAllowed() ? 0 : 1;
        record.aiRainProbability = forecast.rainProbability;
        record.aiForecastRainMm = forecast.rainMm24h;
        record.aiForecastTempC = forecast.temperatureC24h;
        record.aiForecastEt0Mm = forecast.et0Mm24h;
        dataLogger.enqueue(record);

        char aiWeatherJson[384];
        const float forecastTemp = isfinite(forecast.temperatureC24h) ? forecast.temperatureC24h : 0.0f;
        const float forecastRain = isfinite(forecast.rainMm24h) ? forecast.rainMm24h : 0.0f;
        const float forecastProbability = isfinite(forecast.rainProbability) ? forecast.rainProbability : 0.0f;
        const float forecastEt0 = isfinite(forecast.et0Mm24h) ? forecast.et0Mm24h : 0.0f;
        snprintf(aiWeatherJson, sizeof(aiWeatherJson),
            R"({"type":"ai_weather","model_version":%lu,"shadow_mode":%s,"valid":%s,"temperature_c_24h":%.2f,"rain_mm_24h":%.2f,"rain_probability":%.4f,"et0_mm_24h":%.2f})",
            static_cast<unsigned long>(EdgeAIModel::MODEL_VERSION),
            edgeAI.controlAllowed() ? "false" : "true", forecast.valid ? "true" : "false",
            forecastTemp, forecastRain, forecastProbability, forecastEt0);
        dashboard.pushRaw(aiWeatherJson);

        queueCloudWeather(wd, epochMs);
    }
    delay(10);
}

static uint32_t lastAutoCmdMs[NodeManager::MAX_NODES] = {};
static uint16_t linkedSensorForActuator(uint16_t actuatorNodeId) {
    for (int i = 0; i < nodeMgr.count(); ++i) {
        const NodeInfo* node = nodeMgr.getNode(i);
        if (node && node->id == actuatorNodeId && node->type == 0x02) return node->sensorId;
    }
    return 0;
}

static const NodeInfo* actuatorForSensor(uint16_t sensorNodeId) {
    for (int i = 0; i < nodeMgr.count(); ++i) {
        const NodeInfo* node = nodeMgr.getNode(i);
        if (node && node->registered && node->type == 0x02 && node->sensorId == sensorNodeId) return node;
    }
    return nullptr;
}

static bool isProvisionedActuator(uint16_t nodeId) {
    for (int i = 0; i < nodeMgr.count(); ++i) {
        const NodeInfo* node = nodeMgr.getNode(i);
        if (node && node->id == nodeId) return node->registered && node->type == 0x02;
    }
    return false;
}

static void checkAutoControl(uint16_t sensorNodeId, float moisturePct,
                             const EdgeAIPrediction& prediction) {
    uint32_t now = millis();
    for (int i = 0; i < nodeMgr.count(); i++) {
        const NodeInfo* ni = nodeMgr.getNode(i);
        if (!ni || ni->type != 0x02 || !ni->autoMode || ni->sensorId == 0 || ni->sensorId != sensorNodeId) continue;
        bool shouldOpen = ni->valveOpen;
        uint16_t timeoutS = ACTUATOR_MAX_RUNTIME_S;
        const char* source = "auto";
#if EDGE_AI_ALLOW_CONTROL
        if (prediction.valid && prediction.actuatorId == ni->id) {
            shouldOpen = prediction.irrigationNow;
            timeoutS = prediction.cycleRuntimeMin > 0
                ? static_cast<uint16_t>(prediction.cycleRuntimeMin * 60U)
                : ACTUATOR_MAX_RUNTIME_S;
            source = "edge_ai";
        } else
#endif
        {
            if (!ni->valveOpen && moisturePct < ni->threshold) shouldOpen = true;
            else if (ni->valveOpen && moisturePct >= ni->threshold + AUTO_HYSTERESIS_PCT) shouldOpen = false;
        }
        if (shouldOpen == ni->valveOpen) continue;
        if (now - lastAutoCmdMs[i] < 10000) continue;
        lastAutoCmdMs[i] = now;
        enqueueLoraTx(ni->id, shouldOpen, 0, source, timeoutS);
        char logBuf[192];
        snprintf(logBuf, sizeof(logBuf), "%s %s actuator 0x%04X (moisture=%.1f threshold=%u)",
                 source, shouldOpen ? "OPEN" : "CLOSE", ni->id, moisturePct, ni->threshold);
        dashboard.pushLog("info", logBuf);
    }
}

void onDecryptedPkt(uint16_t nodeId, PacketType type,
                     const uint8_t* pt, size_t len, uint32_t seq) {
    switch (type) {
        case PacketType::SENSOR_TELEMETRY: {
            if (len < 12) break;
            SensorTelemetry st;
            deserializeTelemetry(pt, st);
            if (st.sequence != seq) break;
            const FieldConfig& cfg = fieldConfig.get(nodeId);
            float mp = fieldConfig.calibrate(nodeId, st.moisture_raw);
            float tc = st.temperature_c / 100.0f;
            float bv = st.battery_mv / 1000.0f;
            const uint64_t epochMs = timeService.epochMillis();

            char t[32];
            MqttTopics::sensorTelemetry(nodeId, t, sizeof(t));
            char p[96];
            MqttTopics::formatTelemetry(p, sizeof(p), st.sequence, mp, tc, bv, st.error_flags);
            if (mqttReady) mqttBroker.publish(t, p);

            int16_t rssi = static_cast<int16_t>(lroundf(radio.getRSSI(true)));
            SensorTelemetryData d;
            d.node_id = nodeId; d.moisture_raw = st.moisture_raw;
            d.moisture_percent = mp; d.temperature_c = tc;
            d.battery_v = bv; d.sequence = st.sequence;
            d.rssi = rssi; d.error_flags = st.error_flags;
            d.timestamp = millis();
            d.epoch_ms = epochMs;
            dashboard.pushSensorTelemetry(d);

            const NodeInfo* linkedActuator = actuatorForSensor(nodeId);
            EdgeAIFieldProfile aiField;
            aiField.sensorId = nodeId;
            aiField.actuatorId = linkedActuator ? linkedActuator->id : 0;
            aiField.thresholdPct = linkedActuator ? linkedActuator->threshold : 45.0f;
            aiField.zoneAreaM2 = cfg.zoneAreaM2;
            aiField.emitterFlowLph = cfg.emitterFlowLph;
            aiField.crop = cfg.crop;
            aiField.growthStage = cfg.growthStage;
            aiField.soilType = cfg.soilType;
            EdgeAISensorReading aiSensor;
            aiSensor.sensorId = nodeId;
            aiSensor.moistureRaw = st.moisture_raw;
            aiSensor.moisturePct = mp;
            aiSensor.soilTemperatureC = tc;
            aiSensor.batteryV = bv;
            aiSensor.rssiDbm = rssi;
            aiSensor.errorFlags = st.error_flags;
            aiSensor.epochMs = epochMs;
            const EdgeAIPrediction aiPrediction = edgeAI.predict(aiSensor, aiField);
            const std::string aiJson = edgeAI.predictionJson(aiPrediction);
            dashboard.pushRaw(aiJson.c_str());

            DatasetRecord record = dataLogger.makeRecord("sensor", epochMs, millis(), timeService.bootId());
            record.nodeId = nodeId;
            record.linkedNodeId = aiField.actuatorId;
            record.sequence = st.sequence;
            record.moistureRaw = st.moisture_raw;
            record.moisturePct = mp;
            record.soilTempC = tc;
            record.nodeBatteryV = bv;
            record.rssiDbm = rssi;
            record.errorFlags = st.error_flags;
            strlcpy(record.crop, cfg.crop, sizeof(record.crop));
            strlcpy(record.growthStage, cfg.growthStage, sizeof(record.growthStage));
            strlcpy(record.soilType, cfg.soilType, sizeof(record.soilType));
            record.zoneAreaM2 = cfg.zoneAreaM2;
            record.emitterFlowLph = cfg.emitterFlowLph;
            record.aiModelVersion = EdgeAIModel::MODEL_VERSION;
            record.aiShadowMode = aiPrediction.shadowMode ? 1 : 0;
            record.aiIrrigationProbability = aiPrediction.irrigationProbability;
            record.aiIrrigationNeeded = aiPrediction.irrigationNeeded ? 1 : 0;
            record.aiIrrigationNow = aiPrediction.irrigationNow ? 1 : 0;
            record.aiRecommendedWaterMm = aiPrediction.recommendedWaterMm;
            record.aiRuntimeMin = aiPrediction.cycleRuntimeMin;
            record.aiTotalRuntimeMin = aiPrediction.totalRuntimeMin;
            record.aiWaitHours = aiPrediction.waitHours;
            record.aiRainProbability = aiPrediction.weather.rainProbability;
            record.aiForecastRainMm = aiPrediction.weather.rainMm24h;
            record.aiForecastTempC = aiPrediction.weather.temperatureC24h;
            record.aiForecastEt0Mm = aiPrediction.weather.et0Mm24h;
            record.aiDryingRatePctDay = aiPrediction.dryingRatePctDay;
            strlcpy(record.aiWateringEffect, EdgeAIEngine::wateringEffectName(aiPrediction.wateringEffect),
                    sizeof(record.aiWateringEffect));
            record.aiWateringIncreasePct = aiPrediction.wateringMoistureIncreasePct;
            strlcpy(record.aiFault, EdgeAIEngine::faultName(aiPrediction.fault), sizeof(record.aiFault));
            record.aiFaultConfidence = aiPrediction.faultConfidence;
            dataLogger.enqueue(record);

            queueCloudTelemetry(nodeId, st.moisture_raw, mp, tc, bv, st.sequence,
                                st.error_flags, rssi, epochMs);
            checkAutoControl(nodeId, mp, aiPrediction);
            break;
        }
        case PacketType::ACK: {
            if (len < ACK_WIRE_SIZE) break;
            AckPayload ack;
            deserializeAck(pt, ack);
            PendingCommand pending;
            bool hasPending = consumePending(ack.ack_seq, nodeId, pending);
            nodeMgr.setValveState(nodeId, ack.valve_state != 0);
            const uint64_t epochMs = timeService.epochMillis();
            char t[32];
            MqttTopics::actuatorAck(nodeId, t, sizeof(t));
            char p[96];
            MqttTopics::formatAck(p, sizeof(p), ack.ack_seq, ack.result == 0, ack.battery_mv / 1000.0f);
            if (mqttReady) mqttBroker.publish(t, p);
            for (int i = 0; i < nodeMgr.count(); i++) {
                const NodeInfo* ni = nodeMgr.getNode(i);
                if (ni && ni->id == nodeId) {
                    ActuatorStateData asd;
                    asd.node_id = nodeId;
                    asd.valve_open = ni->valveOpen;
                    asd.valve_commanded_open = hasPending ? pending.desiredState : ni->valveOpen;
                    asd.feedback_valid = ack.feedback_valid != 0;
                    asd.battery_v = ack.battery_mv / 1000.0f;
                    asd.flow_lpm = asd.line_pressure_bar = asd.tank_pct = asd.pump_current_a = NAN;
                    asd.error_flags = ack.error_flags;
                    asd.timestamp = millis();
                    asd.epoch_ms = epochMs;
                    EdgeAIActuatorReading aiActuator;
                    aiActuator.actuatorId = nodeId;
                    aiActuator.linkedSensorId = linkedSensorForActuator(nodeId);
                    aiActuator.valveCommanded = asd.valve_commanded_open;
                    aiActuator.valveActual = asd.valve_open;
                    aiActuator.feedbackValid = asd.feedback_valid;
                    aiActuator.batteryV = asd.battery_v;
                    aiActuator.errorFlags = asd.error_flags;
                    aiActuator.epochMs = epochMs;
                    edgeAI.updateActuator(aiActuator);
                    dashboard.pushActuatorState(asd);
                    break;
                }
            }
            DatasetRecord record = dataLogger.makeRecord("command_ack", epochMs, millis(), timeService.bootId());
            record.nodeId = nodeId;
            record.linkedNodeId = linkedSensorForActuator(nodeId);
            record.sequence = ack.ack_seq;
            record.nodeBatteryV = ack.battery_mv / 1000.0f;
            record.valveCommanded = hasPending ? (pending.desiredState ? 1 : 0) : -1;
            record.valveActual = ack.valve_state ? 1 : 0;
            record.valveFeedbackValid = ack.feedback_valid ? 1 : 0;
            record.commandResult = ack.result;
            record.commandId = hasPending ? pending.cloudCmdId : 0;
            record.commandTimeoutS = hasPending ? pending.timeoutS : 0;
            record.errorFlags = ack.error_flags;
            strlcpy(record.commandSource, hasPending ? pending.source : "unknown", sizeof(record.commandSource));
            dataLogger.enqueue(record);
            if (hasPending && pending.cloudCmdId != 0) {
                queueCloudAck(pending.cloudCmdId, ack.result == 0, ack.battery_mv / 1000.0f,
                              ack.valve_state != 0, ack.feedback_valid != 0,
                              ack.error_flags, epochMs);
            }
            break;
        }
        case PacketType::HEARTBEAT: {
            if (len < HEARTBEAT_WIRE_SIZE) break;
            HeartbeatPayload hb;
            deserializeHeartbeat(pt, hb);
            if (hb.sequence != seq) break;
            nodeMgr.setValveState(nodeId, hb.valve_state != 0);
            const uint64_t epochMs = timeService.epochMillis();
            const float flowLpm = hb.flow_centi_lpm == UINT16_MAX ? NAN : hb.flow_centi_lpm / 100.0f;
            const float pressureBar = hb.pressure_kpa == UINT16_MAX ? NAN : hb.pressure_kpa / 100.0f;
            const float tankPct = hb.tank_pct == UINT8_MAX ? NAN : static_cast<float>(hb.tank_pct);
            const float pumpCurrentA = hb.pump_current_ma == UINT16_MAX ? NAN : hb.pump_current_ma / 1000.0f;
            ActuatorStateData asd;
            asd.node_id = nodeId;
            asd.valve_open = hb.valve_state;
            asd.valve_commanded_open = hb.valve_commanded;
            asd.feedback_valid = hb.feedback_valid;
            asd.battery_v = hb.battery_mv / 1000.0f;
            asd.flow_lpm = flowLpm;
            asd.line_pressure_bar = pressureBar;
            asd.tank_pct = tankPct;
            asd.pump_current_a = pumpCurrentA;
            asd.error_flags = hb.error_flags;
            asd.timestamp = millis();
            asd.epoch_ms = epochMs;
            EdgeAIActuatorReading aiActuator;
            aiActuator.actuatorId = nodeId;
            aiActuator.linkedSensorId = linkedSensorForActuator(nodeId);
            aiActuator.valveCommanded = asd.valve_commanded_open;
            aiActuator.valveActual = asd.valve_open;
            aiActuator.feedbackValid = asd.feedback_valid;
            aiActuator.batteryV = asd.battery_v;
            aiActuator.flowLpm = flowLpm;
            aiActuator.pressureBar = pressureBar;
            aiActuator.tankPct = tankPct;
            aiActuator.pumpCurrentA = pumpCurrentA;
            aiActuator.errorFlags = asd.error_flags;
            aiActuator.epochMs = epochMs;
            edgeAI.updateActuator(aiActuator);
            dashboard.pushActuatorState(asd);
            Serial.printf("[GW] HB node=%04X batt=%.2f valve=%s\n",
                          nodeId, asd.battery_v, asd.valve_open ? "OPEN" : "CLOSED");
            DatasetRecord record = dataLogger.makeRecord("actuator", epochMs, millis(), timeService.bootId());
            record.nodeId = nodeId;
            record.linkedNodeId = linkedSensorForActuator(nodeId);
            record.sequence = hb.sequence;
            record.nodeBatteryV = asd.battery_v;
            record.valveCommanded = hb.valve_commanded ? 1 : 0;
            record.valveActual = hb.valve_state ? 1 : 0;
            record.valveFeedbackValid = hb.feedback_valid ? 1 : 0;
            record.flowLpm = flowLpm;
            record.linePressureBar = pressureBar;
            record.tankPct = tankPct;
            record.pumpCurrentA = pumpCurrentA;
            record.errorFlags = hb.error_flags;
            dataLogger.enqueue(record);
            queueCloudHeartbeat(nodeId, hb.valve_commanded, hb.valve_state,
                                hb.feedback_valid, asd.battery_v, flowLpm, pressureBar,
                                tankPct, pumpCurrentA, hb.error_flags, epochMs);
            break;
        }
        default: break;
    }
}

void enqueueLoraTx(uint16_t nodeId, bool on, uint32_t cloudCmdId,
                   const char* source, uint16_t timeoutS) {
    if (!isProvisionedActuator(nodeId)) {
        Serial.printf("[GW] Rejecting valve command for non-actuator node %04X\n", nodeId);
        return;
    }
    if (hasRecentCloudCommand(cloudCmdId, nodeId)) {
        Serial.printf("[GW] Ignoring duplicate cloud command %u for node %04X\n",
                      cloudCmdId, nodeId);
        return;
    }
    Serial.printf("[GW] Toggle actuator %04X -> %s\n", nodeId, on ? "ON" : "OFF");

    ActuatorCommand cmd;
    cmd.sequence  = nextDownlinkSequence();
    if (cmd.sequence == 0) { Serial.println(F("[GW] Downlink sequence unavailable")); return; }
    cmd.command   = 0x01;
    cmd.value     = on ? 1 : 0;
    cmd.timeout_s = on ? constrain(timeoutS, static_cast<uint16_t>(1), ACTUATOR_MAX_RUNTIME_S) : 0;

    uint8_t pt[COMMAND_WIRE_SIZE];
    serializeCommand(pt, cmd);

    const uint8_t* nodePsk = nodeMgr.getPsk(nodeId);
    if (!nodePsk) { Serial.printf("[GW] No PSK for node %04X\n", nodeId); return; }

    LoraCrypto enc;
    enc.setKey(nodePsk, 16);
    uint8_t iv[GCM_IV_SIZE], mic[MIC_SIZE];
    CryptoResult cr = enc.encrypt(pt, COMMAND_WIRE_SIZE,
                                   (uint8_t)PacketType::ACTUATOR_COMMAND,
                                   cmd.sequence, nodeId, iv, mic);
    if (cr != CryptoResult::OK) { Serial.printf("[GW] encrypt fail err=%d\n", (int)cr); return; }

    LoraTxItem item; size_t o = 0;
    uint8_t node_be[2] = { uint8_t(nodeId >> 8), uint8_t(nodeId & 0xFF) };
    memcpy(item.data+o, node_be, 2); o+=2;
    memcpy(item.data+o, iv, 12);     o+=12;
    item.data[o++] = (uint8_t)PacketType::ACTUATOR_COMMAND;
    memcpy(item.data+o, pt, COMMAND_WIRE_SIZE); o+=COMMAND_WIRE_SIZE;
    memcpy(item.data+o, mic, MIC_SIZE);         o+=MIC_SIZE;
    item.len = o;

    if (xQueueSend(loraTxQueue, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.println("[GW] loraTxQueue full, dropping cmd");
    } else {
        rememberPending(cmd.sequence, cloudCmdId, nodeId, on, cmd.timeout_s, source);
        DatasetRecord record = dataLogger.makeRecord("command", timeService.epochMillis(),
                                                     millis(), timeService.bootId());
        record.nodeId = nodeId;
        record.linkedNodeId = linkedSensorForActuator(nodeId);
        record.sequence = cmd.sequence;
        record.valveCommanded = on ? 1 : 0;
        record.commandId = cloudCmdId;
        record.commandTimeoutS = cmd.timeout_s;
        strlcpy(record.commandSource, source ? source : "unknown", sizeof(record.commandSource));
        dataLogger.enqueue(record);
    }
}

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

void onNodeProvision(uint16_t nodeId, const uint8_t psk[16], const char* type, const char* alias) {
    if (!type || !alias || nodeId == 0 ||
        (strcmp(type, "sensor") != 0 && strcmp(type, "actuator") != 0)) return;
    uint8_t ntype = (strcmp(type, "actuator") == 0) ? 0x02 : 0x01;
    if (!nodeMgr.provision(nodeId, ntype, psk, alias)) {
        dashboard.pushLog("error", "Node provisioning failed");
        return;
    }
    Serial.printf("[GW] Provisioned node %04X as %s (%s)\n", nodeId, type, alias);
    char logBuf[64];
    snprintf(logBuf, sizeof(logBuf), "Node 0x%04X provisioned as %s", nodeId, type);
    dashboard.pushLog("info", logBuf);
    char aliasEsc[144];
    jsonEscape(alias, aliasEsc, sizeof(aliasEsc));
    char buf[256];
    snprintf(buf, sizeof(buf),
        R"({"type":"provisioned","id":%u,"node_type":"%s","alias":"%s","status":"ok"})",
        nodeId, type, aliasEsc);
    dashboard.pushRaw(buf);
    queueCloudNode(nodeId, type, alias);
}

void onActuatorConfig(uint16_t nodeId, bool autoMode, uint8_t threshold, uint16_t sensorId) {
    if (nodeMgr.setActuatorConfig(nodeId, autoMode, threshold, sensorId)) {
        Serial.printf("[GW] Actuator 0x%04X config: auto=%s threshold=%u sensorId=%u\n",
                      nodeId, autoMode ? "ON" : "OFF", threshold, sensorId);
        char logBuf[64];
        snprintf(logBuf, sizeof(logBuf), "Actuator 0x%04X config updated", nodeId);
        dashboard.pushLog("info", logBuf);
    }
}

static bool enqueueCloudRequest(const CloudReq& request) {
    if (!cloud.isConfigured() || WiFi.status() != WL_CONNECTED) return false;
    if (!cloudReqQueue || xQueueSend(cloudReqQueue, &request, 0) != pdTRUE) {
        Serial.println(F("[GW] Cloud queue full; record retained in local dataset"));
        return false;
    }
    return true;
}

void queueCloudTelemetry(uint16_t nodeId, uint16_t raw, float mp, float tc, float bv,
                         uint32_t seq, uint8_t err, int16_t rssi, uint64_t epochMs) {
    CloudReq r{}; r.type = CloudReqType::TELEMETRY; r.nodeId = nodeId;
    r.u1 = raw; r.f1 = mp; r.f2 = tc; r.f3 = bv; r.seq = seq;
    r.flags = err; r.i1 = rssi; r.epochMs = epochMs;
    enqueueCloudRequest(r);
}
void queueCloudHeartbeat(uint16_t nodeId, bool commanded, bool actual, bool feedbackValid,
                         float bv, float flow, float pressure, float tank, float pumpCurrent,
                         uint8_t err, uint64_t epochMs) {
    CloudReq r{}; r.type = CloudReqType::HEARTBEAT; r.nodeId = nodeId;
    r.flags = commanded ? 0x01 : 0;
    r.flags2 = (actual ? 0x01 : 0) | (feedbackValid ? 0x02 : 0);
    r.f1 = bv; r.f2 = flow; r.f3 = pressure; r.f4 = tank; r.f5 = pumpCurrent;
    r.u1 = err; r.epochMs = epochMs;
    enqueueCloudRequest(r);
}
void queueCloudWeather(const WeatherData& wd, uint64_t epochMs) {
    CloudReq r{}; r.type = CloudReqType::WEATHER;
    r.f1 = wd.rain_mm; r.f2 = wd.wind_speed_ms;
    r.f3 = isfinite(wd.wind_dir_deg) ? wd.wind_dir_deg : -1.0f;
    r.f4 = wd.temperature_c; r.f5 = wd.humidity_pct; r.f6 = wd.pressure_hpa;
    r.f7 = wd.luminosity_lux; r.f8 = wd.battery_mv; r.epochMs = epochMs;
    enqueueCloudRequest(r);
}
void queueCloudNode(uint16_t nodeId, const char* type, const char* alias) {
    CloudReq r{}; r.type = CloudReqType::REGISTER; r.nodeId = nodeId;
    strlcpy(r.str1, type, sizeof(r.str1));
    strlcpy(r.str2, alias, sizeof(r.str2));
    enqueueCloudRequest(r);
}
void queueCloudAck(uint32_t cmdId, bool ok, float bv, bool valveActual,
                   bool feedbackValid, uint8_t err, uint64_t epochMs) {
    CloudReq r{}; r.type = CloudReqType::ACK; r.seq = cmdId; r.flags = ok ? 1 : 0; r.f1 = bv;
    r.flags2 = (valveActual ? 0x01 : 0) | (feedbackValid ? 0x02 : 0);
    r.u1 = err; r.epochMs = epochMs;
    enqueueCloudRequest(r);
}

void initLoRa() {
    SPI.begin(12, 13, 11, LORA_NSS);
    loraReady = false;
    int st = radio.begin(LORA_FREQ, 125.0f, 9, 5, 0x12, 10, 12);
    Serial.printf("[GW] LoRa begin=%d chipver=0x%02X\n", st, radio.getChipVersion());
    if (st != 0) { return; }
    loraReady = true;
    st = radio.startReceive();
    if (st == 0) { Serial.println("[GW] LoRa ready @ 868 MHz"); } else { Serial.println("[GW] Rx start fail"); }
}
