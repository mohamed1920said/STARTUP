#pragma once
#include <Arduino.h>
#include <DHT.h>
#include <Adafruit_BMP280.h>
#include <Wire.h>

/* ---------- Rain ----------
 * Tipping bucket rain gauge. Each tip = RAIN_TIP_MM millimeters of rainfall.
 * Pulse counted on FALLING edge via interrupt with software debounce.
 * Typical: 0.2794 mm/tip (= 0.011 inches/tip) */
constexpr float RAIN_TIP_MM       = 0.2794f;  // mm per bucket tip

/* ---------- Wind ----------
 * Cup anemometer with reed switch. The reed generates pulses as the cups spin.
 * Calibration: wind speed (m/s) = pulse_frequency_Hz * WIND_MPS_PER_HZ
 *
 * For a typical cup anemometer: 1 revolution/second ~ 2.4 km/h = 0.667 m/s.
 * Adjust WIND_MPS_PER_HZ to match YOUR anemometer's datasheet.
 *
 * Example calibrations:
 *   0.667 m/s per Hz  — generic cup anemometer, 2.4 km/h per Hz
 *   1.492 m/s per Hz  — Davis cup anemometer, 1 mph = 1 rotation/1.492 s
 *   0.350 m/s per Hz  — Inspeed EZ (0.350 m/s per rotation/s) */
constexpr float WIND_MPS_PER_HZ   = 0.667f;

/* The Arduino loop reads weather every WIND_INTERVAL_S seconds.
 * This must match the interval in main.cpp.
 * dw = pulse count over the interval.
 * wind_speed = (dw / WIND_INTERVAL_S_ * WIND_MPS_PER_HZ */
constexpr float WIND_INTERVAL_S   = 30.0f;
constexpr float WIND_FACTOR       = WIND_MPS_PER_HZ / WIND_INTERVAL_S;  // ~0.02223

/* Debounce time for reed-switch / mechanical contacts (milliseconds).
 * A typical reed switch bounces for 0.5-5 ms.  5 ms is safe. */
constexpr uint32_t DEBOUNCE_US    = 5000;  // 5 ms in microseconds

struct WindCal {
    int adc;
    float degrees;
};

struct WeatherData {
    float rain_mm;
    float wind_speed_ms;
    int   wind_vane_adc;
    float wind_dir_deg;
    float temperature_c;
    float humidity_pct;
    float pressure_hpa;
    float luminosity_lux;
    float battery_mv;
};

class WeatherStation {
public:
    WeatherStation(uint8_t rainPin, uint8_t windPin, uint8_t vanePin,
                   uint8_t dhtPin, uint8_t ldrPin, uint8_t battPin,
                   uint8_t i2cSda, uint8_t i2cScl);
    void begin();
    WeatherData read();
    void reset();
    static IRAM_ATTR void onRainISR();
    static IRAM_ATTR void onWindISR();
private:
    uint8_t _rain, _wind, _vane, _dht, _ldr, _batt;
    uint8_t _i2cSda, _i2cScl;
    static volatile uint32_t _rainCnt;
    static volatile uint32_t _windCnt;
    static volatile uint32_t _rainLastUs;
    static volatile uint32_t _windLastUs;
    uint32_t _lastRain, _lastWind;
    DHT* _dhtSensor;
    Adafruit_BMP280* _bmp;
    bool _bmpOk;
    float _smoothWindDeg;
    bool _windDegInit;
    float readBattery();
    float readLuminosity();
    float calcWindDeg(int rawAdc);
    int median5();
    static const WindCal _adcCal[16];
};
