#pragma once

#include <Arduino.h>

class TimeService {
public:
    void begin();
    uint64_t epochMillis() const;
    bool synchronized() const;
    uint32_t bootId() const { return _bootId; }

private:
    uint32_t _bootId = 0;
};
