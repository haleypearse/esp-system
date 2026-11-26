#pragma once

#include <Arduino.h>
#include <WiFi.h>

namespace EspSystem {

class WifiService {
public:
    void begin(const char* ssid = nullptr, const char* password = nullptr, const char* hostname = nullptr);
    void loop();

    // Stop the Wi-Fi radio without erasing stored credentials. Requires a
    // connected STA-only mode; AP/AP+STA is refused because an AP beacon would
    // keep the radio active. The System reconnect loop is held while suspended.
    // Useful for classic ESP32 ADC2 and other radio-conflicting peripherals.
    bool suspend();
    bool resume();
    bool suspended() const { return _suspended; }

    bool connected() const { return WiFi.status() == WL_CONNECTED; }
    IPAddress address() const { return WiFi.localIP(); }
    const String& hostname() const { return _hostname; }

    bool hasCredentials() const;
    void saveCredentials(const String& ssid, const String& password);
    void clearCredentials();

    void saveOtaCredentials(const String& username, const String& password);
    bool loadOtaCredentials(String& username, String& password) const;

private:
    String _hostname;
    String _ssid;
    String _password;
    uint32_t _lastAttemptMs = 0;
    uint32_t _retryMs = 10000;
    bool _suspended = false;
};

} // namespace EspSystem
