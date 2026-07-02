#include <SPI.h>
#include <RadioLib.h>

#define FREQ      868.0f
#define BW        125.0f
#define SF        9
#define CR        5
#define SYNC      0x12
#define POWER     10
#define PREAMBLE  8

#define NSS  18
#define DIO0 26
#define RST  14

Module mod(NSS, DIO0, RST, RADIOLIB_NC);
SX1276 radio(&mod);

uint32_t lastTx = 0;
uint8_t  mode   = 1; // 1=tx, 0=rx

void setup() {
  Serial.begin(115200); delay(500);
  Serial.printf("TTGO LoRa Test: NSS=%d DIO0=%d RST=%d\n", NSS, DIO0, RST);
  SPI.begin(5, 19, 27, NSS);
  int st = radio.begin(FREQ, BW, SF, CR, SYNC, POWER, PREAMBLE);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("radio.begin err: %d\n", st);
  } else {
    Serial.println("LoRa OK");
  }
  if (mode) {
    radio.transmit((uint8_t*)"Hello", 5);
    Serial.println("Tx: Hello");
    lastTx = millis();
  } else {
    radio.startReceive();
  }
}

void loop() {
  if (mode) {
    if (millis() - lastTx >= 5000) {
      lastTx = millis();
      int st = radio.transmit((uint8_t*)"Hello", 5);
      Serial.printf("Tx: Hello -> %d\n", st);
    }
  } else {
    if (radio.available() > 0) {
      uint8_t buf[64];
      int len = radio.getPacketLength();
      int st = radio.readData(buf, len);
      buf[len] = 0;
      Serial.printf("Rx len=%d rssi=%d snr=%.1f: %s\n", len, radio.getRSSI(), radio.getSNR(), (char*)buf);
      radio.startReceive();
    }
  }
}
