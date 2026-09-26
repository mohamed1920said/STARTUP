#pragma once
#include <Arduino.h>

// Retain only diagnostic numbers across a software/watchdog reset, never keys.
// No automatic reboot, watchdog disabling or flash writes are performed here.
namespace RuntimeDiagnostics {
enum class Stage : uint32_t { BOOT, WIFI, MQTT, DASHBOARD, RADIO_PACKET, WEATHER, IDLE };
void begin();
void mark(Stage stage);
void report(uint32_t radioStack, uint32_t cloudStack, uint32_t loggerStack);
const char* resetName();
const char* previousStageName();
uint32_t previousUptimeMs();
uint32_t loopStackFree();
}
