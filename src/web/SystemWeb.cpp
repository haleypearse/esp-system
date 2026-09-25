#include "SystemWeb.h"
#include "../ota/OtaManager.h"
#include "../connectivity/WifiService.h"
#include <ArduinoJson.h>
#include <Update.h>

namespace EspSystem {

SystemWeb::SystemWeb(OtaManager& ota, WifiService& wifi)
    : _server(80), _ota(ota), _wifi(wifi)
{
}

void SystemWeb::begin(const FirmwareInfo& firmware,
                      const char* hostname,
                      const char* otaUsername,
                      const char* otaPassword)
{
    _firmware = firmware;
    _hostname = hostname && *hostname ? String(hostname) : String("esp32");
    _otaUsername = otaUsername ? String(otaUsername) : String();
    _otaPassword = otaPassword ? String(otaPassword) : String();

    setupRoutes();
    _server.begin();
    _enabled = true;
}

void SystemWeb::loop()
{
    if (_enabled) _server.handleClient();
}

bool SystemWeb::authenticateUpdate()
{
    if (_otaUsername.isEmpty()) return false;
    if (_server.authenticate(_otaUsername.c_str(), _otaPassword.c_str())) return true;
    _server.requestAuthentication(BASIC_AUTH, "ESP System OTA");
    return false;
}

void SystemWeb::setupRoutes()
{
    _server.on("/", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/update", HTTP_GET, [this]() { handleUpdatePage(); });
    _server.on("/api/system", HTTP_GET, [this]() { handleStatus(); });
    _server.on("/api/update/check", HTTP_POST, [this]() {
        if (!authenticateUpdate()) return;
        const bool found = _ota.checkForUpdate();
        _server.send(found ? 200 : 204, "application/json", found ? "{\"available\":true}" : "");
    });
    _server.on("/api/update/install", HTTP_POST, [this]() {
        if (!authenticateUpdate()) return;
        const bool ok = _ota.installAvailable();
        if (!ok) {
            JsonDocument doc;
            doc["ok"] = false;
            doc["message"] = _ota.status().message;
            String body;
            serializeJson(doc, body);
            _server.send(409, "application/json", body);
            return;
        }
        _server.send(200, "application/json", "{\"ok\":true}");
    });

    // Browser upload is multipart/form-data POST /api/update/upload.
    _server.on("/api/update/upload", HTTP_POST,
               [this]() { handleUpdateRequest(); },
               [this]() { handleUpload(); });

    _server.onNotFound([this]() { handleNotFound(); });
}

void SystemWeb::handleRoot()
{
    _server.send(200, "text/html; charset=utf-8", htmlPage());
}

void SystemWeb::handleStatus()
{
    JsonDocument doc;
    doc["product"] = _firmware.product;
    doc["hardware"] = _firmware.hardware;
    doc["version"] = _firmware.version;
    doc["build"] = _firmware.build;
    doc["git"] = _firmware.git;
    doc["config_schema"] = _firmware.configSchema;
    doc["api_version"] = _firmware.apiVersion;
    doc["hostname"] = _hostname;
    doc["ip"] = _wifi.address().toString();
    doc["wifi_connected"] = _wifi.connected();
    doc["ota_state"] = static_cast<int>(_ota.status().state);
    doc["ota_message"] = _ota.status().message;
    doc["update_version"] = _ota.status().candidateVersion;
    doc["update_source"] = _ota.status().source;
    doc["rollback_available"] = _ota.rollbackAvailable();

    String body;
    serializeJson(doc, body);
    _server.send(200, "application/json", body);
}

void SystemWeb::handleUpdatePage()
{
    if (!authenticateUpdate()) return;

    const OtaStatus& st = _ota.status();
    String body;
    body.reserve(5000);
    body += "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>ESP System Update</title>";
    body += "<style>body{font-family:system-ui;max-width:720px;margin:40px auto;padding:0 18px}pre{background:#f4f4f4;padding:12px;overflow:auto}button{padding:8px 14px;margin:4px 0}input{margin:6px 0}code{font-family:ui-monospace}</style></head><body>";
    body += "<h1>ESP System</h1><p><b>" + _firmware.product + "</b> &mdash; " + _firmware.version + "</p>";
    body += "<p>Hardware: <code>" + _firmware.hardware + "</code><br>Build: <code>" + _firmware.build + "</code><br>Git: <code>" + _firmware.git + "</code></p>";
    body += "<h2>Firmware update</h2>";
    body += "<form method='POST' action='/api/update/check'><button>Check for remote update</button></form>";
    if (st.state == OtaState::UpdateAvailable) {
        body += "<p>Update available: <b>" + st.candidateVersion + "</b></p>";
        if (!st.candidateNotes.isEmpty()) body += "<p>" + st.candidateNotes + "</p>";
        body += "<form method='POST' action='/api/update/install'><button>Install remote update</button></form>";
    }
    body += "<h3>Local firmware upload</h3>";
    body += "<form method='POST' enctype='multipart/form-data' action='/api/update/upload'>";
    body += "<input type='file' name='firmware' accept='.bin,application/octet-stream' required><br><button type='submit'>Upload firmware</button></form>";
    body += "<h3>Status</h3><pre>" + st.message + "</pre>";
    body += "</body></html>";
    _server.send(200, "text/html; charset=utf-8", body);
}

void SystemWeb::handleUpdateRequest()
{
    // Multipart upload authentication is performed at UPLOAD_FILE_START, before
    // firmware bytes are written.
    if (!_uploadAuthorized) return;
    const OtaStatus& st = _ota.status();
    const bool ok = st.state == OtaState::Installing || st.state == OtaState::Confirmed;
    _server.send(ok ? 200 : 500, "text/plain", st.message);
}

void SystemWeb::handleUpload()
{
    HTTPUpload& upload = _server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        _uploadStarted = false;
        _uploadAuthorized = authenticateUpdate();
        if (!_uploadAuthorized) return;
        if (!upload.filename.endsWith(".bin")) return;
        _uploadStarted = _ota.beginUpload();
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (!_uploadStarted || !_uploadAuthorized) return;
        const size_t written = _ota.writeUpload(upload.buf, upload.currentSize);
        if (written != upload.currentSize) _ota.abortUpload();
    } else if (upload.status == UPLOAD_FILE_END) {
        if (!_uploadStarted || !_uploadAuthorized) return;
        _ota.endUpload(true);
        _uploadStarted = false;
        _uploadAuthorized = false;
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        if (_uploadStarted) _ota.abortUpload();
        _uploadStarted = false;
        _uploadAuthorized = false;
    }
}

void SystemWeb::handleNotFound()
{
    _server.send(404, "text/plain", "Not found");
}

String SystemWeb::htmlPage() const
{
    String html;
    html.reserve(3000);
    html += "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>";
    html += _firmware.product;
    html += "</title><style>body{font-family:system-ui;max-width:720px;margin:40px auto;padding:0 18px}a{display:inline-block;margin:8px 0}pre{background:#f4f4f4;padding:12px}</style></head><body>";
    html += "<h1>" + _firmware.product + "</h1>";
    html += "<p>Firmware <b>" + _firmware.version + "</b><br>Hardware <code>" + _firmware.hardware + "</code><br>IP <code>" + _wifi.address().toString() + "</code></p>";
    html += "<p><a href='/api/system'>System JSON</a></p>";
    if (!_otaUsername.isEmpty()) html += "<p><a href='/update'>Firmware update</a></p>";
    html += "</body></html>";
    return html;
}

} // namespace EspSystem
