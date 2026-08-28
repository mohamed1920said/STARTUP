#pragma once

#include <Arduino.h>
#include <string>

#ifndef EDGE_AI_ALLOW_CONTROL
#define EDGE_AI_ALLOW_CONTROL 0
#endif

enum class EdgeAIFault : uint8_t {
    NORMAL = 0,
    LEAK,
    BLOCKED_PIPE,
    EMPTY_TANK,
    STUCK_VALVE,
    SENSOR_FAULT,
    WEAK_BATTERY
};

enum class WateringEffect : uint8_t {
    NOT_AVAILABLE = 0,
    PENDING,
    MOISTURE_INCREASED,
    NO_RESPONSE
};

struct EdgeAIWeatherReading {
    float temperatureC = NAN;
    float humidityPct = NAN;
    float pressureHpa = NAN;
    float rainMm = NAN;
    float windMs = NAN;
    float luminosityLux = NAN;
    uint64_t epochMs = 0;
};

struct EdgeAISensorReading {
    uint16_t sensorId = 0;
    uint16_t moistureRaw = 0;
    float moisturePct = NAN;
    float soilTemperatureC = NAN;
    float batteryV = NAN;
    int16_t rssiDbm = 0;
    uint8_t errorFlags = 0;
    uint64_t epochMs = 0;
};

struct EdgeAIActuatorReading {
    uint16_t actuatorId = 0;
    uint16_t linkedSensorId = 0;
    bool valveCommanded = false;
    bool valveActual = false;
    bool feedbackValid = false;
    float batteryV = NAN;
    float flowLpm = NAN;
    float pressureBar = NAN;
    float tankPct = NAN;
    float pumpCurrentA = NAN;
    uint8_t errorFlags = 0;
    uint64_t epochMs = 0;
};

struct EdgeAIFieldProfile {
    uint16_t sensorId = 0;
    uint16_t actuatorId = 0;
    float thresholdPct = 45.0f;
    float zoneAreaM2 = 0.0f;
    float emitterFlowLph = 0.0f;
    const char* crop = "unknown";
    const char* growthStage = "unknown";
    const char* soilType = "unknown";
};

struct EdgeAIWeatherForecast {
    bool valid = false;
    float temperatureC24h = NAN;
    float rainMm24h = NAN;
    float rainProbability = NAN;
    float et0Mm24h = NAN;
};

struct EdgeAIPrediction {
    bool valid = false;
    uint16_t sensorId = 0;
    uint16_t actuatorId = 0;
    bool irrigationNeeded = false;
    bool irrigationNow = false;
    float irrigationProbability = 0.0f;
    float recommendedWaterMm = 0.0f;
    uint16_t totalRuntimeMin = 0;
    uint16_t cycleRuntimeMin = 0;
    uint8_t cycleCount = 0;
    uint16_t waitHours = 0;
    bool waitForRain = false;
    bool slowDrying = false;
    float dryingRatePctDay = 0.0f;
    EdgeAIWeatherForecast weather;
    WateringEffect wateringEffect = WateringEffect::NOT_AVAILABLE;
    float wateringMoistureIncreasePct = 0.0f;
    EdgeAIFault fault = EdgeAIFault::NORMAL;
    float faultConfidence = 0.0f;
    bool shadowMode = EDGE_AI_ALLOW_CONTROL == 0;
    char reason[192] = {};
};

class EdgeAIEngine {
public:
    static constexpr uint8_t MAX_ZONES = 16;
    static constexpr uint16_t MAX_CYCLE_RUNTIME_MIN = 30;

    void begin();
    void updateWeather(const EdgeAIWeatherReading& reading);
    void updateActuator(const EdgeAIActuatorReading& reading);
    EdgeAIPrediction predict(const EdgeAISensorReading& sensor,
                             const EdgeAIFieldProfile& field);

    EdgeAIWeatherForecast weatherForecast() const { return _forecast; }
    bool controlAllowed() const { return EDGE_AI_ALLOW_CONTROL != 0; }
    std::string predictionJson(const EdgeAIPrediction& prediction) const;
    std::string statusJson() const;

    static const char* faultName(EdgeAIFault fault);
    static const char* wateringEffectName(WateringEffect effect);

private:
    struct ZoneState {
        bool used = false;
        uint16_t sensorId = 0;
        uint16_t actuatorId = 0;
        float lastMoisturePct = NAN;
        uint64_t lastSensorEpochMs = 0;
        float trendAnchorPct = NAN;
        uint64_t trendAnchorEpochMs = 0;
        float dryingRatePctDay = 0.0f;
        bool dryingRateValid = false;
        EdgeAIActuatorReading actuator;
        bool actuatorValid = false;
        bool wateringActive = false;
        bool wateringPending = false;
        float preWaterMoisturePct = NAN;
        uint64_t wateringEndedEpochMs = 0;
        WateringEffect wateringEffect = WateringEffect::NOT_AVAILABLE;
        float wateringIncreasePct = 0.0f;
        EdgeAIPrediction lastPrediction;
    };

    struct DailyWeather {
        bool valid = false;
        uint32_t dayNumber = 0;
        float meanTempC = NAN;
        float meanHumidityPct = NAN;
        float meanPressureHpa = NAN;
        float meanWindMs = NAN;
        float rainMm = 0.0f;
        uint32_t samples = 0;
    };

    ZoneState _zones[MAX_ZONES]{};
    EdgeAIWeatherReading _latestWeather;
    bool _weatherValid = false;
    DailyWeather _currentDay;
    DailyWeather _weatherHistory[3]{};
    EdgeAIWeatherForecast _forecast;

    ZoneState& zoneFor(uint16_t sensorId);
    const ZoneState* findZone(uint16_t sensorId) const;
    void updateDryingState(ZoneState& zone, const EdgeAISensorReading& sensor);
    void updateWateringEffect(ZoneState& zone, const EdgeAISensorReading& sensor);
    void updateWeatherForecast();
    float cropCoefficient(const char* crop, const char* growthStage) const;
    float soilHoldingFactor(const char* soilType) const;
    float estimatedEt0() const;
    EdgeAIFault detectFault(const ZoneState& zone, const EdgeAISensorReading& sensor,
                            const EdgeAIFieldProfile& field, float& confidence) const;
};
