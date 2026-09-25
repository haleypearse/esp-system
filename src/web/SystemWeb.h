#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include "../core/FirmwareInfo.h"

namespace EspSystem {

class OtaManager;
class WifiService;

class SystemWeb {
public:
    SystemWeb(OtaManager& ota, WifiService& wifi);

    void begin(const FirmwareInfo& firmware,
               const char* hostname,
               const char* otaUsername,
               const char* otaPassword);
    void loop();

    WebServer& server() { return _server; }
    bool enabled() const { return _enabled; }

private:
    void setupRoutes();
    bool authenticateUpdate();
    void handleRoot();
    void handleStatus();
    void handleUpdatePage();
    void handleUpdateRequest();
    void handleUpload();
    void handleNotFound();
    String htmlPage() const;

    WebServer _server;
    OtaManager& _ota;
    WifiService& _wifi;
    FirmwareInfo _firmware;
    String _hostname;
    String _otaUsername;
    String _otaPassword;
    bool _enabled = false;
    bool _uploadStarted = false;
    bool _uploadAuthorized = false;
};

} // namespace EspSystem
