#include "EdgeAIEngine.h"

#include "EdgeAIModel.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {
constexpr uint64_t DAY_MS = 86400000ULL;
constexpr uint64_t HOUR_MS = 3600000ULL;
constexpr float PI_F = 3.14159265358979323846f;
constexpr uint8_t SENSOR_MOISTURE_FAULT = 0x01;
constexpr uint8_t SENSOR_TEMPERATURE_FAULT = 0x02;
constexpr uint8_t ACTUATOR_FEEDBACK_MISMATCH = 0x02;

class EngineLock {
public:
    explicit EngineLock(SemaphoreHandle_t mutex) : _mutex(mutex),
        _held(mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) == pdTRUE) {}
    ~EngineLock() { if (_held) xSemaphoreGive(_mutex); }
    explicit operator bool() const { return _held; }
private:
    SemaphoreHandle_t _mutex;
    bool _held;
};

bool inRange(float value, float low, float high) {
    return std::isfinite(value) && value >= low && value <= high;
}

std::string jsonNumber(float value, unsigned precision = 2) {
    if (!std::isfinite(value)) return "null";
    char number[32];
    snprintf(number, sizeof(number), "%.*f", static_cast<int>(precision), value);
    return number;
}

const char* predictionReason(const EdgeAIPrediction& prediction) {
    if (prediction.sensorReceived &&
        static_cast<uint32_t>(millis() - prediction.receivedAtMs) > EdgeAIEngine::SENSOR_STALE_MS)
        return "Soil sensor telemetry is stale. Check sensor power and the LoRa link; recommendations are paused.";
    if (prediction.valid && prediction.weatherAvailable &&
        static_cast<uint32_t>(millis() - prediction.weatherReceivedAtMs) > EdgeAIEngine::WEATHER_STALE_MS)
        return "Weather inputs are stale. Water amount, runtime and rain-based scheduling are paused until fresh weather arrives.";
    return prediction.reason;
}

float clampValue(float value, float low, float high) {
    if (!std::isfinite(value)) return low;
    return std::max(low, std::min(high, value));
}

float sigmoidValue(float value) {
    value = clampValue(value, -30.0f, 30.0f);
    return 1.0f / (1.0f + std::exp(-value));
}

float normalizedLinear(const float* features, const float* mean, const float* scale,
                       const float* weights, size_t count, float bias) {
    float result = bias;
    for (size_t i = 0; i < count; ++i) {
        const float value = std::isfinite(features[i]) ? features[i] : mean[i];
        const float divisor = std::fabs(scale[i]) < 1e-6f ? 1.0f : scale[i];
        result += weights[i] * ((value - mean[i]) / divisor);
    }
    return result;
}

bool textContains(const char* value, const char* needle) {
    if (!value || !needle) return false;
    const size_t needleLength = std::strlen(needle);
    if (needleLength == 0) return true;
    for (const char* start = value; *start; ++start) {
        size_t offset = 0;
        while (offset < needleLength && start[offset] &&
               std::tolower(static_cast<unsigned char>(start[offset])) ==
               std::tolower(static_cast<unsigned char>(needle[offset]))) ++offset;
        if (offset == needleLength) return true;
    }
    return false;
}

int localHour(uint64_t epochMs) {
    if (epochMs < 1577836800000ULL) return -1;
    time_t seconds = static_cast<time_t>(epochMs / 1000ULL);
    struct tm value{};
    localtime_r(&seconds, &value);
    return value.tm_hour;
}

int dayOfYear(uint64_t epochMs) {
    if (epochMs < 1577836800000ULL) return 180;
    time_t seconds = static_cast<time_t>(epochMs / 1000ULL);
    struct tm value{};
    localtime_r(&seconds, &value);
    return value.tm_yday + 1;
}
}

void EdgeAIEngine::begin() {
    // Called once in setup, before LoRa/HTTP tasks are started.
    if (!_mutex) _mutex = xSemaphoreCreateMutex();
    EngineLock lock(_mutex);
    if (!lock) return;
    _weatherValid = false;
    _weatherReceivedAtMs = 0;
    _latestWeather = {};
    _forecast = {};
    for (auto& zone : _zones) zone = {};
    for (auto& day : _weatherHistory) day = {};
    _currentDay = {};
}

EdgeAIEngine::ZoneState& EdgeAIEngine::zoneFor(uint16_t sensorId) {
    for (auto& zone : _zones) {
        if (zone.used && zone.sensorId == sensorId) return zone;
    }
    for (auto& zone : _zones) {
        if (!zone.used) {
            zone = {};
            zone.used = true;
            zone.sensorId = sensorId;
            return zone;
        }
    }
    // Deterministic replacement keeps memory bounded when the configured fleet changes.
    ZoneState& zone = _zones[sensorId % MAX_ZONES];
    zone = {};
    zone.used = true;
    zone.sensorId = sensorId;
    return zone;
}

const EdgeAIEngine::ZoneState* EdgeAIEngine::findZone(uint16_t sensorId) const {
    for (const auto& zone : _zones) if (zone.used && zone.sensorId == sensorId) return &zone;
    return nullptr;
}

void EdgeAIEngine::updateWeather(const EdgeAIWeatherReading& reading) {
    EngineLock lock(_mutex);
    if (!lock) return;
    _latestWeather = reading;
    _weatherReceivedAtMs = millis();
    // A disconnected rain gauge reads zero. That alone is not enough to infer
    // temperature, rainfall or evapotranspiration using model-mean inputs.
    _weatherValid = inRange(reading.temperatureC, -40.0f, 60.0f) &&
        inRange(reading.humidityPct, 0.0f, 100.0f) &&
        inRange(reading.pressureHpa, 300.0f, 1100.0f) &&
        inRange(reading.rainMm, 0.0f, 200.0f) &&
        inRange(reading.windMs, 0.0f, 100.0f);
    if (!_weatherValid) { _forecast = {}; return; }
    const uint32_t day = reading.epochMs >= 1577836800000ULL
        ? static_cast<uint32_t>(reading.epochMs / DAY_MS) : 0;
    if (!_currentDay.valid || _currentDay.dayNumber != day) {
        if (_currentDay.valid) {
            _weatherHistory[2] = _weatherHistory[1];
            _weatherHistory[1] = _weatherHistory[0];
            _weatherHistory[0] = _currentDay;
        }
        _currentDay = {};
        _currentDay.valid = true;
        _currentDay.dayNumber = day;
    }
    const float n = static_cast<float>(_currentDay.samples);
    auto addMean = [n](float current, float value) {
        if (!std::isfinite(value)) return current;
        if (!std::isfinite(current)) return value;
        return (current * n + value) / (n + 1.0f);
    };
    _currentDay.meanTempC = addMean(_currentDay.meanTempC, reading.temperatureC);
    _currentDay.meanHumidityPct = addMean(_currentDay.meanHumidityPct, reading.humidityPct);
    _currentDay.meanPressureHpa = addMean(_currentDay.meanPressureHpa, reading.pressureHpa);
    _currentDay.meanWindMs = addMean(_currentDay.meanWindMs, reading.windMs);
    if (std::isfinite(reading.rainMm) && reading.rainMm > 0.0f) _currentDay.rainMm += reading.rainMm;
    ++_currentDay.samples;
    updateWeatherForecast();
}

void EdgeAIEngine::updateWeatherForecast() {
    if (!_weatherValid || _latestWeather.epochMs < 1577836800000ULL) {
        _forecast = {};
        return;
    }
    const float temp = std::isfinite(_currentDay.meanTempC) ? _currentDay.meanTempC : EdgeAIModel::WEATHER_MEAN[0];
    const float humidity = std::isfinite(_currentDay.meanHumidityPct) ? _currentDay.meanHumidityPct : EdgeAIModel::WEATHER_MEAN[1];
    const float pressure = std::isfinite(_currentDay.meanPressureHpa) ? _currentDay.meanPressureHpa : EdgeAIModel::WEATHER_MEAN[2];
    const float rain = std::max(0.0f, _currentDay.rainMm);
    const float wind = std::isfinite(_currentDay.meanWindMs) ? std::max(0.0f, _currentDay.meanWindMs) : EdgeAIModel::WEATHER_MEAN[4];
    const float tempTrend = _weatherHistory[0].valid && std::isfinite(_weatherHistory[0].meanTempC)
        ? temp - _weatherHistory[0].meanTempC : 0.0f;
    const float pressureTrend = _weatherHistory[0].valid && std::isfinite(_weatherHistory[0].meanPressureHpa)
        ? pressure - _weatherHistory[0].meanPressureHpa : 0.0f;
    float rain3d = _currentDay.rainMm;
    for (size_t i = 0; i < 2; ++i) if (_weatherHistory[i].valid) rain3d += _weatherHistory[i].rainMm;
    const float radians = 2.0f * PI_F * (dayOfYear(_latestWeather.epochMs) - 1.0f) / 365.25f;
    const float features[EdgeAIModel::WEATHER_FEATURE_COUNT] = {
        temp, humidity, pressure, rain, wind, tempTrend, pressureTrend,
        rain3d, std::sin(radians), std::cos(radians)
    };
    _forecast.valid = true;
    _forecast.temperatureC24h = clampValue(normalizedLinear(
        features, EdgeAIModel::WEATHER_MEAN, EdgeAIModel::WEATHER_SCALE,
        EdgeAIModel::WEATHER_TEMP_WEIGHTS, EdgeAIModel::WEATHER_FEATURE_COUNT,
        EdgeAIModel::WEATHER_TEMP_BIAS), -10.0f, 55.0f);
    _forecast.rainMm24h = clampValue(normalizedLinear(
        features, EdgeAIModel::WEATHER_MEAN, EdgeAIModel::WEATHER_SCALE,
        EdgeAIModel::WEATHER_RAIN_WEIGHTS, EdgeAIModel::WEATHER_FEATURE_COUNT,
        EdgeAIModel::WEATHER_RAIN_BIAS), 0.0f, 80.0f);
    _forecast.et0Mm24h = clampValue(normalizedLinear(
        features, EdgeAIModel::WEATHER_MEAN, EdgeAIModel::WEATHER_SCALE,
        EdgeAIModel::WEATHER_ET0_WEIGHTS, EdgeAIModel::WEATHER_FEATURE_COUNT,
        EdgeAIModel::WEATHER_ET0_BIAS), 0.0f, 15.0f);
    _forecast.rainProbability = sigmoidValue(normalizedLinear(
        features, EdgeAIModel::WEATHER_MEAN, EdgeAIModel::WEATHER_SCALE,
        EdgeAIModel::RAIN_PROB_WEIGHTS, EdgeAIModel::WEATHER_FEATURE_COUNT,
        EdgeAIModel::RAIN_PROB_BIAS));
}

void EdgeAIEngine::updateActuator(const EdgeAIActuatorReading& reading) {
    if (reading.linkedSensorId == 0) return;
    EngineLock lock(_mutex);
    if (!lock) return;
    ZoneState& zone = zoneFor(reading.linkedSensorId);
    zone.actuatorId = reading.actuatorId;
    const bool wasOpen = zone.actuatorValid && zone.actuator.valveActual;
    EdgeAIActuatorReading merged = reading;
    if (zone.actuatorValid) {
        if (!std::isfinite(merged.batteryV)) merged.batteryV = zone.actuator.batteryV;
        if (!std::isfinite(merged.tankPct)) merged.tankPct = zone.actuator.tankPct;
        // Hydraulic measurements from an earlier valve state are stale. Keep
        // them only when an ACK repeats the same state; the next heartbeat will
        // provide current readings after an open/close transition.
        if (zone.actuator.valveActual == reading.valveActual) {
            if (!std::isfinite(merged.flowLpm)) merged.flowLpm = zone.actuator.flowLpm;
            if (!std::isfinite(merged.pressureBar)) merged.pressureBar = zone.actuator.pressureBar;
            if (!std::isfinite(merged.pumpCurrentA)) merged.pumpCurrentA = zone.actuator.pumpCurrentA;
        }
    }
    zone.actuator = merged;
    zone.actuatorValid = true;
    zone.actuatorReceivedAtMs = millis();
    if (!wasOpen && reading.valveActual) {
        zone.wateringActive = true;
        zone.wateringPending = false;
        zone.preWaterMoisturePct = zone.lastMoisturePct;
        zone.wateringEffect = WateringEffect::PENDING;
        zone.wateringIncreasePct = 0.0f;
    } else if (wasOpen && !reading.valveActual && zone.wateringActive) {
        zone.wateringActive = false;
        zone.wateringPending = true;
        zone.wateringEndedEpochMs = reading.epochMs;
        zone.wateringEffect = WateringEffect::PENDING;
    }
}

void EdgeAIEngine::updateDryingState(ZoneState& zone, const EdgeAISensorReading& sensor) {
    if (!std::isfinite(sensor.moisturePct)) return;
    if (!std::isfinite(zone.trendAnchorPct) || zone.trendAnchorEpochMs == 0 ||
        sensor.epochMs <= zone.trendAnchorEpochMs) {
        zone.trendAnchorPct = sensor.moisturePct;
        zone.trendAnchorEpochMs = sensor.epochMs;
        zone.dryingRateValid = false;
        return;
    }
    const uint64_t elapsed = sensor.epochMs - zone.trendAnchorEpochMs;
    if (elapsed < 6ULL * HOUR_MS) return;
    const float elapsedDays = static_cast<float>(elapsed) / static_cast<float>(DAY_MS);
    const float instant = clampValue((sensor.moisturePct - zone.trendAnchorPct) / elapsedDays, -20.0f, 20.0f);
    zone.dryingRatePctDay = zone.dryingRateValid
        ? 0.65f * zone.dryingRatePctDay + 0.35f * instant : instant;
    zone.dryingRateValid = true;
    zone.trendAnchorPct = sensor.moisturePct;
    zone.trendAnchorEpochMs = sensor.epochMs;
}

void EdgeAIEngine::updateWateringEffect(ZoneState& zone, const EdgeAISensorReading& sensor) {
    if (!zone.wateringPending || !std::isfinite(zone.preWaterMoisturePct) ||
        !std::isfinite(sensor.moisturePct) || sensor.epochMs <= zone.wateringEndedEpochMs) return;
    zone.wateringIncreasePct = sensor.moisturePct - zone.preWaterMoisturePct;
    const uint64_t elapsed = sensor.epochMs - zone.wateringEndedEpochMs;
    if (zone.wateringIncreasePct >= 1.5f) {
        zone.wateringEffect = WateringEffect::MOISTURE_INCREASED;
        zone.wateringPending = false;
    } else if (elapsed >= 2ULL * HOUR_MS) {
        zone.wateringEffect = WateringEffect::NO_RESPONSE;
        zone.wateringPending = false;
    } else {
        zone.wateringEffect = WateringEffect::PENDING;
    }
}

float EdgeAIEngine::cropCoefficient(const char* crop, const char* growthStage) const {
    float kc = 0.85f;
    if (textContains(crop, "olive")) kc = 0.65f;
    else if (textContains(crop, "date")) kc = 0.90f;
    else if (textContains(crop, "citrus")) kc = 0.85f;
    else if (textContains(crop, "tomato")) kc = 1.05f;
    else if (textContains(crop, "potato")) kc = 1.00f;
    else if (textContains(crop, "wheat")) kc = 0.95f;
    if (textContains(growthStage, "initial") || textContains(growthStage, "seed")) kc *= 0.65f;
    else if (textContains(growthStage, "late") || textContains(growthStage, "harvest")) kc *= 0.75f;
    else if (textContains(growthStage, "dormant") || textContains(growthStage, "fallow")) kc *= 0.45f;
    return clampValue(kc, 0.25f, 1.25f);
}

float EdgeAIEngine::soilHoldingFactor(const char* soilType) const {
    if (textContains(soilType, "clay")) return 1.15f;
    if (textContains(soilType, "sand")) return 0.75f;
    if (textContains(soilType, "loam")) return 1.0f;
    return 0.95f;
}

float EdgeAIEngine::estimatedEt0() const {
    if (_forecast.valid && std::isfinite(_forecast.et0Mm24h)) return _forecast.et0Mm24h;
    if (!_weatherValid) return 4.0f;
    const float temp = std::isfinite(_latestWeather.temperatureC) ? _latestWeather.temperatureC : 22.0f;
    const float humidity = std::isfinite(_latestWeather.humidityPct) ? _latestWeather.humidityPct : 55.0f;
    const float wind = std::isfinite(_latestWeather.windMs) ? _latestWeather.windMs : 2.0f;
    return clampValue(0.12f * std::max(0.0f, temp) + 0.08f * wind + 0.025f * (100.0f - humidity), 0.5f, 12.0f);
}

EdgeAIFault EdgeAIEngine::detectFault(const ZoneState& zone, const EdgeAISensorReading& sensor,
                                      const EdgeAIFieldProfile& field, float& confidence) const {
    confidence = 0.0f;
    if ((sensor.errorFlags & SENSOR_MOISTURE_FAULT) != 0 || sensor.moistureRaw < 10 || sensor.moistureRaw > 4000 ||
        !std::isfinite(sensor.moisturePct) || sensor.moisturePct < 0.0f || sensor.moisturePct > 100.0f) {
        confidence = 1.0f;
        return EdgeAIFault::SENSOR_FAULT;
    }
    if ((std::isfinite(sensor.batteryV) && sensor.batteryV < 3.35f) ||
        (zone.actuatorValid && std::isfinite(zone.actuator.batteryV) && zone.actuator.batteryV < 3.35f)) {
        confidence = 1.0f;
        return EdgeAIFault::WEAK_BATTERY;
    }
    if (zone.actuatorValid) {
        const auto& actuator = zone.actuator;
        // Missing feedback/hydraulic sensors are capability flags, not proof
        // of a stuck valve. Only a measured mismatch can establish this fault.
        if (actuator.feedbackValid && ((actuator.errorFlags & ACTUATOR_FEEDBACK_MISMATCH) != 0 ||
                                      actuator.valveCommanded != actuator.valveActual)) {
            confidence = 1.0f;
            return EdgeAIFault::STUCK_VALVE;
        }
        if (std::isfinite(actuator.tankPct) && actuator.tankPct <= 5.0f) {
            confidence = 1.0f;
            return EdgeAIFault::EMPTY_TANK;
        }
        if (std::isfinite(actuator.flowLpm)) {
            const float expected = field.emitterFlowLph > 0.0f ? field.emitterFlowLph / 60.0f : 0.0f;
            if (actuator.feedbackValid && !actuator.valveActual && actuator.flowLpm > 0.5f) {
                confidence = 1.0f;
                return EdgeAIFault::LEAK;
            }
            if (actuator.feedbackValid && actuator.valveActual && expected > 0.2f && actuator.flowLpm < expected * 0.25f) {
                confidence = 0.98f;
                return std::isfinite(actuator.pressureBar) && actuator.pressureBar < 0.5f
                    ? EdgeAIFault::EMPTY_TANK : EdgeAIFault::BLOCKED_PIPE;
            }
            if (actuator.feedbackValid && actuator.valveActual && expected > 0.2f && actuator.flowLpm > expected * 1.6f) {
                confidence = 0.98f;
                return EdgeAIFault::LEAK;
            }
        }
    }

    // The fault model was trained on complete hydraulic observations. Do not
    // replace absent sensors with training means and call that a diagnosis.
    if (!zone.actuatorValid || !zone.actuator.feedbackValid ||
        !std::isfinite(zone.actuator.flowLpm) || !std::isfinite(zone.actuator.pressureBar) ||
        !std::isfinite(zone.actuator.tankPct) || !std::isfinite(zone.actuator.pumpCurrentA)) {
        return EdgeAIFault::NORMAL;
    }

    const EdgeAIActuatorReading blank{};
    const EdgeAIActuatorReading& actuator = zone.actuatorValid ? zone.actuator : blank;
    const float features[EdgeAIModel::FAULT_FEATURE_COUNT] = {
        sensor.moisturePct, static_cast<float>(sensor.moistureRaw), sensor.batteryV,
        static_cast<float>(sensor.rssiDbm), actuator.valveCommanded ? 1.0f : 0.0f,
        actuator.valveActual ? 1.0f : 0.0f, actuator.feedbackValid ? 1.0f : 0.0f,
        actuator.flowLpm, actuator.pressureBar, actuator.tankPct, actuator.pumpCurrentA,
        static_cast<float>((sensor.errorFlags & SENSOR_MOISTURE_FAULT) |
                           (actuator.errorFlags & ACTUATOR_FEEDBACK_MISMATCH))
    };
    float logits[EdgeAIModel::FAULT_CLASS_COUNT];
    float maxLogit = -1e30f;
    for (size_t cls = 0; cls < EdgeAIModel::FAULT_CLASS_COUNT; ++cls) {
        logits[cls] = normalizedLinear(features, EdgeAIModel::FAULT_MEAN, EdgeAIModel::FAULT_SCALE,
                                       EdgeAIModel::FAULT_WEIGHTS[cls], EdgeAIModel::FAULT_FEATURE_COUNT,
                                       EdgeAIModel::FAULT_BIAS[cls]);
        maxLogit = std::max(maxLogit, logits[cls]);
    }
    float sum = 0.0f;
    size_t best = 0;
    for (size_t cls = 0; cls < EdgeAIModel::FAULT_CLASS_COUNT; ++cls) {
        logits[cls] = std::exp(clampValue(logits[cls] - maxLogit, -30.0f, 30.0f));
        sum += logits[cls];
        if (logits[cls] > logits[best]) best = cls;
    }
    confidence = sum > 0.0f ? logits[best] / sum : 0.0f;
    if (best == 0 || confidence < 0.70f) return EdgeAIFault::NORMAL;
    static const EdgeAIFault mapping[] = {
        EdgeAIFault::NORMAL, EdgeAIFault::LEAK, EdgeAIFault::BLOCKED_PIPE,
        EdgeAIFault::EMPTY_TANK, EdgeAIFault::STUCK_VALVE, EdgeAIFault::SENSOR_FAULT
    };
    return mapping[best];
}

EdgeAIPrediction EdgeAIEngine::predict(const EdgeAISensorReading& sensor,
                                       const EdgeAIFieldProfile& field) {
    EdgeAIPrediction prediction;
    prediction.sensorId = sensor.sensorId;
    prediction.actuatorId = field.actuatorId;
    prediction.sensorReceived = sensor.sensorId != 0;
    prediction.receivedAtMs = millis();
    prediction.sensorEpochMs = sensor.epochMs;
    EngineLock lock(_mutex);
    if (!lock) {
        strlcpy(prediction.reason, "Recommendation temporarily unavailable: engine busy", sizeof(prediction.reason));
        return prediction;
    }
    if (sensor.sensorId == 0) {
        strlcpy(prediction.reason, "Waiting for telemetry from a registered soil sensor", sizeof(prediction.reason));
        return prediction;
    }
    ZoneState& zone = zoneFor(sensor.sensorId);
    zone.sensorReceived = true;
    zone.actuatorId = field.actuatorId;
    if (!inRange(sensor.moisturePct, 0.0f, 100.0f) || sensor.moistureRaw < 10 ||
        sensor.moistureRaw > 4000 || (sensor.errorFlags & SENSOR_MOISTURE_FAULT)) {
        prediction.fault = EdgeAIFault::SENSOR_FAULT;
        prediction.faultConfidence = 1.0f;
        strlcpy(prediction.reason, "Sensor received, but soil moisture is invalid. Check probe wiring and calibration.", sizeof(prediction.reason));
        zone.lastPrediction = prediction;
        return prediction;
    }
    if (zone.actuatorValid && static_cast<uint32_t>(millis() - zone.actuatorReceivedAtMs) > SENSOR_STALE_MS)
        zone.actuatorValid = false;
    prediction.runtimeConfigured = inRange(field.zoneAreaM2, 0.001f, 10000000.0f) &&
        inRange(field.emitterFlowLph, 0.001f, 10000000.0f);
    prediction.weatherAvailable = _weatherValid && _forecast.valid &&
        static_cast<uint32_t>(millis() - _weatherReceivedAtMs) <= WEATHER_STALE_MS;
    prediction.weatherReceivedAtMs = _weatherReceivedAtMs;
    prediction.weather = prediction.weatherAvailable ? _forecast : EdgeAIWeatherForecast{};
    prediction.feedbackAvailable = zone.actuatorValid && zone.actuator.feedbackValid;
    prediction.faultChecksLimited = !prediction.feedbackAvailable || !std::isfinite(zone.actuator.flowLpm) ||
        !std::isfinite(zone.actuator.pressureBar) || !std::isfinite(zone.actuator.tankPct) ||
        !std::isfinite(zone.actuator.pumpCurrentA);
    updateWateringEffect(zone, sensor);
    updateDryingState(zone, sensor);
    prediction.dryingRateMeasured = zone.dryingRateValid;
    prediction.wateringEffect = zone.wateringEffect;
    prediction.wateringMoistureIncreasePct = zone.wateringIncreasePct;

    const float threshold = clampValue(field.thresholdPct, 5.0f, 95.0f);
    if (!prediction.weatherAvailable) {
        // A real moisture reading still answers "is the soil dry?" even while
        // weather hardware is unavailable. Do not fabricate water/runtime data.
        prediction.valid = true;
        prediction.irrigationNeeded = sensor.moisturePct < threshold;
        prediction.irrigationProbability = NAN;
        prediction.recommendedWaterMm = NAN;
        prediction.dryingRatePctDay = zone.dryingRateValid ? zone.dryingRatePctDay : NAN;
        prediction.fault = detectFault(zone, sensor, field, prediction.faultConfidence);
        if (prediction.fault != EdgeAIFault::NORMAL) {
            snprintf(prediction.reason, sizeof(prediction.reason), "Irrigation inhibited: %s detected", faultName(prediction.fault));
        } else {
            snprintf(prediction.reason, sizeof(prediction.reason),
                "Soil moisture %.1f%% is %s the %.1f%% threshold. Check weather sensors and clock sync; water amount and runtime are unavailable.",
                sensor.moisturePct, prediction.irrigationNeeded ? "below" : "above", threshold);
        }
        zone.lastMoisturePct = sensor.moisturePct;
        zone.lastSensorEpochMs = sensor.epochMs;
        zone.lastPrediction = prediction;
        return prediction;
    }
    const float temp = _weatherValid && std::isfinite(_currentDay.meanTempC) ? _currentDay.meanTempC : 22.0f;
    const float humidity = _weatherValid && std::isfinite(_currentDay.meanHumidityPct) ? _currentDay.meanHumidityPct : 55.0f;
    const float pressure = _weatherValid && std::isfinite(_currentDay.meanPressureHpa) ? _currentDay.meanPressureHpa : 1015.0f;
    const float rain = _weatherValid ? std::max(0.0f, _currentDay.rainMm) : 0.0f;
    const float wind = _weatherValid && std::isfinite(_currentDay.meanWindMs) ? std::max(0.0f, _currentDay.meanWindMs) : 2.0f;
    const float soilTemp = !(sensor.errorFlags & SENSOR_TEMPERATURE_FAULT) &&
        inRange(sensor.soilTemperatureC, -40.0f, 80.0f) ? sensor.soilTemperatureC : temp;
    const float saturation = 0.6108f * std::exp((17.27f * temp) / (temp + 237.3f));
    const float vpd = std::max(0.0f, saturation * (1.0f - humidity / 100.0f));
    const float kc = cropCoefficient(field.crop, field.growthStage);
    const float et0 = estimatedEt0();
    float rain3d = _currentDay.rainMm;
    for (size_t i = 0; i < 2; ++i) if (_weatherHistory[i].valid) rain3d += _weatherHistory[i].rainMm;
    const float radians = 2.0f * PI_F * (dayOfYear(sensor.epochMs) - 1.0f) / 365.25f;
    const float measuredTrend = zone.dryingRateValid ? zone.dryingRatePctDay : 0.0f;
    const float features[EdgeAIModel::IRRIGATION_FEATURE_COUNT] = {
        sensor.moisturePct, threshold - sensor.moisturePct, soilTemp, temp, humidity,
        pressure, rain, wind, vpd, et0, kc, measuredTrend, rain3d,
        std::sin(radians), std::cos(radians)
    };
    prediction.irrigationProbability = sigmoidValue(normalizedLinear(
        features, EdgeAIModel::IRRIGATION_MEAN, EdgeAIModel::IRRIGATION_SCALE,
        EdgeAIModel::IRRIGATION_WEIGHTS, EdgeAIModel::IRRIGATION_FEATURE_COUNT,
        EdgeAIModel::IRRIGATION_BIAS));
    prediction.irrigationNeeded = prediction.irrigationProbability >= EdgeAIModel::IRRIGATION_THRESHOLD;

    prediction.recommendedWaterMm = clampValue(normalizedLinear(
        features, EdgeAIModel::WATER_MEAN, EdgeAIModel::WATER_SCALE,
        EdgeAIModel::WATER_WEIGHTS, EdgeAIModel::IRRIGATION_FEATURE_COUNT,
        EdgeAIModel::WATER_BIAS) * soilHoldingFactor(field.soilType), 0.0f, 35.0f);
    prediction.dryingRatePctDay = zone.dryingRateValid ? zone.dryingRatePctDay : clampValue(normalizedLinear(
        features, EdgeAIModel::DRYING_MEAN, EdgeAIModel::DRYING_SCALE,
        EdgeAIModel::DRYING_WEIGHTS, EdgeAIModel::IRRIGATION_FEATURE_COUNT,
        EdgeAIModel::DRYING_BIAS), -20.0f, 20.0f);

    const bool critical = sensor.moisturePct <= threshold - 12.0f;
    prediction.waitForRain = prediction.irrigationNeeded && !critical && prediction.weather.valid &&
        prediction.weather.rainProbability >= EdgeAIModel::RAIN_THRESHOLD && prediction.weather.rainMm24h >= 1.5f;
    prediction.slowDrying = prediction.irrigationNeeded && !critical && zone.dryingRateValid &&
        prediction.dryingRatePctDay > -0.7f && et0 < 4.0f;

    if (prediction.runtimeConfigured && prediction.recommendedWaterMm > 0.0f) {
        const float total = prediction.recommendedWaterMm * field.zoneAreaM2 * 60.0f / field.emitterFlowLph;
        prediction.totalRuntimeMin = static_cast<uint16_t>(std::ceil(clampValue(total, 0.0f, 720.0f)));
        prediction.cycleRuntimeMin = std::min<uint16_t>(prediction.totalRuntimeMin, MAX_CYCLE_RUNTIME_MIN);
        prediction.cycleCount = static_cast<uint8_t>(std::max<uint16_t>(1, (prediction.totalRuntimeMin + MAX_CYCLE_RUNTIME_MIN - 1) / MAX_CYCLE_RUNTIME_MIN));
    }

    float faultConfidence = 0.0f;
    prediction.fault = detectFault(zone, sensor, field, faultConfidence);
    prediction.faultConfidence = faultConfidence;
    // No moisture increase is a useful observation, not enough evidence to
    // distinguish a failed sensor, missed watering, soil lag or a blocked pipe.

    if (!prediction.irrigationNeeded) prediction.waitHours = 24;
    else if (prediction.waitForRain) prediction.waitHours = 24;
    else if (prediction.slowDrying) prediction.waitHours = 12;
    else if (!critical) {
        const int hour = localHour(sensor.epochMs);
        if (hour >= 0 && hour < 5) prediction.waitHours = static_cast<uint16_t>(5 - hour);
        else if (hour > 9 && hour < 19) prediction.waitHours = static_cast<uint16_t>(19 - hour);
    }
    prediction.irrigationNow = prediction.irrigationNeeded && prediction.waitHours == 0 &&
        prediction.fault == EdgeAIFault::NORMAL && prediction.cycleRuntimeMin > 0;
    prediction.valid = true;

    if (prediction.fault != EdgeAIFault::NORMAL) {
        snprintf(prediction.reason, sizeof(prediction.reason), "Irrigation inhibited: %s detected", faultName(prediction.fault));
    } else if (!prediction.runtimeConfigured) {
        prediction.irrigationNow = false;
        snprintf(prediction.reason, sizeof(prediction.reason), "Set zone area and emitter flow before automatic runtime control");
    } else if (!prediction.irrigationNeeded) {
        snprintf(prediction.reason, sizeof(prediction.reason), "No irrigation: moisture %.1f%% is adequate (AI probability %.0f%%)",
                 sensor.moisturePct, prediction.irrigationProbability * 100.0f);
    } else if (prediction.waitForRain) {
        snprintf(prediction.reason, sizeof(prediction.reason), "Wait for likely rain: %.0f%% probability, %.1f mm predicted",
                 _forecast.rainProbability * 100.0f, _forecast.rainMm24h);
    } else if (prediction.slowDrying) {
        snprintf(prediction.reason, sizeof(prediction.reason), "Wait: soil drying slowly at %.1f percentage points/day", prediction.dryingRatePctDay);
    } else if (prediction.waitHours > 0) {
        snprintf(prediction.reason, sizeof(prediction.reason), "Schedule in %u h during the cooler irrigation window", prediction.waitHours);
    } else {
        snprintf(prediction.reason, sizeof(prediction.reason), "Irrigate now: %u min cycle (%u cycles, %u min total)",
                 prediction.cycleRuntimeMin, prediction.cycleCount, prediction.totalRuntimeMin);
    }

    zone.lastMoisturePct = sensor.moisturePct;
    zone.lastSensorEpochMs = sensor.epochMs;
    zone.lastPrediction = prediction;
    return prediction;
}

const char* EdgeAIEngine::faultName(EdgeAIFault fault) {
    switch (fault) {
        case EdgeAIFault::LEAK: return "leak";
        case EdgeAIFault::BLOCKED_PIPE: return "blocked_pipe";
        case EdgeAIFault::EMPTY_TANK: return "empty_tank";
        case EdgeAIFault::STUCK_VALVE: return "stuck_valve";
        case EdgeAIFault::SENSOR_FAULT: return "sensor_fault";
        case EdgeAIFault::WEAK_BATTERY: return "weak_battery";
        default: return "normal";
    }
}

const char* EdgeAIEngine::wateringEffectName(WateringEffect effect) {
    switch (effect) {
        case WateringEffect::PENDING: return "pending";
        case WateringEffect::MOISTURE_INCREASED: return "moisture_increased";
        case WateringEffect::NO_RESPONSE: return "no_response";
        default: return "not_available";
    }
}

EdgeAIWeatherForecast EdgeAIEngine::weatherForecast() const {
    EngineLock lock(_mutex);
    if (!lock || !_weatherValid ||
        static_cast<uint32_t>(millis() - _weatherReceivedAtMs) > WEATHER_STALE_MS) return {};
    return _forecast;
}

const char* EdgeAIEngine::readinessName(const EdgeAIPrediction& p) {
    if (!p.sensorReceived) return "waiting_sensor";
    if (static_cast<uint32_t>(millis() - p.receivedAtMs) > SENSOR_STALE_MS) return "stale_sensor";
    if (!p.valid) return "invalid_sensor";
    if (!p.weatherAvailable || static_cast<uint32_t>(millis() - p.weatherReceivedAtMs) > WEATHER_STALE_MS) return "needs_weather";
    if (!p.runtimeConfigured) return "needs_field_config";
    return "ready";
}

std::string EdgeAIEngine::predictionJson(const EdgeAIPrediction& p) const {
    // Append bounded sections instead of a large snprintf buffer on the LoRa
    // task stack. Unavailable values are JSON null, never NaN or invented zero.
    char part[640];
    const uint32_t ageMs = static_cast<uint32_t>(millis() - p.receivedAtMs);
    const bool valid = p.valid && p.sensorReceived && ageMs <= SENSOR_STALE_MS;
    const bool weatherAvailable = p.weatherAvailable &&
        static_cast<uint32_t>(millis() - p.weatherReceivedAtMs) <= WEATHER_STALE_MS;
    const bool forecastValid = valid && p.weather.valid && weatherAvailable;
    const bool runtimeValid = valid && p.runtimeConfigured && weatherAvailable;
    const char* reason = predictionReason(p);
    std::string result;
    result.reserve(1400);
    snprintf(part, sizeof(part),
        R"({"type":"ai_prediction","model_version":%lu,"shadow_mode":%s,"sensor_id":%u,"actuator_id":%u,"valid":%s,"readiness":"%s","sensor_epoch_ms":%llu,"sensor_age_s":%lu,"runtime_configured":%s,"weather_available":%s,"feedback_available":%s,"fault_checks_limited":%s,"drying_rate_measured":%s,)",
        static_cast<unsigned long>(EdgeAIModel::MODEL_VERSION), p.shadowMode ? "true" : "false",
        p.sensorId, p.actuatorId, valid ? "true" : "false", readinessName(p),
        static_cast<unsigned long long>(p.sensorEpochMs), static_cast<unsigned long>(ageMs / 1000),
        p.runtimeConfigured ? "true" : "false", weatherAvailable ? "true" : "false",
        p.feedbackAvailable ? "true" : "false", p.faultChecksLimited ? "true" : "false",
        p.dryingRateMeasured ? "true" : "false");
    result += part;
    snprintf(part, sizeof(part),
        R"("irrigation_needed":%s,"irrigation_now":%s,"probability":%s,"water_mm":%s,"runtime_min":%s,"total_runtime_min":%s,"cycles":%u,"wait_hours":%u,"wait_for_rain":%s,"slow_drying":%s,"drying_rate":%s,"forecast_valid":%s,"forecast_temp_c":%s,"forecast_rain_mm":%s,"rain_probability":%s,"forecast_et0_mm":%s,)",
        valid && p.irrigationNeeded ? "true" : "false", runtimeValid && p.irrigationNow ? "true" : "false",
        jsonNumber(valid ? p.irrigationProbability : NAN, 4).c_str(),
        jsonNumber(valid && weatherAvailable ? p.recommendedWaterMm : NAN).c_str(),
        jsonNumber(runtimeValid ? p.cycleRuntimeMin : NAN, 0).c_str(),
        jsonNumber(runtimeValid ? p.totalRuntimeMin : NAN, 0).c_str(),
        valid ? p.cycleCount : 0, valid ? p.waitHours : 0,
        forecastValid && p.waitForRain ? "true" : "false", valid && weatherAvailable && p.slowDrying ? "true" : "false",
        jsonNumber(valid ? p.dryingRatePctDay : NAN).c_str(), forecastValid ? "true" : "false",
        jsonNumber(forecastValid ? p.weather.temperatureC24h : NAN).c_str(),
        jsonNumber(forecastValid ? p.weather.rainMm24h : NAN).c_str(),
        jsonNumber(forecastValid ? p.weather.rainProbability : NAN, 4).c_str(),
        jsonNumber(forecastValid ? p.weather.et0Mm24h : NAN).c_str());
    result += part;
    snprintf(part, sizeof(part),
        R"("watering_effect":"%s","moisture_increase_pct":%s,"fault":"%s","fault_confidence":%s,"reason":"%s"})",
        wateringEffectName(p.wateringEffect), jsonNumber(p.wateringMoistureIncreasePct).c_str(),
        faultName(p.fault), jsonNumber(p.faultConfidence, 4).c_str(), reason);
    result += part;
    return result;
}

std::string EdgeAIEngine::statusJson() const {
    EngineLock lock(_mutex);
    if (!lock) return R"({"readiness":"busy","reason":"Recommendation engine is busy. Retry shortly.","weather":{"valid":false},"zones":[]})";
    unsigned sensorCount = 0, readyCount = 0;
    const char* readiness = "waiting_sensor";
    const char* reason = "Waiting for telemetry from a soil sensor. Check sensor power, LoRa registration and radio settings; the sensor reports every two minutes.";
    for (const auto& zone : _zones) {
        if (!zone.used || !zone.sensorReceived) continue;
        ++sensorCount;
        const char* state = readinessName(zone.lastPrediction);
        if (strcmp(state, "ready") == 0) ++readyCount;
        if (sensorCount == 1) { readiness = state; reason = predictionReason(zone.lastPrediction); }
    }
    if (readyCount) { readiness = "ready"; reason = "Recommendations are available. Review each zone's data and safety status."; }
    else if (strcmp(readiness, "stale_sensor") == 0)
        reason = "Soil sensor telemetry is stale. Check sensor power and the LoRa link.";
    const bool forecastValid = _forecast.valid &&
        static_cast<uint32_t>(millis() - _weatherReceivedAtMs) <= WEATHER_STALE_MS;
    char part[640];
    snprintf(part, sizeof(part),
        R"({"model_name":"%s","model_version":%lu,"shadow_mode":%s,"dataset_sha256":"%s","readiness":"%s","sensor_count":%u,"ready_zone_count":%u,"reason":"%s",)",
        EdgeAIModel::MODEL_NAME, static_cast<unsigned long>(EdgeAIModel::MODEL_VERSION),
        controlAllowed() ? "false" : "true", EdgeAIModel::DATASET_SHA256,
        readiness, sensorCount, readyCount, reason);
    std::string result(part);
    snprintf(part, sizeof(part),
        R"("weather":{"valid":%s,"temperature_c_24h":%s,"rain_mm_24h":%s,"rain_probability":%s,"et0_mm_24h":%s},"zones":[)",
        forecastValid ? "true" : "false", jsonNumber(forecastValid ? _forecast.temperatureC24h : NAN).c_str(),
        jsonNumber(forecastValid ? _forecast.rainMm24h : NAN).c_str(),
        jsonNumber(forecastValid ? _forecast.rainProbability : NAN, 4).c_str(),
        jsonNumber(forecastValid ? _forecast.et0Mm24h : NAN).c_str());
    result += part;
    bool first = true;
    for (const auto& zone : _zones) {
        // Keep invalid sensor observations visible, so "waiting" cannot hide
        // a received packet with a disconnected or miscalibrated moisture probe.
        if (!zone.used || !zone.sensorReceived) continue;
        if (!first) result += ',';
        first = false;
        result += predictionJson(zone.lastPrediction);
    }
    result += "]}";
    return result;
}
