#pragma once
#include <stddef.h>
#include <stdint.h>

// Fixed-size, allocation-free last-value cache. The caller owns synchronization.
// This is a live-boot snapshot, never a fabricated reading or persistent history.
template <typename T, size_t Capacity>
class TelemetryCache {
public:
    struct Entry { bool used = false; T value{}; };
    Entry entries[Capacity]{};

    void put(const T& value, uint32_t now) {
        size_t slot = 0;
        uint32_t oldestAge = 0;
        for (size_t i = 0; i < Capacity; ++i) {
            if (entries[i].used && entries[i].value.node_id == value.node_id) {
                entries[i].value = value;
                return;
            }
        }
        for (size_t i = 0; i < Capacity; ++i) {
            if (!entries[i].used) { slot = i; break; }
            const uint32_t age = now - entries[i].value.timestamp;
            if (age >= oldestAge) { oldestAge = age; slot = i; }
        }
        entries[slot].used = true;
        entries[slot].value = value;
    }

    void remove(uint16_t nodeId) {
        for (auto& entry : entries) if (entry.used && entry.value.node_id == nodeId) entry.used = false;
    }
};
