#include "FieldConfigManager.h"

#include <Preferences.h>
#include <cstring>

namespace {
void escapeJson(const char* src, char* dst, size_t size) {
    size_t j = 0;
    for (size_t i = 0; src && src[i] && j + 1 < size; ++i) {
        const char c = src[i];
        if (c == '"' || c == '\\') {
            if (j + 2 >= size) break;
            dst[j++] = '\\';
            dst[j++] = c;
        } else if (static_cast<unsigned char>(c) >= 0x20) {
            dst[j++] = c;
        }
    }
    dst[j] = '\0';
}
}

void FieldConfigManager::begin() {
    _count = 0;
}

FieldConfig FieldConfigManager::makeDefault(uint16_t nodeId) const {
    FieldConfig cfg{};
    cfg.version = CURRENT_VERSION;
    cfg.nodeId = nodeId;
    cfg.moistureDryRaw = 2500;
    cfg.moistureWetRaw = 400;
    strlcpy(cfg.crop, "unknown", sizeof(cfg.crop));
    strlcpy(cfg.growthStage, "unknown", sizeof(cfg.growthStage));
    strlcpy(cfg.soilType, "unknown", sizeof(cfg.soilType));
    return cfg;
}

void FieldConfigManager::load(uint16_t nodeId, FieldConfig& cfg) {
    Preferences prefs;
    if (!prefs.begin("field_cfg", true)) return;
    char key[12];
    snprintf(key, sizeof(key), "f_%04X", nodeId);
    if (prefs.getBytesLength(key) == sizeof(FieldConfig)) {
        FieldConfig stored{};
        if (prefs.getBytes(key, &stored, sizeof(stored)) == sizeof(stored) &&
            stored.version == CURRENT_VERSION && stored.nodeId == nodeId) {
            cfg = stored;
            cfg.crop[sizeof(cfg.crop) - 1] = '\0';
            cfg.growthStage[sizeof(cfg.growthStage) - 1] = '\0';
            cfg.soilType[sizeof(cfg.soilType) - 1] = '\0';
        }
    }
    prefs.end();
}

bool FieldConfigManager::save(const FieldConfig& cfg) {
    Preferences prefs;
    if (!prefs.begin("field_cfg", false)) return false;
    char key[12];
    snprintf(key, sizeof(key), "f_%04X", cfg.nodeId);
    const bool ok = prefs.putBytes(key, &cfg, sizeof(cfg)) == sizeof(cfg);
    prefs.end();
    return ok;
}

FieldConfig* FieldConfigManager::find(uint16_t nodeId) {
    for (int i = 0; i < _count; ++i) if (_configs[i].nodeId == nodeId) return &_configs[i];
    return nullptr;
}

const FieldConfig* FieldConfigManager::find(uint16_t nodeId) const {
    for (int i = 0; i < _count; ++i) if (_configs[i].nodeId == nodeId) return &_configs[i];
    return nullptr;
}

const FieldConfig& FieldConfigManager::get(uint16_t nodeId) {
    FieldConfig* existing = find(nodeId);
    if (existing) return *existing;
    if (_count >= MAX_CONFIGS) return _configs[0];
    _configs[_count] = makeDefault(nodeId);
    load(nodeId, _configs[_count]);
    return _configs[_count++];
}

bool FieldConfigManager::update(uint16_t nodeId, uint16_t dryRaw, uint16_t wetRaw,
                                const char* crop, const char* growthStage, const char* soilType,
                                float zoneAreaM2, float emitterFlowLph) {
    if (nodeId == 0 || dryRaw > 4095 || wetRaw > 4095 || dryRaw == wetRaw ||
        !crop || !growthStage || !soilType || strlen(crop) >= 24 ||
        strlen(growthStage) >= 24 || strlen(soilType) >= 24 ||
        zoneAreaM2 < 0 || emitterFlowLph < 0) return false;
    get(nodeId);
    FieldConfig* cfg = find(nodeId);
    if (!cfg) return false;
    cfg->moistureDryRaw = dryRaw;
    cfg->moistureWetRaw = wetRaw;
    cfg->zoneAreaM2 = zoneAreaM2;
    cfg->emitterFlowLph = emitterFlowLph;
    strlcpy(cfg->crop, crop, sizeof(cfg->crop));
    strlcpy(cfg->growthStage, growthStage, sizeof(cfg->growthStage));
    strlcpy(cfg->soilType, soilType, sizeof(cfg->soilType));
    return save(*cfg);
}

float FieldConfigManager::calibrate(uint16_t nodeId, uint16_t raw) {
    const FieldConfig& cfg = get(nodeId);
    const float span = static_cast<float>(cfg.moistureWetRaw) - cfg.moistureDryRaw;
    if (fabsf(span) < 1.0f) return NAN;
    const float pct = (static_cast<float>(raw) - cfg.moistureDryRaw) * 100.0f / span;
    return constrain(pct, 0.0f, 100.0f);
}

std::string FieldConfigManager::toJson() const {
    std::string json = "[";
    for (int i = 0; i < _count; ++i) {
        if (i) json += ',';
        char crop[64], stage[64], soil[64], row[384];
        escapeJson(_configs[i].crop, crop, sizeof(crop));
        escapeJson(_configs[i].growthStage, stage, sizeof(stage));
        escapeJson(_configs[i].soilType, soil, sizeof(soil));
        snprintf(row, sizeof(row),
                 R"({"node_id":%u,"dry_raw":%u,"wet_raw":%u,"crop":"%s","growth_stage":"%s","soil_type":"%s","zone_area_m2":%.2f,"emitter_flow_lph":%.2f})",
                 _configs[i].nodeId, _configs[i].moistureDryRaw, _configs[i].moistureWetRaw,
                 crop, stage, soil, _configs[i].zoneAreaM2, _configs[i].emitterFlowLph);
        json += row;
    }
    json += ']';
    return json;
}
