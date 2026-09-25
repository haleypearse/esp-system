# ESP System

A reusable Arduino/PlatformIO system layer for ESP32 projects.

The goal is to let individual projects concentrate on their application while a common
system layer provides the boring-but-important infrastructure:

- Wi-Fi station connection with persistent credentials in NVS
- local HTTP management page
- authenticated browser firmware upload
- PlatformIO/Arduino OTA-compatible flash layout
- device-pull HTTP update discovery
- SHA-256 verification for manifest-driven updates
- boot health confirmation and rollback support
- firmware/hardware/build/config metadata
- a small, stable application-facing interface

This repository is intentionally **not** a cloud OTA platform. The firmware can pull from
an ordinary HTTP/HTTPS server, a home computer, a Raspberry Pi, NAS, or later a hosted
service. The ESP's native OTA mechanism remains responsible for writing the inactive
application partition and selecting it on reboot.

## Design boundary

```text
Application
    |
    v
EspSystem
    +-- WifiService
    +-- SystemWeb
    +-- OtaManager
    |     +-- local browser upload
    |     +-- UpdateSource interface
    |           +-- HttpUpdateSource (implemented)
    |           +-- future ZigbeeUpdateSource
    +-- FirmwareInfo
```

Applications normally need only:

```cpp
system.begin(config);

void loop() {
    system.loop();
    app.loop();
}
```

The application can optionally supply a health check and a pre-update safety check.

## First-boot / bootstrap workflow

1. Copy `examples/MinimalWifiOta/include/secrets.h.example` to `secrets.h`.
2. Put the Wi-Fi credentials and OTA browser credentials in `secrets.h`.
3. Build and flash the example once over USB.
4. Open `http://esp32-ota.local/` (or its IP address).
5. Future firmware images that retain this system layer can be uploaded over Wi-Fi.
6. USB remains the recovery path, but it is not part of the normal development/deployment loop.

The Wi-Fi credentials and local OTA browser credentials are persisted in NVS. A later
project can omit them and reuse the stored values. This is what allows the one-time bootstrap
image to hand off to unrelated application firmware without embedding the network password in
every project. The credentials are not encrypted by this
project; this is intended for a trusted home/workshop device, not an exposed hostile system.

## Local browser OTA

The `/update` page accepts an ESP32 application `.bin` image. The upload writes the inactive
OTA slot using Arduino-ESP32's `Update` implementation and schedules a reboot after the HTTP
handler returns. The existing application remains
intact during the transfer.

The update page uses HTTP Basic Auth. This is **not** encryption. Do not expose the HTTP
management port to the public Internet. For a local-only workshop/LAN deployment this is a
simple first-stage authentication mechanism.

## Trying device-pull updates locally

Build a firmware `.bin`, place it in a small release directory, and generate a manifest:

```text
python tools/make_manifest.py firmware.bin \
  --product minimal-wifi-ota \
  --version 0.2.0 \
  --hardware generic-esp32 \
  --base-url http://192.168.1.50:8000/ \
  --notes "Test OTA release"
```

Then serve that directory on the LAN:

```text
python tools/serve_updates.py . --port 8000
```

Point `UPDATE_MANIFEST_URL` at the resulting `manifest.json`. The device discovers the release
periodically but does not install it automatically in v0.1; installation is initiated from the
authenticated update page.

## Device-pull OTA

The device can periodically request a JSON manifest such as:

```json
{
  "product": "minimal-wifi-ota",
  "version": "0.2.0",
  "hardware": "generic-esp32",
  "url": "http://192.168.1.50:8000/releases/generic-esp32-0.2.0.bin",
  "size": 1048576,
  "sha256": "...",
  "notes": "Example release"
}
```

The update source discovers the candidate; `OtaManager` checks product, hardware, and
version; the common installer writes and verifies it. Remote manifests in v0.1 require a
SHA-256 digest. A plain HTTP hash is integrity checking, not authentication against a malicious
server; use HTTPS and later signed releases for Internet-hosted deployment.

HTTPS is supported when a root CA PEM is supplied in the system configuration. No
certificate-pinning shortcut is used.

## Rollback

Arduino-ESP32 currently has an application startup verification hook. This library overrides
the weak default so that the system can defer confirmation until after a configurable delay.
If the health check fails, the ESP marks the new image invalid and reboots to the previous
working image.

The application can provide:

```cpp
system.setHealthCheck([] {
    return app.selfTestPassed();
});
```

For hardware with real safety constraints:

```cpp
system.setPreUpdateCheck([] {
    return compressor.isOff() && sensors.areHealthy();
});
```

If neither callback is supplied, a successfully running boot is enough to confirm the image.

## Partition tables

`partitions/ota_4mb.csv` and `partitions/ota_8mb.csv` provide two application slots.
They do not assume that the slots consume exactly half of flash; their sizes are explicit in
the partition CSV.

A normal 4 MB layout here is approximately:

```text
NVS / OTA metadata
ota_0       1.875 MiB
ota_1       1.875 MiB
filesystem  0.1875 MiB
```

An 8 MB layout gives substantially more room for both firmware and web assets.

## Git dependency model

A project can depend on a tagged release from GitHub:

```ini
lib_deps =
    https://github.com/YOUR_GITHUB_USER/esp-system.git#v0.1.0
```

During local development the included example uses the repository itself as a dependency.
Pin project dependencies to tags/releases for reproducible builds. Do not track `main` for
deployed firmware.

## Future transports

`UpdateSource` deliberately describes *where firmware bytes come from* separately from how
they are installed. A future Zigbee implementation can therefore become:

```text
Zigbee OTA cluster -> ZigbeeUpdateSource -> OtaManager -> ESP OTA
```

without changing application code or the flash/rollback machinery.

## AI-assisted development

The intended invariant is:

> Every deployable application retains the ESP System dependency and an OTA-capable partition table.

Future projects should begin from `template-project/` and modify the application layer rather
than copying OTA code into the application. `docs/OTA_INTEGRATION.md` is written as a compact
contract for subsequent AI-assisted modifications.

## Current scope / deliberate omissions

Not implemented yet:

- Zigbee OTA transport
- fleet/device registry
- automatic cloud release rollout
- production Secure Boot / key provisioning
- anti-rollback security version policy
- resumable multi-boot OTA downloads
- automatic configuration migration beyond exposing a schema version

Those are intentionally outside v0.1.0. The interfaces are chosen so they can be added later.

## Release process

For a tagged project release, build the application, keep the `.bin` artifact, and record the
`esp-system` dependency version in `platformio.ini`. For a device-pull release, put the image
and generated manifest in the update repository/server. Do not point deployed projects at the
`main` branch of this repository.

The included GitHub Action builds the bootstrap example on every push/PR once the repository
is published. Device-specific application repositories can use the same pattern.
