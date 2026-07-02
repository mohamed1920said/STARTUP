#include <SPI.h>
#include <RadioLib.h>

Module mod(18, 26, 14, RADIOLIB_NC);
SX1276 radio(&mod);

// Set this: 1 = transmit every 5s, 0 = receive continuously
uint8_t mode = 1;
uint32_t lastTx = 0;
uint8_t seq = 0;

void setup() {
  Serial.begin(115200); delay(500);
  SPI.begin(5, 19, 27, 18);
  int st = radio.begin(868.0f, 125.0f, 9, 5, 0x12, 10, 8);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("FAIL: %d\n", st);
  } else {
    Serial.println("OK");
  }
  if (mode) {
    radio.transmit(&seq, 1);
    Serial.printf("Tx: seq=%u OK\n", seq++);
    lastTx = millis();
  } else {
    radio.startReceive();
    Serial.println("Rx mode");
  }
}

void loop() {
  if (mode) {
    if (millis() - lastTx >= 5000) {
      lastTx = millis();
      int st = radio.transmit(&seq, 1);
      if (st == RADIOLIB_ERR_NONE) Serial.printf("Tx: seq=%u RSSI=%d\n", seq, radio.getRSSI());
      seq++;
    }
  } else {
    if (radio.available() > 0) {
      uint8_t buf[64];
      int len = radio.getPacketLength();
      int st = radio.readData(buf, len);
      if (st == RADIOLIB_ERR_NONE) {
        Serial.printf("Rx: seq=%u len=%d rssi=%d snr=%.1f\n", buf[0], len, radio.getRSSI(), radio.getSNR());
      } else {
        Serial.printf("Rx err: %d\n", st);
      }
      radio.startReceive();
    }
  }
}
