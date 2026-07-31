#include "WeatherStation.h"

volatile uint32_t WeatherStation::_rainCnt   = 0;
volatile uint32_t WeatherStation::_windCnt   = 0;
volatile uint32_t WeatherStation::_rainLastUs = 0;
volatile uint32_t WeatherStation::_windLastUs = 0;

// ADC -> wind direction lookup table (8 cardinal/intercardinal points)
const WindCal WeatherStation::_adcCal[8] = {
    {3070,   0.0f},
    {1750,  45.0f},
    {330,   90.0f},
    {680,  135.0f},
    {1055, 180.0f},
    {2440, 225.0f},
    {3995, 270.0f},
    {3500, 315.0f}
};

WeatherStation::WeatherStation(uint8_t rainPin, uint8_t windPin, uint8_t vanePin,
                               uint8_t dhtPin, uint8_t ldrPin, uint8_t battPin,
                               uint8_t i2cSda, uint8_t i2cScl)
    : _rain(rainPin), _wind(windPin), _vane(vanePin),
      _dht(dhtPin), _ldr(ldrPin), _batt(battPin),
      _i2cSda(i2cSda), _i2cScl(i2cScl),
      _lastRain(0), _lastWind(0), _bmpOk(false),
      _smoothWindDeg(0), _windDegInit(false)
{
    _dhtSensor = new DHT(_dht, DHT11);
    _bmp = new Adafruit_BMP280();
}

void WeatherStation::begin() {
    // ---------- rain / wind: reed switches ----------
    // INPUT_PULLUP is MANDATORY — without it the GPIO floats and interrupts
    // never fire (or fire randomly from noise).
    pinMode(_rain, INPUT_PULLUP);
    pinMode(_wind, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(_rain), onRainISR, FALLING);
    attachInterrupt(digitalPinToInterrupt(_wind), onWindISR, FALLING);

    // ---------- wind vane ----------
    pinMode(_vane, INPUT);
    // ---------- LDR (light) ----------
    pinMode(_ldr, INPUT);
    // ---------- battery voltage divider ----------
    pinMode(_batt, INPUT);

    analogReadResolution(12);
#ifdef analogSetPinAttenuation
    analogSetPinAttenuation(_vane, ADC_11db);
    analogSetPinAttenuation(_ldr, ADC_11db);
    analogSetPinAttenuation(_batt, ADC_11db);
#endif

    _dhtSensor->begin();

    // ---------- I²C barometric pressure (BMP280) ----------
    Wire.begin(_i2cSda, _i2cScl);
    _bmpOk = _bmp->begin(0x76);
    if (_bmpOk) {
        _bmp->setSampling(Adafruit_BMP280::MODE_NORMAL,
                          Adafruit_BMP280::SAMPLING_X2,
                          Adafruit_BMP280::SAMPLING_X16,
                          Adafruit_BMP280::FILTER_X16,
                          Adafruit_BMP280::STANDBY_MS_500);
    }
}

WeatherData WeatherStation::read() {
    // Atomic read of volatile counters (uint32_t is naturally aligned on
    // ESP32, so no special barrier needed).  The spin-loop removed: it was
    // wasteful and could never guarantee consistency on a multi-core CPU
    // without a proper memory fence.
    uint32_t r = _rainCnt;
    uint32_t w = _windCnt;
    uint32_t dr = r - _lastRain;
    uint32_t dw = w - _lastWind;
    _lastRain = r;
    _lastWind = w;

    WeatherData d;

    // ---------- rain (mm) ----------
    d.rain_mm = dr * RAIN_TIP_MM;

    // ---------- wind speed (m/s) ----------
    // wind_speed = (pulses / interval_s) * WIND_MPS_PER_HZ
    //            = dw * WIND_FACTOR   where WIND_FACTOR = WIND_MPS_PER_HZ / WIND_INTERVAL_S
    d.wind_speed_ms = dw * WIND_FACTOR;

    // ---------- wind direction ----------
    // Use median-of-5 to filter noise from the ADC reading
    d.wind_vane_adc = median5();
    d.wind_dir_deg  = calcWindDeg(d.wind_vane_adc);

    // ---------- temperature & humidity (DHT11) ----------
    d.humidity_pct  = _dhtSensor->readHumidity();
    float dhtTemp   = _dhtSensor->readTemperature();

    // ---------- temperature & pressure (BMP280) ----------
    float bmpTemp = 0, bmpPres = 0;
    if (_bmpOk) {
        bmpTemp = _bmp->readTemperature();
        bmpPres = _bmp->readPressure() / 100.0f;
    }
    // Prefer BMP280 temperature over DHT11 (BMP280 is more accurate)
    d.temperature_c = (!isnan(bmpTemp) && bmpTemp != 0) ? bmpTemp : dhtTemp;
    d.pressure_hpa  = bmpPres;

    d.luminosity_lux = readLuminosity();
    d.battery_mv     = readBattery();
    return d;
}

void WeatherStation::reset() { _lastRain = _rainCnt; _lastWind = _windCnt; }

// ---------- Interrupt Service Routines ----------
// Both use microsecond-based debounce to filter contact bounce.
// A reed switch typically bounces for 0.5-5 ms.
// DEBOUNCE_US = 5000 (5 ms) means we ignore any edge that arrives within
// 5 ms of the previous accepted edge.

void IRAM_ATTR WeatherStation::onRainISR() {
    uint32_t now = micros();
    if (now - _rainLastUs >= DEBOUNCE_US) {
        _rainCnt++;
        _rainLastUs = now;
    }
}

void IRAM_ATTR WeatherStation::onWindISR() {
    uint32_t now = micros();
    if (now - _windLastUs >= DEBOUNCE_US) {
        _windCnt++;
        _windLastUs = now;
    }
}

// ---------- Battery ----------
// Voltage divider: 2:1 (measured on pin = Vbat * R2/(R1+R2)).
// ESP32-S3 ADC reference = 3.3 V  (internal).
// ADC range 0-4095  →  0-3.3 V on pin.
// Battery voltage    = pin_voltage * 2.0
float WeatherStation::readBattery() {
    int raw = analogRead(_batt);
    float v  = (raw / 4095.0f) * 3.3f;
    float vBat = v * 2.0f;
    return vBat * 1000.0f;  // millivolts
}

// ---------- Luminosity (LDR) ----------
// Voltage divider: LDR (to GND) + 10 kΩ (to 3.3 V).
// LDR resistance ~ 1 kΩ (bright) … 1 MΩ (dark).
float WeatherStation::readLuminosity() {
    int raw = analogRead(_ldr);
    if (raw <= 0 || raw >= 4095) return (raw >= 4095) ? 100000.0f : 0.0f;
    float vOut = (raw / 4095.0f) * 3.3f;
    float rLdr = 10000.0f * ((3.3f / vOut) - 1.0f);
    if (rLdr <= 0) return 0.0f;
    // Simplified inverse relationship: lux ≈ 500000 / R_ldr
    return 500000.0f / rLdr;
}

// ---------- Wind Vane ADC median filter ----------
// Takes 5 rapid readings and returns the median value.
// Rejects transient noise. Uses micros() delay instead of blocking delay().
int WeatherStation::median5() {
    int samples[5];
    for (int i = 0; i < 5; i++) {
        samples[i] = analogRead(_vane);
        if (i < 4) {
            uint32_t t = micros();
            while (micros() - t < 200) {}  // 200 us ADC settling
        }
    }
    // insertion sort
    for (int i = 1; i < 5; i++) {
        int key = samples[i];
        int j   = i - 1;
        while (j >= 0 && samples[j] > key) { samples[j + 1] = samples[j]; j--; }
        samples[j + 1] = key;
    }
    return samples[2];
}

// ---------- Wind direction from ADC ----------
// Matches 8-position lookup table, then applies exponential smoothing.
float WeatherStation::calcWindDeg(int rawAdc) {
    const int ADC_TOL = 300;
    int bestDelta = 99999;
    int bestIdx   = -1;
    for (int i = 0; i < 8; i++) {
        int delta = abs(rawAdc - _adcCal[i].adc);
        if (delta < bestDelta) { bestDelta = delta; bestIdx = i; }
    }

    float deg;
    if (bestIdx >= 0 && bestDelta <= ADC_TOL) {
        deg = _adcCal[bestIdx].degrees;
    } else {
        deg = 0;  // fallback
    }

    // Exponential smoothing (alpha = 0.25) to avoid jitter
    if (!_windDegInit) {
        _smoothWindDeg = deg;
        _windDegInit = true;
    } else {
        float delta = fmod((deg - _smoothWindDeg + 540.0f), 360.0f) - 180.0f;
        float sm    = _smoothWindDeg + 0.25f * delta;
        if (sm < 0)    sm += 360.0f;
        if (sm >= 360.0f) sm -= 360.0f;
        _smoothWindDeg = sm;
    }
    return _smoothWindDeg;
}