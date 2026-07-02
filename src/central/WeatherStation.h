#pragma once
#include <Arduino.h>

constexpr float RAIN_TIP_MM = 0.2794f;
constexpr float WIND_FACTOR = 0.24f;

struct WeatherData {
    float rain_mm;
    float wind_speed_ms;
    int   wind_vane_adc;
};

class WeatherStation {
public:
    WeatherStation(uint8_t rainPin, uint8_t windPin, uint8_t vanePin);
    void begin();
    WeatherData read();
    void reset();
    static IRAM_ATTR void onRainISR();
    static IRAM_ATTR void onWindISR();
private:
    uint8_t _rain, _wind, _vane;
    static volatile uint32_t _rainCnt, _windCnt;
    uint32_t _lastRain, _lastWind;
};
