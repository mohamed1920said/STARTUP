#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <LoraProtocol.h>
#include <Preferences.h>

const uint16_t NODE_ID      = 0x0002;
const uint8_t  NODE_PSK[16] = {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00,0x11,
                               0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99};

const float    LORA_FREQ = 868.0f;
const uint8_t  PIN_RST = 14;
Module loraMod(LORA_CS, LORA_IRQ, PIN_RST, RADIOLIB_NC);
SX1276 radio(&loraMod);

const uint8_t AIN1 = 2, AIN2 = 4, PWMA = 23, STBY = 17;
const uint8_t VALVE_FB_PIN = 3;
const uint32_t VALVE_PULSE_MS = 30;
bool valveOpen = false;
const uint32_t HEARTBEAT_INTERVAL = 30000;
uint32_t lastHbMs = 0;
uint32_t uplinkSeq = 0;
uint32_t uplinkSeqLimit = 0;

void valveSet(bool open) {
    digitalWrite(STBY, HIGH);
    digitalWrite(AIN1, open ? HIGH : LOW);
    digitalWrite(AIN2, open ? LOW : HIGH);
    digitalWrite(PWMA, HIGH);
    delay(VALVE_PULSE_MS);
    digitalWrite(PWMA, LOW);
    digitalWrite(AIN1, LOW);
    digitalWrite(AIN2, LOW);
    digitalWrite(STBY, LOW);
    valveOpen = open;
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

void sendAck(uint32_t ackSeq);
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
        valveSet(cmd.value != 0);
        Serial.printf("[AN] Valve -> %s\n", valveOpen ? "OPEN" : "CLOSED");
        sendAck(cmd.sequence);
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
    delay(10);
}

void sendHeartbeat() {
    uint32_t txSeq = nextPersistentSequence();
    if (txSeq == 0) { Serial.println(F("[AN] Sequence space exhausted")); return; }
    HeartbeatPayload hb;
    hb.sequence    = txSeq;
    hb.battery_mv  = (uint16_t)(readBattery() * 1000);
    hb.valve_state = valveOpen ? 1 : 0;

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

void sendAck(uint32_t ackSeq) {
    uint32_t txSeq = nextPersistentSequence();
    if (txSeq == 0) { Serial.println(F("[AN] Sequence space exhausted")); return; }
    AckPayload ack;
    ack.ack_seq    = ackSeq;
    ack.result     = 0x00;
    ack.battery_mv = (uint16_t)(readBattery() * 1000);

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
