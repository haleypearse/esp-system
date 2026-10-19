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

    _wifi.begin(_config.wifiSsid, _config.wifiPassword, _config.deviceName);

    String otaUser = _config.otaUsername ? String(_config.otaUsername) : String();
    String otaPass = _config.otaPassword ? String(_config.otaPassword) : String();
    String storedUser;
    String storedPass;
    if (otaUser.isEmpty() && _wifi.loadOtaCredentials(storedUser, storedPass)) {
        otaUser = storedUser;
        otaPass = storedPass;
    }
    if (!otaUser.isEmpty()) _wifi.saveOtaCredentials(otaUser, otaPass);

    _ota.begin(_config.firmware, _config.rollbackConfirmationDelayMs);

    if (_config.updateManifestUrl && *_config.updateManifestUrl) {
        _httpSource.setManifestUrl(_config.updateManifestUrl);
        _httpSource.setRootCACertificate(_config.updateRootCaPem);
        _ota.setUpdateSource(&_httpSource);
        _remoteSourceConfigured = true;
        _remoteCheckPending = _config.checkForUpdateOnBoot;
    }

    if (_config.enableWeb) {
        // mDNS is best-effort. HTTP remains usable through the IP address if it fails.
        if (MDNS.begin(_config.deviceName)) {
            MDNS.addService("http", "tcp", 80);
        }
        _web.begin(_config.firmware,
                   _config.deviceName,
                   otaUser.c_str(),
                   otaPass.c_str());
    }

    _started = true;
}

void System::loop()
{
    if (!_started) return;

    _wifi.loop();
    _web.loop();
    _ota.loop();

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

} // namespace EspSystem
