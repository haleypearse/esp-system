#pragma once

#include <Arduino.h>
#include "UpdateSource.h"

namespace EspSystem {

class OtaManager;

class HttpUpdateSource final : public UpdateSource {
public:
    explicit HttpUpdateSource(const String& manifestUrl = String());

    void setManifestUrl(const String& url) { _manifestUrl = url; }
    const String& manifestUrl() const { return _manifestUrl; }

    // For HTTPS, supply a PEM-encoded root CA. HTTP works without this.
    void setRootCACertificate(const char* pem) { _rootCa = pem ? String(pem) : String(); }

    bool check(OtaCandidate& candidate) override;
    bool install(const OtaCandidate& candidate, OtaManager& ota) override;
    const char* name() const override { return "http"; }

private:
    bool getText(const String& url, String& body);

    String _manifestUrl;
    String _rootCa;
};

} // namespace EspSystem
