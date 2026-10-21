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

The current release is intentionally **not** a cloud OTA platform. The firmware can pull from an ordinary
HTTP/HTTPS server, a home computer, a Raspberry Pi, NAS, or later a hosted service. The current System
profile itself is Wi-Fi-based; Zigbee-only operation is not implemented. The ESP native OTA mechanism
remains responsible for writing the inactive application partition and selecting it on reboot.

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

For a Wi-Fi-first application, System manages the station connection by default. Set `config.manageWifi = false` when the application already owns Arduino Wi-Fi setup/reconnection; the HTTP update source then reuses that connection. Applications normally need only:

```cpp
espSystem.begin(config);

void loop() {
    espSystem.loop();
    app.loop();
}
```

The application can optionally supply a health check and a pre-update safety check.

## First-boot / bootstrap workflow

1. Copy `bootstrap/wifi-ota/include/secrets.h.example` to `secrets.h`.
2. Put the Wi-Fi credentials and OTA browser credentials in `secrets.h`.
3. Build and flash `bootstrap/wifi-ota/` once over USB.
4. Open `http://esp32-ota.local:8080/` (or `http://<device-ip>:8080/`).
5. Future firmware images that retain this system layer can be uploaded over Wi-Fi at the management port (8080 by default).
6. USB remains the recovery path, but it is not part of the normal development/deployment loop.

The Wi-Fi credentials and local OTA browser credentials are persisted in NVS. A later
project can omit them and reuse the stored values. This is what allows the one-time bootstrap
image to hand off to unrelated application firmware without embedding the network password in
every project. The credentials are not encrypted by this
project; this is intended for a trusted home/workshop device, not an exposed hostile system.

## Separate management endpoint

ESP System runs its own HTTP server on `SystemConfig::managementPort` (default `8080`), independently of any web server in the application. The default URL is `http://<device-hostname>.local:8080/`; the update page is `/update`. The system advertises an `_esp-system._tcp` DNS-SD service for the management port rather than claiming the application's ordinary HTTP endpoint is on port 80. Set `enableManagement = false` to disable this server, or choose another nonzero port. If the application already calls `MDNS.begin()` for the same hostname, set `manageMdns = false` so System adds its DNS-SD service to the existing responder instead of initializing mDNS again.

The management server is intentionally separate at the HTTP-server level, not a separate security boundary. It shares the device and LAN. Basic authentication over plain HTTP is suitable only for a trusted network.

## Bootstrap versus application template

- `bootstrap/wifi-ota/` is the minimal image intended for the one-time USB flash on a virgin board.
- `templates/wifi-application/` is the starting point for new application repositories. It builds the application together with ESP System as one firmware image.

The bootstrap is replaced by the first real application OTA; it does not remain resident underneath it. Each subsequent application must retain the ESP System dependency and OTA partition layout. A new application's own HTTP server can use port 80 without merging routes with the system's management server.

## Local browser OTA

The independent management server (default port 8080) exposes the `/update` page, which accepts an ESP32 application `.bin` image. The upload writes the inactive
OTA slot using Arduino-ESP32's `Update` implementation and schedules a reboot after the HTTP
handler returns. The existing application remains
intact during the transfer.

The update page uses HTTP Basic Auth. This is **not** encryption: credentials and firmware travel unencrypted over HTTP. Do not expose the management port to the public Internet or an untrusted network. Change the example credentials before flashing. HTTPS and signed firmware are separate future security improvements; the current SHA-256 check detects accidental corruption but does not authenticate who created a firmware image.

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

Point `UPDATE_MANIFEST_URL` at the resulting `manifest.json`. The device discovers the release periodically but does not install it automatically; installation is initiated from the authenticated management page.

## Device-pull OTA

The device can periodically request a JSON manifest such as:

```json
{
  "product": "minimal-wifi-ota",
  "version": "0.2.0",
  "hardware": "generic-esp32",
  "url": "http://192.168.1.50:8000/firmware.bin",
  "size": 1048576,
  "sha256": "...",
  "notes": "Example release"
}
```

The update source discovers the candidate; `OtaManager` checks product, hardware, and
version; the common installer writes and verifies it. Remote manifests require an explicit product and hardware identifier, a positive image size, and a valid hexadecimal SHA-256 digest. A plain HTTP hash is integrity checking, not authentication against a malicious
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
espSystem.setHealthCheck([] {
    return app.selfTestPassed();
});
```

For hardware with real safety constraints:

```cpp
espSystem.setPreUpdateCheck([] {
    return compressor.isOff() && sensors.areHealthy();
});
```

If neither callback is supplied, a successfully running boot is enough to confirm the image.

## Partition tables

`partitions/ota_4mb.csv` and `partitions/ota_8mb.csv` provide two application slots.
They do not assume that the slots consume exactly half of flash; their sizes are explicit in
the partition CSV.

Both standard layouts reserve a 64 KiB `data,coredump` partition for flash-based core dumps.
The 4 MiB layout is approximately:

```text
NVS / OTA metadata
ota_0       1.875 MiB
ota_1       1.875 MiB
filesystem  0.125 MiB
coredump    0.0625 MiB
```

The 8 MiB layout preserves the same 64 KiB coredump partition and allocates more space to the
application slots and filesystem. CI checks both root layouts and the copies included in the
standalone application template, and rejects tables without coredump storage.

A partition CSV is used when flashing/building a partition table; an ordinary application `.bin`
OTA does not write a new table. If a table needs to change on an already provisioned board, schedule
a partition-table reflash with a recovery path. The new 4 MiB table allocates the final 64 KiB of
the previous filesystem region to coredump storage, so any existing filesystem data in that region
must not be assumed to survive the layout change.

## Git dependency model

A project can depend on a tagged release from GitHub:

```ini
lib_deps =
    https://github.com/haleypearse/esp-system.git#v0.2.0
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

Future projects should begin from `templates/wifi-application/` and modify the application layer rather than copying OTA code into the application. `docs/OTA_INTEGRATION.md` is written as a compact
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

Those are intentionally outside v0.2.0. The interfaces are chosen so they can be added later.

## Release process

For a tagged project release, build the application, keep the `.bin` artifact, and record the
`esp-system` dependency version in `platformio.ini`. For a device-pull release, put the image
and generated manifest in the update repository/server. Do not point deployed projects at the
`main` branch of this repository.

The included GitHub Action builds both the Wi-Fi bootstrap and the application template against the checked-out library source, then validates the update tools and partition-table bounds. Device-specific application repositories can use the same pattern.
