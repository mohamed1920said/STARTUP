#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#ifndef TTGO_GATEWAY
#include <RadioLib.h>
#else
#include <LoraRadio.h>
#endif
#include <PicoMQTT.h>
#include <PacketTypes.h>
#include <CryptoEngine.h>
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
CryptoEngine crypto;
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

void setup() {
    Serial.begin(115200); delay(1000);
    Serial.println(F("[GW] Starting Central Gateway v1.0"));

    initWiFi(); Serial.println(F("[DBG] WiFi done"));
    Serial.printf("[DBG] Free heap: %u\n", ESP.getFreeHeap()); Serial.flush();

    crypto.begin(GW_PSK, 16); Serial.println(F("[DBG] Crypto done"));

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
    int state = radio.receive(buf, sizeof(buf), 250);
    if (state != RADIOLIB_ERR_NONE) return;
    uint16_t len = radio.getPacketLength();
    Serial.printf("[GW] Raw pkt: len=%u\n", len);
    if (len < LORA_HEADER_SIZE + MIC_SIZE || len > sizeof(buf)) {
        return;
    }

    uint16_t nodeId = (buf[0] << 8) | buf[1];
    uint8_t  pktType = buf[NODE_ID_SIZE + IV_NONCE_SIZE];
    uint32_t seq = 0;
    for (int i = 0; i < 8; i++) seq = (seq << 8) | buf[NODE_ID_SIZE + 4 + i];

    size_t ctLen = len - LORA_HEADER_SIZE - MIC_SIZE;
    if (ctLen > LORA_MAX_CIPHERTEXT) return;

    LoraFrame frame;
    memcpy(frame.node_id, buf, NODE_ID_SIZE);
    memcpy(frame.iv, buf + NODE_ID_SIZE, IV_NONCE_SIZE);
    frame.pkt_type = pktType;
    memcpy(frame.ciphertext, buf + LORA_HEADER_SIZE, ctLen);
    memcpy(frame.mic, buf + LORA_HEADER_SIZE + ctLen, MIC_SIZE);
    frame.ciphertext_len = ctLen;

    const uint8_t* nodePsk = nodeMgr.getPsk(nodeId);
    if (!nodePsk) {
        Serial.printf("[GW] Unknown node %04X\n", nodeId);
        return;
    }
    CryptoEngine nodeCrypto;
    if (!nodeCrypto.begin(nodePsk, 16)) return;

    uint8_t pt[LORA_MAX_CIPHERTEXT];
    memcpy(pt, frame.ciphertext, ctLen);
    if (!nodeCrypto.decrypt(pt, ctLen, pktType, seq, nodeId, frame)) {
        Serial.printf("[GW] Decrypt fail node=%04X seq=%u\n", nodeId, seq);
        return;
    }
    onDecryptedPkt(nodeId, (PacketType)pktType, pt, ctLen, seq);
}

void onDecryptedPkt(uint16_t nodeId, PacketType type,
                     const uint8_t* pt, size_t len, uint32_t seq) {
    nodeMgr.handlePacket(nodeId, type, pt, len, seq);

    switch (type) {
        case PacketType::SENSOR_TELEMETRY: {
            if (len < sizeof(SensorTelemetry)) break;
            auto* st = (const SensorTelemetry*)pt;
            float mp = st->moisture_pct;
            float tc = st->temperature_c / 100.0f;
            float bv = st->battery_mv / 1000.0f;

            String t = MqttTopics::sensorTelemetry(nodeId).c_str();
            String p = MqttTopics::jsonTelemetry(st->sequence, mp, tc, bv, st->error_flags).c_str();
            mqttBroker.publish(t, p);

            SensorTelemetryData d;
            d.node_id = nodeId; d.moisture_percent = mp; d.temperature_c = tc;
            d.battery_v = bv; d.sequence = st->sequence; d.rssi = 0; d.timestamp = millis();
            dashboard.pushSensorTelemetry(d);
            break;
        }
        case PacketType::ACK: {
            if (len < sizeof(AckPayload)) break;
            auto* ack = (const AckPayload*)pt;
            String t = MqttTopics::actuatorAck(nodeId).c_str();
            String p = MqttTopics::jsonAck(ack->ack_seq, ack->result == 0, ack->battery_mv / 1000.0f).c_str();
            mqttBroker.publish(t, p);
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

void onActuatorToggle(uint16_t nodeId, bool on) {
    Serial.printf("[GW] Toggle actuator %04X -> %s\n", nodeId, on ? "ON" : "OFF");
    ActuatorCommand cmd;
    cmd.sequence = downlinkSeq++;
    cmd.command = 0x01; cmd.value = on ? 1 : 0; cmd.timeout_s = 0;

    uint8_t pt[sizeof(cmd)];
    memcpy(pt, &cmd, sizeof(cmd));

    uint8_t sk[16];
    CryptoEngine::deriveSessionKey(GW_PSK, nodeId, sk);
    CryptoEngine enc;
    if (!enc.begin(sk, 16)) return;

    LoraFrame f;
    f.ciphertext_len = sizeof(cmd);
    f.node_id[0] = nodeId >> 8; f.node_id[1] = nodeId & 0xFF;
    f.pkt_type = (uint8_t)PacketType::ACTUATOR_COMMAND;

    if (!enc.encrypt(pt, sizeof(cmd), (uint8_t)PacketType::ACTUATOR_COMMAND,
                      cmd.sequence, nodeId, f)) return;

    uint8_t tx[LORA_MAX_PAYLOAD]; size_t o = 0;
    memcpy(tx+o, f.node_id, 2); o+=2;
    memcpy(tx+o, f.iv, 12);     o+=12;
    tx[o++] = f.pkt_type;
    memcpy(tx+o, pt, sizeof(cmd)); o+=sizeof(cmd);
    memcpy(tx+o, f.mic, 4); o+=4;

    radio.transmit(tx, o);
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
    int st = radio.begin(LORA_FREQ, 125.0f, 9, 5, 0x12, 10, 8);
    if (st != 0) {
        Serial.printf("[GW] LoRa error: %d\n", st);
        return;
    }
    Serial.println(F("[GW] LoRa ready @ 868 MHz"));
}
