#include "System.h"
#include <ESPmDNS.h>
#include <esp_system.h>

namespace EspSystem {

System::System()
    : _web(_ota, _wifi)
{
}

void System::begin(const SystemConfig& config)
{
    _config = config;
    _deviceName = (_config.deviceName && *_config.deviceName)
                    ? String(_config.deviceName) : String("esp32");

    if (_config.manageWifi) {
        _wifi.begin(_config.wifiSsid, _config.wifiPassword, _deviceName.c_str());
    }

    String otaUser = _config.otaUsername ? String(_config.otaUsername) : String();
    String otaPass = _config.otaPassword ? String(_config.otaPassword) : String();
    String storedUser;
    String storedPass;
    if (otaUser.isEmpty() && _wifi.loadOtaCredentials(storedUser, storedPass)) {
        otaUser = storedUser;
        otaPass = storedPass;
    }
    if (!otaUser.isEmpty() && !otaPass.isEmpty()) _wifi.saveOtaCredentials(otaUser, otaPass);

    _ota.begin(_config.firmware, _config.rollbackConfirmationDelayMs);

    if (_config.updateManifestUrl && *_config.updateManifestUrl) {
        _httpSource.setManifestUrl(_config.updateManifestUrl);
        _httpSource.setRootCACertificate(_config.updateRootCaPem);
        _ota.setUpdateSource(&_httpSource);
        _remoteSourceConfigured = true;
        _remoteCheckPending = _config.checkForUpdateOnBoot;
    }

    if (_config.enableManagement) {
        // A separate DNS-SD service advertises the management endpoint without
        // claiming that the application's ordinary HTTP server is on this port.
        if (_config.managementPort == 0) {
            Serial.println("[ESP System] managementPort=0 is invalid; management server disabled");
        } else {
            // mDNS is started later when Wi-Fi is actually connected. The
            // management server itself can start before connectivity is ready.
            _web.begin(_config.firmware,
                       _deviceName.c_str(),
                       otaUser.c_str(),
                       otaPass.c_str(),
                       _config.managementPort);
        }
    }

    _started = true;
}

void System::loop()
{
    if (!_started) return;

    if (_config.manageWifi) _wifi.loop();
    _web.loop();
    _ota.loop();
    ensureMdns();

    if (!_remoteSourceConfigured) return;
    if (_ota.status().state == OtaState::UpdateAvailable) return;
    if (WiFi.status() != WL_CONNECTED) return;
    if (!_remoteCheckPending && _config.updateCheckIntervalMs == 0) return;

    const uint32_t now = millis();
    if (_remoteCheckPending || now - _lastRemoteCheckMs >= _config.updateCheckIntervalMs) {
        _remoteCheckPending = false;
        _lastRemoteCheckMs = now;
        _ota.checkForUpdate();
    }
}

void System::ensureMdns()
{
    if (!_config.enableManagement || _config.managementPort == 0 || _mdnsServiceRegistered) return;
    if (WiFi.status() != WL_CONNECTED) return;

    const uint32_t now = millis();
    if (_mdnsAttempted && now - _lastMdnsAttemptMs < 5000) return;
    _mdnsAttempted = true;
    _lastMdnsAttemptMs = now;

    // An application that already owns mDNS can set manageMdns=false. In that
    // mode, System assumes MDNS.begin() has been called by the application and
    // only adds its own service record. Registration is retried on failure.
    if (!_mdnsInitialized) {
        if (_config.manageMdns) {
            if (!MDNS.begin(_deviceName.c_str())) {
                Serial.println("[ESP System] mDNS initialization failed; will retry");
                return;
            }
        }
        _mdnsInitialized = true;
    }

    // The host name is shared; only the management service/port is distinct.
    // addService() returns false on failure, so a later loop can retry.
    _mdnsServiceRegistered = MDNS.addService("esp-system", "tcp", _config.managementPort);
    if (!_mdnsServiceRegistered) {
        Serial.println("[ESP System] mDNS management service registration failed; will retry");
    }
}

} // namespace EspSystem
