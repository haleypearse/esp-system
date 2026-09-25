# Changelog

## 0.1.0 - 2026-09-25

Initial reusable system layer.

- OTA-capable 4 MB and 8 MB partition tables
- native ESP32 application OTA installation via Arduino `Update`
- browser firmware upload over local HTTP
- device-pull HTTP manifest discovery
- HTTPS transport path requiring a configured root CA
- SHA-256 verification for remote manifests
- application health confirmation / rollback hook
- persistent Wi-Fi and OTA browser credentials in NVS
- firmware/hardware/build/config metadata
- PlatformIO Git dependency model
- minimal bootstrap and new-project template
- local manifest generation and update-server helper tools
