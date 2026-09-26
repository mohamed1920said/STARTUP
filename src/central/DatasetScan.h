#pragma once

#include <cstddef>
#include <cstdint>

namespace DatasetScan {
struct Result {
    uint32_t records = 0;
    size_t bytesRead = 0;
    bool complete = false;
};

// Scan only the size captured when the file was opened. Read in small blocks
// and give the scheduler time at least every 4 KiB, including a short last block.
// A zero-byte read before the captured end is an error, never an infinite loop.
template <typename Reader, typename Yield>
Result countRecords(size_t size, Reader read, Yield yield) {
    Result result;
    uint8_t buffer[512];
    uint32_t newlines = 0;
    size_t sinceYield = 0;
    while (result.bytesRead < size) {
        const size_t remaining = size - result.bytesRead;
        size_t wanted = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        const size_t untilYield = 4096 - sinceYield;
        if (wanted > untilYield) wanted = untilYield;
        const size_t received = read(buffer, wanted);
        if (received == 0 || received > wanted) return result;
        for (size_t i = 0; i < received; ++i) {
            if (buffer[i] == '\n') ++newlines;
        }
        result.bytesRead += received;
        sinceYield += received;
        if (sinceYield >= 4096 || result.bytesRead == size) {
            yield();
            sinceYield = 0;
        }
    }
    // A final incomplete row from interrupted power is not a completed record.
    result.records = newlines > 0 ? newlines - 1 : 0;
    result.complete = true;
    return result;
}
}  // namespace DatasetScan
