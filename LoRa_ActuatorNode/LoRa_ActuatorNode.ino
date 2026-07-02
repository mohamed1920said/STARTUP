/*
 * Actuator Node — ESP32-S3 + LoRa + Solenoid Valve
 * Board: ESP32-S3 Dev Module
 * Dependencies: RadioLib, LoraNetwork (local library)
 *
 * Lifecycle: Light Sleep → CAD sniff (1.5 mA / 2 ms) →
 *   (if preamble) → Rx window (12 mA / 3s) → Decrypt → Execute → ACK
 *
 * Average current: ~150 µA at 15s CAD interval
 */
#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <PacketTypes.h>
#include <CryptoEngine.h>

// ── Provisioned Identity ──────────────────────────────────────────────
const uint16_t NODE_ID      = 0x0002;
const uint8_t  NODE_PSK[16] = {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00,0x11,
                               0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99};

// ── LoRa ──────────────────────────────────────────────────────────────
const float    LORA_FREQ = 868.0f;
const uint8_t  LORA_NSS = 5, LORA_DIO1 = 9, LORA_RST = 10, LORA_BUSY = 6;
Module loraMod(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
SX1262 radio(&loraMod);

// ── Valve ─────────────────────────────────────────────────────────────
const uint8_t VALVE_CTL_PIN = 2;
const uint8_t VALVE_FB_PIN  = 3;  // optional limit switch
bool valveOpen = false;

// ── Timing ────────────────────────────────────────────────────────────
const uint32_t LISTEN_INTERVAL_MS = 15000;  // 15s between Rx windows
const uint32_t RX_TIMEOUT_MS      = 3000;   // 3s Rx window
uint32_t lastListenMs = 0;
uint32_t lastSeq = 0;

// ── Battery ───────────────────────────────────────────────────────────
const uint8_t BATT_PIN = 4;
float readBattery() {
    uint32_t sum = 0;
    for (int i = 0; i < 32; i++) { sum += analogRead(BATT_PIN); delayMicroseconds(50); }
    return (sum / 32.0f / 4095.0f) * 3.3f * 2.0f;
}

// ═══════════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200); delay(100);
    Serial.printf("[AN] Actuator Node %04X starting\n", NODE_ID);

    pinMode(VALVE_CTL_PIN, OUTPUT); digitalWrite(VALVE_CTL_PIN, LOW);
    if (VALVE_FB_PIN != 0xFF) pinMode(VALVE_FB_PIN, INPUT_PULLUP);

    SPI.begin(6, 8, 7, LORA_NSS);
    int st = radio.begin(LORA_FREQ, 125.0f, 9, 5,
                          RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 8, 10, 1.6f, false);
    if (st != RADIOLIB_ERR_NONE) {
        Serial.printf("[AN] LoRa error: %d\n", st);
    } else {
        Serial.println(F("[AN] LoRa ready"));
    }
}

void loop() {
    uint32_t now = millis();

    if (now - lastListenMs >= LISTEN_INTERVAL_MS) {
        lastListenMs = now;
        listenForCommand();
    }

    // Light sleep between cycles (in production: esp_light_sleep_start)
    delay(100);
}

// ═══════════════════════════════════════════════════════════════════════
// CAD + Rx cycle
// ═══════════════════════════════════════════════════════════════════════
void listenForCommand() {
    // Step 1: CAD — very low power, ~2 ms
    int cad = radio.startCAD();
    if (cad != RADIOLIB_ERR_NONE) return;

    uint32_t t0 = millis();
    bool activity = false;
    while (millis() - t0 < 100) {
        if (radio.isChannelActive()) { activity = true; break; }
        delay(1);
    }
    if (!activity) return;

    Serial.println(F("[AN] CAD detected — opening Rx window"));

    // Step 2: Full Rx
    uint8_t buf[LORA_MAX_PAYLOAD];
    int st = radio.startReceive();
    if (st != RADIOLIB_ERR_NONE) return;

    t0 = millis(); bool got = false;
    size_t len = 0;
    while (millis() - t0 < RX_TIMEOUT_MS) {
        len = radio.getPacketLength();
        if (len > 0) {
            if (len > sizeof(buf)) { len = sizeof(buf); }
            st = radio.readData(buf, len);
            if (st == RADIOLIB_ERR_NONE) { got = true; break; }
        }
        delay(10);
    }
    if (!got) { Serial.println(F("[AN] Rx timeout")); return; }

    Serial.printf("[AN] Rx %u bytes, RSSI=%.1f\n", len, radio.getRSSI());

    // Step 3: Validate + decrypt
    if (len < LORA_HEADER_SIZE + MIC_SIZE) return;
    uint16_t target = (buf[0] << 8) | buf[1];
    if (target != NODE_ID) { Serial.printf("[AN] Not for us (%04X)\n", target); return; }

    uint8_t pktType = buf[NODE_ID_SIZE + IV_NONCE_SIZE];
    uint32_t seq = 0;
    for (int i = 0; i < 8; i++) seq = (seq << 8) | buf[NODE_ID_SIZE + 4 + i];
    if (seq <= lastSeq) { Serial.printf("[AN] Replay %u\n", seq); return; }

    size_t ctLen = len - LORA_HEADER_SIZE - MIC_SIZE;
    if (ctLen < sizeof(ActuatorCommand)) return;

    LoraFrame frame;
    memcpy(frame.node_id, buf, 2);
    memcpy(frame.iv, buf + 2, 12);
    frame.pkt_type = pktType;
    memcpy(frame.ciphertext, buf + LORA_HEADER_SIZE, ctLen);
    memcpy(frame.mic, buf + LORA_HEADER_SIZE + ctLen, 4);
    frame.ciphertext_len = ctLen;

    uint8_t sk[16];
    CryptoEngine::deriveSessionKey(NODE_PSK, NODE_ID, sk);
    CryptoEngine crypto;
    if (!crypto.begin(sk, 16)) return;

    uint8_t pt[LORA_MAX_CIPHERTEXT];
    memcpy(pt, frame.ciphertext, ctLen);
    if (!crypto.decrypt(pt, ctLen, pktType, seq, NODE_ID, frame)) {
        Serial.println(F("[AN] Decrypt/MIC fail")); return;
    }

    lastSeq = seq;

    // Step 4: Execute command
    ActuatorCommand* cmd = (ActuatorCommand*)pt;
    if (cmd->command == 0x01) {
        valveOpen = (cmd->value != 0);
        digitalWrite(VALVE_CTL_PIN, valveOpen ? HIGH : LOW);
        Serial.printf("[AN] Valve → %s\n", valveOpen ? "OPEN" : "CLOSED");

        // Step 5: Send ACK
        sendAck(cmd->sequence);
    }
}

// ═══════════════════════════════════════════════════════════════════════
// ACK transmission
// ═══════════════════════════════════════════════════════════════════════
void sendAck(uint32_t ackSeq) {
    AckPayload ack;
    ack.ack_seq    = ackSeq;
    ack.result     = 0x00;
    ack.battery_mv = (uint16_t)(readBattery() * 1000);

    uint8_t sk[16];
    CryptoEngine::deriveSessionKey(NODE_PSK, NODE_ID, sk);
    CryptoEngine crypto;
    if (!crypto.begin(sk, 16)) return;

    uint8_t pt[sizeof(ack)];
    memcpy(pt, &ack, sizeof(ack));

    LoraFrame f;
    f.ciphertext_len = sizeof(ack);
    f.node_id[0] = NODE_ID >> 8; f.node_id[1] = NODE_ID & 0xFF;
    f.pkt_type = (uint8_t)PacketType::ACK;

    if (!crypto.encrypt(pt, sizeof(ack), (uint8_t)PacketType::ACK,
                         ackSeq, NODE_ID, f)) return;

    uint8_t tx[LORA_MAX_PAYLOAD]; size_t o = 0;
    memcpy(tx+o, f.node_id, 2); o+=2;
    memcpy(tx+o, f.iv, 12);     o+=12;
    tx[o++] = f.pkt_type;
    memcpy(tx+o, pt, sizeof(ack)); o+=sizeof(ack);
    memcpy(tx+o, f.mic, 4);       o+=4;

    int st = radio.transmit(tx, o);
    Serial.printf("[AN] ACK seq=%u → %s\n", ackSeq, st == RADIOLIB_ERR_NONE ? "OK" : "FAIL");
}
