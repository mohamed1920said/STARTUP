#include "DeviceIdentity.h"

#include <Preferences.h>
#include <cctype>
#include <cstring>

const char* const DeviceIdentity::NVS_NAMESPACE = "dev_ident";
const char* const DeviceIdentity::NVS_KEY = "identity";

DeviceIdentity::DeviceIdentity(DeviceRole expectedRole) : _expectedRole(expectedRole) {}

IdentityState DeviceIdentity::begin() {
    return load();
}

IdentityState DeviceIdentity::load() {
    wipe(&_record, sizeof(_record));
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        _state = IdentityState::STORAGE_ERROR;
        return _state;
    }

    const size_t storedLength = prefs.getBytesLength(NVS_KEY);
    if (storedLength == 0) {
        prefs.end();
        _state = IdentityState::EMPTY;
        return _state;
    }
    if (storedLength != sizeof(_record) ||
        prefs.getBytes(NVS_KEY, &_record, sizeof(_record)) != sizeof(_record)) {
        prefs.end();
        wipe(&_record, sizeof(_record));
        _state = IdentityState::CORRUPT;
        return _state;
    }
    prefs.end();

    const uint32_t storedCrc = _record.crc32;
    _record.crc32 = 0;
    const uint32_t calculatedCrc = crc32(reinterpret_cast<const uint8_t*>(&_record), sizeof(_record));
    _record.crc32 = storedCrc;
    if (_record.magic != MAGIC || _record.version != VERSION || storedCrc != calculatedCrc ||
        _record.nodeId == 0 || _record.nodeId == 0xFFFF || !keyAllowed(_record.psk)) {
        wipe(&_record, sizeof(_record));
        _state = IdentityState::CORRUPT;
        return _state;
    }
    if (_record.role != static_cast<uint8_t>(_expectedRole)) {
        wipe(&_record, sizeof(_record));
        _state = IdentityState::ROLE_MISMATCH;
        return _state;
    }

    _state = IdentityState::READY;
    return _state;
}

bool DeviceIdentity::provision(uint16_t nodeId, const uint8_t psk[16]) {
    if (_state != IdentityState::EMPTY || nodeId == 0 || nodeId == 0xFFFF || !keyAllowed(psk)) return false;

    IdentityRecord candidate{};
    candidate.magic = MAGIC;
    candidate.version = VERSION;
    candidate.role = static_cast<uint8_t>(_expectedRole);
    candidate.nodeId = nodeId;
    memcpy(candidate.psk, psk, sizeof(candidate.psk));
    candidate.crc32 = 0;
    candidate.crc32 = crc32(reinterpret_cast<const uint8_t*>(&candidate), sizeof(candidate));

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        wipe(&candidate, sizeof(candidate));
        _state = IdentityState::STORAGE_ERROR;
        return false;
    }
    const bool written = prefs.putBytes(NVS_KEY, &candidate, sizeof(candidate)) == sizeof(candidate);
    prefs.end();
    wipe(&candidate, sizeof(candidate));
    if (!written) {
        _state = IdentityState::STORAGE_ERROR;
        return false;
    }
    return load() == IdentityState::READY &&
        _record.nodeId == nodeId &&
        _record.role == static_cast<uint8_t>(_expectedRole) &&
        memcmp(_record.psk, psk, sizeof(_record.psk)) == 0;
}

bool DeviceIdentity::eraseIdentity() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        _state = IdentityState::STORAGE_ERROR;
        return false;
    }
    const bool removed = prefs.remove(NVS_KEY);
    prefs.end();
    wipe(&_record, sizeof(_record));
    _state = removed ? IdentityState::EMPTY : IdentityState::STORAGE_ERROR;
    return removed;
}

IdentityEvent DeviceIdentity::serviceConsole(Stream& serial) {
    while (serial.available() > 0) {
        const char c = static_cast<char>(serial.read());
        if (c == '\r') continue;
        if (c != '\n') {
            if (_lineLength + 1 < sizeof(_line)) _line[_lineLength++] = c;
            else _lineOverflow = true;
            continue;
        }

        _line[_lineLength] = '\0';
        IdentityEvent event = IdentityEvent::NONE;
        if (_lineOverflow) serial.println(F("[IDENTITY] Command too long"));
        else if (_lineLength > 0) event = processLine(serial);
        wipe(_line, sizeof(_line));
        _lineLength = 0;
        _lineOverflow = false;
        if (event != IdentityEvent::NONE) return event;
    }
    return IdentityEvent::NONE;
}

IdentityEvent DeviceIdentity::processLine(Stream& serial) {
    char* save = nullptr;
    char* command = strtok_r(_line, " ", &save);
    if (!command) return IdentityEvent::NONE;

    if (strcmp(command, "STATUS") == 0 && strtok_r(nullptr, " ", &save) == nullptr) {
        printStatus(serial);
        return IdentityEvent::NONE;
    }

    if (strcmp(command, "PROVISION") == 0) {
        char* idText = strtok_r(nullptr, " ", &save);
        char* keyText = strtok_r(nullptr, " ", &save);
        char* extra = strtok_r(nullptr, " ", &save);
        uint16_t nodeId = 0;
        uint8_t key[16]{};
        const bool valid = !extra && parseNodeId(idText, nodeId) && parseKey(keyText, key);
        if (_state != IdentityState::EMPTY) {
            serial.println(F("[IDENTITY] Refused: erase the existing identity first"));
        } else if (!valid || !keyAllowed(key)) {
            serial.println(F("[IDENTITY] Invalid ID/key; use PROVISION 0001 <32 hex>"));
        } else if (provision(nodeId, key)) {
            serial.print(F("[IDENTITY] Provisioned node 0x"));
            if (nodeId < 0x1000) serial.print('0');
            if (nodeId < 0x0100) serial.print('0');
            if (nodeId < 0x0010) serial.print('0');
            serial.println(nodeId, HEX);
            wipe(key, sizeof(key));
            return IdentityEvent::PROVISIONED;
        } else {
            serial.println(F("[IDENTITY] Provisioning failed"));
        }
        wipe(key, sizeof(key));
        return IdentityEvent::NONE;
    }

    if (strcmp(command, "UNPROVISION") == 0) {
        char* idText = strtok_r(nullptr, " ", &save);
        char* confirmation = strtok_r(nullptr, " ", &save);
        char* extra = strtok_r(nullptr, " ", &save);
        uint16_t nodeId = 0;
        if (!ready() || extra || !parseNodeId(idText, nodeId) || nodeId != _record.nodeId ||
            !confirmation || strcmp(confirmation, "CONFIRM") != 0) {
            serial.println(F("[IDENTITY] Refused; use UNPROVISION <current 4-hex ID> CONFIRM"));
            return IdentityEvent::NONE;
        }
        if (eraseIdentity()) {
            serial.println(F("[IDENTITY] Identity erased; LoRa sequence state preserved"));
            return IdentityEvent::ERASED;
        }
        serial.println(F("[IDENTITY] Erase failed"));
        return IdentityEvent::NONE;
    }

    if (strcmp(command, "ERASE") == 0) {
        char* reason = strtok_r(nullptr, " ", &save);
        char* confirmation = strtok_r(nullptr, " ", &save);
        char* extra = strtok_r(nullptr, " ", &save);
        if ((_state != IdentityState::CORRUPT && _state != IdentityState::ROLE_MISMATCH) || extra ||
            !reason || strcmp(reason, "CORRUPT") != 0 || !confirmation || strcmp(confirmation, "CONFIRM") != 0) {
            serial.println(F("[IDENTITY] Refused; corrupt records require ERASE CORRUPT CONFIRM"));
            return IdentityEvent::NONE;
        }
        if (eraseIdentity()) {
            serial.println(F("[IDENTITY] Corrupt identity erased; LoRa sequence state preserved"));
            return IdentityEvent::ERASED;
        }
        serial.println(F("[IDENTITY] Erase failed"));
        return IdentityEvent::NONE;
    }

    serial.println(F("[IDENTITY] Commands: STATUS, PROVISION, UNPROVISION"));
    return IdentityEvent::NONE;
}

void DeviceIdentity::printStatus(Stream& serial) const {
    serial.print(F("[IDENTITY] role="));
    serial.print(_expectedRole == DeviceRole::SENSOR ? F("sensor") : F("actuator"));
    serial.print(F(" state="));
    serial.print(stateName(_state));
    if (ready()) {
        serial.print(F(" node=0x"));
        if (_record.nodeId < 0x1000) serial.print('0');
        if (_record.nodeId < 0x0100) serial.print('0');
        if (_record.nodeId < 0x0010) serial.print('0');
        serial.print(_record.nodeId, HEX);
    }
    const uint64_t chip = ESP.getEfuseMac();
    serial.print(F(" chip="));
    serial.print(static_cast<uint32_t>(chip >> 32), HEX);
    serial.println(static_cast<uint32_t>(chip), HEX);
}

uint32_t DeviceIdentity::crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
    }
    return ~crc;
}

bool DeviceIdentity::parseNodeId(const char* text, uint16_t& nodeId) {
    if (!text || strlen(text) != 4) return false;
    uint16_t value = 0;
    for (size_t i = 0; i < 4; ++i) {
        const char c = text[i];
        uint8_t nibble;
        if (c >= '0' && c <= '9') nibble = static_cast<uint8_t>(c - '0');
        else if (c >= 'a' && c <= 'f') nibble = static_cast<uint8_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') nibble = static_cast<uint8_t>(c - 'A' + 10);
        else return false;
        value = static_cast<uint16_t>((value << 4) | nibble);
    }
    if (value == 0 || value == 0xFFFF) return false;
    nodeId = value;
    return true;
}

bool DeviceIdentity::parseKey(const char* text, uint8_t psk[16]) {
    if (!text || strlen(text) != 32) return false;
    for (size_t i = 0; i < 16; ++i) {
        uint8_t value = 0;
        for (size_t half = 0; half < 2; ++half) {
            const char c = text[i * 2 + half];
            uint8_t nibble;
            if (c >= '0' && c <= '9') nibble = static_cast<uint8_t>(c - '0');
            else if (c >= 'a' && c <= 'f') nibble = static_cast<uint8_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') nibble = static_cast<uint8_t>(c - 'A' + 10);
            else { wipe(psk, 16); return false; }
            value = static_cast<uint8_t>((value << 4) | nibble);
        }
        psk[i] = value;
    }
    return true;
}

bool DeviceIdentity::keyAllowed(const uint8_t psk[16]) {
    if (!psk) return false;
    bool allZero = true;
    bool allFF = true;
    for (size_t i = 0; i < 16; ++i) {
        allZero = allZero && psk[i] == 0;
        allFF = allFF && psk[i] == 0xFF;
    }
    return !allZero && !allFF;
}

const char* DeviceIdentity::stateName(IdentityState state) {
    switch (state) {
        case IdentityState::EMPTY: return "empty";
        case IdentityState::READY: return "ready";
        case IdentityState::CORRUPT: return "corrupt";
        case IdentityState::ROLE_MISMATCH: return "role_mismatch";
        case IdentityState::STORAGE_ERROR: return "storage_error";
        default: return "unknown";
    }
}

void DeviceIdentity::wipe(void* data, size_t length) {
    volatile uint8_t* cursor = static_cast<volatile uint8_t*>(data);
    while (length--) *cursor++ = 0;
}
