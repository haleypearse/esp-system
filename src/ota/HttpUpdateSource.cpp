#include "HttpUpdateSource.h"
#include "OtaManager.h"

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <ArduinoJson.h>

namespace EspSystem {

static bool isHttps(const String& url)
{
    return url.startsWith("https://");
}

bool HttpUpdateSource::getText(const String& url, String& body)
{
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;

    if (isHttps(url)) {
        if (!_rootCa || !*_rootCa) return false;
        auto* client = new NetworkClientSecure();
        client->setCACert(_rootCa);
        if (!http.begin(*client, url)) {
            delete client;
            return false;
        }
        const int code = http.GET();
        if (code != HTTP_CODE_OK) {
            http.end();
            delete client;
            return false;
        }
        body = http.getString();
        http.end();
        delete client;
        return true;
    }

    auto* client = new NetworkClient();
    if (!http.begin(*client, url)) {
        delete client;
        return false;
    }
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        delete client;
        return false;
    }
    body = http.getString();
    http.end();
    delete client;
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

    return !candidate.product.isEmpty() && !candidate.version.isEmpty() &&
           !candidate.hardware.isEmpty() && !candidate.url.isEmpty() &&
           candidate.sha256.length() == 64;
}

bool HttpUpdateSource::install(const OtaCandidate& candidate, OtaManager& ota)
{
    if (WiFi.status() != WL_CONNECTED || candidate.url.isEmpty()) return false;

    HTTPClient http;
    if (isHttps(candidate.url)) {
        if (!_rootCa || !*_rootCa) return false;
        auto* client = new NetworkClientSecure();
        client->setCACert(_rootCa);
        if (!http.begin(*client, candidate.url)) {
            delete client;
            return false;
        }

        const int code = http.GET();
        if (code != HTTP_CODE_OK) {
            http.end();
            delete client;
            return false;
        }
        const size_t size = http.getSize() > 0 ? static_cast<size_t>(http.getSize()) : candidate.size;
        const bool ok = size > 0 && ota.installFromStream(http.getStream(), size, candidate.sha256, true);
        http.end();
        delete client;
        return ok;
    }

    auto* client = new NetworkClient();
    if (!http.begin(*client, candidate.url)) {
        delete client;
        return false;
    }

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        delete client;
        return false;
    }

    const size_t size = http.getSize() > 0 ? static_cast<size_t>(http.getSize()) : candidate.size;
    if (size == 0) {
        http.end();
        delete client;
        return false;
    }

    const bool ok = ota.installFromStream(http.getStream(), size, candidate.sha256, true);
    http.end();
    delete client;
    return ok;
}

} // namespace EspSystem
