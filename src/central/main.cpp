#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <SPI.h>
#ifndef TTGO_GATEWAY
#include <RadioLib.h>
#else
#include <LoraRadio.h>
#endif
#include <PicoMQTT.h>
#include <LoraProtocol.h>
#include <MqttTopics.h>
#include <WebDashboard.h>
#include "WiFiManager.h"
#include "WeatherStation.h"
#include "NodeManager.h"
#include "CloudClient.h"

const float    LORA_FREQ  = 868.0f;
const float    MOISTURE_DRY_OFFSET = 6.0f;
const uint32_t WEATHER_INTERVAL_MS = 30000;  // must match WIND_INTERVAL_S in WeatherStation.h
#ifdef TTGO_GATEWAY
const uint8_t  PIN_LORA_RST = 14;
#else
const uint8_t  LORA_NSS = 18, LORA_DIO0 = 2, LORA_RST = 14;
const uint8_t  RAIN_PIN = 6, WIND_PIN = 3, VANE_PIN = 10;
const uint8_t  DHT_PIN  = 7, LDR_PIN  = 9, BATT_PIN = 8;
const uint8_t  I2C_SDA  = 4, I2C_SCL  = 5;
#endif

const uint16_t GW_NODE_ID = 0x0000;
const uint8_t  GW_PSK[16] = {0};

#ifndef TTGO_GATEWAY
Module* loraMod = new Module(LORA_NSS, LORA_DIO0, LORA_RST, RADIOLIB_NC);
SX1276 radio(loraMod);
#else
LoraRadio radio(LORA_CS, LORA_IRQ, PIN_LORA_RST);
#endif
PicoMQTT::Server mqttBroker(1883);
AsyncWebServer webServer(80);
WebDashboard dashboard;
#ifndef TTGO_GATEWAY
WeatherStation weather(RAIN_PIN, WIND_PIN, VANE_PIN, DHT_PIN, LDR_PIN, BATT_PIN, I2C_SDA, I2C_SCL);
#endif
NodeManager nodeMgr;
CloudClient cloud;
WiFiManager wifiManager;

uint32_t downlinkSeq = 0x80000000;
QueueHandle_t loraTxQueue = NULL;
QueueHandle_t cloudReqQueue = NULL;
bool loraReady = false;

struct LoraTxItem {
    uint8_t data[LORA_MAX_PAYLOAD];
    size_t len;
};

enum class CloudReqType { TELEMETRY, WEATHER, HEARTBEAT, REGISTER, ACK };

struct CloudReq {
    CloudReqType type;
    uint16_t nodeId;
    float f1, f2, f3;
    uint32_t seq;
    uint8_t flags;
    uint8_t rssi;
    char str1[24];
    char str2[24];
};

void initLoRa();
void processLoRaPacket(const uint8_t* buf, size_t len);
void onDecryptedPkt(uint16_t node, PacketType type,
                     const uint8_t* pt, size_t len, uint32_t seq);
void enqueueLoraTx(uint16_t nodeId, bool on, uint32_t cloudCmdId);
void onNodeProvision(uint16_t nodeId, const uint8_t psk[16], const char* type, const char* alias);
void onActuatorConfig(uint16_t nodeId, bool autoMode, uint8_t threshold, uint16_t sensorId);
void queueCloudTelemetry(uint16_t nodeId, float mp, float tc, float bv, uint32_t seq, uint8_t err, uint8_t rssi);
void queueCloudHeartbeat(uint16_t nodeId, bool valveOpen, float bv);
void queueCloudWeather(float rain, float wind, int wdir, float temp);
void queueCloudNode(uint16_t nodeId, const char* type, const char* alias);
void queueCloudAck(uint32_t cmdId, bool ok, float bv);
void loraTask(void* pv);
void cloudTask(void* pv);

static int manualTransmit(SX1276& r, uint8_t* data, size_t len) {
    int st = r.startTransmit(data, len);
    if (st != 0) { Serial.printf("[GW] startTransmit fail: %d\n", st); return st; }
    uint32_t start = millis();
    while (millis() - start < 2000) {
        if (r.getIRQFlags() & RADIOLIB_SX127X_CLEAR_IRQ_FLAG_TX_DONE) { r.standby(); return 0; }
        delay(1);
    }
    r.standby(); return -5;
}

void loraTask(void* pv) {
    LoraTxItem txItem;
    uint8_t buf[LORA_MAX_PAYLOAD];
    size_t len;

    while (1) {
        if (xQueueReceive(loraTxQueue, &txItem, 0) == pdTRUE) {
            if (loraReady) {
                radio.standby();
                delay(100);
                radio.setFrequency(LORA_FREQ); delay(100);
                int tr = manualTransmit(radio, txItem.data, txItem.len);
                Serial.printf("[GW] LoraTask Tx: ret=%d\n", tr);
                if (tr != 0) { delay(200); manualTransmit(radio, txItem.data, txItem.len); }
                radio.startReceive();
            }
        }

        if (loraReady && (radio.getIRQFlags() & RADIOLIB_SX127X_CLEAR_IRQ_FLAG_RX_DONE)) {
            len = radio.getPacketLength();
            if (len >= LORA_HEADER_SIZE + MIC_SIZE && len <= sizeof(buf)) {
                if (radio.readData(buf, len) == 0) { processLoRaPacket(buf, len); }
            }
            radio.startReceive();
        }
        delay(10);
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
    onDecryptedPkt(nodeId, (PacketType)pktType, pt, ctLen, seq);
}

void cloudTask(void* pv) {
    CloudReq req;
    TickType_t wait = pdMS_TO_TICKS(5000);

    while (1) {
        if (xQueueReceive(cloudReqQueue, &req, wait) == pdTRUE) {
            switch (req.type) {
                case CloudReqType::TELEMETRY:
                    cloud.sendTelemetry(req.nodeId, req.f1, req.f2, req.f3, req.seq, req.flags, req.rssi);
                    break;
                case CloudReqType::WEATHER:
                    cloud.sendWeather(req.f1, req.f2, (int)req.f3, req.seq);
                    break;
                case CloudReqType::HEARTBEAT:
                    cloud.sendHeartbeat(req.nodeId, (bool)(req.flags), req.f1);
                    break;
                case CloudReqType::REGISTER:
                    cloud.registerNode(req.nodeId, String(req.str1), String(req.str2));
                    break;
                case CloudReqType::ACK:
                    cloud.sendAck(req.seq, (bool)(req.flags), req.f1);
                    break;
            }
        }
        cloud.loop();
    }
}

void setup() {
    Serial.begin(115200); delay(1000);
    Serial.println(F("[GW] Starting Central Gateway v3.0 (WiFi Manager)"));

    wifiManager.begin(webServer);
    wifiManager.waitForConnection(15000);

    if (wifiManager.isConnected()) {
        if (MDNS.begin("startup-gateway")) {
            MDNS.addService("http", "tcp", 80);
            Serial.println(F("[GW] mDNS: http://startup-gateway.local"));
        }
        Serial.printf("[DBG] Free heap: %u\n", ESP.getFreeHeap()); Serial.flush();

        initLoRa(); Serial.println(F("[DBG] LoRa done"));

        nodeMgr.begin(); Serial.println(F("[DBG] NodeMgr done")); Serial.flush();

        Serial.println(F("[DBG] Dashboard begin...")); Serial.flush();
        dashboard.begin(webServer); Serial.println(F("[DBG] Dashboard done")); Serial.flush();

    #ifndef TTGO_GATEWAY
        weather.begin(); Serial.println(F("[DBG] Weather done")); Serial.flush();
    #endif

        mqttBroker.begin(); Serial.println(F("[DBG] MQTT done")); Serial.flush();

        dashboard.onActuatorToggle([](uint16_t nodeId, bool on) {
            enqueueLoraTx(nodeId, on, 0);
            nodeMgr.setValveState(nodeId, on);
        });
        dashboard.onNodeProvision(onNodeProvision);
        dashboard.onNodeRemove([](uint16_t id) {
            nodeMgr.remove(id);
            dashboard.pushLog("info", ("Node 0x" + String(id, HEX) + " removed").c_str());
        });
        dashboard.onActuatorConfig(onActuatorConfig);
        dashboard.setNodesProvider([]() -> std::string { return nodeMgr.toJson(); });
        dashboard.setCloudCfgProvider([]() -> std::string {
            char buf[256];
            snprintf(buf, sizeof(buf), R"({"url":"%s","apiKey":"%s"})", cloud.getUrl().c_str(), cloud.getApiKey().c_str());
            return std::string(buf);
        });
        dashboard.onCloudCfgUpdate([](const char* url, const char* apiKey) {
            cloud.saveConfig(String(url), String(apiKey));
            Serial.printf("[GW] Cloud config updated: %s\n", url);
        });

        cloud.begin("", "");
        cloud.setCommandCallback([](uint32_t cmdId, uint16_t nodeId, bool on) {
            enqueueLoraTx(nodeId, on, cmdId);
            nodeMgr.setValveState(nodeId, on);
        });

        loraTxQueue = xQueueCreate(4, sizeof(LoraTxItem));
        cloudReqQueue = xQueueCreate(8, sizeof(CloudReq));
        xTaskCreatePinnedToCore(loraTask, "lora", 4096, NULL, 3, NULL, 0);
        xTaskCreatePinnedToCore(cloudTask, "cloud", 4096, NULL, 1, NULL, 0);

        Serial.print(F("[GW] Dashboard: http://")); Serial.println(wifiManager.getLocalIP());
    } else {
        webServer.begin();
        Serial.println(F("[GW] Setup mode: connect to AP and visit http://192.168.4.1"));
    }
}

void loop() {
    wifiManager.loop();
    if (!wifiManager.isConnected()) { vTaskDelay(pdMS_TO_TICKS(100)); return; }
    static uint32_t lastWeatherMs = 0;
    uint32_t now = millis();
    mqttBroker.loop();
    dashboard.loop();

#ifndef TTGO_GATEWAY
    if (now - lastWeatherMs >= WEATHER_INTERVAL_MS) {
        lastWeatherMs = now;
        WeatherData wd = weather.read();
        char buf[300];
        snprintf(buf, sizeof(buf),
            R"({"rain":%.2f,"wind":%.2f,"wind_adc":%d,"wind_deg":%.0f,"temp":%.1f,"hum":%.1f,"pres":%.1f,"lux":%.0f,"bat":%.0f,"ts":%u})",
            wd.rain_mm, wd.wind_speed_ms, wd.wind_vane_adc, wd.wind_dir_deg,
            wd.temperature_c, wd.humidity_pct, wd.pressure_hpa,
            wd.luminosity_lux, wd.battery_mv, now);
        mqttBroker.publish(String(MqttTopics::GW_TELEMETRY), String(buf));

        WeatherTelemetryData wt;
        wt.rain_mm = wd.rain_mm; wt.wind_speed_ms = wd.wind_speed_ms;
        wt.wind_dir_deg = (uint16_t)wd.wind_dir_deg; wt.temperature_c = wd.temperature_c;
        wt.humidity_pct = wd.humidity_pct; wt.pressure_hpa = wd.pressure_hpa;
        wt.luminosity_lux = wd.luminosity_lux; wt.battery_mv = wd.battery_mv;
        dashboard.pushWeatherTelemetry(wt);

        queueCloudWeather(wd.rain_mm, wd.wind_speed_ms, (int)wd.wind_dir_deg, wd.temperature_c);
    }
#endif
    vTaskDelay(pdMS_TO_TICKS(5));
}

static uint32_t lastAutoCmdMs = 0;
static void checkAutoControl(float moisturePct) {
    uint32_t now = millis();
    if (now - lastAutoCmdMs < 10000) return;
    for (int i = 0; i < nodeMgr.count(); i++) {
        const NodeInfo* ni = nodeMgr.getNode(i);
        if (!ni || ni->type != 0x02 || !ni->autoMode) continue;
        bool shouldOpen = moisturePct < ni->threshold;
        if (shouldOpen == ni->valveOpen) continue;
        lastAutoCmdMs = now;
        enqueueLoraTx(ni->id, shouldOpen, 0);
        nodeMgr.setValveState(ni->id, shouldOpen);
        dashboard.pushLog("info", (String("Auto ") + (shouldOpen ? "OPEN" : "CLOSE") +
                          " actuator 0x" + String(ni->id, HEX) +
                          " (moisture=" + String(moisturePct, 1) +
                          " threshold=" + String(ni->threshold) + ")").c_str());
    }
}

void onDecryptedPkt(uint16_t nodeId, PacketType type,
                     const uint8_t* pt, size_t len, uint32_t seq) {
    nodeMgr.handlePacket(nodeId, type, pt, len, seq);

    switch (type) {
        case PacketType::SENSOR_TELEMETRY: {
            if (len < 12) break;
            SensorTelemetry st;
            deserializeTelemetry(pt, st);
            float mp = max(0.0f, (float)st.moisture_pct - MOISTURE_DRY_OFFSET);
            float tc = st.temperature_c / 100.0f;
            float bv = st.battery_mv / 1000.0f;

            String t = MqttTopics::sensorTelemetry(nodeId).c_str();
            String p = MqttTopics::jsonTelemetry(st.sequence, mp, tc, bv, st.error_flags).c_str();
            mqttBroker.publish(t, p);

            SensorTelemetryData d;
            d.node_id = nodeId; d.moisture_percent = mp; d.temperature_c = tc;
            d.battery_v = bv; d.sequence = st.sequence; d.rssi = 0; d.timestamp = millis();
            dashboard.pushSensorTelemetry(d);

            queueCloudTelemetry(nodeId, mp, tc, bv, st.sequence, st.error_flags, 0);
            checkAutoControl(mp);
            break;
        }
        case PacketType::ACK: {
            if (len < 7) break;
            AckPayload ack;
            deserializeAck(pt, ack);
            String t = MqttTopics::actuatorAck(nodeId).c_str();
            String p = MqttTopics::jsonAck(ack.ack_seq, ack.result == 0, ack.battery_mv / 1000.0f).c_str();
            mqttBroker.publish(t, p);
            for (int i = 0; i < nodeMgr.count(); i++) {
                const NodeInfo* ni = nodeMgr.getNode(i);
                if (ni && ni->id == nodeId) {
                    ActuatorStateData asd;
                    asd.node_id = nodeId;
                    asd.valve_open = ni->valveOpen;
                    asd.battery_v = ack.battery_mv / 1000.0f;
                    asd.timestamp = millis();
                    dashboard.pushActuatorState(asd);
                    break;
                }
            }
            if (ack.ack_seq < 0x80000000) {
                queueCloudAck(ack.ack_seq, ack.result == 0, ack.battery_mv / 1000.0f);
            }
            break;
        }
        case PacketType::HEARTBEAT: {
            if (len < 7) break;
            HeartbeatPayload hb;
            deserializeHeartbeat(pt, hb);
            ActuatorStateData asd;
            asd.node_id = nodeId;
            asd.valve_open = hb.valve_state;
            asd.battery_v = hb.battery_mv / 1000.0f;
            asd.timestamp = millis();
            dashboard.pushActuatorState(asd);
            Serial.printf("[GW] HB node=%04X batt=%.2f valve=%s\n",
                          nodeId, asd.battery_v, asd.valve_open ? "OPEN" : "CLOSED");
            queueCloudHeartbeat(nodeId, hb.valve_state, asd.battery_v);
            break;
        }
        default: break;
    }
}

void enqueueLoraTx(uint16_t nodeId, bool on, uint32_t cloudCmdId) {
    Serial.printf("[GW] Toggle actuator %04X -> %s\n", nodeId, on ? "ON" : "OFF");

    ActuatorCommand cmd;
    cmd.sequence  = (cloudCmdId != 0) ? cloudCmdId : downlinkSeq++;
    cmd.command   = 0x01;
    cmd.value     = on ? 1 : 0;
    cmd.timeout_s = 0;

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
    }
}

void onNodeProvision(uint16_t nodeId, const uint8_t psk[16], const char* type, const char* alias) {
    uint8_t ntype = (strcmp(type, "actuator") == 0) ? 0x02 : 0x01;
    nodeMgr.provision(nodeId, ntype, psk, alias);
    Serial.printf("[GW] Provisioned node %04X as %s (%s)\n", nodeId, type, alias);
    dashboard.pushLog("info", ("Node " + String(nodeId, HEX) + " provisioned as " + String(type)).c_str());
    {   char buf[200];
        snprintf(buf, sizeof(buf),
            R"({"type":"provisioned","id":%u,"node_type":"%s","alias":"%s","status":"ok"})",
            nodeId, type, alias);
        dashboard.pushRaw(buf);
    }
    queueCloudNode(nodeId, type, alias);
}

void onActuatorConfig(uint16_t nodeId, bool autoMode, uint8_t threshold, uint16_t sensorId) {
    if (nodeMgr.setActuatorConfig(nodeId, autoMode, threshold, sensorId)) {
        Serial.printf("[GW] Actuator 0x%04X config: auto=%s threshold=%u sensorId=%u\n",
                      nodeId, autoMode ? "ON" : "OFF", threshold, sensorId);
        dashboard.pushLog("info", ("Actuator 0x" + String(nodeId, HEX) + " config updated").c_str());
    }
}

void queueCloudTelemetry(uint16_t nodeId, float mp, float tc, float bv, uint32_t seq, uint8_t err, uint8_t rssi) {
    CloudReq r; r.type = CloudReqType::TELEMETRY; r.nodeId = nodeId;
    r.f1 = mp; r.f2 = tc; r.f3 = bv; r.seq = seq; r.flags = err; r.rssi = rssi;
    xQueueSend(cloudReqQueue, &r, 0);
}
void queueCloudHeartbeat(uint16_t nodeId, bool valveOpen, float bv) {
    CloudReq r; r.type = CloudReqType::HEARTBEAT; r.nodeId = nodeId;
    r.flags = valveOpen ? 1 : 0; r.f1 = bv;
    xQueueSend(cloudReqQueue, &r, 0);
}
void queueCloudWeather(float rain, float wind, int wdir, float temp) {
    CloudReq r; r.type = CloudReqType::WEATHER;
    r.f1 = rain; r.f2 = wind; r.f3 = (float)wdir; r.seq = (uint32_t)temp;
    xQueueSend(cloudReqQueue, &r, 0);
}
void queueCloudNode(uint16_t nodeId, const char* type, const char* alias) {
    CloudReq r; r.type = CloudReqType::REGISTER; r.nodeId = nodeId;
    strncpy(r.str1, type, sizeof(r.str1)-1);
    strncpy(r.str2, alias, sizeof(r.str2)-1);
    xQueueSend(cloudReqQueue, &r, 0);
}
void queueCloudAck(uint32_t cmdId, bool ok, float bv) {
    CloudReq r; r.type = CloudReqType::ACK; r.seq = cmdId; r.flags = ok ? 1 : 0; r.f1 = bv;
    xQueueSend(cloudReqQueue, &r, 0);
}

void initLoRa() {
#ifdef TTGO_GATEWAY
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
#else
    SPI.begin(12, 13, 11, LORA_NSS);
#endif
    loraReady = false;
    int st = radio.begin(LORA_FREQ, 125.0f, 9, 5, 0x12, 10, 12);
    Serial.printf("[GW] LoRa begin=%d chipver=0x%02X\n", st, radio.getChipVersion());
    if (st != 0) { return; }
    loraReady = true;
    uint8_t warmup[2] = {0};
    int tst = manualTransmit(radio, warmup, 2);
    Serial.printf("[GW] warmup Tx: ret=%d\n", tst);
    tst = manualTransmit(radio, warmup, 2);
    Serial.printf("[GW] warmup2 Tx: ret=%d\n", tst);
    st = radio.startReceive();
    if (st == 0) { Serial.println("[GW] LoRa ready @ 868 MHz"); } else { Serial.println("[GW] Rx start fail"); }
}
