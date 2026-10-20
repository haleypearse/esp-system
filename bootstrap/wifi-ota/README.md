# Wi-Fi OTA bootstrap

This is a minimal **first-install image**, not the template for ordinary application development. Flash it over USB once to establish Wi-Fi and the ESP System management endpoint. Subsequent complete application images should include the `esp-system` dependency.

## Provision

1. Copy `include/secrets.h.example` to `include/secrets.h`.
2. Replace the example Wi-Fi and OTA credentials with your actual values. Do not use the sample password. Do not commit `secrets.h`.
3. Build and flash this project over USB.
4. Open `http://esp32-ota.local:8080/` (or the board's IP address with `:8080`).
5. Select **Firmware update** to upload a compatible application `.bin`.

The bootstrap and future application must use compatible OTA partition layouts. The included `platformio.ini` selects `partitions/ota_4mb.csv` from the repository. Choose the table matching the board's actual flash size. An ordinary OTA update does not rewrite the partition table.

Wi-Fi and management credentials are stored in NVS. A later application can leave these fields empty to reuse the stored credentials, provided it uses the same NVS layout and namespace.

## Why this is separate from the template

The bootstrap exists to get a virgin board reachable without needing a custom application. To create a new application, copy `templates/wifi-application/`, then change the product/hardware identifiers and add your application code. Do not keep using this bootstrap as the starting source tree for every project.

## Limitations

The management interface uses HTTP Basic Authentication on a trusted LAN. Basic Auth over HTTP is not encrypted; do not expose port 8080 to the Internet. USB/esptool remains the recovery path if an application loses its network or OTA functionality.
