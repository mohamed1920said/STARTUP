#pragma once
#include <WiFi.h>
#include <DNSServer.h>
#include <Preferences.h>

class AsyncWebServer;

class WiFiManager {
public:
    WiFiManager();
    void begin(AsyncWebServer& server);
    bool waitForConnection(unsigned long timeoutMs = 15000);
    bool isConnected();
    void resetSettings();
    String getSsid();
    IPAddress getLocalIP();
    void loop();
    void setResetPin(uint8_t pin, bool activeLow = true, unsigned long holdMs = 5000);

private:
    static const char* AP_SSID_PREFIX;
    static const char* NVS_NAMESPACE;
    static constexpr unsigned long RESET_HOLD_MS_DEFAULT = 5000;
    static constexpr uint8_t RESET_PIN_DEFAULT = 0;
    AsyncWebServer* _server = nullptr;
    DNSServer _dns;
    Preferences _prefs;
    bool _apMode = false;
    unsigned long _apStart = 0;

    bool loadCredentials(char* ssid, size_t ssidLen, char* pass, size_t passLen);
    bool loadStaticIP(IPAddress& ip, IPAddress& gw, IPAddress& mask, IPAddress& dns);
    void saveCredentials(const char* ssid, const char* pass);
    void saveStaticIP(const char* ip, const char* gw, const char* mask, const char* dns);
    void startAP();
    void tryConnect(const char* ssid, const char* pass, bool useStatic, const IPAddress& ip, const IPAddress& gw, const IPAddress& mask, const IPAddress& dns);
    void serveSetupPage();
    void handleSetupSubmit();
    String generateAPSSID();
    bool connectToSaved();
    void checkResetButton();
    void handleReset();

    bool _configDone = false;
    bool _pendingConnect = false;
    bool _connecting = false;
    IPAddress _configIP;

    uint8_t _resetPin = RESET_PIN_DEFAULT;
    bool _resetActiveLow = true;
    bool _resetPinConfigured = false;
    unsigned long _resetHoldMs = RESET_HOLD_MS_DEFAULT;
    unsigned long _resetPressStart = 0;
    bool _resetButtonHeld = false;
    bool _resetTriggered = false;
    bool _resetPendingRestart = false;
    int _resetLastState = HIGH;
    unsigned long _resetDebounceMs = 0;
};
