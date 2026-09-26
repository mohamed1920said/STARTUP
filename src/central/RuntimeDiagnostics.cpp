#include "RuntimeDiagnostics.h"
#include <esp_system.h>
#include <esp_heap_caps.h>

namespace RuntimeDiagnostics {
namespace {
constexpr uint32_t MAGIC = 0x414D5202;
struct Retained { uint32_t magic, stage, uptime; };
RTC_NOINIT_ATTR Retained retained;
Retained previous{};
uint32_t lastReport = 0;
uint32_t loopFree = 0;
const char* stageName(uint32_t stage) {
    switch (static_cast<Stage>(stage)) {
        case Stage::BOOT: return "boot";
        case Stage::WIFI: return "wifi";
        case Stage::MQTT: return "mqtt";
        case Stage::DASHBOARD: return "dashboard";
        case Stage::RADIO_PACKET: return "radio_packet";
        case Stage::WEATHER: return "weather";
        case Stage::IDLE: return "idle";
        default: return "unknown";
    }
}
}
const char* resetName() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "power_on_or_reset_pin";
        case ESP_RST_EXT: return "external_reset";
        case ESP_RST_SW: return "software_restart";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "interrupt_watchdog";
        case ESP_RST_TASK_WDT: return "task_watchdog";
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deep_sleep";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_SDIO: return "sdio";
        default: return "unknown";
    }
}
void begin() {
    if (esp_reset_reason() != ESP_RST_POWERON && retained.magic == MAGIC &&
        retained.stage <= static_cast<uint32_t>(Stage::IDLE)) previous = retained;
    Serial.printf("[GW] Reset reason=%d (%s), previous_stage=%s, previous_uptime_ms=%lu\n",
        static_cast<int>(esp_reset_reason()), resetName(), previousStageName(),
        static_cast<unsigned long>(previousUptimeMs()));
    retained = {MAGIC, static_cast<uint32_t>(Stage::BOOT), millis()};
}
void mark(Stage stage) { retained.stage = static_cast<uint32_t>(stage); retained.uptime = millis(); }
const char* previousStageName() { return previous.magic == MAGIC ? stageName(previous.stage) : "unavailable"; }
uint32_t previousUptimeMs() { return previous.magic == MAGIC ? previous.uptime : 0; }
uint32_t loopStackFree() { return loopFree; }
void report(uint32_t radioStack, uint32_t cloudStack, uint32_t loggerStack) {
    if (millis() - lastReport < 30000UL) return;
    lastReport = millis();
    loopFree = uxTaskGetStackHighWaterMark(nullptr);
    Serial.printf("[GW] Health uptime=%lus heap=%u min_heap=%u largest=%u stack_free(loop/radio/cloud/logger)=%u/%u/%u/%u\n",
        static_cast<unsigned long>(millis()/1000), ESP.getFreeHeap(), ESP.getMinFreeHeap(),
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), loopFree, radioStack, cloudStack, loggerStack);
}
}
