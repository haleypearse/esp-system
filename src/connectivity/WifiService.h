#pragma once

#include <Arduino.h>
#include <WiFi.h>

namespace EspSystem {

class WifiService {
public:
    void begin(const char* ssid = nullptr, const char* password = nullptr, const char* hostname = nullptr);
    void loop();

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
};

} // namespace EspSystem
