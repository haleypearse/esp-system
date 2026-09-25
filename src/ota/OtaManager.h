#pragma once

#include <Arduino.h>
#include <functional>
#include <utility>
#include "../core/FirmwareInfo.h"
#include "UpdateSource.h"

namespace EspSystem {

enum class OtaState : uint8_t {
    Idle,
    Checking,
    UpdateAvailable,
    Installing,
    PendingVerification,
    Confirmed,
    Failed
};

struct OtaStatus {
    OtaState state = OtaState::Idle;
    String message;
    String candidateVersion;
    String candidateHardware;
    String candidateNotes;
    uint32_t progressBytes = 0;
    uint32_t totalBytes = 0;
    int lastError = 0;
    String source;
    uint32_t lastCheckMs = 0;
};

class OtaManager {
public:
    using HealthCheck = std::function<bool()>;
    using PreUpdateCheck = std::function<bool()>;
    using ProgressCallback = std::function<void(size_t, size_t)>;

    void begin(const FirmwareInfo& current, uint32_t confirmationDelayMs = 10000);
    void loop();

    void setUpdateSource(UpdateSource* source) { _source = source; }
    void setHealthCheck(HealthCheck fn) { _healthCheck = std::move(fn); }
    void setPreUpdateCheck(PreUpdateCheck fn) { _preUpdateCheck = std::move(fn); }
    void setProgressCallback(ProgressCallback fn) { _progressCallback = std::move(fn); }

    bool checkForUpdate();
    bool installAvailable();

    bool beginUpload(size_t totalSize = 0);
    size_t writeUpload(const uint8_t* data, size_t len);
    bool endUpload(bool rebootOnSuccess = true);
    void abortUpload();

    bool installFromStream(Stream& stream, size_t size, const String& expectedSha256 = String(), bool rebootOnSuccess = true);

    void confirmBoot();
    bool rollbackAvailable() const;
    bool rollbackAndReboot();

    const OtaStatus& status() const { return _status; }
    const FirmwareInfo& currentFirmware() const { return _current; }

private:
    bool beginNativeUpdate(size_t size, const String& expectedSha256);
    bool preUpdateAllowed();
    void fail(const String& message, int error = 0);

    FirmwareInfo _current;
    OtaStatus _status;
    UpdateSource* _source = nullptr;
    OtaCandidate _candidate;
    HealthCheck _healthCheck;
    PreUpdateCheck _preUpdateCheck;
    ProgressCallback _progressCallback;
    uint32_t _confirmationDelayMs = 10000;
    uint32_t _bootStartedMs = 0;
    bool _pendingConfirmation = false;
    bool _uploadInProgress = false;
    bool _rebootPending = false;
    uint32_t _rebootAtMs = 0;
    size_t _uploadExpected = 0;
    size_t _uploadWritten = 0;
};

} // namespace EspSystem
