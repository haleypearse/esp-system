#include <Arduino.h>
#include <System.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#define OTA_USERNAME ""
#define OTA_PASSWORD ""
#define UPDATE_MANIFEST_URL ""
#define UPDATE_ROOT_CA ""
#endif

using namespace EspSystem;

System espSystem;

bool appHealthCheck()
{
    // Add application-specific startup checks here.
    return true;
}

bool appCanSafelyUpdate()
{
    // Add application-specific "safe to reboot" logic here.
    return true;
}

void appBegin()
{
    // Initialize project-specific hardware here.
}

void appLoop()
{
    // Put the project's main loop here.
}

void setup()
{
    Serial.begin(115200);

    SystemConfig config;
    config.firmware.product = "my-project";           // change
    config.firmware.hardware = "my-board-v1";        // change
    config.firmware.version = "0.1.0";                // change on release
    config.firmware.build = __DATE__ " " __TIME__;
    config.firmware.git = "local";                    // CI can replace this
    config.firmware.configSchema = 1;
    config.firmware.apiVersion = 1;

    config.deviceName = "my-project";                 // change
    config.wifiSsid = WIFI_SSID;                       // may be empty: reuse NVS
    config.wifiPassword = WIFI_PASSWORD;
    config.otaUsername = OTA_USERNAME;                 // blank = reuse NVS
    config.otaPassword = OTA_PASSWORD;
    config.updateManifestUrl = UPDATE_MANIFEST_URL;
    config.updateRootCaPem = UPDATE_ROOT_CA;
    // config.enableManagement = true;
    // config.managementPort = 8080;
    // config.manageMdns = true; // set false if the application already initializes MDNS
    config.updateCheckIntervalMs = UPDATE_MANIFEST_URL[0] ? 6UL * 60UL * 60UL * 1000UL : 0;

    espSystem.setHealthCheck(appHealthCheck);
    espSystem.setPreUpdateCheck(appCanSafelyUpdate);
    espSystem.begin(config);

    appBegin();
}

void loop()
{
    espSystem.loop();
    appLoop();
}
