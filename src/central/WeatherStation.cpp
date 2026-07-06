#include "WeatherStation.h"

volatile uint32_t WeatherStation::_rainCnt = 0;
volatile uint32_t WeatherStation::_windCnt = 0;

WeatherStation::WeatherStation(uint8_t rainPin, uint8_t windPin, uint8_t vanePin)
    : _rain(rainPin), _wind(windPin), _vane(vanePin), _lastRain(0), _lastWind(0) {}

void WeatherStation::begin() {
    pinMode(_rain, INPUT_PULLUP);
    pinMode(_wind, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(_rain), onRainISR, RISING);
    attachInterrupt(digitalPinToInterrupt(_wind), onWindISR, RISING);
    pinMode(_vane, INPUT);
}

WeatherData WeatherStation::read() {
    uint32_t r, w;
    do { r = _rainCnt; w = _windCnt; } while (r != _rainCnt || w != _windCnt);
    uint32_t dr = r - _lastRain;
    uint32_t dw = w - _lastWind;
    _lastRain = r; _lastWind = w;
    WeatherData d;
    d.rain_mm = dr * RAIN_TIP_MM;
    d.wind_speed_ms = dw * WIND_FACTOR;
    d.wind_vane_adc = analogRead(_vane);
    return d;
}

void WeatherStation::reset() { _lastRain = _rainCnt; _lastWind = _windCnt; }

void IRAM_ATTR WeatherStation::onRainISR() { _rainCnt++; }
void IRAM_ATTR WeatherStation::onWindISR() { _windCnt++; }
