#include "TimeService.h"

#include <esp_system.h>
#include <sys/time.h>
#include <time.h>

namespace {
constexpr time_t MIN_VALID_EPOCH = 1704067200;  // 2024-01-01 UTC
}

void TimeService::begin() {
    _bootId = esp_random();
    // Tunisia uses CET (UTC+1) without a current daylight-saving transition.
    // Epoch timestamps remain UTC; localtime() is used only for farm scheduling.
    configTzTime("CET-1", "pool.ntp.org", "time.nist.gov", "time.google.com");
}

uint64_t TimeService::epochMillis() const {
    timeval tv{};
    if (gettimeofday(&tv, nullptr) != 0 || tv.tv_sec < MIN_VALID_EPOCH) return 0;
    return static_cast<uint64_t>(tv.tv_sec) * 1000ULL + static_cast<uint64_t>(tv.tv_usec / 1000);
}

bool TimeService::synchronized() const {
    return time(nullptr) >= MIN_VALID_EPOCH;
}
