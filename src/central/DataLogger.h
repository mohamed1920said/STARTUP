#pragma once

#include <Arduino.h>
#include <string>

struct DatasetRecord {
    char recordType[16];
    uint64_t epochMs;
    uint32_t uptimeMs;
    uint32_t bootId;
    uint16_t nodeId;
    uint16_t linkedNodeId;
    uint32_t sequence;
    int32_t moistureRaw;
    float moisturePct;
    float soilTempC;
    float airTempC;
    float humidityPct;
    float pressureHpa;
    float rainMm;
    float windMs;
    float windDirDeg;
    float luminosityLux;
    float gatewayBatteryMv;
    float nodeBatteryV;
    int16_t rssiDbm;
    int8_t valveCommanded;
    int8_t valveActual;
    int8_t valveFeedbackValid;
    int8_t commandResult;
    uint32_t commandId;
    uint16_t commandTimeoutS;
    char commandSource[16];
    float flowLpm;
    float linePressureBar;
    float tankPct;
    float pumpCurrentA;
    uint8_t errorFlags;
    char crop[24];
    char growthStage[24];
    char soilType[24];
    float zoneAreaM2;
    float emitterFlowLph;
    char label[24];
    char notes[80];
    uint32_t aiModelVersion;
    int8_t aiShadowMode;
    float aiIrrigationProbability;
    int8_t aiIrrigationNeeded;
    int8_t aiIrrigationNow;
    float aiRecommendedWaterMm;
    uint16_t aiRuntimeMin;
    uint16_t aiTotalRuntimeMin;
    uint16_t aiWaitHours;
    float aiRainProbability;
    float aiForecastRainMm;
    float aiForecastTempC;
    float aiForecastEt0Mm;
    float aiDryingRatePctDay;
    char aiWateringEffect[24];
    float aiWateringIncreasePct;
    char aiFault[24];
    float aiFaultConfidence;
};

class DataLogger {
public:
    static constexpr const char* DATASET_PATH = "/dataset.csv";
    static constexpr const char* ARCHIVE_PATH = "/dataset_previous.csv";

    bool begin();
    DatasetRecord makeRecord(const char* type, uint64_t epochMs,
                             uint32_t uptimeMs, uint32_t bootId) const;
    bool enqueue(const DatasetRecord& record);
    bool addLabel(const char* label, const char* notes, uint64_t epochMs,
                  uint32_t uptimeMs, uint32_t bootId);
    bool clear();
    std::string statusJson() const;

private:
    QueueHandle_t _queue = nullptr;
    SemaphoreHandle_t _fileMutex = nullptr;
    TaskHandle_t _task = nullptr;
    volatile uint32_t _recordCount = 0;
    volatile uint32_t _droppedCount = 0;

    static void taskEntry(void* arg);
    void taskLoop();
    bool append(const DatasetRecord& record);
    bool ensureHeader();
    void rotateIfNeeded();
    static void sanitize(char* value);
};
