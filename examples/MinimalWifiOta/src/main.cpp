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

bool applicationHealthCheck()
{
    // Replace this with real checks in an application project.
    // For the bootstrap application, reaching this point means the core is running.
    return true;
}

void setup()
{
    Serial.begin(115200);
    delay(250);
    Serial.println();
    Serial.println("ESP System bootstrap");

    SystemConfig config;
    config.firmware.product = "minimal-wifi-ota";
    config.firmware.hardware = "generic-esp32";
    config.firmware.version = "0.1.0";
    config.firmware.build = __DATE__ " " __TIME__;
    config.firmware.git = "bootstrap";
    config.firmware.configSchema = 1;
    config.firmware.apiVersion = 1;

    config.deviceName = "esp32-ota";
    config.wifiSsid = WIFI_SSID;
    config.wifiPassword = WIFI_PASSWORD;
    config.otaUsername = OTA_USERNAME;
    config.otaPassword = OTA_PASSWORD;
    config.updateManifestUrl = UPDATE_MANIFEST_URL;
    config.updateRootCaPem = UPDATE_ROOT_CA;
    config.updateCheckIntervalMs = UPDATE_MANIFEST_URL[0] ? 60UL * 60UL * 1000UL : 0;
    config.checkForUpdateOnBoot = UPDATE_MANIFEST_URL[0] != '\0';
    config.enableWeb = true;

    espSystem.setHealthCheck(applicationHealthCheck);
    espSystem.setPreUpdateCheck([] {
        // Bootstrap has no actuators. Real projects should reject updates when it
        // is unsafe to reboot their particular hardware.
        return true;
    });

    espSystem.begin(config);

    Serial.println("System started");
    Serial.print("Wi-Fi: ");
    Serial.println(espSystem.wifi().connected() ? "connected" : "waiting");
}

void loop()
{
    espSystem.loop();

    // The application layer is deliberately empty here.
    // Future projects retain the system.loop() call and put their own logic after it.
    delay(2);
}
