#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PacketTypes.h>
#include <CryptoEngine.h>

const uint16_t NODE_ID      = 0x0001;
const uint8_t  NODE_PSK[16] = {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
                               0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF};

const float    LORA_FREQ = 868.0f;
const uint8_t  PIN_RST = 14;
Module loraMod(LORA_CS, LORA_IRQ, PIN_RST, RADIOLIB_NC);
SX1276 radio(&loraMod);

const uint8_t MOISTURE_PIN = 34;
const uint8_t ONE_WIRE_BUS = 4;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);

const uint8_t BATT_PIN = 35;
const int MOISTURE_DRY = 2500;
const int MOISTURE_WET = 400;
const uint32_t TX_INTERVAL_MS = 5000;
uint32_t lastTxMs = 0;
uint32_t seqCounter = 0;
bool tempRequested = false;

float readBattery() {
    uint32_t sum = 0;
    for (int i = 0; i < 32; i++) { sum += analogRead(BATT_PIN); delayMicroseconds(50); }
    return (sum / 32.0f / 4095.0f) * 3.3f * 2.0f;
}

bool sendTelemetry() {
    seqCounter++;
    float battV = readBattery();
    int moistADC = 0;
    for (int i = 0; i < 16; i++) { moistADC += analogRead(MOISTURE_PIN); delayMicroseconds(100); }
    moistADC /= 16;

    if (!tempRequested) { ds18b20.requestTemperatures(); tempRequested = true; }
    float tempC = ds18b20.getTempCByIndex(0);
    bool tempOk = (tempC != DEVICE_DISCONNECTED_C);
    uint8_t err = 0;
    if (moistADC > 4000 || moistADC < 10) err |= 0x01;
    if (!tempOk) { tempC = 0; err |= 0x02; }

    Serial.printf("[SN] Seq=%u Moist=%u Temp=%.2f Batt=%.2fV\n",
                  seqCounter, moistADC, tempC, battV);

    SensorTelemetry payload;
    payload.sequence      = seqCounter;
    payload.moisture_raw  = moistADC;
    int pct = map(moistADC, MOISTURE_DRY, MOISTURE_WET, 0, 100);
    payload.moisture_pct = (uint8_t)constrain(pct, 0, 100);
    payload.temperature_c = (int16_t)(tempC * 100);
    payload.battery_mv    = (uint16_t)(battV * 1000);
    payload.error_flags   = err;

    CryptoEngine crypto;
    crypto.begin(NODE_PSK, 16);

    uint8_t pt[sizeof(payload)];
    memcpy(pt, &payload, sizeof(payload));

    LoraFrame frame;
    frame.ciphertext_len = sizeof(payload);
    frame.node_id[0] = NODE_ID >> 8; frame.node_id[1] = NODE_ID & 0xFF;
    frame.pkt_type = (uint8_t)PacketType::SENSOR_TELEMETRY;

    if (!crypto.encrypt(pt, sizeof(payload), (uint8_t)PacketType::SENSOR_TELEMETRY,
                         seqCounter, NODE_ID, frame)) {
        Serial.println(F("[SN] Encrypt failed"));
        return false;
    }

    uint8_t tx[LORA_MAX_PAYLOAD]; size_t o = 0;
    memcpy(tx+o, frame.node_id, 2); o+=2;
    memcpy(tx+o, frame.iv, 12);     o+=12;
    tx[o++] = frame.pkt_type;
    memcpy(tx+o, pt, sizeof(payload)); o+=sizeof(payload);
    memcpy(tx+o, frame.mic, 4);        o+=4;

    int st = radio.transmit(tx, o);
    Serial.printf("[SN] Tx %u bytes -> %s\n", o, st == RADIOLIB_ERR_NONE ? "OK" : "FAIL");
    ds18b20.requestTemperatures();
    return (st == RADIOLIB_ERR_NONE);
}

void setup() {
    Serial.begin(115200); delay(100);
    Serial.printf("[SN] Sensor Node %04X starting (no deep sleep)\n", NODE_ID);

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    int st = radio.begin(LORA_FREQ, 125.0f, 9, 5,
                          RADIOLIB_SX127X_SYNC_WORD, 10, 8);
    if (st != RADIOLIB_ERR_NONE) {
        Serial.printf("[SN] LoRa error: %d\n", st);
    } else {
        Serial.println(F("[SN] LoRa ready"));
    }

    ds18b20.begin();
    ds18b20.requestTemperatures();
    tempRequested = true;
    sendTelemetry();
    lastTxMs = millis();
}

void loop() {
    uint32_t now = millis();
    if (now - lastTxMs >= TX_INTERVAL_MS) {
        lastTxMs = now;
        sendTelemetry();
    }
    delay(100);
}
