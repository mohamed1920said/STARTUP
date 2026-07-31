#include "WiFiManager.h"
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <esp_system.h>
#include "StartupLogo.h"

const char* WiFiManager::AP_SSID_PREFIX = "STARTUP-Gateway-";
const char* WiFiManager::NVS_NAMESPACE  = "wifi_cfg";

WiFiManager::WiFiManager() {
    setResetPin(RESET_PIN_DEFAULT, true, RESET_HOLD_MS_DEFAULT);
}

void WiFiManager::begin(AsyncWebServer& server) {
    _server = &server;
    _prefs.begin(NVS_NAMESPACE, false);
    ensureAdminCredentials();

    char ssid[33] = {0}, pass[65] = {0};
    bool hasCreds = loadCredentials(ssid, sizeof(ssid), pass, sizeof(pass));

    if (!hasCreds) {
        Serial.println(F("[WM] No saved WiFi config. Starting AP mode."));
        startAP();
        return;
    }

    IPAddress ip, gw, mask, dns;
    bool useStatic = loadStaticIP(ip, gw, mask, dns);

    tryConnect(ssid, pass, useStatic, ip, gw, mask, dns);
}

void WiFiManager::ensureAdminCredentials() {
    String saved = _prefs.getString("adminPass", "");
    if (saved.length() >= 8 && saved.length() < sizeof(_adminPass)) {
        strlcpy(_adminPass, saved.c_str(), sizeof(_adminPass));
        return;
    }
    uint32_t a = esp_random();
    uint32_t b = esp_random();
    snprintf(_adminPass, sizeof(_adminPass), "%08lX%08lX",
             (unsigned long)a, (unsigned long)b);
    _prefs.putString("adminPass", _adminPass);
    Serial.printf("[WM] New dashboard login: user=%s password=%s\n", _adminUser, _adminPass);
    Serial.println(F("[WM] Save this password, or replace it during WiFi setup."));
}

void WiFiManager::saveAdminPassword(const char* password) {
    if (!password || strlen(password) < 8 || strlen(password) >= sizeof(_adminPass)) return;
    strlcpy(_adminPass, password, sizeof(_adminPass));
    _prefs.putString("adminPass", _adminPass);
}

bool WiFiManager::waitForConnection(unsigned long timeoutMs) {
    if (_apMode) return false;
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(100);
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WM] Connected @ %s\n", WiFi.localIP().toString().c_str());
        return true;
    }
    Serial.println(F("[WM] Connection timeout. Starting AP mode."));
    startAP();
    return false;
}

bool WiFiManager::isConnected() {
    return !_apMode && WiFi.status() == WL_CONNECTED;
}

void WiFiManager::resetSettings() {
    _prefs.clear();
    _configDone = false;
    _pendingConnect = false;
    _connecting = false;
    _resetPendingRestart = false;
    Serial.println(F("[WM] WiFi settings cleared."));
}

String WiFiManager::getSsid() {
    return _apMode ? WiFi.softAPSSID() : WiFi.SSID();
}

IPAddress WiFiManager::getLocalIP() {
    return _apMode ? WiFi.softAPIP() : WiFi.localIP();
}

void WiFiManager::loop() {
    checkResetButton();
    if (_resetPendingRestart) {
        static unsigned long restartAt = 0;
        static unsigned long lastBlink = 0;
        static bool ledOn = false;
        if (restartAt == 0) restartAt = millis() + 5000;
        if (millis() - lastBlink >= 250) {
            lastBlink = millis();
            ledOn = !ledOn;
#ifdef LED_BUILTIN
            digitalWrite(LED_BUILTIN, ledOn ? LOW : HIGH);
#endif
        }
        if (millis() >= restartAt) {
#ifdef LED_BUILTIN
            digitalWrite(LED_BUILTIN, HIGH);
#endif
            _prefs.end();
            ESP.restart();
        }
        return;
    }
    if (_apMode) {
        _dns.processNextRequest();
        if (_pendingConnect) {
            _pendingConnect = false;
            _connecting = true;
            connectToSaved();
        }
        if (_configDone) {
            static unsigned long restartAt = 0;
            if (restartAt == 0) restartAt = millis() + 30000;
            if (millis() >= restartAt) {
                _prefs.end();
                ESP.restart();
            }
        }
    }
}

void WiFiManager::setResetPin(uint8_t pin, bool activeLow, unsigned long holdMs) {
    _resetPin = pin;
    _resetActiveLow = activeLow;
    _resetHoldMs = holdMs;
    _resetPinConfigured = true;
    pinMode(_resetPin, INPUT_PULLUP);
    _resetLastState = digitalRead(_resetPin);
    Serial.printf("[WM] Reset button on GPIO%u (%s, %ums hold)\n",
                  pin, activeLow ? "active LOW" : "active HIGH", holdMs);
}

void WiFiManager::checkResetButton() {
    if (!_resetPinConfigured) return;
    int raw = digitalRead(_resetPin);
    unsigned long now = millis();
    if (raw != _resetLastState) {
        _resetDebounceMs = now;
        _resetLastState = raw;
    }
    if (now - _resetDebounceMs < 50) return;
    int activeState = _resetActiveLow ? LOW : HIGH;
    bool pressed = (raw == activeState);
    if (pressed && !_resetButtonHeld) {
        _resetPressStart = now;
        _resetButtonHeld = true;
    }
    if (!pressed && _resetButtonHeld) {
        _resetButtonHeld = false;
        _resetPressStart = 0;
    }
    if (_resetButtonHeld && !_resetTriggered && (now - _resetPressStart) >= _resetHoldMs) {
        _resetTriggered = true;
        Serial.println(F("[WM] Reset button held long enough. Clearing settings..."));
        handleReset();
    }
}

void WiFiManager::handleReset() {
    _prefs.clear();
    _configDone = false;
    _pendingConnect = false;
    _connecting = false;
    _resetPendingRestart = true;
    Serial.println(F("[WM] WiFi credentials cleared. Restarting in 5s - release the button..."));
}

bool WiFiManager::loadCredentials(char* ssid, size_t ssidLen, char* pass, size_t passLen) {
    String s = _prefs.getString("ssid", "");
    String p = _prefs.getString("pass", "");
    if (s.length() == 0 || s.length() >= ssidLen) return false;
    strncpy(ssid, s.c_str(), ssidLen - 1);
    strncpy(pass, p.c_str(), passLen - 1);
    return true;
}

bool WiFiManager::loadStaticIP(IPAddress& ip, IPAddress& gw, IPAddress& mask, IPAddress& dns) {
    String sip = _prefs.getString("ip", "");
    if (sip.length() == 0) return false;
    ip.fromString(sip);
    gw.fromString(_prefs.getString("gw", ""));
    mask.fromString(_prefs.getString("mask", "255.255.255.0"));
    dns.fromString(_prefs.getString("dns", "8.8.8.8"));
    return true;
}

void WiFiManager::saveCredentials(const char* ssid, const char* pass) {
    _prefs.putString("ssid", ssid);
    _prefs.putString("pass", pass);
    Serial.printf("[WM] Saved WiFi credentials for %s\n", ssid);
}

void WiFiManager::saveStaticIP(const char* ip, const char* gw, const char* mask, const char* dns) {
    if (strlen(ip) > 0) {
        _prefs.putString("ip", ip);
        _prefs.putString("gw", gw);
        _prefs.putString("mask", mask);
        _prefs.putString("dns", dns);
    } else {
        _prefs.remove("ip");
        _prefs.remove("gw");
        _prefs.remove("mask");
        _prefs.remove("dns");
    }
}

String WiFiManager::generateAPSSID() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char apName[32];
    snprintf(apName, sizeof(apName), "%s%02X%02X%02X", AP_SSID_PREFIX, mac[3], mac[4], mac[5]);
    return String(apName);
}

void WiFiManager::startAP() {
    _apMode = true;
    String apSsid = generateAPSSID();
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP(apSsid.c_str(), nullptr, 1, 0, 1);
    Serial.printf("[WM] Open AP started: %s @ 192.168.4.1 (no password)\n",
                  apSsid.c_str());

    _dns.start(53, "*", IPAddress(192, 168, 4, 1));

    serveSetupPage();
    handleSetupSubmit();
}

bool WiFiManager::connectToSaved() {
    char ssid[33] = {0}, pass[65] = {0};
    if (!loadCredentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
        _connecting = false;
        return false;
    }
    IPAddress ip, gw, mask, dns;
    bool useStatic = loadStaticIP(ip, gw, mask, dns);

    Serial.printf("[WM] Connecting to %s ... ", ssid);
    WiFi.mode(WIFI_AP_STA);
    if (useStatic) {
        WiFi.config(ip, gw, mask, dns);
        Serial.printf("(static IP %s) ", ip.toString().c_str());
    }
    WiFi.begin(ssid, pass);

    for (int i = 0; i < 30; i++) {
        if (WiFi.status() == WL_CONNECTED) {
            _configIP = WiFi.localIP();
            _configDone = true;
            _connecting = false;
            Serial.printf("OK @ %s\n", _configIP.toString().c_str());
            return true;
        }
        delay(500);
        _dns.processNextRequest();
    }
    _connecting = false;
    Serial.println(F("FAIL"));
    return false;
}

void WiFiManager::tryConnect(const char* ssid, const char* pass, bool useStatic,
                              const IPAddress& ip, const IPAddress& gw,
                              const IPAddress& mask, const IPAddress& dns) {
    Serial.printf("[WM] Connecting to %s ... ", ssid);
    WiFi.mode(WIFI_STA);
    if (useStatic) {
        WiFi.config(ip, gw, mask, dns);
        Serial.printf("(static IP %s) ", ip.toString().c_str());
    }
    WiFi.begin(ssid, pass);
}

void WiFiManager::serveSetupPage() {
    _server->on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (_configDone) {
            String html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>STARTUP Gateway - Connected</title>"
                "<style>body{font-family:Arial,sans-serif;padding:20px;background:#f5f5f5}"
                ".card{max-width:480px;margin:40px auto;background:#fff;border-radius:8px;padding:24px;"
                "box-shadow:0 2px 8px rgba(0,0,0,0.15);text-align:center}"
                "h2{color:#2E7D32}.ip{font-size:24px;font-weight:bold;color:#1976D2;margin:16px 0;padding:12px;"
                "background:#E3F2FD;border-radius:4px}.note{color:#666;font-size:13px}</style></head><body>"
                "<div class='card'><img src='" + String(STARTUP_LOGO_DATA_URI) + "' alt='STARTUP' style='width:220px;max-width:80%;height:auto'>"
                "<h2>&#10003; Connected!</h2>"
                "<p>Gateway joined the configured WiFi network.</p>"
                "<div class='ip'><a href='http://" + _configIP.toString() + "'>http://" + _configIP.toString() + "</a></div>"
                "<p class='note'>Rebooting in 30 seconds. Dashboard will be available at the address above.</p>"
                "</div></body></html>";
            request->send(200, "text/html", html);
            return;
        }

        if (_connecting) {
            String html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Connecting...</title>"
                "<script>setTimeout(function(){location.reload()},3000)</script>"
                "<style>body{font-family:Arial;padding:20px;text-align:center;background:#f5f5f5}"
                ".card{max-width:400px;margin:40px auto;background:#fff;border-radius:8px;padding:24px;box-shadow:0 2px 8px rgba(0,0,0,0.15)}"
                ".spinner{border:4px solid #f3f3f3;border-top:4px solid #1976D2;border-radius:50%;width:36px;height:36px;animation:spin 1s linear infinite;margin:16px auto}"
                "@keyframes spin{0%{transform:rotate(0deg)}100%{transform:rotate(360deg)}}</style></head><body>"
                "<div class='card'><h2 style='color:#E65100'>Connecting to WiFi...</h2>"
                "<div class='spinner'></div>"
                "<p>Attempting to join your network...</p>"
                "<p style='font-size:13px;color:#666'>Stay connected to the gateway's AP.<br>This page refreshes automatically.</p>"
                "</div></body></html>";
            request->send(200, "text/html", html);
            return;
        }

        String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>STARTUP Gateway Setup</title>
<style>
body{font-family:Arial,sans-serif;margin:0;padding:20px;background:#f5f5f5}
.card{max-width:480px;margin:40px auto;background:#fff;border-radius:8px;padding:24px;box-shadow:0 2px 8px rgba(0,0,0,0.15)}
h2{text-align:center;color:#1976D2;margin:0 0 20px 0}
label{display:block;margin:12px 0 4px;font-weight:600;font-size:14px}
input{width:100%;padding:10px;border:1px solid #ccc;border-radius:4px;box-sizing:border-box;font-size:14px}
.advanced-toggle{text-align:right;margin:8px 0;font-size:13px;color:#1976D2;cursor:pointer}
.advanced{display:none;margin-top:12px;padding:12px;background:#fafafa;border:1px solid #eee;border-radius:4px}
.advanced label{font-size:12px}
.advanced input{font-size:13px;padding:7px}
.btn{width:100%;padding:12px;background:#1976D2;color:#fff;border:none;border-radius:4px;font-size:16px;cursor:pointer;margin-top:16px}
.btn:hover{background:#1565C0}
.footer{text-align:center;margin-top:16px;font-size:11px;color:#999}
</style>
<script>
function toggleAdvanced(){var e=document.getElementById('advanced');e.style.display=e.style.display==='block'?'none':'block'}
</script>
</head>
<body>
<div class="card">
<img src="%STARTUP_LOGO%" alt="STARTUP" style="display:block;width:240px;max-width:82%;height:auto;margin:0 auto 8px">
<h2>Gateway Setup</h2>
<form action="/api/setup" method="post">
<label for="ssid">WiFi Network</label>
<input type="text" id="ssid" name="ssid" placeholder="SSID" required>
<label for="pass">Password</label>
<input type="password" id="pass" name="pass" placeholder="Password">
<label for="adminpass">Dashboard administrator password</label>
<input type="password" id="adminpass" name="adminpass" placeholder="Minimum 8 characters" minlength="8" maxlength="32" required>
<div class="advanced-toggle" onclick="toggleAdvanced()">Advanced: Static IP &#9660;</div>
<div class="advanced" id="advanced">
<label for="ip">IP Address</label>
<input type="text" id="ip" name="ip" placeholder="192.168.1.100">
<label for="gw">Gateway</label>
<input type="text" id="gw" name="gw" placeholder="192.168.1.1">
<label for="mask">Subnet Mask</label>
<input type="text" id="mask" name="mask" placeholder="255.255.255.0">
<label for="dns">DNS</label>
<input type="text" id="dns" name="dns" placeholder="8.8.8.8">
</div>
<button type="submit" class="btn">Save & Connect</button>
</form>
<div class="footer">Hold BOOT button for 5s to reset WiFi settings</div>
</div>
</body>
</html>
)rawliteral";
        html.replace("%STARTUP_LOGO%", STARTUP_LOGO_DATA_URI);
        request->send(200, "text/html", html);
    });

    _server->onNotFound([](AsyncWebServerRequest* request) {
        request->redirect("http://192.168.4.1/");
    });
}

void WiFiManager::handleSetupSubmit() {
    _server->on("/api/setup", HTTP_POST,
        [this](AsyncWebServerRequest* request) {
            String ssid = request->arg("ssid");
            String pass = request->arg("pass");
            String adminPass = request->arg("adminpass");
            String ip   = request->arg("ip");
            String gw   = request->arg("gw");
            String mask = request->arg("mask");
            String dns  = request->arg("dns");

            if (ssid.length() == 0) {
                request->send(200, "text/html",
                    "<html><body style='font-family:Arial;padding:20px;text-align:center'>"
                    "<h2 style='color:#C62828'>Error</h2><p>SSID is required.</p>"
                    "<a href='/'>Go back</a></body></html>");
                return;
            }
            if (adminPass.length() < 8 || adminPass.length() > 32) {
                request->send(400, "text/html",
                    "<html><body style='font-family:Arial;padding:20px;text-align:center'>"
                    "<h2 style='color:#C62828'>Error</h2><p>Administrator password must contain 8-32 characters.</p>"
                    "<a href='/'>Go back</a></body></html>");
                return;
            }

            saveCredentials(ssid.c_str(), pass.c_str());
            saveAdminPassword(adminPass.c_str());
            if (ip.length() > 0) saveStaticIP(ip.c_str(), gw.c_str(), mask.c_str(), dns.c_str());
            else saveStaticIP("", "", "", "");

            _pendingConnect = true;

            String html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<meta http-equiv='refresh' content='2;url=/'>"
                "<title>Saved</title>"
                "<style>body{font-family:Arial;padding:20px;text-align:center;background:#f5f5f5}"
                ".card{max-width:400px;margin:40px auto;background:#fff;border-radius:8px;padding:24px;box-shadow:0 2px 8px rgba(0,0,0,0.15)}"
                "</style></head><body>"
                "<div class='card'>"
                "<h2 style='color:#E65100'>Configuration Saved</h2>"
                "<p>Gateway will now try to connect to the configured WiFi network.</p>"
                "<p style='font-size:13px;color:#666'>The setup page will show the dashboard address once connected.</p>"
                "</div></body></html>";
            request->send(200, "text/html", html);
        });
}
