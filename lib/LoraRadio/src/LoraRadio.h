#ifndef LORARADIO_H
#define LORARADIO_H

#include <Arduino.h>
#include <LoRa.h>

class LoraRadio {
public:
    LoraRadio(int cs, int irq, int rst) : _cs(cs), _irq(irq), _rst(rst) {}

    int begin(float freq, float bw, int sf, int cr, uint8_t syncWord, int8_t power, uint16_t preamble) {
        LoRa.setPins(_cs, _rst, _irq);
        if (!LoRa.begin((long)(freq * 1E6))) return -1;
        LoRa.setSpreadingFactor(sf);
        LoRa.setSignalBandwidth((long)(bw * 1E3));
        LoRa.setCodingRate4(cr);
        LoRa.setSyncWord(syncWord);
        LoRa.setTxPower(power);
        LoRa.setPreambleLength(preamble);
        return 0;
    }

    int startReceive() {
        LoRa.receive();
        return 0;
    }

    int available() {
        int len = LoRa.parsePacket();
        if (len > 0) _packetLen = len;
        return len;
    }

    size_t getPacketLength() {
        return _packetLen;
    }

    int readData(uint8_t* buf, size_t len) {
        int i = 0;
        while (LoRa.available() && i < (int)len) buf[i++] = LoRa.read();
        return 0;
    }

    int transmit(uint8_t* buf, size_t len) {
        LoRa.beginPacket();
        LoRa.write(buf, len);
        return LoRa.endPacket(false) ? 0 : -1;
    }

private:
    int _cs, _irq, _rst;
    size_t _packetLen = 0;
};

#endif
