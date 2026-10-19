#include "OtaManager.h"

#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>
#include <algorithm>
#include <cctype>

namespace EspSystem {

void OtaManager::begin(const FirmwareInfo& current, uint32_t confirmationDelayMs)
{
    _current = current;
    _confirmationDelayMs = confirmationDelayMs;
    _bootStartedMs = millis();

    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        _pendingConfirmation = true;
        _status.state = OtaState::PendingVerification;
        _status.message = "New firmware awaiting health confirmation";
    } else {
        _status.state = OtaState::Idle;
        _status.message = "Ready";
    }
}

void OtaManager::loop()
{
    if (_rebootPending && static_cast<int32_t>(millis() - _rebootAtMs) >= 0) {
        ESP.restart();
    }

    if (!_pendingConfirmation) return;
    if (millis() - _bootStartedMs < _confirmationDelayMs) return;

    const bool healthy = _healthCheck ? _healthCheck() : true;
    if (healthy) {
        confirmBoot();
    } else {
        _status.state = OtaState::Failed;
        _status.message = "Boot health check failed; rolling back";
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}

bool OtaManager::preUpdateAllowed()
{
    if (_preUpdateCheck && !_preUpdateCheck()) {
        fail("Application vetoed update");
        return false;
    }
    return true;
}

namespace {
bool isValidSha256Hex(const String& s)
{
    if (s.length() != 64) return false;
    for (size_t i = 0; i < s.length(); ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    return true;
}

String toHexLower(const uint8_t* bytes, size_t len)
{
    static const char* kHex = "0123456789abcdef";
    String out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out += kHex[(bytes[i] >> 4) & 0x0F];
        out += kHex[bytes[i] & 0x0F];
    }
    return out;
}
} // namespace

bool OtaManager::beginNativeUpdate(size_t size, const String& expectedSha256)
{
    if (!preUpdateAllowed()) return false;

    const size_t updateSize = (size == 0) ? UPDATE_SIZE_UNKNOWN : size;
    if (!Update.begin(updateSize)) {
        fail(String("Update.begin failed: ") + Update.errorString(), Update.getError());
        return false;
    }

    if (!expectedSha256.isEmpty() && !isValidSha256Hex(expectedSha256)) {
        Update.abort();
        fail("Invalid expected SHA-256", UPDATE_ERROR_BAD_ARGUMENT);
        return false;
    }

    Update.onProgress([this](size_t done, size_t total) {
        _status.progressBytes = done;
        _status.totalBytes = total;
        if (_progressCallback) _progressCallback(done, total);
    });

    _status.state = OtaState::Installing;
    _status.message = "Installing firmware";
    _status.progressBytes = 0;
    _status.totalBytes = (updateSize == UPDATE_SIZE_UNKNOWN) ? 0 : updateSize;
    return true;
}

bool OtaManager::installFromStream(Stream& stream, size_t size, const String& expectedSha256, bool rebootOnSuccess)
{
    if (!beginNativeUpdate(size, expectedSha256)) return false;

    const bool verifySha = !expectedSha256.isEmpty();
    mbedtls_sha256_context shaCtx;
    uint8_t shaDigest[32] = {0};

    if (verifySha) {
        mbedtls_sha256_init(&shaCtx);
        mbedtls_sha256_starts_ret(&shaCtx, 0);
    }

    uint8_t buffer[1024];
    size_t totalWritten = 0;

    while (totalWritten < size) {
        const size_t toRead = std::min(sizeof(buffer), size - totalWritten);
        const int got = stream.readBytes(reinterpret_cast<char*>(buffer), toRead);
        if (got <= 0) {
            if (verifySha) mbedtls_sha256_free(&shaCtx);
            Update.abort();
            fail("Stream read failed during OTA", UPDATE_ERROR_STREAM);
            return false;
        }

        if (verifySha) {
            mbedtls_sha256_update_ret(&shaCtx, buffer, static_cast<size_t>(got));
        }

        const size_t written = Update.write(buffer, static_cast<size_t>(got));
        if (written != static_cast<size_t>(got)) {
            if (verifySha) mbedtls_sha256_free(&shaCtx);
            Update.abort();
            fail(String("Only wrote ") + written + " of " + got + " bytes", UPDATE_ERROR_WRITE);
            return false;
        }

        totalWritten += written;
    }

    if (verifySha) {
        mbedtls_sha256_finish_ret(&shaCtx, shaDigest);
        mbedtls_sha256_free(&shaCtx);

        String actual = toHexLower(shaDigest, sizeof(shaDigest));
        String expected = expectedSha256;
        expected.toLowerCase();
        if (actual != expected) {
            Update.abort();
            fail("SHA-256 mismatch", UPDATE_ERROR_MD5);
            return false;
        }
    }

    if (!Update.end()) {
        fail(String("Update failed: ") + Update.errorString(), Update.getError());
        return false;
    }

    _status.state = OtaState::Installing;
    _status.message = "Firmware installed; rebooting";
    if (rebootOnSuccess) {
        _rebootPending = true;
        _rebootAtMs = millis() + 250;
    }
    return true;
}

bool OtaManager::checkForUpdate()
{
    if (!_source) {
        fail("No update source configured");
        return false;
    }

    _status.state = OtaState::Checking;
    _status.candidateVersion = String();
    _status.candidateHardware = String();
    _status.candidateNotes = String();
    _status.source = _source->name();
    _status.message = "Checking for firmware";
    _status.lastCheckMs = millis();

    OtaCandidate candidate;
    if (!_source->check(candidate)) {
        _status.state = OtaState::Idle;
        _status.message = "No update found or check failed";
        return false;
    }

    const bool productMatches = candidate.product == "*" || candidate.product == _current.product;
    const bool hardwareMatches = candidate.hardware == "*" || candidate.hardware == _current.hardware;
    const bool newer = compareVersions(candidate.version, _current.version) > 0;

    if (!productMatches || !hardwareMatches || !newer) {
        _status.state = OtaState::Idle;
        if (!productMatches) _status.message = "Update rejected: product mismatch";
        else if (!hardwareMatches) _status.message = "Update rejected: hardware mismatch";
        else _status.message = "No newer compatible firmware";
        return false;
    }

    _candidate = candidate;
    _status.state = OtaState::UpdateAvailable;
    _status.message = "Update available";
    _status.candidateVersion = candidate.version;
    _status.candidateHardware = candidate.hardware;
    _status.candidateNotes = candidate.notes;
    _status.totalBytes = candidate.size;
    return true;
}

bool OtaManager::installAvailable()
{
    if (!_source || _status.state != OtaState::UpdateAvailable) return false;
    if (!preUpdateAllowed()) return false;

    return _source->install(_candidate, *this);
}

bool OtaManager::beginUpload(size_t totalSize)
{
    if (_uploadInProgress) return false;
    if (!beginNativeUpdate(totalSize, String())) return false;

    _uploadInProgress = true;
    _uploadExpected = totalSize; // 0 means unknown-size multipart upload.
    _uploadWritten = 0;
    return true;
}

size_t OtaManager::writeUpload(const uint8_t* data, size_t len)
{
    if (!_uploadInProgress || !data || len == 0) return 0;
    const size_t written = Update.write(const_cast<uint8_t*>(data), len);
    _uploadWritten += written;
    return written;
}

bool OtaManager::endUpload(bool rebootOnSuccess)
{
    if (!_uploadInProgress) return false;
    _uploadInProgress = false;

    if (_uploadExpected != 0 && _uploadWritten != _uploadExpected) {
        Update.abort();
        fail(String("Upload size mismatch: wrote ") + _uploadWritten + " of " + _uploadExpected, UPDATE_ERROR_WRITE);
        return false;
    }

    if (!Update.end(_uploadExpected == 0)) {
        fail(String("Upload failed: ") + Update.errorString(), Update.getError());
        return false;
    }

    _status.message = "Firmware uploaded; rebooting";
    if (rebootOnSuccess) {
        _rebootPending = true;
        _rebootAtMs = millis() + 250;
    }
    return true;
}

void OtaManager::abortUpload()
{
    if (!_uploadInProgress) return;
    _uploadInProgress = false;
    Update.abort();
    fail("Firmware upload aborted", UPDATE_ERROR_ABORT);
}

void OtaManager::confirmBoot()
{
    if (!_pendingConfirmation) return;
    const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        _pendingConfirmation = false;
        _status.state = OtaState::Confirmed;
        _status.message = "Firmware confirmed";
    } else {
        fail(String("Could not confirm firmware: ") + esp_err_to_name(err), err);
    }
}

bool OtaManager::rollbackAvailable() const
{
    return Update.canRollBack();
}

bool OtaManager::rollbackAndReboot()
{
    if (!Update.canRollBack()) return false;
    return Update.rollBack();
}

void OtaManager::fail(const String& message, int error)
{
    _status.state = OtaState::Failed;
    _status.message = message;
    _status.lastError = error;
}

} // namespace EspSystem

// Arduino-ESP32 currently auto-confirms pending OTA images during startup.
// Provide the documented weak-hook override so our System layer gets a chance
// to run an application health check before rollback is cancelled.
extern "C" bool verifyRollbackLater(void)
{
    return true;
}
