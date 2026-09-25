# OTA integration contract for future projects

This document is deliberately short and prescriptive. Read it before changing a project that
uses `esp-system`.

## Required

1. Use an OTA-capable partition table with two application slots.
2. Keep the `esp-system` dependency in `platformio.ini`.
3. Call `System::begin()` from `setup()`.
4. Call `system.loop()` on every iteration of the main loop.
5. Do not call ESP OTA partition APIs directly from application code.
6. Keep firmware version, hardware ID, and config schema accurate.

Minimal shape:

```cpp
#include <System.h>

EspSystem::System system;

void setup() {
    EspSystem::SystemConfig config;
    config.firmware.product = "my-project";
    config.firmware.hardware = "my-board-v1";
    config.firmware.version = "0.1.0";
    config.firmware.build = "dev";
    config.firmware.git = "local";
    system.begin(config);
}

void loop() {
    system.loop();
    app.loop();
}
```

## Optional application hooks

Use a pre-update check when a reboot/update must only happen in a safe state:

```cpp
system.setPreUpdateCheck([] {
    return app.canSafelyReboot();
});
```

Use a health check when the application can detect a bad update after boot:

```cpp
system.setHealthCheck([] {
    return app.selfTestPassed();
});
```

The system confirms a pending OTA image only after its configured startup delay.

## Network ownership

The system layer may supply Wi-Fi, but OTA does not intrinsically depend on Wi-Fi.
The HTTP update source uses whatever IP connection is available to the ESP32. Do not create a
second Wi-Fi manager inside the application unless there is a concrete reason to replace the
system connectivity service.

Future transports should implement `UpdateSource` instead of changing `OtaManager`.

## Configuration and secrets

Wi-Fi credentials supplied to `SystemConfig` are persisted by `WifiService`. Later applications
may leave `wifiSsid` and `wifiPassword` empty/null to reuse those values.

Do not commit `secrets.h`.

## Partition rule

A firmware image must fit within the application slots. Increasing application size may require
changing the partition table before an OTA release can be deployed.

Do not casually change the partition table of a deployed device; treat partition-table changes
as a separate migration/recovery concern.

## OTA rule for AI-assisted edits

Do not replace or duplicate the OTA system because the application does not immediately need it.
Keep the dependency and main-loop integration intact. Add application-specific code in the
application layer.
