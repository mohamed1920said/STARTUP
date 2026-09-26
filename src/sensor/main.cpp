#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>
#include <LoraProtocol.h>
#include <DeviceIdentity.h>
#include <esp_sleep.h>

#ifndef SENSOR_DEEP_SLEEP
#define SENSOR_DEEP_SLEEP 1
#endif
#ifndef SENSOR_TX_INTERVAL_MS
#define SENSOR_TX_INTERVAL_MS 120000UL
#endif
#ifndef SENSOR_PROVISION_WINDOW_MS
#define SENSOR_PROVISION_WINDOW_MS 10000UL
#endif

const float    LORA_FREQ = 868.0f;
Module loraMod(LORA_CS, LORA_IRQ, LORA_RST, RADIOLIB_NC);
SX1276 radio(&loraMod);
DeviceIdentity identity(DeviceRole::SENSOR);
bool radioReady = false;

const uint8_t MOISTURE_PIN = 34;
const uint8_t ONE_WIRE_BUS = 4;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);

const uint8_t BATT_PIN = 35;
const int MOISTURE_DRY = 2500;
const int MOISTURE_WET = 400;
const uint32_t TX_INTERVAL_MS = SENSOR_TX_INTERVAL_MS;
uint32_t lastTxMs = 0;
RTC_DATA_ATTR uint32_t seqCounter = 0;
RTC_DATA_ATTR uint32_t seqLimit = 0;
bool tempRequested = false;

uint32_t nextPersistentSequence() {
    if (seqCounter >= seqLimit) {
        Preferences prefs;
        if (!prefs.begin("lora_seq", false)) return 0;
        const uint32_t start = prefs.getULong("next", 1);
        if (start == 0 || start > UINT32_MAX - 1024) { prefs.end(); return 0; }
        const uint32_t reservedLimit = start + 1023;
        const uint32_t nextBlock = reservedLimit + 1;
        const bool reserved = prefs.putULong("next", nextBlock) == sizeof(uint32_t) &&
            prefs.getULong("next", 0) == nextBlock;
        prefs.end();
        if (!reserved) return 0;
        seqCounter = start - 1;
        seqLimit = reservedLimit;
    }
    return ++seqCounter;
}

float readBattery() {
    uint32_t sum = 0;
    for (int i = 0; i < 32; i++) { sum += analogRead(BATT_PIN); delayMicroseconds(50); }
    return (sum / 32.0f / 4095.0f) * 3.3f * 2.0f;
}

bool sendTelemetry() {
    if (!identity.ready() || !radioReady) return false;
    uint32_t sequence = nextPersistentSequence();
    if (sequence == 0) { Serial.println(F("[SN] Sequence space exhausted")); return false; }
    float battV = readBattery();
    int moistADC = 0;
    for (int i = 0; i < 16; i++) { moistADC += analogRead(MOISTURE_PIN); delayMicroseconds(100); }
    moistADC /= 16;

    if (!tempRequested) { ds18b20.requestTemperatures(); tempRequested = true; }
    float tempC = ds18b20.getTempCByIndex(0);
    bool tempOk = (tempC != DEVICE_DISCONNECTED_C);
    uint8_t err = 0;
    if (moistADC > 4000 || moistADC < 10) err |= SENSOR_ERROR_MOISTURE;
    if (!tempOk) { tempC = 0; err |= SENSOR_ERROR_TEMPERATURE; }

    Serial.printf("[SN] Seq=%u Moist=%u Temp=%.2f Batt=%.2fV\n",
                  sequence, moistADC, tempC, battV);

    SensorTelemetry payload;
    payload.sequence      = sequence;
    payload.moisture_raw  = moistADC;
    int pct = map(moistADC, MOISTURE_DRY, MOISTURE_WET, 0, 100);
    payload.moisture_pct = (uint8_t)constrain(pct, 0, 100);
    payload.temperature_c = (int16_t)(tempC * 100);
    payload.battery_mv    = (uint16_t)(battV * 1000);
    payload.error_flags   = err;

    uint8_t pt[TELEMETRY_WIRE_SIZE];
    serializeTelemetry(pt, payload);

    LoraCrypto crypto;
    crypto.setKey(identity.psk(), AES128_KEY_SIZE);

    uint8_t iv[GCM_IV_SIZE], mic[MIC_SIZE];
    CryptoResult cr = crypto.encrypt(pt, TELEMETRY_WIRE_SIZE,
                                      (uint8_t)PacketType::SENSOR_TELEMETRY,
                                      sequence, identity.nodeId(), iv, mic);
    if (cr != CryptoResult::OK) {
        Serial.printf("[SN] Encrypt failed err=%d\n", (int)cr);
        return false;
    }

    uint8_t tx[LORA_MAX_PAYLOAD]; size_t o = 0;
    const uint16_t nodeId = identity.nodeId();
    uint8_t node_be[2] = { uint8_t(nodeId >> 8), uint8_t(nodeId & 0xFF) };
    memcpy(tx+o, node_be, 2); o+=2;
    memcpy(tx+o, iv, 12);     o+=12;
    tx[o++] = (uint8_t)PacketType::SENSOR_TELEMETRY;
    memcpy(tx+o, pt, TELEMETRY_WIRE_SIZE); o+=TELEMETRY_WIRE_SIZE;
    memcpy(tx+o, mic, MIC_SIZE);           o+=MIC_SIZE;

    int st = radio.transmit(tx, o);
    Serial.printf("[SN] Tx %u bytes -> %s\n", o, st == RADIOLIB_ERR_NONE ? "OK" : "FAIL");
    ds18b20.requestTemperatures();
    return (st == RADIOLIB_ERR_NONE);
}

bool serviceIdentityConsole() {
    const IdentityEvent event = identity.serviceConsole(Serial);
    if (event == IdentityEvent::NONE) return false;
    Serial.println(F("[SN] Restarting after identity change"));
    Serial.flush();
    delay(100);
    ESP.restart();
    return true;
}

void printProvisioningHelp() {
    identity.printStatus(Serial);
    if (identity.state() == IdentityState::EMPTY) {
        Serial.println(F("[SN] Radio disabled. Use: PROVISION 0001 <32-hex-PSK>"));
    } else if (identity.state() == IdentityState::CORRUPT ||
               identity.state() == IdentityState::ROLE_MISMATCH) {
        Serial.println(F("[SN] Radio disabled. Use: ERASE CORRUPT CONFIRM"));
    } else {
        Serial.println(F("[SN] Identity storage unavailable; radio remains disabled"));
    }
}

void enterDeepSleep() {
#if SENSOR_DEEP_SLEEP
    if (radioReady) radio.sleep();
    Serial.printf("[SN] Sleeping for %lu seconds\n", static_cast<unsigned long>(TX_INTERVAL_MS / 1000UL));
    Serial.flush();
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(TX_INTERVAL_MS) * 1000ULL);
    esp_deep_sleep_start();
#endif
}

void setup() {
    Serial.begin(115200); delay(100);
    identity.begin();
    if (!identity.ready()) {
        printProvisioningHelp();
        return;
    }

#if SENSOR_DEEP_SLEEP
    // Timer wakes skip this window. A power-on/reset gives a technician time
    // to inspect or remove identity through the physical serial connection.
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) {
        Serial.println(F("[SN] Physical-serial maintenance window: 10 seconds"));
        const uint32_t windowStart = millis();
        while (millis() - windowStart < SENSOR_PROVISION_WINDOW_MS) {
            if (serviceIdentityConsole()) return;
            delay(10);
        }
    }
#endif

    Serial.printf("[SN] Sensor Node %04X starting\n", identity.nodeId());

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    int st = radio.begin(LORA_FREQ, 125.0f, 9, 5,
                          RADIOLIB_SX127X_SYNC_WORD, 10, 12);
    if (st != RADIOLIB_ERR_NONE) {
        Serial.printf("[SN] LoRa error: %d\n", st);
    } else {
        radioReady = true;
        Serial.println(F("[SN] LoRa ready"));
    }

    ds18b20.begin();
    ds18b20.requestTemperatures();
    tempRequested = true;
    sendTelemetry();
    lastTxMs = millis();
    enterDeepSleep();
}

void loop() {
    if (serviceIdentityConsole()) return;
    if (!identity.ready()) { delay(20); return; }
#if !SENSOR_DEEP_SLEEP
    uint32_t now = millis();
    if (now - lastTxMs >= TX_INTERVAL_MS) {
        lastTxMs = now;
        sendTelemetry();
    }
    delay(100);
#else
    // esp_deep_sleep_start() should not return. Remain fail-quiet if it does.
    delay(1000);
#endif
}
