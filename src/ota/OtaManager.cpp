#include "OtaManager.h"

#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <mbedtls/version.h>
#if MBEDTLS_VERSION_MAJOR >= 4
#include <psa/crypto.h>
#else
#include <mbedtls/sha256.h>
#endif
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

// Mbed TLS 4 (used by newer Arduino-ESP32 / ESP-IDF builds) removes the legacy
// mbedtls_sha256_* interface. Use PSA Crypto there while keeping the classic
// mbedTLS SHA-256 API for Arduino-ESP32 2.x / ESP-IDF 4.4 installations.
#if MBEDTLS_VERSION_MAJOR >= 4
using Sha256Context = psa_hash_operation_t;

bool sha256Init(Sha256Context& ctx)
{
    psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS) return false;

    ctx = psa_hash_operation_init();
    status = psa_hash_setup(&ctx, PSA_ALG_SHA_256);
    if (status != PSA_SUCCESS) {
        psa_hash_abort(&ctx);
        ctx = psa_hash_operation_init();
        return false;
    }
    return true;
}

bool sha256Update(Sha256Context& ctx, const uint8_t* data, size_t len)
{
    return psa_hash_update(&ctx, data, len) == PSA_SUCCESS;
}

bool sha256Finish(Sha256Context& ctx, uint8_t digest[32])
{
    size_t digestLength = 0;
    const psa_status_t status =
        psa_hash_finish(&ctx, digest, 32, &digestLength);
    ctx = psa_hash_operation_init();
    return status == PSA_SUCCESS && digestLength == 32;
}

void sha256Free(Sha256Context& ctx)
{
    psa_hash_abort(&ctx);
    ctx = psa_hash_operation_init();
}
#else
using Sha256Context = mbedtls_sha256_context;

bool sha256Init(Sha256Context& ctx)
{
    mbedtls_sha256_init(&ctx);
#if MBEDTLS_VERSION_MAJOR >= 3
    return mbedtls_sha256_starts(&ctx, 0) == 0;
#else
    return mbedtls_sha256_starts_ret(&ctx, 0) == 0;
#endif
}

bool sha256Update(Sha256Context& ctx, const uint8_t* data, size_t len)
{
#if MBEDTLS_VERSION_MAJOR >= 3
    return mbedtls_sha256_update(&ctx, data, len) == 0;
#else
    return mbedtls_sha256_update_ret(&ctx, data, len) == 0;
#endif
}

bool sha256Finish(Sha256Context& ctx, uint8_t digest[32])
{
#if MBEDTLS_VERSION_MAJOR >= 3
    return mbedtls_sha256_finish(&ctx, digest) == 0;
#else
    return mbedtls_sha256_finish_ret(&ctx, digest) == 0;
#endif
}

void sha256Free(Sha256Context& ctx)
{
    mbedtls_sha256_free(&ctx);
}
#endif

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
    Sha256Context shaCtx{};
    uint8_t shaDigest[32] = {0};

    if (verifySha && !sha256Init(shaCtx)) {
        Update.abort();
        fail("Could not initialize SHA-256 verification");
        return false;
    }

    uint8_t buffer[1024];
    size_t totalWritten = 0;

    while (totalWritten < size) {
        const size_t toRead = std::min(sizeof(buffer), size - totalWritten);
        const int got = stream.readBytes(reinterpret_cast<char*>(buffer), toRead);
        if (got <= 0) {
            if (verifySha) sha256Free(shaCtx);
            Update.abort();
            fail("Stream read failed during OTA", UPDATE_ERROR_STREAM);
            return false;
        }

        if (verifySha &&
            !sha256Update(shaCtx, buffer, static_cast<size_t>(got))) {
            sha256Free(shaCtx);
            Update.abort();
            fail("SHA-256 update failed");
            return false;
        }

        const size_t written = Update.write(buffer, static_cast<size_t>(got));
        if (written != static_cast<size_t>(got)) {
            if (verifySha) sha256Free(shaCtx);
            Update.abort();
            fail(String("Only wrote ") + written + " of " + got + " bytes", UPDATE_ERROR_WRITE);
            return false;
        }

        totalWritten += written;
    }

    if (verifySha) {
        if (!sha256Finish(shaCtx, shaDigest)) {
            sha256Free(shaCtx);
            Update.abort();
            fail("SHA-256 finalization failed");
            return false;
        }

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

    // Fail closed: a release must explicitly target this product and hardware.
    const bool productMatches = !candidate.product.isEmpty() && candidate.product == _current.product;
    const bool hardwareMatches = !candidate.hardware.isEmpty() && candidate.hardware == _current.hardware;
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
    // The common installer runs preUpdateAllowed() once. Avoid invoking a
    // potentially stateful application callback twice for one installation.
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
