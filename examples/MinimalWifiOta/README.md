# MinimalWifiOta

This is the one-time bootstrap image. Flash it over USB once, then use the device web page
for subsequent firmware uploads.

1. Copy `include/secrets.h.example` to `include/secrets.h`.
2. Fill in Wi-Fi and OTA credentials.
3. Build and upload over USB.
4. Open `http://esp32-ota.local/` or the IP address shown by the serial monitor.
5. Use **Firmware update** to upload the next application image.

The Wi-Fi and OTA browser credentials are persisted in NVS. Projects derived from the standard
template can leave these fields empty and inherit the existing credentials.

## Testing a second firmware without USB

After the bootstrap is running, create a new application project from `template-project/`
and install the same `esp-system` Git tag. Build the `.bin`, open the bootstrap's `/update`
page, authenticate, and upload it. The board should reboot into the new project with its
existing Wi-Fi and OTA credentials retained in NVS.
