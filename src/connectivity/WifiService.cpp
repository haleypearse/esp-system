#include "WifiService.h"
#include <Preferences.h>

namespace EspSystem {

static constexpr const char* NVS_NAMESPACE = "esp_system";

void WifiService::begin(const char* ssid, const char* password, const char* hostname)
{
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);

    _ssid = ssid ? String(ssid) : String();
    _password = password ? String(password) : String();

    if (_ssid.isEmpty()) _ssid = prefs.getString("wifi_ssid", "");
    if (_password.isEmpty()) _password = prefs.getString("wifi_pass", "");

    if (ssid && *ssid) prefs.putString("wifi_ssid", _ssid);
    if (password && *password) prefs.putString("wifi_pass", _password);

    prefs.end();

    _hostname = hostname && *hostname ? String(hostname) : String("esp32");

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(_hostname.c_str());

    if (!_ssid.isEmpty()) {
        WiFi.begin(_ssid.c_str(), _password.c_str());
        _lastAttemptMs = millis();
    }
}

void WifiService::loop()
{
    if (_ssid.isEmpty() || connected()) return;

    const uint32_t now = millis();
    if (now - _lastAttemptMs < _retryMs) return;

    WiFi.disconnect();
    WiFi.begin(_ssid.c_str(), _password.c_str());
    _lastAttemptMs = now;
}

bool WifiService::hasCredentials() const
{
    return !_ssid.isEmpty();
}

void WifiService::saveCredentials(const String& ssid, const String& password)
{
    _ssid = ssid;
    _password = password;

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString("wifi_ssid", _ssid);
    prefs.putString("wifi_pass", _password);
    prefs.end();
}

void WifiService::clearCredentials()
{
    _ssid = String();
    _password = String();

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.remove("wifi_ssid");
    prefs.remove("wifi_pass");
    prefs.end();

    WiFi.disconnect(true, true);
}

void WifiService::saveOtaCredentials(const String& username, const String& password)
{
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString("ota_user", username);
    prefs.putString("ota_pass", password);
    prefs.end();
}

bool WifiService::loadOtaCredentials(String& username, String& password) const
{
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) return false;
    username = prefs.getString("ota_user", "");
    password = prefs.getString("ota_pass", "");
    prefs.end();
    return !username.isEmpty();
}

} // namespace EspSystem
