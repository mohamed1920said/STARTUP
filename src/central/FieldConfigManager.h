#pragma once

#include <Arduino.h>
#include <string>

struct FieldConfig {
    uint16_t version;
    uint16_t nodeId;
    uint16_t moistureDryRaw;
    uint16_t moistureWetRaw;
    float zoneAreaM2;
    float emitterFlowLph;
    char crop[24];
    char growthStage[24];
    char soilType[24];
};

class FieldConfigManager {
public:
    static constexpr int MAX_CONFIGS = 16;
    static constexpr uint16_t CURRENT_VERSION = 1;

    void begin();
    const FieldConfig& get(uint16_t nodeId);
    bool update(uint16_t nodeId, uint16_t dryRaw, uint16_t wetRaw,
                const char* crop, const char* growthStage, const char* soilType,
                float zoneAreaM2, float emitterFlowLph);
    float calibrate(uint16_t nodeId, uint16_t raw);
    std::string toJson() const;

private:
    FieldConfig _configs[MAX_CONFIGS]{};
    int _count = 0;

    FieldConfig* find(uint16_t nodeId);
    const FieldConfig* find(uint16_t nodeId) const;
    FieldConfig makeDefault(uint16_t nodeId) const;
    void load(uint16_t nodeId, FieldConfig& cfg);
    bool save(const FieldConfig& cfg);
};
