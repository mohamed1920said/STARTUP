#include "DataLogger.h"
#include "DatasetScan.h"

#include <LittleFS.h>
#include <cmath>
#include <cstring>

namespace {
// Two 1.25 MiB segments leave room for dashboard assets and LittleFS overhead
// in the custom 3.375 MiB gateway filesystem partition.
constexpr size_t MAX_DATASET_BYTES = 1280U * 1024U;
constexpr char CSV_HEADER[] =
    "schema_version,record_type,epoch_ms,uptime_ms,boot_id,node_id,linked_node_id,sequence,"
    "moisture_raw,moisture_pct,soil_temp_c,air_temp_c,humidity_pct,pressure_hpa,"
    "rain_mm,wind_ms,wind_dir_deg,luminosity_lux,gateway_battery_mv,node_battery_v,"
    "rssi_dbm,valve_commanded,valve_actual,valve_feedback_valid,command_result,command_id,command_source,command_timeout_s,"
    "flow_lpm,line_pressure_bar,tank_pct,pump_current_a,error_flags,crop,growth_stage,"
    "soil_type,zone_area_m2,emitter_flow_lph,label,notes,ai_model_version,ai_shadow_mode,"
    "ai_irrigation_probability,ai_irrigation_needed,ai_irrigation_now,ai_recommended_water_mm,"
    "ai_runtime_min,ai_total_runtime_min,ai_wait_hours,ai_rain_probability,ai_forecast_rain_mm,"
    "ai_forecast_temp_c,ai_forecast_et0_mm,ai_drying_rate_pct_day,ai_watering_effect,"
    "ai_watering_increase_pct,ai_fault,ai_fault_confidence\n";
}

bool DataLogger::begin() {
    if (_task) return true;
    if (!LittleFS.begin(false)) return false;
    auto releaseInitialization = [this]() {
        if (_queue) { vQueueDelete(_queue); _queue = nullptr; }
        if (_fileMutex) { vSemaphoreDelete(_fileMutex); _fileMutex = nullptr; }
        _task = nullptr;
        return false;
    };
    _fileMutex = xSemaphoreCreateMutex();
    _queue = xQueueCreate(48, sizeof(DatasetRecord));
    if (!_fileMutex || !_queue || !ensureHeader()) return releaseInitialization();
    File existing = LittleFS.open(DATASET_PATH, FILE_READ);
    if (!existing) return releaseInitialization();
    const auto scan = DatasetScan::countRecords(existing.size(),
        [&existing](uint8_t* buffer, size_t length) { return existing.read(buffer, length); },
        []() { vTaskDelay(1); });
    existing.close();
    if (!scan.complete) return releaseInitialization();
    _recordCount = scan.records;
    if (xTaskCreatePinnedToCore(taskEntry, "data_logger", 6144, this, 1, &_task, 0) != pdPASS) {
        return releaseInitialization();
    }
    return true;
}

DatasetRecord DataLogger::makeRecord(const char* type, uint64_t epochMs,
                                     uint32_t uptimeMs, uint32_t bootId) const {
    DatasetRecord r{};
    strlcpy(r.recordType, type ? type : "unknown", sizeof(r.recordType));
    r.epochMs = epochMs;
    r.uptimeMs = uptimeMs;
    r.bootId = bootId;
    r.moistureRaw = -1;
    r.moisturePct = r.soilTempC = r.airTempC = r.humidityPct = NAN;
    r.pressureHpa = r.rainMm = r.windMs = r.windDirDeg = NAN;
    r.luminosityLux = r.gatewayBatteryMv = r.nodeBatteryV = NAN;
    r.rssiDbm = INT16_MIN;
    r.valveCommanded = r.valveActual = r.valveFeedbackValid = r.commandResult = -1;
    r.flowLpm = r.linePressureBar = r.tankPct = r.pumpCurrentA = NAN;
    r.zoneAreaM2 = r.emitterFlowLph = NAN;
    r.aiShadowMode = r.aiIrrigationNeeded = r.aiIrrigationNow = -1;
    r.aiIrrigationProbability = r.aiRecommendedWaterMm = NAN;
    r.aiRainProbability = r.aiForecastRainMm = r.aiForecastTempC = NAN;
    r.aiForecastEt0Mm = r.aiDryingRatePctDay = NAN;
    r.aiWateringIncreasePct = r.aiFaultConfidence = NAN;
    return r;
}

void DataLogger::sanitize(char* value) {
    if (!value) return;
    for (size_t i = 0; value[i]; ++i) {
        if (value[i] == ',' || value[i] == '"' || value[i] == '\r' || value[i] == '\n') value[i] = ' ';
    }
}

bool DataLogger::enqueue(const DatasetRecord& input) {
    if (!_queue) return false;
    DatasetRecord record = input;
    sanitize(record.recordType);
    sanitize(record.commandSource);
    sanitize(record.crop);
    sanitize(record.growthStage);
    sanitize(record.soilType);
    sanitize(record.label);
    sanitize(record.notes);
    sanitize(record.aiWateringEffect);
    sanitize(record.aiFault);
    if (xQueueSend(_queue, &record, 0) != pdTRUE) {
        ++_droppedCount;
        return false;
    }
    return true;
}

bool DataLogger::addLabel(const char* label, const char* notes, uint64_t epochMs,
                          uint32_t uptimeMs, uint32_t bootId) {
    if (!label || !label[0] || strlen(label) >= 24 || (notes && strlen(notes) >= 80)) return false;
    DatasetRecord r = makeRecord("label", epochMs, uptimeMs, bootId);
    strlcpy(r.label, label, sizeof(r.label));
    strlcpy(r.notes, notes ? notes : "", sizeof(r.notes));
    return enqueue(r);
}

void DataLogger::taskEntry(void* arg) {
    static_cast<DataLogger*>(arg)->taskLoop();
}

void DataLogger::taskLoop() {
    DatasetRecord record;
    while (true) {
        if (xQueueReceive(_queue, &record, portMAX_DELAY) == pdTRUE) append(record);
    }
}

bool DataLogger::ensureHeader() {
    if (LittleFS.exists(DATASET_PATH)) {
        File existing = LittleFS.open(DATASET_PATH, FILE_READ);
        if (!existing) return false;
        // A corrupt file without a newline must not allocate the whole dataset.
        char headerBuffer[sizeof(CSV_HEADER)]{};
        const size_t length = existing.readBytesUntil('\n', headerBuffer, sizeof(headerBuffer) - 1);
        headerBuffer[length] = '\0';
        existing.close();
        String header(headerBuffer);
        String expected(CSV_HEADER);
        expected.trim();
        header.trim();
        if (header == expected) return true;

        // Preserve the previous-schema file as the downloadable archive.  Mixing
        // row widths in one CSV would silently corrupt future model training.
        LittleFS.remove(ARCHIVE_PATH);
        if (!LittleFS.rename(DATASET_PATH, ARCHIVE_PATH)) return false;
    }
    File file = LittleFS.open(DATASET_PATH, FILE_WRITE);
    if (!file) return false;
    const size_t written = file.print(CSV_HEADER);
    file.close();
    return written == strlen(CSV_HEADER);
}

void DataLogger::rotateIfNeeded() {
    File current = LittleFS.open(DATASET_PATH, FILE_READ);
    const size_t size = current ? current.size() : 0;
    if (current) current.close();
    if (size < MAX_DATASET_BYTES) return;
    LittleFS.remove(ARCHIVE_PATH);
    LittleFS.rename(DATASET_PATH, ARCHIVE_PATH);
    ensureHeader();
    _recordCount = 0;
}

bool DataLogger::append(const DatasetRecord& r) {
    if (!_fileMutex || xSemaphoreTake(_fileMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ++_droppedCount;
        return false;
    }
    rotateIfNeeded();
    File file = LittleFS.open(DATASET_PATH, FILE_APPEND);
    if (!file) {
        xSemaphoreGive(_fileMutex);
        ++_droppedCount;
        return false;
    }
    const int written = file.printf(
        "3,%s,%llu,%lu,%08lX,%u,%u,%lu,%ld,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%.3f,%d,%d,%d,%d,%d,%lu,%s,%u,%.3f,%.3f,%.3f,%.3f,%u,%s,%s,%s,%.3f,%.3f,%s,%s,%lu,%d,%.5f,%d,%d,%.3f,%u,%u,%u,%.5f,%.3f,%.3f,%.3f,%.3f,%s,%.3f,%s,%.5f\n",
        r.recordType, static_cast<unsigned long long>(r.epochMs),
        static_cast<unsigned long>(r.uptimeMs), static_cast<unsigned long>(r.bootId),
        r.nodeId, r.linkedNodeId, static_cast<unsigned long>(r.sequence), static_cast<long>(r.moistureRaw),
        r.moisturePct, r.soilTempC, r.airTempC, r.humidityPct, r.pressureHpa,
        r.rainMm, r.windMs, r.windDirDeg, r.luminosityLux, r.gatewayBatteryMv,
        r.nodeBatteryV, r.rssiDbm == INT16_MIN ? 0 : r.rssiDbm,
        r.valveCommanded, r.valveActual, r.valveFeedbackValid, r.commandResult,
        static_cast<unsigned long>(r.commandId), r.commandSource, r.commandTimeoutS,
        r.flowLpm, r.linePressureBar, r.tankPct, r.pumpCurrentA, r.errorFlags,
        r.crop, r.growthStage, r.soilType, r.zoneAreaM2, r.emitterFlowLph,
        r.label, r.notes, static_cast<unsigned long>(r.aiModelVersion), r.aiShadowMode,
        r.aiIrrigationProbability, r.aiIrrigationNeeded, r.aiIrrigationNow,
        r.aiRecommendedWaterMm, r.aiRuntimeMin, r.aiTotalRuntimeMin, r.aiWaitHours,
        r.aiRainProbability, r.aiForecastRainMm, r.aiForecastTempC, r.aiForecastEt0Mm,
        r.aiDryingRatePctDay, r.aiWateringEffect, r.aiWateringIncreasePct,
        r.aiFault, r.aiFaultConfidence);
    file.flush();
    file.close();
    if (written <= 0) {
        ++_droppedCount;
        xSemaphoreGive(_fileMutex);
        return false;
    }
    ++_recordCount;
    xSemaphoreGive(_fileMutex);
    return true;
}

bool DataLogger::clear() {
    if (!_fileMutex || xSemaphoreTake(_fileMutex, pdMS_TO_TICKS(3000)) != pdTRUE) return false;
    if (_queue) xQueueReset(_queue);
    LittleFS.remove(DATASET_PATH);
    LittleFS.remove(ARCHIVE_PATH);
    const bool ok = ensureHeader();
    if (ok) {
        _recordCount = 0;
        _droppedCount = 0;
    }
    xSemaphoreGive(_fileMutex);
    return ok;
}

std::string DataLogger::statusJson() const {
    char buf[304];
    if (!_fileMutex || xSemaphoreTake(_fileMutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        snprintf(buf, sizeof(buf),
                 R"({"ready":%s,"busy":true,"records":%lu,"dropped":%lu,"bytes":null,"archive_bytes":null,"segment_limit":%u,"path":"%s"})",
                 _task ? "true" : "false", static_cast<unsigned long>(_recordCount.load()),
                 static_cast<unsigned long>(_droppedCount.load()),
                 static_cast<unsigned>(MAX_DATASET_BYTES), DATASET_PATH);
        return std::string(buf);
    }
    size_t bytes = 0;
    size_t archiveBytes = 0;
    File file = LittleFS.open(DATASET_PATH, FILE_READ);
    if (file) {
        bytes = file.size();
        file.close();
    }
    file = LittleFS.open(ARCHIVE_PATH, FILE_READ);
    if (file) {
        archiveBytes = file.size();
        file.close();
    }
    const uint32_t records = _recordCount.load();
    const uint32_t dropped = _droppedCount.load();
    xSemaphoreGive(_fileMutex);
    snprintf(buf, sizeof(buf),
             R"({"ready":%s,"busy":false,"records":%lu,"dropped":%lu,"bytes":%u,"archive_bytes":%u,"segment_limit":%u,"path":"%s"})",
             _task ? "true" : "false", static_cast<unsigned long>(records), static_cast<unsigned long>(dropped),
             static_cast<unsigned>(bytes), static_cast<unsigned>(archiveBytes),
             static_cast<unsigned>(MAX_DATASET_BYTES), DATASET_PATH);
    return std::string(buf);
}

uint32_t DataLogger::stackHighWaterMark() const {
    return _task ? uxTaskGetStackHighWaterMark(_task) : 0;
}
