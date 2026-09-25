#pragma once

#include <Arduino.h>
#include "core/FirmwareInfo.h"
#include "connectivity/WifiService.h"
#include "ota/OtaManager.h"
#include "ota/HttpUpdateSource.h"
#include "web/SystemWeb.h"

namespace EspSystem {

struct SystemConfig {
    FirmwareInfo firmware;

    const char* deviceName = "esp32";

    // If supplied, these are saved in NVS. If blank/null, saved credentials are used.
    const char* wifiSsid = nullptr;
    const char* wifiPassword = nullptr;

    // Local browser OTA authentication. HTTP Basic Auth is appropriate only on a trusted LAN.
    const char* otaUsername = "";
    const char* otaPassword = "";

    // Optional device-pull manifest URL. Leave empty to disable remote update checks.
    const char* updateManifestUrl = nullptr;
    const char* updateRootCaPem = nullptr;
    uint32_t updateCheckIntervalMs = 0; // 0 = disabled
    bool checkForUpdateOnBoot = false;

    bool enableWeb = true;
    uint32_t rollbackConfirmationDelayMs = 10000;
};

class System {
public:
    System();

    void begin(const SystemConfig& config);
    void loop();

    WifiService& wifi() { return _wifi; }
    OtaManager& ota() { return _ota; }
    SystemWeb& web() { return _web; }
    const FirmwareInfo& firmware() const { return _config.firmware; }

    void setHealthCheck(OtaManager::HealthCheck fn) { _ota.setHealthCheck(std::move(fn)); }
    void setPreUpdateCheck(OtaManager::PreUpdateCheck fn) { _ota.setPreUpdateCheck(std::move(fn)); }

private:
    SystemConfig _config;
    WifiService _wifi;
    OtaManager _ota;
    HttpUpdateSource _httpSource;
    SystemWeb _web;
    bool _started = false;
    bool _remoteSourceConfigured = false;
    bool _remoteCheckPending = false;
    uint32_t _lastRemoteCheckMs = 0;
};

} // namespace EspSystem
