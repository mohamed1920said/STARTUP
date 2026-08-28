#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <LoraProtocol.h>
#include <Preferences.h>

const uint16_t NODE_ID      = 0x0002;
const uint8_t  NODE_PSK[16] = {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00,0x11,
                               0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99};

const float    LORA_FREQ = 868.0f;
Module loraMod(LORA_CS, LORA_IRQ, LORA_RST, RADIOLIB_NC);
SX1276 radio(&loraMod);

// GPIO23 is the onboard SX1276 reset on TTGO LoRa32 v2.1 and must stay reserved.
const uint8_t AIN1 = 2, AIN2 = 4, PWMA = 12, STBY = 17;
const uint8_t VALVE_FB_PIN = 3;
const uint8_t VALVE_FB_OPEN_LEVEL = LOW;
const uint32_t VALVE_PULSE_MS = 30;
bool valveOpen = false;
bool valveCommandedOpen = false;
bool valveFeedbackValid = false;
uint8_t actuatorErrorFlags = 0;
uint32_t valveCloseDeadlineMs = 0;
uint32_t lastSafetyCloseAttemptMs = 0;
const uint16_t DEFAULT_MAX_OPEN_S = 1800;
const uint16_t ABSOLUTE_MAX_OPEN_S = 3600;
const uint32_t HEARTBEAT_INTERVAL = 30000;
uint32_t lastHbMs = 0;
uint32_t uplinkSeq = 0;
uint32_t uplinkSeqLimit = 0;

enum ActuatorError : uint8_t {
    ACT_ERR_FEEDBACK_MISSING = 0x01,
    ACT_ERR_FEEDBACK_MISMATCH = 0x02,
    ACT_ERR_SAFETY_TIMEOUT = 0x04,
    ACT_ERR_HYDRAULICS_DISABLED = 0x08
};

#ifndef ENABLE_HYDRAULIC_SENSORS
#define ENABLE_HYDRAULIC_SENSORS 0
#endif

const uint8_t FLOW_PIN = 13;
// Use input-only ADC pins that do not overlap the LoRa DIO1/DIO2 lines.
const uint8_t PRESSURE_PIN = 34;
const uint8_t TANK_PIN = 36;
const uint8_t PUMP_CURRENT_PIN = 37;
const float FLOW_PULSES_PER_LITER = 450.0f;
const float PRESSURE_FULL_SCALE_BAR = 10.0f;
const float PUMP_CURRENT_FULL_SCALE_A = 20.0f;
volatile uint32_t flowPulses = 0;
portMUX_TYPE flowMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t lastHydraulicMs = 0;

void IRAM_ATTR onFlowPulse() {
    portENTER_CRITICAL_ISR(&flowMux);
    flowPulses++;
    portEXIT_CRITICAL_ISR(&flowMux);
}

struct HydraulicData {
    uint16_t flowCentiLpm;
    uint16_t pressureKpa;
    uint8_t tankPct;
    uint16_t pumpCurrentMa;
};

HydraulicData readHydraulics() {
    HydraulicData data{UINT16_MAX, UINT16_MAX, UINT8_MAX, UINT16_MAX};
#if ENABLE_HYDRAULIC_SENSORS
    const uint32_t now = millis();
    uint32_t pulses;
    portENTER_CRITICAL(&flowMux);
    pulses = flowPulses;
    flowPulses = 0;
    portEXIT_CRITICAL(&flowMux);
    const float minutes = max(0.001f, (now - lastHydraulicMs) / 60000.0f);
    lastHydraulicMs = now;
    const float flowLpm = (pulses / FLOW_PULSES_PER_LITER) / minutes;
    const float pressureBar = analogRead(PRESSURE_PIN) * PRESSURE_FULL_SCALE_BAR / 4095.0f;
    const float tank = analogRead(TANK_PIN) * 100.0f / 4095.0f;
    const float pumpA = analogRead(PUMP_CURRENT_PIN) * PUMP_CURRENT_FULL_SCALE_A / 4095.0f;
    data.flowCentiLpm = static_cast<uint16_t>(constrain(flowLpm * 100.0f, 0.0f, 65534.0f));
    data.pressureKpa = static_cast<uint16_t>(constrain(pressureBar * 100.0f, 0.0f, 65534.0f));
    data.tankPct = static_cast<uint8_t>(constrain(tank, 0.0f, 100.0f));
    data.pumpCurrentMa = static_cast<uint16_t>(constrain(pumpA * 1000.0f, 0.0f, 65534.0f));
#else
    actuatorErrorFlags |= ACT_ERR_HYDRAULICS_DISABLED;
#endif
    return data;
}

bool readValveFeedback(bool& open) {
    if (VALVE_FB_PIN == 0xFF) return false;
    open = digitalRead(VALVE_FB_PIN) == VALVE_FB_OPEN_LEVEL;
    return true;
}

bool valveSet(bool open, uint16_t timeoutS = DEFAULT_MAX_OPEN_S) {
    valveCommandedOpen = open;
    digitalWrite(STBY, HIGH);
    digitalWrite(AIN1, open ? HIGH : LOW);
    digitalWrite(AIN2, open ? LOW : HIGH);
    digitalWrite(PWMA, HIGH);
    delay(VALVE_PULSE_MS);
    digitalWrite(PWMA, LOW);
    digitalWrite(AIN1, LOW);
    digitalWrite(AIN2, LOW);
    digitalWrite(STBY, LOW);
    delay(100);

    bool actual = false;
    valveFeedbackValid = readValveFeedback(actual);
    if (valveFeedbackValid) {
        valveOpen = actual;
        actuatorErrorFlags &= ~ACT_ERR_FEEDBACK_MISSING;
        if (actual != open) {
            actuatorErrorFlags |= ACT_ERR_FEEDBACK_MISMATCH;
            // A failed close with an actually open valve must keep retrying.
            if (!open && actual) {
                valveCloseDeadlineMs = millis() + 5000UL;
            }
            return false;
        }
        actuatorErrorFlags &= ~ACT_ERR_FEEDBACK_MISMATCH;
    } else {
        valveOpen = open;
        actuatorErrorFlags |= ACT_ERR_FEEDBACK_MISSING;
    }

    if (open) {
        const uint16_t safeTimeout = constrain(timeoutS ? timeoutS : DEFAULT_MAX_OPEN_S,
                                               static_cast<uint16_t>(1), ABSOLUTE_MAX_OPEN_S);
        valveCloseDeadlineMs = millis() + static_cast<uint32_t>(safeTimeout) * 1000UL;
    } else {
        valveCloseDeadlineMs = 0;
    }
    return true;
}

uint32_t lastSeq = 0;

uint32_t nextPersistentSequence() {
    if (uplinkSeq >= uplinkSeqLimit) {
        Preferences prefs;
        if (!prefs.begin("lora_seq", false)) return 0;
        uint32_t start = prefs.getULong("next_tx", 1);
        if (start == 0 || start > UINT32_MAX - 1024) { prefs.end(); return 0; }
        uplinkSeq = start - 1;
        uplinkSeqLimit = start + 1023;
        prefs.putULong("next_tx", uplinkSeqLimit + 1);
        prefs.end();
    }
    return ++uplinkSeq;
}

void loadReplayState() {
    Preferences prefs;
    if (!prefs.begin("lora_seq", true)) return;
    lastSeq = prefs.getULong("last_rx", 0);
    prefs.end();
}

void saveReplayState(uint32_t sequence) {
    Preferences prefs;
    if (!prefs.begin("lora_seq", false)) return;
    prefs.putULong("last_rx", sequence);
    prefs.end();
}

const uint8_t BATT_PIN = 35;
float readBattery() {
    uint32_t sum = 0;
    for (int i = 0; i < 32; i++) { sum += analogRead(BATT_PIN); delayMicroseconds(50); }
    return (sum / 32.0f / 4095.0f) * 3.3f * 2.0f;
}

void sendAck(uint32_t ackSeq, uint8_t result);
void sendHeartbeat();

void processPacket() {
    uint8_t buf[LORA_MAX_PAYLOAD];
    size_t len = radio.getPacketLength();
    if (len < LORA_HEADER_SIZE + MIC_SIZE || len > sizeof(buf)) return;
    int st = radio.readData(buf, len);
    if (st != RADIOLIB_ERR_NONE) return;

    uint16_t target = (buf[0] << 8) | buf[1];
    Serial.printf("[AN] Rx %u bytes target=%04X RSSI=%.1f\n", len, target, radio.getRSSI());
    if (target != NODE_ID) return;

    uint8_t pktType = buf[NODE_ID_SIZE + IV_NONCE_SIZE];
    uint32_t seq = 0;
    for (int i = 0; i < 8; i++) seq = (seq << 8) | buf[NODE_ID_SIZE + 4 + i];
    size_t ctLen = len - LORA_HEADER_SIZE - MIC_SIZE;

    Serial.printf("[AN] pktType=0x%02X seq=%u ctLen=%u\n", pktType, seq, (unsigned)ctLen);

    if (ctLen < COMMAND_WIRE_SIZE) {
        Serial.printf("[AN] ctLen %u < COMMAND_WIRE_SIZE %u\n", (unsigned)ctLen, (unsigned)COMMAND_WIRE_SIZE);
        return;
    }

    const uint8_t* iv = buf + NODE_ID_SIZE;
    const uint8_t* ct = buf + LORA_HEADER_SIZE;
    const uint8_t* mic = ct + ctLen;

    uint8_t pt[LORA_MAX_CIPHERTEXT];
    memcpy(pt, ct, ctLen);

    LoraCrypto crypto;
    crypto.setKey(NODE_PSK, 16);
    CryptoResult cr = crypto.decrypt(pt, ctLen, pktType, seq, NODE_ID, iv, mic);
    if (cr != CryptoResult::OK) {
        Serial.printf("[AN] DECRYPT_FAIL err=%d seq=%u pktType=0x%02X ctLen=%u\n",
                      (int)cr, seq, pktType, (unsigned)ctLen);
        return;
    }

    ActuatorCommand cmd;
    deserializeCommand(pt, cmd);
    if (cmd.sequence != seq) { Serial.println(F("[AN] Command sequence mismatch")); return; }
    if (seq <= lastSeq) { Serial.printf("[AN] Replay %u (last=%u)\n", seq, lastSeq); return; }
    lastSeq = seq;
    saveReplayState(seq);
    Serial.printf("[AN] cmd: seq=%u cmd=0x%02X val=%u timeout=%u\n",
                  cmd.sequence, cmd.command, cmd.value, cmd.timeout_s);

    if (cmd.command == 0x01) {
        const bool ok = valveSet(cmd.value != 0, cmd.timeout_s);
        Serial.printf("[AN] Valve -> %s\n", valveOpen ? "OPEN" : "CLOSED");
        sendAck(cmd.sequence, ok ? 0x00 : 0x01);
    }
}

void setup() {
    Serial.begin(115200); delay(100);
    Serial.printf("[AN] Actuator Node %04X starting\n", NODE_ID);
    loadReplayState();

    pinMode(AIN1, OUTPUT); pinMode(AIN2, OUTPUT);
    pinMode(PWMA, OUTPUT); pinMode(STBY, OUTPUT);
    digitalWrite(AIN1, LOW); digitalWrite(AIN2, LOW);
    digitalWrite(PWMA, LOW); digitalWrite(STBY, LOW);
    if (VALVE_FB_PIN != 0xFF) pinMode(VALVE_FB_PIN, INPUT_PULLUP);
#if ENABLE_HYDRAULIC_SENSORS
    pinMode(FLOW_PIN, INPUT_PULLUP);
    pinMode(PRESSURE_PIN, INPUT);
    pinMode(TANK_PIN, INPUT);
    pinMode(PUMP_CURRENT_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(FLOW_PIN), onFlowPulse, FALLING);
#endif
    analogReadResolution(12);
    lastHydraulicMs = millis();

    bool bootValveState = false;
    valveFeedbackValid = readValveFeedback(bootValveState);
    valveOpen = valveCommandedOpen = valveFeedbackValid ? bootValveState : false;
    if (!valveFeedbackValid) actuatorErrorFlags |= ACT_ERR_FEEDBACK_MISSING;
    if (valveOpen) {
        actuatorErrorFlags |= ACT_ERR_SAFETY_TIMEOUT;
        valveSet(false);
    }

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    int st = radio.begin(LORA_FREQ, 125.0f, 9, 5,
                          RADIOLIB_SX127X_SYNC_WORD, 10, 12);
    if (st != RADIOLIB_ERR_NONE) {
        Serial.printf("[AN] LoRa error: %d\n", st);
    } else {
        Serial.println(F("[AN] LoRa ready"));
        radio.startReceive();
        sendHeartbeat();
        lastHbMs = millis();
    }
}

void loop() {
    uint16_t irqFlags = radio.getIRQFlags();
    if ((irqFlags & RADIOLIB_SX127X_CLEAR_IRQ_FLAG_RX_DONE) && radio.getPacketLength() > 0) {
        processPacket();
        radio.startReceive();
    }
    uint32_t now = millis();
    if (now - lastHbMs >= HEARTBEAT_INTERVAL) {
        lastHbMs = now;
        sendHeartbeat();
        radio.startReceive();
    }
    if ((valveCommandedOpen || valveOpen) && valveCloseDeadlineMs != 0 &&
        static_cast<int32_t>(now - valveCloseDeadlineMs) >= 0 &&
        now - lastSafetyCloseAttemptMs >= 5000UL) {
        lastSafetyCloseAttemptMs = now;
        actuatorErrorFlags |= ACT_ERR_SAFETY_TIMEOUT;
        const bool closed = valveSet(false);
        Serial.println(closed ? F("[AN] Safety timeout: valve forced CLOSED")
                              : F("[AN] Safety close failed; retry scheduled"));
        sendHeartbeat();
        lastHbMs = now;
        radio.startReceive();
    }
    delay(10);
}

void sendHeartbeat() {
    uint32_t txSeq = nextPersistentSequence();
    if (txSeq == 0) { Serial.println(F("[AN] Sequence space exhausted")); return; }
    HeartbeatPayload hb;
    hb.sequence    = txSeq;
    hb.battery_mv  = (uint16_t)(readBattery() * 1000);
    hb.valve_state = valveOpen ? 1 : 0;
    hb.valve_commanded = valveCommandedOpen ? 1 : 0;
    hb.feedback_valid = valveFeedbackValid ? 1 : 0;
    hb.error_flags = actuatorErrorFlags;
    const HydraulicData hydraulic = readHydraulics();
    hb.flow_centi_lpm = hydraulic.flowCentiLpm;
    hb.pressure_kpa = hydraulic.pressureKpa;
    hb.tank_pct = hydraulic.tankPct;
    hb.pump_current_ma = hydraulic.pumpCurrentMa;

    uint8_t pt[HEARTBEAT_WIRE_SIZE];
    serializeHeartbeat(pt, hb);

    LoraCrypto crypto;
    crypto.setKey(NODE_PSK, 16);

    uint8_t iv[GCM_IV_SIZE], mic[MIC_SIZE];
    CryptoResult cr = crypto.encrypt(pt, HEARTBEAT_WIRE_SIZE,
                                      (uint8_t)PacketType::HEARTBEAT,
                                      txSeq, NODE_ID, iv, mic);
    if (cr != CryptoResult::OK) { Serial.printf("[AN] HB encrypt err=%d\n", (int)cr); return; }

    uint8_t tx[LORA_MAX_PAYLOAD]; size_t o = 0;
    uint8_t node_be[2] = { uint8_t(NODE_ID >> 8), uint8_t(NODE_ID & 0xFF) };
    memcpy(tx+o, node_be, 2); o+=2;
    memcpy(tx+o, iv, 12);     o+=12;
    tx[o++] = (uint8_t)PacketType::HEARTBEAT;
    memcpy(tx+o, pt, HEARTBEAT_WIRE_SIZE); o+=HEARTBEAT_WIRE_SIZE;
    memcpy(tx+o, mic, MIC_SIZE);           o+=MIC_SIZE;

    int st = radio.standby(); if (st != 0) return;
    radio.setFrequency(LORA_FREQ);
    st = radio.transmit(tx, o);
    Serial.printf("[AN] HB seq=%u -> %s\n", hb.sequence, st == RADIOLIB_ERR_NONE ? "OK" : "FAIL");
}

void sendAck(uint32_t ackSeq, uint8_t result) {
    uint32_t txSeq = nextPersistentSequence();
    if (txSeq == 0) { Serial.println(F("[AN] Sequence space exhausted")); return; }
    AckPayload ack;
    ack.ack_seq    = ackSeq;
    ack.result     = result;
    ack.battery_mv = (uint16_t)(readBattery() * 1000);
    ack.valve_state = valveOpen ? 1 : 0;
    ack.feedback_valid = valveFeedbackValid ? 1 : 0;
    ack.error_flags = actuatorErrorFlags;

    uint8_t pt[ACK_WIRE_SIZE];
    serializeAck(pt, ack);

    LoraCrypto crypto;
    crypto.setKey(NODE_PSK, 16);

    uint8_t iv[GCM_IV_SIZE], mic[MIC_SIZE];
    CryptoResult cr = crypto.encrypt(pt, ACK_WIRE_SIZE,
                                      (uint8_t)PacketType::ACK,
                                      txSeq, NODE_ID, iv, mic);
    if (cr != CryptoResult::OK) { Serial.printf("[AN] ACK encrypt err=%d\n", (int)cr); return; }

    uint8_t tx[LORA_MAX_PAYLOAD]; size_t o = 0;
    uint8_t node_be[2] = { uint8_t(NODE_ID >> 8), uint8_t(NODE_ID & 0xFF) };
    memcpy(tx+o, node_be, 2); o+=2;
    memcpy(tx+o, iv, 12);     o+=12;
    tx[o++] = (uint8_t)PacketType::ACK;
    memcpy(tx+o, pt, ACK_WIRE_SIZE); o+=ACK_WIRE_SIZE;
    memcpy(tx+o, mic, MIC_SIZE);     o+=MIC_SIZE;

    int st = radio.standby(); if (st != 0) return;
    radio.setFrequency(LORA_FREQ);
    st = radio.transmit(tx, o);
    Serial.printf("[AN] ACK seq=%u -> %s\n", ackSeq, st == RADIOLIB_ERR_NONE ? "OK" : "FAIL");
}
