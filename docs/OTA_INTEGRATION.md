# OTA integration contract

Read `OTA_REQUIREMENTS.md` before adding features or editing a deployed application.

## Required integration

1. Pin a released version of `esp-system` in `platformio.ini`.
2. Use a board-compatible OTA partition table with two application slots and `otadata`.
3. Set exact product, hardware, firmware version, build/Git identity, API version and configuration
   schema in `SystemConfig`.
4. Call `espSystem.begin(config)` in `setup()` and `espSystem.loop()` every normal loop iteration.
5. Leave `enableManagement=true` and `managementPort=8080` unless the deployment has a reason to change
   them. System management uses its own web server; do not merge its handlers into the application web
   server.
6. Configure non-default Wi-Fi/management credentials during bootstrap, with both OTA username and password nonempty. Later images may leave the
   values blank to load the credentials stored in NVS when `manageWifi=true`; if the application owns Wi-Fi, it owns Wi-Fi credential persistence.
7. Implement safety/health callbacks when an ordinary reboot or a successful startup cannot be assumed.
8. Never manipulate OTA partitions directly from application code.

## Basic application shape

```cpp
#include <Arduino.h>
#include <System.h>

EspSystem::System espSystem;

void appBegin();
void appLoop();
bool appCanSafelyUpdate();
bool appHealthCheck();

void setup() {
    EspSystem::SystemConfig config;
    config.firmware.product = "my-product";
    config.firmware.hardware = "my-board-v1";
    config.firmware.version = "0.1.0";
    config.firmware.build = __DATE__ " " __TIME__;
    config.firmware.git = "local";
    config.firmware.configSchema = 1;
    config.firmware.apiVersion = 1;
    config.deviceName = "my-product";
    config.managementPort = 8080;
    config.enableManagement = true;
    config.manageMdns = true;

    espSystem.setPreUpdateCheck(appCanSafelyUpdate);
    espSystem.setHealthCheck(appHealthCheck);
    espSystem.begin(config);
    appBegin();
}

void loop() {
    espSystem.loop();
    appLoop();
}
```

## Network and management endpoint

The current System profile can manage Wi-Fi connectivity by default and supports HTTP(S) pull plus browser upload.
The management URL is `http://<hostname>.local:<managementPort>/`; the update page is `/update`.
The default port is 8080. The app's own web server can remain on port 80. `enableManagement=false`
disables the System management server. Port 0 is invalid. `manageMdns=true` lets System initialize mDNS; set
it to `false` only if the application already calls `MDNS.begin()` for the same hostname. In that mode, System
registers its `_esp-system._tcp` service on the existing responder rather than starting another one.

Set `config.manageWifi = false` if application code already owns `WiFi.begin()`/connection recovery; System will reuse the global Arduino Wi-Fi connection rather than reconfiguring it. If the application leaves Wi-Fi management enabled (the default), System owns station setup/retry. Blank SSID/password may load previously stored NVS Wi-Fi values only when `manageWifi=true`; blank OTA user/password may load the stored management credentials. Stored Wi-Fi and OTA
credentials share the `esp_system` NVS namespace. Do not erase this namespace as part of a normal OTA. Avoid choosing a management port already used by another server on the device.

## Partition and release requirements

Use a partition table compatible with what is already flashed on the board. Regular OTA does not alter
the partition table. For device-pull releases, include exact `product`, `hardware`, a numeric
`MAJOR.MINOR.PATCH` version, positive `size`, firmware URL and 64-character hexadecimal `sha256` in
the manifest. The digest detects mismatch/corruption, not a maliciously replaced unsigned manifest.

For safety constraints, see the acceptance checklist in `OTA_REQUIREMENTS.md`. USB/esptool remains the
recovery path.
