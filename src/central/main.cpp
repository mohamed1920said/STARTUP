#include <Arduino.h>
#include <WiFi.h>
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
#include "WeatherStation.h"
#include "NodeManager.h"

const char* WIFI_SSID = "msi";
const char* WIFI_PASS = "12345678";

const float    LORA_FREQ  = 868.0f;
#ifdef TTGO_GATEWAY
const uint8_t  PIN_LORA_RST = 14;
#else
const uint8_t  LORA_NSS = 10, LORA_DIO0 = 2, LORA_RST = 9;
const uint8_t  RAIN_PIN   = 14, WIND_PIN = 15, VANE_PIN = 16;
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
WeatherStation weather(RAIN_PIN, WIND_PIN, VANE_PIN);
#endif
NodeManager nodeMgr;

uint32_t lastWeatherMs = 0;
uint32_t downlinkSeq   = 1;

void initWiFi();
void initLoRa();
void processLoRa();
void onDecryptedPkt(uint16_t node, PacketType type,
                     const uint8_t* pt, size_t len, uint32_t seq);
void onActuatorToggle(uint16_t nodeId, bool on);
void onNodeProvision(uint16_t nodeId, const uint8_t psk[16], const char* type, const char* alias);
void onActuatorConfig(uint16_t nodeId, bool autoMode, uint8_t threshold, uint16_t sensorId);


void setup() {
    Serial.begin(115200); delay(1000);
    Serial.println(F("[GW] Starting Central Gateway v1.0"));

    initWiFi(); Serial.println(F("[DBG] WiFi done"));
    Serial.printf("[DBG] Free heap: %u\n", ESP.getFreeHeap()); Serial.flush();

    initLoRa(); Serial.println(F("[DBG] LoRa done"));

    nodeMgr.begin(); Serial.println(F("[DBG] NodeMgr done")); Serial.flush();

    Serial.println(F("[DBG] Dashboard begin...")); Serial.flush();
    dashboard.begin(webServer); Serial.println(F("[DBG] Dashboard done")); Serial.flush();

#ifndef TTGO_GATEWAY
    weather.begin(); Serial.println(F("[DBG] Weather done")); Serial.flush();
#endif

    mqttBroker.begin(); Serial.println(F("[DBG] MQTT done")); Serial.flush();

    dashboard.onActuatorToggle(onActuatorToggle);
    dashboard.onNodeProvision(onNodeProvision);
    dashboard.onNodeRemove([](uint16_t id) {
        nodeMgr.remove(id);
        dashboard.pushLog("info", ("Node 0x" + String(id, HEX) + " removed").c_str());
    });
    dashboard.onActuatorConfig(onActuatorConfig);
    dashboard.setNodesProvider([]() -> std::string { return nodeMgr.toJson(); });

    Serial.print(F("[GW] Dashboard: http://")); Serial.println(WiFi.localIP());
}

void loop() {
    uint32_t now = millis();
    processLoRa();
    mqttBroker.loop();
    dashboard.loop();

#ifndef TTGO_GATEWAY
    if (now - lastWeatherMs >= 30000) {
        lastWeatherMs = now;
        WeatherData wd = weather.read();
        char buf[200];
        snprintf(buf, sizeof(buf),
            R"({"rain":%.2f,"wind":%.2f,"wind_adc":%d,"ts":%u})",
            wd.rain_mm, wd.wind_speed_ms, wd.wind_vane_adc, now);
        mqttBroker.publish(String(MqttTopics::GW_TELEMETRY), String(buf));

        WeatherTelemetryData wt;
        wt.rain_mm = wd.rain_mm; wt.wind_speed_ms = wd.wind_speed_ms;
        wt.wind_dir_deg = wd.wind_vane_adc; wt.temperature_c = 0;
        dashboard.pushWeatherTelemetry(wt);
    }
#endif
    delay(5);
}

void processLoRa() {
    uint8_t buf[LORA_MAX_PAYLOAD];
    size_t len;
#ifdef TTGO_GATEWAY
    if (radio.available() <= 0) return;
    len = radio.getPacketLength();
    if (len < LORA_HEADER_SIZE + MIC_SIZE || len > sizeof(buf)) { radio.startReceive(); return; }
    if (radio.readData(buf, len) != 0) { radio.startReceive(); return; }
    radio.startReceive();
#else
    // Non-blocking: poll IRQ register directly (bypass DIO0 interrupt)
    if (!(radio.getIRQFlags() & RADIOLIB_SX127X_CLEAR_IRQ_FLAG_RX_DONE)) return;
    len = radio.getPacketLength();
    if (len < LORA_HEADER_SIZE + MIC_SIZE || len > sizeof(buf)) { radio.startReceive(); return; }
    if (radio.readData(buf, len) != 0) { radio.startReceive(); return; }
    radio.startReceive();
#endif
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
    if (!nodePsk) {
        Serial.printf("[GW] Unknown node %04X\n", nodeId);
        return;
    }

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
        onActuatorToggle(ni->id, shouldOpen);
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
            float mp = st.moisture_pct;
            float tc = st.temperature_c / 100.0f;
            float bv = st.battery_mv / 1000.0f;

            String t = MqttTopics::sensorTelemetry(nodeId).c_str();
            String p = MqttTopics::jsonTelemetry(st.sequence, mp, tc, bv, st.error_flags).c_str();
            mqttBroker.publish(t, p);

            SensorTelemetryData d;
            d.node_id = nodeId; d.moisture_percent = mp; d.temperature_c = tc;
            d.battery_v = bv; d.sequence = st.sequence; d.rssi = 0; d.timestamp = millis();
            dashboard.pushSensorTelemetry(d);

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
            break;
        }
        default: break;
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
}

void onActuatorConfig(uint16_t nodeId, bool autoMode, uint8_t threshold, uint16_t sensorId) {
    if (nodeMgr.setActuatorConfig(nodeId, autoMode, threshold, sensorId)) {
        Serial.printf("[GW] Actuator 0x%04X config: auto=%s threshold=%u sensorId=%u\n",
                      nodeId, autoMode ? "ON" : "OFF", threshold, sensorId);
        dashboard.pushLog("info", ("Actuator 0x" + String(nodeId, HEX) + " config updated").c_str());
    }
}

// Use startTransmit (sets up FIFO, enters TX mode) then poll IRQ flags register for TxDone
// This bypasses DIO0 polling which fails on boards where DIO0 conflicts with PSRAM
static int manualTransmit(SX1276& radio, uint8_t* data, size_t len) {
    int st = radio.startTransmit(data, len);
    if (st != 0) { Serial.printf("[GW] startTransmit fail: %d\n", st); return st; }
    uint32_t start = millis();
    while (millis() - start < 2000) {
        uint16_t irq = radio.getIRQFlags();
        if (irq & RADIOLIB_SX127X_CLEAR_IRQ_FLAG_TX_DONE) {
            radio.standby();
            return 0;
        }
        delay(1);
    }
    radio.standby();
    return -5;
}

void onActuatorToggle(uint16_t nodeId, bool on) {
    Serial.printf("[GW] Toggle actuator %04X -> %s\n", nodeId, on ? "ON" : "OFF");
    nodeMgr.setValveState(nodeId, on);

    ActuatorCommand cmd;
    cmd.sequence  = downlinkSeq++;
    cmd.command   = 0x01;
    cmd.value     = on ? 1 : 0;
    cmd.timeout_s = 0;

    uint8_t pt[COMMAND_WIRE_SIZE];
    serializeCommand(pt, cmd);

    Serial.printf("[GW] cmd seq=%u\n", cmd.sequence);

    const uint8_t* nodePsk = nodeMgr.getPsk(nodeId);
    if (!nodePsk) { Serial.printf("[GW] No PSK for node %04X\n", nodeId); return; }

    LoraCrypto enc;
    enc.setKey(nodePsk, 16);
    uint8_t iv[GCM_IV_SIZE], mic[MIC_SIZE];
    CryptoResult cr = enc.encrypt(pt, COMMAND_WIRE_SIZE,
                                   (uint8_t)PacketType::ACTUATOR_COMMAND,
                                   cmd.sequence, nodeId, iv, mic);
    if (cr != CryptoResult::OK) {
        Serial.printf("[GW] encrypt fail err=%d\n", (int)cr);
        return;
    }

    uint8_t tx[LORA_MAX_PAYLOAD]; size_t o = 0;
    uint8_t node_be[2] = { uint8_t(nodeId >> 8), uint8_t(nodeId & 0xFF) };
    memcpy(tx+o, node_be, 2); o+=2;
    memcpy(tx+o, iv, 12);     o+=12;
    tx[o++] = (uint8_t)PacketType::ACTUATOR_COMMAND;
    memcpy(tx+o, pt, COMMAND_WIRE_SIZE); o+=COMMAND_WIRE_SIZE;
    memcpy(tx+o, mic, MIC_SIZE);         o+=MIC_SIZE;
    Serial.printf("[GW] Tx pkt: %u bytes (wire ct=%u)\n", (unsigned)o, (unsigned)COMMAND_WIRE_SIZE);

    radio.standby();
    delay(100);
    radio.setFrequency(LORA_FREQ); delay(100);
    int tr = manualTransmit(radio, tx, o);
    Serial.printf("[GW] Tx seq=%u: ret=%d\n", cmd.sequence, tr);
    if (tr != 0) {
        delay(200);
        tr = manualTransmit(radio, tx, o);
        Serial.printf("[GW] Tx seq=%u retry: ret=%d\n", cmd.sequence, tr);
    }
    radio.startReceive();
}

void initWiFi() {
    Serial.printf("[GW] Connecting to %s ... ", WIFI_SSID);
    WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print('.'); }
    Serial.println(F(" OK"));
}

void initLoRa() {
#ifdef TTGO_GATEWAY
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
#else
    SPI.begin(12, 13, 11, LORA_NSS);
#endif
    int st = radio.begin(LORA_FREQ, 125.0f, 9, 5, 0x12, 10, 12);
    Serial.printf("[GW] LoRa begin=%d chipver=0x%02X\n", st, radio.getChipVersion());
    if (st != 0) { return; }
    uint8_t warmup[2] = {0};
    int tst = manualTransmit(radio, warmup, 2);
    Serial.printf("[GW] warmup Tx: ret=%d\n", tst);
    tst = manualTransmit(radio, warmup, 2);
    Serial.printf("[GW] warmup2 Tx: ret=%d\n", tst);
    st = radio.startReceive();
    if (st == 0) { Serial.println("[GW] LoRa ready @ 868 MHz"); } else { Serial.println("[GW] Rx start fail"); }
}
