#pragma once

#include <Arduino.h>

enum class DeviceRole : uint8_t {
    SENSOR = 0x01,
    ACTUATOR = 0x02
};

enum class IdentityState : uint8_t {
    EMPTY,
    READY,
    CORRUPT,
    ROLE_MISMATCH,
    STORAGE_ERROR
};

enum class IdentityEvent : uint8_t {
    NONE,
    PROVISIONED,
    ERASED
};

class DeviceIdentity {
public:
    explicit DeviceIdentity(DeviceRole expectedRole);

    IdentityState begin();
    bool ready() const { return _state == IdentityState::READY; }
    uint16_t nodeId() const { return ready() ? _record.nodeId : 0; }
    const uint8_t* psk() const { return ready() ? _record.psk : nullptr; }
    IdentityState state() const { return _state; }

    // Physical-serial provisioning. This parser never prints the PSK.
    IdentityEvent serviceConsole(Stream& serial);
    void printStatus(Stream& serial) const;

private:
    struct __attribute__((packed)) IdentityRecord {
        uint32_t magic;
        uint8_t version;
        uint8_t role;
        uint16_t nodeId;
        uint8_t psk[16];
        uint32_t crc32;
    };
    static_assert(sizeof(IdentityRecord) == 28, "Unexpected identity record layout");

    static constexpr uint32_t MAGIC = 0x414D5249UL; // "AMRI"
    static constexpr uint8_t VERSION = 1;
    static const char* const NVS_NAMESPACE;
    static const char* const NVS_KEY;

    DeviceRole _expectedRole;
    IdentityState _state = IdentityState::EMPTY;
    IdentityRecord _record{};
    char _line[80]{};
    size_t _lineLength = 0;
    bool _lineOverflow = false;

    IdentityState load();
    bool provision(uint16_t nodeId, const uint8_t psk[16]);
    bool eraseIdentity();
    IdentityEvent processLine(Stream& serial);

    static uint32_t crc32(const uint8_t* data, size_t length);
    static bool parseNodeId(const char* text, uint16_t& nodeId);
    static bool parseKey(const char* text, uint8_t psk[16]);
    static bool keyAllowed(const uint8_t psk[16]);
    static const char* stateName(IdentityState state);
    static void wipe(void* data, size_t length);
};
