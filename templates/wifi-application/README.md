# Wi-Fi application template

Use this directory as the starting point for new Wi-Fi-connected ESP32 applications. Copy it into a new project repository, then edit `platformio.ini`, `src/main.cpp`, and application code.

The template depends on the versioned ESP System library. The system provides Wi-Fi connectivity, a separate HTTP management server (default port **8080**), browser firmware upload, remote manifest checks, version metadata, and OTA boot health confirmation. Your application may run its own web server on port 80 without registering OTA routes in that server.

## First deployment

- For a virgin board, install `bootstrap/wifi-ota` over USB first.
- Keep the partition table compatible with the currently installed board layout and ensure the application fits both OTA slots.
- Preserve `espSystem.begin(config)` in `setup()` and `espSystem.loop()` in `loop()`.
- Update `config.firmware.product`, `.hardware`, `.version`, and `.configSchema`.
- Use meaningful pre-update safety and post-boot health callbacks for applications with physical effects.

Management URL: `http://<device-hostname>.local:8080/` (or the board's IP address on port 8080). Configure `managementPort` to use another port.

For the integration rules and acceptance checks, read `docs/OTA_REQUIREMENTS.md` and `docs/OTA_INTEGRATION.md` in the ESP System repository.

## Partition variants and coredumps

The default `partitions/ota_4mb.csv` matches the standard bootstrap table and reserves 64 KiB for
flash core dumps. For a board with 8 MiB flash, switch both `board_build.partitions` and the selected
installed table to `partitions/ota_8mb.csv`. Changing an installed table is not possible through an
ordinary application OTA; flash the table over serial and retain a recovery path.
