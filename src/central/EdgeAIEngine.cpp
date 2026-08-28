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
    _weatherValid = false;
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
    if (!std::isfinite(reading.temperatureC) && !std::isfinite(reading.pressureHpa) &&
        !std::isfinite(reading.rainMm)) return;
    _latestWeather = reading;
    _weatherValid = true;
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
    if (!_weatherValid) {
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
    if (sensor.errorFlags != 0 || sensor.moistureRaw == 0 || sensor.moistureRaw >= 4095 ||
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
        if (actuator.errorFlags != 0 || (actuator.feedbackValid && actuator.valveCommanded != actuator.valveActual)) {
            confidence = 1.0f;
            return EdgeAIFault::STUCK_VALVE;
        }
        if (std::isfinite(actuator.tankPct) && actuator.tankPct <= 5.0f) {
            confidence = 1.0f;
            return EdgeAIFault::EMPTY_TANK;
        }
        if (std::isfinite(actuator.flowLpm)) {
            const float expected = field.emitterFlowLph > 0.0f ? field.emitterFlowLph / 60.0f : 0.0f;
            if (!actuator.valveActual && actuator.flowLpm > 0.5f) {
                confidence = 1.0f;
                return EdgeAIFault::LEAK;
            }
            if (actuator.valveActual && expected > 0.2f && actuator.flowLpm < expected * 0.25f) {
                confidence = 0.98f;
                return std::isfinite(actuator.pressureBar) && actuator.pressureBar < 0.5f
                    ? EdgeAIFault::EMPTY_TANK : EdgeAIFault::BLOCKED_PIPE;
            }
            if (actuator.valveActual && expected > 0.2f && actuator.flowLpm > expected * 1.6f) {
                confidence = 0.98f;
                return EdgeAIFault::LEAK;
            }
        }
    }

    const EdgeAIActuatorReading blank{};
    const EdgeAIActuatorReading& actuator = zone.actuatorValid ? zone.actuator : blank;
    const float features[EdgeAIModel::FAULT_FEATURE_COUNT] = {
        sensor.moisturePct, static_cast<float>(sensor.moistureRaw), sensor.batteryV,
        static_cast<float>(sensor.rssiDbm), actuator.valveCommanded ? 1.0f : 0.0f,
        actuator.valveActual ? 1.0f : 0.0f, actuator.feedbackValid ? 1.0f : 0.0f,
        actuator.flowLpm, actuator.pressureBar, actuator.tankPct, actuator.pumpCurrentA,
        static_cast<float>(sensor.errorFlags | actuator.errorFlags)
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
    prediction.weather = _forecast;
    if (sensor.sensorId == 0 || !std::isfinite(sensor.moisturePct)) {
        strlcpy(prediction.reason, "Prediction unavailable: invalid moisture input", sizeof(prediction.reason));
        return prediction;
    }

    ZoneState& zone = zoneFor(sensor.sensorId);
    zone.actuatorId = field.actuatorId;
    updateWateringEffect(zone, sensor);
    updateDryingState(zone, sensor);
    prediction.wateringEffect = zone.wateringEffect;
    prediction.wateringMoistureIncreasePct = zone.wateringIncreasePct;

    const float threshold = clampValue(field.thresholdPct, 5.0f, 95.0f);
    const float temp = _weatherValid && std::isfinite(_currentDay.meanTempC) ? _currentDay.meanTempC : 22.0f;
    const float humidity = _weatherValid && std::isfinite(_currentDay.meanHumidityPct) ? _currentDay.meanHumidityPct : 55.0f;
    const float pressure = _weatherValid && std::isfinite(_currentDay.meanPressureHpa) ? _currentDay.meanPressureHpa : 1015.0f;
    const float rain = _weatherValid ? std::max(0.0f, _currentDay.rainMm) : 0.0f;
    const float wind = _weatherValid && std::isfinite(_currentDay.meanWindMs) ? std::max(0.0f, _currentDay.meanWindMs) : 2.0f;
    const float soilTemp = std::isfinite(sensor.soilTemperatureC) ? sensor.soilTemperatureC : temp;
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
    prediction.waitForRain = prediction.irrigationNeeded && !critical && _forecast.valid &&
        _forecast.rainProbability >= EdgeAIModel::RAIN_THRESHOLD && _forecast.rainMm24h >= 1.5f;
    prediction.slowDrying = prediction.irrigationNeeded && !critical &&
        prediction.dryingRatePctDay > -0.7f && et0 < 4.0f;

    if (field.zoneAreaM2 > 0.0f && field.emitterFlowLph > 0.0f && prediction.recommendedWaterMm > 0.0f) {
        const float total = prediction.recommendedWaterMm * field.zoneAreaM2 * 60.0f / field.emitterFlowLph;
        prediction.totalRuntimeMin = static_cast<uint16_t>(std::ceil(clampValue(total, 0.0f, 720.0f)));
        prediction.cycleRuntimeMin = std::min<uint16_t>(prediction.totalRuntimeMin, MAX_CYCLE_RUNTIME_MIN);
        prediction.cycleCount = static_cast<uint8_t>(std::max<uint16_t>(1, (prediction.totalRuntimeMin + MAX_CYCLE_RUNTIME_MIN - 1) / MAX_CYCLE_RUNTIME_MIN));
    }

    float faultConfidence = 0.0f;
    prediction.fault = detectFault(zone, sensor, field, faultConfidence);
    prediction.faultConfidence = faultConfidence;
    if (zone.wateringEffect == WateringEffect::NO_RESPONSE && prediction.fault == EdgeAIFault::NORMAL) {
        prediction.fault = EdgeAIFault::SENSOR_FAULT;
        prediction.faultConfidence = 0.80f;
    }

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
    } else if (field.zoneAreaM2 <= 0.0f || field.emitterFlowLph <= 0.0f) {
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

std::string EdgeAIEngine::predictionJson(const EdgeAIPrediction& p) const {
    char json[1024];
    const auto number = [](float value) { return std::isfinite(value) ? value : 0.0f; };
    snprintf(json, sizeof(json),
        R"({"type":"ai_prediction","model_version":%lu,"shadow_mode":%s,"sensor_id":%u,"actuator_id":%u,"valid":%s,"irrigation_needed":%s,"irrigation_now":%s,"probability":%.4f,"water_mm":%.2f,"runtime_min":%u,"total_runtime_min":%u,"cycles":%u,"wait_hours":%u,"wait_for_rain":%s,"slow_drying":%s,"drying_rate":%.2f,"forecast_temp_c":%.2f,"forecast_rain_mm":%.2f,"rain_probability":%.4f,"forecast_et0_mm":%.2f,"watering_effect":"%s","moisture_increase_pct":%.2f,"fault":"%s","fault_confidence":%.4f,"reason":"%s"})",
        static_cast<unsigned long>(EdgeAIModel::MODEL_VERSION), p.shadowMode ? "true" : "false",
        p.sensorId, p.actuatorId, p.valid ? "true" : "false",
        p.irrigationNeeded ? "true" : "false", p.irrigationNow ? "true" : "false",
        number(p.irrigationProbability), number(p.recommendedWaterMm), p.cycleRuntimeMin,
        p.totalRuntimeMin, p.cycleCount, p.waitHours, p.waitForRain ? "true" : "false",
        p.slowDrying ? "true" : "false", number(p.dryingRatePctDay),
        number(p.weather.temperatureC24h), number(p.weather.rainMm24h), number(p.weather.rainProbability),
        number(p.weather.et0Mm24h), wateringEffectName(p.wateringEffect), number(p.wateringMoistureIncreasePct),
        faultName(p.fault), number(p.faultConfidence), p.reason);
    return std::string(json);
}

std::string EdgeAIEngine::statusJson() const {
    char header[512];
    const auto number = [](float value) { return std::isfinite(value) ? value : 0.0f; };
    snprintf(header, sizeof(header),
        R"({"model_name":"%s","model_version":%lu,"shadow_mode":%s,"dataset_sha256":"%s","weather":{"valid":%s,"temperature_c_24h":%.2f,"rain_mm_24h":%.2f,"rain_probability":%.4f,"et0_mm_24h":%.2f},"zones":[)",
        EdgeAIModel::MODEL_NAME, static_cast<unsigned long>(EdgeAIModel::MODEL_VERSION),
        controlAllowed() ? "false" : "true", EdgeAIModel::DATASET_SHA256,
        _forecast.valid ? "true" : "false", number(_forecast.temperatureC24h),
        number(_forecast.rainMm24h), number(_forecast.rainProbability), number(_forecast.et0Mm24h));
    std::string result(header);
    bool first = true;
    for (const auto& zone : _zones) {
        if (!zone.used || !zone.lastPrediction.valid) continue;
        if (!first) result += ',';
        first = false;
        std::string row = predictionJson(zone.lastPrediction);
        const size_t typeStart = row.find("\"type\":\"ai_prediction\",");
        if (typeStart != std::string::npos) row.erase(typeStart, strlen("\"type\":\"ai_prediction\","));
        result += row;
    }
    result += "]}";
    return result;
}
