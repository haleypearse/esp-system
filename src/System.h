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

    // true: ESP System owns Wi-Fi station setup/retry. false: the application
    // owns connection setup, while System reuses the global Arduino WiFi client.
    bool manageWifi = true;

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

    // Run the independent ESP System management server. It does not share the
    // application's HTTP server. Set false to disable browser management.
    bool enableManagement = true;
    // true: System initializes the mDNS responder. Set false only if the
    // application already calls MDNS.begin() for this device; System will
    // then register its management service on that existing responder.
    bool manageMdns = true;
    // Separate HTTP port for management (default 8080; 0 is invalid).
    uint16_t managementPort = 8080;
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
    String _deviceName;
    WifiService _wifi;
    OtaManager _ota;
    HttpUpdateSource _httpSource;
    SystemWeb _web;
    bool _started = false;
    bool _remoteSourceConfigured = false;
    bool _remoteCheckPending = false;
    uint32_t _lastRemoteCheckMs = 0;
    uint32_t _lastMdnsAttemptMs = 0;
    bool _mdnsAttempted = false;
    bool _mdnsInitialized = false;
    bool _mdnsServiceRegistered = false;
    void ensureMdns();
};

} // namespace EspSystem
