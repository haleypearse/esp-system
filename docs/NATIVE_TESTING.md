# Native tests

The native test target exercises portable ESP System algorithms without Arduino,
ESP-IDF, FreeRTOS, Wi-Fi or physical hardware. It is intended for code such as
telemetry statistics, bucket merging, retention policy and coordination state
machines. Hardware adapters and the actual ESP32 application still use the
existing PlatformIO build.

Requirements:

- CMake 3.16 or newer
- A C++17 compiler

From the repository root:

```sh
cmake -S . -B build/native
cmake --build build/native
ctest --test-dir build/native --output-on-failure
```

The build directory is ignored by Git. GitHub Actions runs this test target
alongside the ESP32 bootstrap and application-template builds.

Host tests do not emulate flash wear, power interruption at the physical
filesystem layer, RF interference, ADC behaviour, FreeRTOS scheduling or
inter-device clock drift. Those require later hardware/integration tests.
