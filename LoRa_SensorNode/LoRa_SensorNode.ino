/*
 * Sensor Node — ESP32-S3 + LoRa + Soil Moisture + DS18B20
 * Board: ESP32-S3 Dev Module
 * Dependencies: RadioLib, OneWire, DallasTemperature,
 *               LoraNetwork (local library)
 *
 * Lifecycle: Deep Sleep → Wake → Read → Encrypt → Transmit → Deep Sleep
 *
 * Deep sleep current: ~10 µA @ 3.3V
 * Active duration:    ~3 seconds @ ~80 mA
 * Interval:           15 minutes (configurable)
 */
#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <PacketTypes.h>
#include <CryptoEngine.h>

// ── Provisioned Identity ──────────────────────────────────────────────
const uint16_t NODE_ID      = 0x0001;
const uint8_t  NODE_PSK[16] = {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
                               0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF};

// ── LoRa ──────────────────────────────────────────────────────────────
const float    LORA_FREQ = 868.0f;
const uint8_t  LORA_NSS = 5, LORA_DIO1 = 9, LORA_RST = 10, LORA_BUSY = 6;
Module loraMod(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
SX1262 radio(&loraMod);

// ── Sensors ───────────────────────────────────────────────────────────
const uint8_t MOISTURE_PIN = 1;   // ADC1_CH0 (GPIO1)
const uint8_t ONE_WIRE_PIN = 2;   // DS18B20 data
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature ds18b20(&oneWire);

// ── Power ─────────────────────────────────────────────────────────────
RTC_DATA_ATTR uint32_t bootCount  = 0;
RTC_DATA_ATTR uint32_t seqCounter = 0;
//const uint64_t SLEEP_US = 900000000ULL;  // 15 minutes

// ── Battery ───────────────────────────────────────────────────────────
const uint8_t BATT_PIN = 4;  // ADC1_CH3 (GPIO4)
float readBattery() {
    uint32_t sum = 0;
    for (int i = 0; i < 32; i++) { sum += analogRead(BATT_PIN); delayMicroseconds(50); }
    return (sum / 32.0f / 4095.0f) * 3.3f * 2.0f;
}

// ═══════════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200); delay(100);
    bootCount++; seqCounter++;
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    // Init LoRa
    SPI.begin(6, 8, 7, LORA_NSS);
    int st = radio.begin(LORA_FREQ, 125.0f, 9, 5,
                          RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 8, 10, 1.6f, false);
    if (st != RADIOLIB_ERR_NONE) {
        Serial.printf("[SN] LoRa error: %d\n", st);
        goToSleep();
        return;
    }

    // Read sensors
    float battV = readBattery();
    int moistADC = 0;
    for (int i = 0; i < 16; i++) { moistADC += analogRead(MOISTURE_PIN); delayMicroseconds(100); }
    moistADC /= 16;

    ds18b20.begin();
    ds18b20.requestTemperatures();
    delay(750);
    float tempC = ds18b20.getTempCByIndex(0);
    bool tempOk = (tempC != DEVICE_DISCONNECTED_C);
    uint8_t err = 0;
    if (moistADC > 4000 || moistADC < 10) err |= 0x01;
    if (!tempOk) { tempC = 0; err |= 0x02; }

    Serial.printf("[SN] Boot=%u Seq=%u Moist=%u Temp=%.2f Batt=%.2fV\n",
                  bootCount, seqCounter, moistADC, tempC, battV);

    // Build, encrypt, transmit
    SensorTelemetry payload;
    payload.sequence      = seqCounter;
    payload.moisture_raw  = moistADC;
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
        goToSleep();
        return;
    }

    uint8_t tx[LORA_MAX_PAYLOAD]; size_t o = 0;
    memcpy(tx+o, frame.node_id, 2); o+=2;
    memcpy(tx+o, frame.iv, 12);     o+=12;
    tx[o++] = frame.pkt_type;
    memcpy(tx+o, pt, sizeof(payload)); o+=sizeof(payload);
    memcpy(tx+o, frame.mic, 4);        o+=4;

    st = radio.transmit(tx, o);
    Serial.printf("[SN] Tx %u bytes → %s\n", o, st == RADIOLIB_ERR_NONE ? "OK" : "FAIL");

    goToSleep();
}

void loop() {}

void goToSleep() {
    radio.sleep();
    Serial.flush();
    esp_sleep_enable_timer_wakeup(SLEEP_US);
    esp_deep_sleep_start();
}
