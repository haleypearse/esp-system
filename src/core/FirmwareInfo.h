#pragma once

#include <Arduino.h>

namespace EspSystem {

struct FirmwareInfo {
    String product;
    String hardware;
    String version;
    String build;
    String git;
    uint32_t configSchema = 1;
    uint32_t apiVersion = 1;
};

int compareVersions(const String& a, const String& b);
String makeVersionJson(const FirmwareInfo& info);

} // namespace EspSystem
