#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <memory>
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
               const char* otaPassword,
               uint16_t managementPort = 8080);
    void loop();

    bool enabled() const { return _enabled; }
    uint16_t port() const { return _port; }

private:
    void setupRoutes();
    bool authenticateUpdate();
    bool originAllowed();
    void handleRoot();
    void handleStatus();
    void handleUpdatePage();
    void handleUpdateRequest();
    void handleUpload();
    void handleNotFound();
    String htmlPage() const;

    std::unique_ptr<WebServer> _server;
    uint16_t _port = 8080;
    OtaManager& _ota;
    WifiService& _wifi;
    FirmwareInfo _firmware;
    String _hostname;
    String _otaUsername;
    String _otaPassword;
    bool _enabled = false;
    bool _uploadStarted = false;
    bool _uploadAuthorized = false;
    bool _uploadSucceeded = false;
    int _uploadHttpStatus = 400;
    String _uploadMessage;
};

} // namespace EspSystem
