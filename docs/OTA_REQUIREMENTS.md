# OTA requirements for ESP System applications

This is the normative integration and acceptance contract for applications built on ESP System.
It is intentionally written so an AI-assisted change can use it as a checklist without having to
reverse-engineer the library.

## Scope of this release

ESP System v0.2 provides the Arduino-ESP32 Wi-Fi application profile: browser upload on a separate
management HTTP server, optional device-pull updates from an HTTP/HTTPS manifest, A/B application
partitions, firmware identity, and boot health confirmation. It does not implement Zigbee OTA,
signed firmware, a fleet-management service, or safe partition-table migration.

## Application MUST requirements

1. **Retain ESP System.** Every deployable application must compile ESP System into the application
   image. Do not replace it with an application that omits `System` or the OTA-capable partition
   table if Wi-Fi OTA is expected to continue working.
2. **Use a compatible partition table.** The firmware must be built for the same partition layout
   already installed on the board and must fit in each OTA app slot. An ordinary application OTA
   does not change the partition table. Partition-layout changes require a separately planned
   reflash/migration and recovery route.
3. **Use the System lifecycle.** Call `espSystem.begin(config)` in `setup()` and `espSystem.loop()` on
   every normal main-loop iteration. Do not invoke low-level ESP OTA APIs from application code.
4. **Identify the image accurately.** Set `firmware.product`, `firmware.hardware`, and
   `firmware.version` to real values. Remote manifests must exactly match product and hardware;
   wildcards are not accepted. Keep build, Git identity, configuration schema, and API version
   meaningful where applicable.
5. **Leave management independent.** The System management server defaults to TCP port 8080.
   Application HTTP servers may use port 80 or another port independently. Do not add duplicate OTA
   routes to the application server merely to make OTA work.
6. **Configure credentials.** Set a nonempty OTA username and a nonempty unique password during bootstrap.
   Blank values may reuse credentials already stored in NVS. Never commit real credentials.
7. **Provide meaningful safety behavior.** Applications controlling actuators or other physical
   processes should supply a pre-update callback that moves the application into a safe maintenance
   state when appropriate and returns false unless it is safe to proceed. The callback must be quick
   and must not depend on services that are about to be shut down. Current Arduino `WebServer` upload
   handling and HTTP pull installation are synchronous and may block the normal application loop;
   ensure outputs remain safe during that period, or do not use this implementation for tight real-time control.
8. **Provide a meaningful health check.** For nontrivial applications, confirm the new image only
   after critical initialization succeeds: configuration is readable, essential sensors respond,
   control outputs are in a safe state, and the main application has reached a stable operating
   condition.
9. **Preserve persistent data deliberately.** Treat NVS settings and logs as persistent data, not
   as part of the application image. If configuration structures change, implement and test the
   application's own schema migration. Merely incrementing `configSchema` does not migrate data.
10. **Retain recovery access.** USB/esptool remains the recovery route if Wi-Fi, credentials,
    firmware, or management service become unusable. Do not OTA-update the bootloader or partition
    table as part of a routine application release.

## Security requirements and limitations

- The management endpoint is intended for a trusted LAN. HTTP Basic Auth over HTTP is not encrypted;
  do not forward its port from the Internet or use it on an untrusted network. State-changing management POSTs reject a supplied cross-origin browser `Origin`, but this is not a full security boundary.
- A SHA-256 digest verifies that a downloaded image matches the manifest, but with an unsigned
  manifest it does not prove who published the image. Do not treat this as production-grade remote
  firmware authentication.
- Use HTTPS with correctly configured CA verification for remote downloads over untrusted networks.
  Signed firmware and key protection are not implemented by this release.
- Never allow an update manifest to target any product or hardware with `*` or an empty identifier.

## Required project checks

For every application release, perform these checks before deploying to an unattended device:

1. PlatformIO build succeeds for the intended board/environment.
2. The selected partition CSV is the one installed on the target board, and the image fits within
   an OTA slot.
3. `System` reports the expected product, hardware, version, build, and Git identity.
4. The management page is reachable at `http://<hostname>.local:<managementPort>/` or by IP and
   the upload route requires configured credentials. If the app also uses mDNS, verify the `_esp-system._tcp`
   service is registered on the same responder and the advertised port is correct.
5. If the application also serves HTTP, verify it continues to work on its own port (typically 80).
6. Upload a known-good test image, verify the image boots, verify application health, and confirm the
   rollback-pending state is cleared only after the health policy passes.
7. On a test device, deliberately test a build that fails the health check and verify rollback to the
   prior image. Do not make this failure test on the only device controlling critical equipment.
8. Verify NVS configuration survives the OTA and that any schema migration works in both the
   installed configuration and a fresh configuration.
9. For device-pull releases, generate a manifest with the exact product/hardware, positive file size,
   and valid SHA-256, then verify a mismatch or malformed manifest is rejected.
10. Measure application-loop disruption during a full-size browser upload and remote update. For
    actuator-bearing applications, confirm safe outputs persist throughout the update and reboot.

## API shape

A normal Wi-Fi application starts with this pattern:

```cpp
EspSystem::System espSystem;

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

    espSystem.setPreUpdateCheck([] { return app.canSafelyReboot(); });
    espSystem.setHealthCheck([] { return app.selfTestPassed(); });
    espSystem.begin(config);
    app.begin();
}

void loop() {
    espSystem.loop();
    app.loop();
}
```

The Wi-Fi system profile owns connection maintenance. A future non-IP or Zigbee-only profile will
need a transport-specific bootstrap/template and integration; Zigbee OTA is not currently supported.
