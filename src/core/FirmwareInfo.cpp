#include "FirmwareInfo.h"
#include <ArduinoJson.h>

namespace EspSystem {

static int part(const String& s, int index)
{
    int start = 0;
    int end = -1;
    for (int i = 0; i <= index; ++i) {
        start = (i == 0) ? 0 : end + 1;
        end = s.indexOf('.', start);
        if (end < 0) end = s.length();
    }
    String p = s.substring(start, end);
    int value = p.toInt();
    return value < 0 ? 0 : value;
}

int compareVersions(const String& a, const String& b)
{
    const int am = part(a, 0), bm = part(b, 0);
    if (am != bm) return am < bm ? -1 : 1;
    const int an = part(a, 1), bn = part(b, 1);
    if (an != bn) return an < bn ? -1 : 1;
    const int ap = part(a, 2), bp = part(b, 2);
    if (ap != bp) return ap < bp ? -1 : 1;
    return 0;
}

String makeVersionJson(const FirmwareInfo& info)
{
    JsonDocument doc;
    doc["product"] = info.product;
    doc["hardware"] = info.hardware;
    doc["version"] = info.version;
    doc["build"] = info.build;
    doc["git"] = info.git;
    doc["config_schema"] = info.configSchema;
    doc["api_version"] = info.apiVersion;
    String out;
    serializeJson(doc, out);
    return out;
}

} // namespace EspSystem
