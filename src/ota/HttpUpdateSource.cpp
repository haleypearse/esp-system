#include "HttpUpdateSource.h"
#include "OtaManager.h"

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

namespace EspSystem {

HttpUpdateSource::HttpUpdateSource(const String& manifestUrl)
    : _manifestUrl(manifestUrl)
{
}

static bool isHttps(const String& url)
{
    return url.startsWith("https://");
}

bool HttpUpdateSource::getText(const String& url, String& body)
{
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;

    if (isHttps(url)) {
        if (_rootCa.isEmpty()) return false;
        WiFiClientSecure client;
        client.setCACert(_rootCa.c_str());
        if (!http.begin(client, url)) return false;

        const int code = http.GET();
        if (code != HTTP_CODE_OK) {
            http.end();
            return false;
        }
        body = http.getString();
        http.end();
        return true;
    }

    WiFiClient client;
    if (!http.begin(client, url)) return false;

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }
    body = http.getString();
    http.end();
    return true;
}

bool HttpUpdateSource::check(OtaCandidate& candidate)
{
    if (_manifestUrl.isEmpty()) return false;

    String body;
    if (!getText(_manifestUrl, body)) return false;

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, body);
    if (err) return false;

    candidate.product = doc["product"] | "";
    candidate.version = doc["version"] | "";
    candidate.hardware = doc["hardware"] | "";
    candidate.url = doc["url"] | "";
    candidate.sha256 = doc["sha256"] | "";
    candidate.notes = doc["notes"] | "";
    candidate.size = doc["size"] | 0;

    if (candidate.product.isEmpty() || candidate.version.isEmpty() ||
        candidate.hardware.isEmpty() || candidate.url.isEmpty() ||
        candidate.sha256.length() != 64 || candidate.size == 0) return false;

    // Reject malformed digests before they reach the flash writer.
    for (size_t i = 0; i < candidate.sha256.length(); ++i) {
        const char c = candidate.sha256[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

bool HttpUpdateSource::install(const OtaCandidate& candidate, OtaManager& ota)
{
    if (WiFi.status() != WL_CONNECTED || candidate.url.isEmpty()) return false;

    HTTPClient http;
    if (isHttps(candidate.url)) {
        if (_rootCa.isEmpty()) return false;
        WiFiClientSecure client;
        client.setCACert(_rootCa.c_str());
        if (!http.begin(client, candidate.url)) return false;

        const int code = http.GET();
        if (code != HTTP_CODE_OK) {
            http.end();
            return false;
        }
        const size_t size = http.getSize() > 0 ? static_cast<size_t>(http.getSize()) : candidate.size;
        const bool ok = size > 0 && ota.installFromStream(http.getStream(), size, candidate.sha256, true);
        http.end();
        return ok;
    }

    WiFiClient client;
    if (!http.begin(client, candidate.url)) return false;

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    const size_t size = http.getSize() > 0 ? static_cast<size_t>(http.getSize()) : candidate.size;
    if (size == 0) {
        http.end();
        return false;
    }

    const bool ok = ota.installFromStream(http.getStream(), size, candidate.sha256, true);
    http.end();
    return ok;
}

} // namespace EspSystem
