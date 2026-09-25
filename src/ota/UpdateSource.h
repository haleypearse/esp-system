#pragma once

#include <Arduino.h>

namespace EspSystem {

class OtaManager;

struct OtaCandidate {
    String product;
    String version;
    String hardware;
    String url;
    String sha256;
    String notes;
    size_t size = 0;
};

class UpdateSource {
public:
    virtual ~UpdateSource() = default;

    // Discover a candidate. This must not install anything.
    virtual bool check(OtaCandidate& candidate) = 0;

    // Install the specified candidate using OtaManager's common installer.
    virtual bool install(const OtaCandidate& candidate, OtaManager& ota) = 0;

    virtual const char* name() const = 0;
};

} // namespace EspSystem
