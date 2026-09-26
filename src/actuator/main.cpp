#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <LoraProtocol.h>
#include <Preferences.h>
#include <DeviceIdentity.h>

#ifndef PILOT_MAX_OPEN_S
#define PILOT_MAX_OPEN_S 300
#endif
#ifndef PILOT_MIN_OPEN_BATTERY_MV
#define PILOT_MIN_OPEN_BATTERY_MV 3400
#endif
static_assert(PILOT_MAX_OPEN_S >= 1 && PILOT_MAX_OPEN_S <= 300,
              "Pilot OPEN time must be between 1 and 300 seconds");
static_assert(PILOT_MIN_OPEN_BATTERY_MV >= 3000 && PILOT_MIN_OPEN_BATTERY_MV <= 4200,
              "Pilot battery threshold must be between 3000 and 4200 mV");

const float    LORA_FREQ = 868.0f;
Module loraMod(LORA_CS, LORA_IRQ, LORA_RST, RADIOLIB_NC);
SX1276 radio(&loraMod);
DeviceIdentity identity(DeviceRole::ACTUATOR);
bool radioReady = false;
bool outputsReady = false;

// GPIO23 is the onboard SX1276 reset on TTGO LoRa32 v2.1 and must stay reserved.
const uint8_t AIN1 = 2, AIN2 = 4, PWMA = 12, STBY = 17;
static_assert(AIN1 != LORA_RST && AIN2 != LORA_RST && PWMA != LORA_RST && STBY != LORA_RST,
              "Valve driver pins must not share the onboard LoRa reset pin");
// No position switch is installed on the current 4.5 V latching solenoid.
// 0xFF disables feedback completely; GPIO3 remains available for serial RX.
#ifndef ACTUATOR_VALVE_FB_PIN
#define ACTUATOR_VALVE_FB_PIN 0xFF
#endif
const uint8_t VALVE_FB_PIN = ACTUATOR_VALVE_FB_PIN;
const uint8_t VALVE_FB_OPEN_LEVEL = LOW;
const uint32_t VALVE_PULSE_MS = 30;
bool valveOpen = false;  // Estimated/commanded state when feedback_valid is false.
bool valveCommandedOpen = false;
bool valveFeedbackValid = false;
uint8_t actuatorErrorFlags = 0;
uint32_t valveCloseDeadlineMs = 0;
bool valveCloseTimerArmed = false;  // Separate flag: a valid deadline can wrap to zero.
uint32_t lastSafetyCloseAttemptMs = 0;
const uint16_t DEFAULT_MAX_OPEN_S = PILOT_MAX_OPEN_S;
const uint16_t ABSOLUTE_MAX_OPEN_S = PILOT_MAX_OPEN_S;
const uint32_t SAFETY_CLOSE_RETRY_MS = 5000;
const uint32_t HEARTBEAT_INTERVAL = 30000;
uint32_t lastHbMs = 0;
uint32_t uplinkSeq = 0;
uint32_t uplinkSeqLimit = 0;

enum ActuatorError : uint8_t {
    ACT_ERR_FEEDBACK_MISSING = 0x01,
    ACT_ERR_FEEDBACK_MISMATCH = 0x02,
    ACT_ERR_SAFETY_TIMEOUT = 0x04,
    ACT_ERR_HYDRAULICS_DISABLED = 0x08,
    ACT_ERR_STORAGE = 0x10
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
    if (open) {
        // Arm before the first OPEN pulse. Repeated OPEN commands must not
        // extend the original deadline indefinitely.
        if (!valveCloseTimerArmed) {
            const uint16_t safeTimeout = constrain(timeoutS ? timeoutS : DEFAULT_MAX_OPEN_S,
                                                   static_cast<uint16_t>(1), ABSOLUTE_MAX_OPEN_S);
            valveCloseDeadlineMs = millis() + static_cast<uint32_t>(safeTimeout) * 1000UL;
            valveCloseTimerArmed = true;
            lastSafetyCloseAttemptMs = millis() - SAFETY_CLOSE_RETRY_MS;
        }
    }
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
            if (open) {
                // A failed OPEN verification is ambiguous: a broken feedback
                // wire can report CLOSED even though the valve moved. Send an
                // immediate CLOSE pulse and only stop retrying when CLOSE is
                // verified.
                const bool closeVerified = valveSet(false, 0);
                actuatorErrorFlags |= ACT_ERR_FEEDBACK_MISMATCH;
                // valveSet(false) already disarms a verified close or schedules
                // the next retry. Preserve that schedule instead of turning a
                // feedback fault into a continuous pulse loop.
                (void)closeVerified;
                return false;
            }
            // A failed close with an actually open valve must keep retrying.
            if (!open && actual) {
                valveCloseDeadlineMs = millis() + SAFETY_CLOSE_RETRY_MS;
                valveCloseTimerArmed = true;
            }
            return false;
        }
        actuatorErrorFlags &= ~ACT_ERR_FEEDBACK_MISMATCH;
    } else {
        // Open-loop operation: this is the requested state, not proof of movement.
        valveOpen = open;
        actuatorErrorFlags |= ACT_ERR_FEEDBACK_MISSING;
        actuatorErrorFlags &= ~ACT_ERR_FEEDBACK_MISMATCH;
    }

    if (!open) {
        valveCloseTimerArmed = false;
        valveCloseDeadlineMs = 0;
    }
    return true;
}

void refreshValveFeedback() {
    bool measuredOpen = false;
    valveFeedbackValid = readValveFeedback(measuredOpen);
    if (!valveFeedbackValid) {
        actuatorErrorFlags |= ACT_ERR_FEEDBACK_MISSING;
        return;
    }

    valveOpen = measuredOpen;
    actuatorErrorFlags &= ~ACT_ERR_FEEDBACK_MISSING;
    if (measuredOpen == valveCommandedOpen) {
        actuatorErrorFlags &= ~ACT_ERR_FEEDBACK_MISMATCH;
        return;
    }

    actuatorErrorFlags |= ACT_ERR_FEEDBACK_MISMATCH;
    if (measuredOpen && !valveCommandedOpen && !valveCloseTimerArmed) {
        // A heartbeat discovered an open valve that should be closed. Arm an
        // immediate close; the main loop performs and retries the pulse.
        valveCloseTimerArmed = true;
        valveCloseDeadlineMs = millis();
        lastSafetyCloseAttemptMs = millis() - SAFETY_CLOSE_RETRY_MS;
    }
}

bool valveSafetyCloseDue(uint32_t now) {
    // Do not gate this on valveOpen: false feedback may hide an open valve.
    return valveCloseTimerArmed && static_cast<int32_t>(now - valveCloseDeadlineMs) >= 0 &&
           now - lastSafetyCloseAttemptMs >= SAFETY_CLOSE_RETRY_MS;
}

uint32_t lastSeq = 0;

uint32_t nextPersistentSequence() {
    if (uplinkSeq >= uplinkSeqLimit) {
        Preferences prefs;
        if (!prefs.begin("lora_seq", false)) return 0;
        const uint32_t start = prefs.getULong("next_tx", 1);
        if (start == 0 || start > UINT32_MAX - 1024) { prefs.end(); return 0; }
        const uint32_t reservedLimit = start + 1023;
        const uint32_t nextBlock = reservedLimit + 1;
        const bool reserved = prefs.putULong("next_tx", nextBlock) == sizeof(uint32_t) &&
            prefs.getULong("next_tx", 0) == nextBlock;
        prefs.end();
        if (!reserved) return 0;
        uplinkSeq = start - 1;
        uplinkSeqLimit = reservedLimit;
    }
    return ++uplinkSeq;
}

bool loadReplayState() {
    Preferences prefs;
    if (!prefs.begin("lora_seq", false)) return false;
    lastSeq = prefs.getULong("last_rx", 0);
    prefs.end();
    return true;
}

bool saveReplayState(uint32_t sequence) {
    Preferences prefs;
    if (!prefs.begin("lora_seq", false)) return false;
    const bool saved = prefs.putULong("last_rx", sequence) == sizeof(uint32_t) &&
        prefs.getULong("last_rx", 0) == sequence;
    prefs.end();
    return saved;
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
    if (!identity.ready() || !radioReady) return;
    uint8_t buf[LORA_MAX_PAYLOAD];
    size_t len = radio.getPacketLength();
    if (len < LORA_HEADER_SIZE + MIC_SIZE || len > sizeof(buf)) return;
    int st = radio.readData(buf, len);
    if (st != RADIOLIB_ERR_NONE) return;

    uint16_t target = (buf[0] << 8) | buf[1];
    Serial.printf("[AN] Rx %u bytes target=%04X RSSI=%.1f\n", len, target, radio.getRSSI());
    if (target != identity.nodeId()) return;

    uint8_t pktType = buf[NODE_ID_SIZE + IV_NONCE_SIZE];
    uint32_t seq = 0;
    for (int i = 0; i < 8; i++) seq = (seq << 8) | buf[NODE_ID_SIZE + 4 + i];
    size_t ctLen = len - LORA_HEADER_SIZE - MIC_SIZE;

    Serial.printf("[AN] pktType=0x%02X seq=%u ctLen=%u\n", pktType, seq, (unsigned)ctLen);

    if (pktType != static_cast<uint8_t>(PacketType::ACTUATOR_COMMAND) || ctLen != COMMAND_WIRE_SIZE) {
        Serial.printf("[AN] Invalid command frame type=0x%02X len=%u\n", pktType, (unsigned)ctLen);
        return;
    }

    const uint8_t* iv = buf + NODE_ID_SIZE;
    const uint8_t* ct = buf + LORA_HEADER_SIZE;
    const uint8_t* mic = ct + ctLen;

    uint8_t pt[LORA_MAX_CIPHERTEXT];
    memcpy(pt, ct, ctLen);

    LoraCrypto crypto;
    crypto.setKey(identity.psk(), AES128_KEY_SIZE);
    CryptoResult cr = crypto.decrypt(pt, ctLen, pktType, seq, identity.nodeId(), iv, mic);
    if (cr != CryptoResult::OK) {
        Serial.printf("[AN] DECRYPT_FAIL err=%d seq=%u pktType=0x%02X ctLen=%u\n",
                      (int)cr, seq, pktType, (unsigned)ctLen);
        return;
    }

    ActuatorCommand cmd;
    deserializeCommand(pt, cmd);
    if (cmd.sequence != seq) { Serial.println(F("[AN] Command sequence mismatch")); return; }
    if (seq <= lastSeq) { Serial.printf("[AN] Replay %u (last=%u)\n", seq, lastSeq); return; }
    const bool validCommand = cmd.command == 0x01 && cmd.value <= 1 &&
        ((cmd.value == 0 && cmd.timeout_s == 0) ||
         (cmd.value == 1 && cmd.timeout_s >= 1 && cmd.timeout_s <= ABSOLUTE_MAX_OPEN_S));
    if (!validCommand) {
        Serial.printf("[AN] Invalid authenticated command cmd=0x%02X value=%u timeout=%u\n",
                      cmd.command, cmd.value, cmd.timeout_s);
        sendAck(cmd.sequence, ACTUATOR_RESULT_INVALID_COMMAND);
        return;
    }
    if (!saveReplayState(seq)) {
        actuatorErrorFlags |= ACT_ERR_STORAGE;
        Serial.println(F("[AN] Command rejected: replay state could not be persisted"));
        sendAck(cmd.sequence, ACTUATOR_RESULT_STORAGE_ERROR);
        return;
    }
    lastSeq = seq;
    actuatorErrorFlags &= ~ACT_ERR_STORAGE;
    Serial.printf("[AN] cmd: seq=%u cmd=0x%02X val=%u timeout=%u\n",
                  cmd.sequence, cmd.command, cmd.value, cmd.timeout_s);

    if (cmd.command == 0x01) {
        const uint16_t batteryMv = static_cast<uint16_t>(readBattery() * 1000.0f);
        if (cmd.value != 0 && (batteryMv < PILOT_MIN_OPEN_BATTERY_MV || batteryMv > 5000U)) {
            Serial.printf("[AN] OPEN rejected: battery reading %u mV is outside pilot limits\n", batteryMv);
            sendAck(cmd.sequence, ACTUATOR_RESULT_LOW_BATTERY);
            return;
        }
        const bool ok = valveSet(cmd.value != 0, cmd.timeout_s);
        Serial.printf("[AN] Valve command=%s state=%s feedback=%s result=%s\n",
                      valveCommandedOpen ? "OPEN" : "CLOSED", valveOpen ? "OPEN" : "CLOSED",
                      valveFeedbackValid ? "AVAILABLE" : "UNAVAILABLE",
                      !ok ? "FEEDBACK_MISMATCH" : (valveFeedbackValid ? "VERIFIED" : "PULSE_SENT_UNVERIFIED"));
        const uint8_t result = !ok ? ACTUATOR_RESULT_FEEDBACK_MISMATCH
            : (valveFeedbackValid ? ACTUATOR_RESULT_VERIFIED : ACTUATOR_RESULT_UNVERIFIED);
        sendAck(cmd.sequence, result);
    }
}

bool serviceIdentityConsole() {
    const IdentityEvent event = identity.serviceConsole(Serial);
    if (event == IdentityEvent::NONE) return false;
    if (outputsReady) valveSet(false);
    if (radioReady) radio.sleep();
    Serial.println(F("[AN] Restarting after identity change"));
    Serial.flush();
    delay(100);
    ESP.restart();
    return true;
}

void printProvisioningHelp() {
    identity.printStatus(Serial);
    if (identity.state() == IdentityState::EMPTY) {
        Serial.println(F("[AN] Radio disabled. Use: PROVISION 0002 <32-hex-PSK>"));
    } else if (identity.state() == IdentityState::CORRUPT ||
               identity.state() == IdentityState::ROLE_MISMATCH) {
        Serial.println(F("[AN] Radio disabled. Use: ERASE CORRUPT CONFIRM"));
    } else {
        Serial.println(F("[AN] Identity storage unavailable; radio remains disabled"));
    }
}

void setup() {
    Serial.begin(115200); delay(100);
    Serial.printf("[AN] Pins: LoRaRST=%u AIN1=%u AIN2=%u PWMA=%u STBY=%u pulse=%ums\n",
                  LORA_RST, AIN1, AIN2, PWMA, STBY, VALVE_PULSE_MS);

    pinMode(AIN1, OUTPUT); pinMode(AIN2, OUTPUT);
    pinMode(PWMA, OUTPUT); pinMode(STBY, OUTPUT);
    digitalWrite(AIN1, LOW); digitalWrite(AIN2, LOW);
    digitalWrite(PWMA, LOW); digitalWrite(STBY, LOW);
    outputsReady = true;
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
    if (!valveFeedbackValid) {
        actuatorErrorFlags |= ACT_ERR_FEEDBACK_MISSING;
        // A latching valve can remain open across MCU resets. Establish a
        // commanded closed state without pretending a position was measured.
        Serial.println(F("[AN] No position feedback: boot CLOSE pulse; position UNVERIFIED"));
        valveSet(false);
    } else if (valveOpen) {
        actuatorErrorFlags |= ACT_ERR_SAFETY_TIMEOUT;
        valveSet(false);
    }

    identity.begin();
    if (!identity.ready()) {
        printProvisioningHelp();
        return;
    }
    Serial.printf("[AN] Actuator Node %04X starting\n", identity.nodeId());
    if (!loadReplayState()) {
        actuatorErrorFlags |= ACT_ERR_STORAGE;
        Serial.println(F("[AN] Replay storage unavailable; radio remains disabled"));
        return;
    }

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    int st = radio.begin(LORA_FREQ, 125.0f, 9, 5,
                          RADIOLIB_SX127X_SYNC_WORD, 10, 12);
    if (st != RADIOLIB_ERR_NONE) {
        Serial.printf("[AN] LoRa error: %d\n", st);
    } else {
        radioReady = true;
        Serial.println(F("[AN] LoRa ready"));
        radio.startReceive();
        sendHeartbeat();
        lastHbMs = millis();
    }
}

void loop() {
    if (serviceIdentityConsole()) return;
    uint32_t now = millis();
    if (valveSafetyCloseDue(now)) {
        lastSafetyCloseAttemptMs = now;
        actuatorErrorFlags |= ACT_ERR_SAFETY_TIMEOUT;
        const bool closed = valveSet(false);
        if (!closed) {
            Serial.println(F("[AN] Safety close feedback mismatch; retry scheduled"));
        } else {
            Serial.println(valveFeedbackValid ? F("[AN] Safety timeout: CLOSED verified")
                                             : F("[AN] Safety timeout: CLOSE pulse sent; position UNVERIFIED"));
        }
        if (identity.ready() && radioReady) {
            sendHeartbeat();
            lastHbMs = now;
            radio.startReceive();
        }
    }
    if (!identity.ready() || !radioReady) { delay(20); return; }
    uint16_t irqFlags = radio.getIRQFlags();
    if ((irqFlags & RADIOLIB_SX127X_CLEAR_IRQ_FLAG_RX_DONE) && radio.getPacketLength() > 0) {
        processPacket();
        radio.startReceive();
    }
    now = millis();
    if (now - lastHbMs >= HEARTBEAT_INTERVAL) {
        lastHbMs = now;
        sendHeartbeat();
        radio.startReceive();
    }
    delay(10);
}

void sendHeartbeat() {
    if (!identity.ready() || !radioReady) return;
    refreshValveFeedback();
    uint32_t txSeq = nextPersistentSequence();
    if (txSeq == 0) { Serial.println(F("[AN] Sequence space exhausted")); return; }
    HeartbeatPayload hb{};
    hb.sequence    = txSeq;
    hb.battery_mv  = (uint16_t)(readBattery() * 1000);
    hb.valve_state = valveOpen ? 1 : 0;
    hb.valve_commanded = valveCommandedOpen ? 1 : 0;
    hb.feedback_valid = valveFeedbackValid ? 1 : 0;
    const HydraulicData hydraulic = readHydraulics();
    hb.error_flags = actuatorErrorFlags;
    hb.flow_centi_lpm = hydraulic.flowCentiLpm;
    hb.pressure_kpa = hydraulic.pressureKpa;
    hb.tank_pct = hydraulic.tankPct;
    hb.pump_current_ma = hydraulic.pumpCurrentMa;

    uint8_t pt[HEARTBEAT_WIRE_SIZE];
    serializeHeartbeat(pt, hb);

    LoraCrypto crypto;
    crypto.setKey(identity.psk(), AES128_KEY_SIZE);

    uint8_t iv[GCM_IV_SIZE], mic[MIC_SIZE];
    CryptoResult cr = crypto.encrypt(pt, HEARTBEAT_WIRE_SIZE,
                                      (uint8_t)PacketType::HEARTBEAT,
                                      txSeq, identity.nodeId(), iv, mic);
    if (cr != CryptoResult::OK) { Serial.printf("[AN] HB encrypt err=%d\n", (int)cr); return; }

    uint8_t tx[LORA_MAX_PAYLOAD]; size_t o = 0;
    const uint16_t nodeId = identity.nodeId();
    uint8_t node_be[2] = { uint8_t(nodeId >> 8), uint8_t(nodeId & 0xFF) };
    memcpy(tx+o, node_be, 2); o+=2;
    memcpy(tx+o, iv, 12);     o+=12;
    tx[o++] = (uint8_t)PacketType::HEARTBEAT;
    memcpy(tx+o, pt, HEARTBEAT_WIRE_SIZE); o+=HEARTBEAT_WIRE_SIZE;
    memcpy(tx+o, mic, MIC_SIZE);           o+=MIC_SIZE;

    int st = radio.standby(); if (st != 0) return;
    radio.setFrequency(LORA_FREQ);
    st = radio.transmit(tx, o);
    Serial.printf("[AN] HB seq=%u commanded=%s feedback=%s flags=0x%02X tx=%s\n",
                  hb.sequence, valveCommandedOpen ? "OPEN" : "CLOSED",
                  valveFeedbackValid ? "AVAILABLE" : "UNAVAILABLE", hb.error_flags,
                  st == RADIOLIB_ERR_NONE ? "OK" : "FAIL");
}

void sendAck(uint32_t ackSeq, uint8_t result) {
    if (!identity.ready() || !radioReady) return;
    uint32_t txSeq = nextPersistentSequence();
    if (txSeq == 0) { Serial.println(F("[AN] Sequence space exhausted")); return; }
    AckPayload ack{};
    ack.ack_seq    = ackSeq;
    ack.result     = result;
    ack.battery_mv = (uint16_t)(readBattery() * 1000);
    ack.valve_state = valveOpen ? 1 : 0;
    ack.feedback_valid = valveFeedbackValid ? 1 : 0;
    ack.error_flags = actuatorErrorFlags;

    uint8_t pt[ACK_WIRE_SIZE];
    serializeAck(pt, ack);

    LoraCrypto crypto;
    crypto.setKey(identity.psk(), AES128_KEY_SIZE);

    uint8_t iv[GCM_IV_SIZE], mic[MIC_SIZE];
    CryptoResult cr = crypto.encrypt(pt, ACK_WIRE_SIZE,
                                      (uint8_t)PacketType::ACK,
                                      txSeq, identity.nodeId(), iv, mic);
    if (cr != CryptoResult::OK) { Serial.printf("[AN] ACK encrypt err=%d\n", (int)cr); return; }

    uint8_t tx[LORA_MAX_PAYLOAD]; size_t o = 0;
    const uint16_t nodeId = identity.nodeId();
    uint8_t node_be[2] = { uint8_t(nodeId >> 8), uint8_t(nodeId & 0xFF) };
    memcpy(tx+o, node_be, 2); o+=2;
    memcpy(tx+o, iv, 12);     o+=12;
    tx[o++] = (uint8_t)PacketType::ACK;
    memcpy(tx+o, pt, ACK_WIRE_SIZE); o+=ACK_WIRE_SIZE;
    memcpy(tx+o, mic, MIC_SIZE);     o+=MIC_SIZE;

    int st = radio.standby(); if (st != 0) return;
    radio.setFrequency(LORA_FREQ);
    st = radio.transmit(tx, o);
    Serial.printf("[AN] ACK seq=%u result=0x%02X feedback=%s tx=%s\n",
                  ackSeq, result, valveFeedbackValid ? "AVAILABLE" : "UNAVAILABLE",
                  st == RADIOLIB_ERR_NONE ? "OK" : "FAIL");
}
