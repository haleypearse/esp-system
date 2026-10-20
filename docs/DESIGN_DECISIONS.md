# ESP System design decisions

These choices describe the current implementation and explain what is deliberately left open.

## 1. Application-level OTA, not a permanently resident updater

**Chosen:** ESP System is compiled into each complete application image. The running application
writes the inactive OTA slot through Arduino-ESP32's `Update` implementation, then the ESP bootloader
selects the new slot on restart.

**Why:** this uses the supported A/B application-update mechanism and avoids maintaining a separate
resident application or custom bootloader. The bootstrap is replaced by the first application OTA;
it does not run underneath it. Each later image must retain ESP System if OTA is to remain available.

**Trade-off:** an application that omits ESP System can lose OTA capability. Build checks and the
project template are the main protections against accidental omission. USB remains the recovery path.

## 2. Separate management server on port 8080

**Chosen:** System runs an independent `WebServer` on `SystemConfig::managementPort`, default 8080.
The application's own web server can use port 80 without integrating OTA routes. mDNS advertises a
separate `_esp-system._tcp` service at the configured port. `manageMdns` allows reuse of an application-initialized responder to avoid duplicate `MDNS.begin()` calls.

**Why:** this avoids coupling OTA functionality to application routes and avoids common port-80
collisions.

**Trade-off:** two servers use RAM and must both be serviced from the main loop. An application using its
own Wi-Fi manager should establish that connection before `System.begin()` to make mDNS discovery more
reliable. A port is not a
security boundary; the service is still on the same device and LAN. Basic Auth over HTTP is unencrypted. Dynamic manifest/build fields are HTML-escaped before display, and update POSTs reject a supplied cross-origin browser `Origin`. Requests without `Origin` remain allowed for non-browser clients, so this is defense-in-depth rather than a substitute for secure transport or full CSRF protection.

## 3. Wi-Fi is the current system profile, not a requirement of the flash installer

**Chosen now:** `System` includes `WifiService`, browser management and HTTP pull updates for a Wi-Fi
connected device. The default `manageWifi=true` makes System manage station setup/retry; `manageWifi=false`
lets an application retain ownership of the global Arduino Wi-Fi connection. `UpdateSource` is a seam for
future sources; current code is not yet transport-neutral at the whole-System level.

**Deferred:** separate connection-provider interfaces, Zigbee OTA, Ethernet, BLE, or other transports.
Those should be implemented only with a real target and SDK. Adding a source alone will not make a
Zigbee-only device work because System currently also initializes Wi-Fi and discovers HTTP releases.

## 4. Browser upload and device-pull are separate workflows

**Chosen:** local browser upload provides interactive deployments. Optional HTTP(S) pull discovers a
manifest and downloads a selected release. Both converge on the OTA installer.

**Why:** local use does not need cloud infrastructure, while pull OTA avoids inbound networking for
future hosted deployments.

**Trade-off:** local browser upload is an operator-selected raw `.bin`; it does not have manifest-level
product/hardware metadata. Check the board/firmware pairing before upload. Remote manifests require
exact product/hardware matches and a positive image size.

## 5. Compatibility is exact-match, not wildcard

**Chosen:** a remote release must specify the exact current product and hardware IDs. The updater does
not treat `"*"` as a match.

**Why:** accidentally installing a firmware intended for a different board/product is more costly than
failing to offer an update. Generic rollouts can be implemented later through an explicit allow-list or
release policy, rather than a wildcard bypass.

## 6. Integrity is not publisher authentication

**Chosen:** pull manifests carry a SHA-256 digest, which is passed to Arduino `Update` for image hashing.
HTTPS validates the server when a trusted CA certificate is configured.

**Known limitation:** a hash inside an unauthenticated HTTP manifest is not a signature. Anyone able to
replace both manifest and image can replace the digest too. Local browser uploads are protected by Basic
Auth but the HTTP traffic, including credentials, is not encrypted. Signed firmware, Secure Boot
provisioning, and signing-key management remain future production-security work.

## 7. Partition layout is a deployment contract

**Chosen:** provide explicit 4 MiB and 8 MiB partition CSVs with two OTA slots and an OTA data partition.
Builds must use the same partition layout already present on the target.

**Why:** the native bootloader reads the partition table already installed in flash. Ordinary application
OTA does not safely migrate partition offsets or size. Partition migration requires a separate, recovery-
aware operation.

**Trade-off:** fixed slots bound application size. Build/CI checks must reject layouts outside the chip's
flash size, and release checks must ensure the application fits in either slot.

## 8. Main-loop service rather than another FreeRTOS task

**Chosen:** applications call `espSystem.loop()` from the ordinary main loop. The manager is idle except
when network checks or updates are requested.

**Why:** fewer concurrency hazards and lower conceptual cost for small Arduino projects.

**Trade-off:** manifest checks and downloads can block the main loop. Applications with strict real-time
control must provide appropriate safety gating and may eventually require a task-based/nonblocking update
implementation. Do not assume the current HTTP transfer is nonblocking.

## 9. Version and configuration policy

**Chosen:** projects pin a tagged ESP System library version. Firmware metadata identifies product,
hardware, version, build, Git revision, API and configuration schema.

**Known limit:** version comparison currently expects numeric `MAJOR.MINOR.PATCH`-style components; it
is not a full SemVer implementation and should not be used for prerelease ordering.

**Deferred:** automated schema migrations are application-specific. The library reports a schema number
but does not transform old configuration data for the application.

## 10. Bootstrap and template have different jobs

- `bootstrap/wifi-ota/`: minimal USB-first provisioning image for a new Wi-Fi board.
- `templates/wifi-application/`: starting point for a real application repository.
- `src/`: reusable system library compiled into each application.

The Wi-Fi template is the default for current projects. A future Zigbee-only use case should add a
separate transport-specific template and provisioning flow rather than pretending the current Wi-Fi
bootstrap supports it.
