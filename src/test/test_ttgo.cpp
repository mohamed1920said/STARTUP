#include <SPI.h>
#include <LoRa.h>

#define LORA_CS 18
#define LORA_IRQ 26
#define LORA_RST 14
#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27

volatile int rxLen = 0;

void onRx(int len) {
  rxLen = len;
}

void setup() {
  Serial.begin(115200); delay(500);
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  int st = LoRa.begin(868E6);
  Serial.printf("LoRa begin: %d\n", st);
  LoRa.setSpreadingFactor(9);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(0x12);
  LoRa.setTxPower(10);
  LoRa.onReceive(onRx);
  LoRa.receive();
  Serial.println("Ready - LoRa lib listening...");
}

uint32_t last = 0;
void loop() {
  if (rxLen) {
    int len = rxLen; rxLen = 0;
    uint8_t buf[64];
    int i = 0;
    while (LoRa.available() && i < 64) buf[i++] = LoRa.read();
    Serial.printf("Rx! len=%d (%d read) rssi=%d snr=%.1f\n", len, i, LoRa.packetRssi(), LoRa.packetSnr());
    for (int j = 0; j < i; j++) Serial.printf("%02X", buf[j]);
    Serial.println();
    LoRa.receive();
  }
  uint32_t now = millis();
  if (now - last >= 10000) {
    last = now;
    Serial.printf("tick %u\n", now/1000);
  }
}
