#include "SystemWeb.h"
#include "../ota/OtaManager.h"
#include "../connectivity/WifiService.h"
#include <ArduinoJson.h>
#include <Update.h>

namespace EspSystem {

namespace {
String htmlEscape(const String& input)
{
    String out;
    out.reserve(input.length());
    for (size_t i = 0; i < input.length(); ++i) {
        switch (input[i]) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += input[i]; break;
        }
    }
    return out;
}
} // namespace

SystemWeb::SystemWeb(OtaManager& ota, WifiService& wifi)
    : _ota(ota), _wifi(wifi)
{
}

void SystemWeb::begin(const FirmwareInfo& firmware,
                      const char* hostname,
                      const char* otaUsername,
                      const char* otaPassword,
                      uint16_t managementPort)
{
    if (managementPort == 0) {
        Serial.println("[ESP System] Management port 0 is invalid; server not started");
        return;
    }

    _port = managementPort;
    _server.reset(new WebServer(_port));
    _firmware = firmware;
    _hostname = hostname && *hostname ? String(hostname) : String("esp32");
    _otaUsername = otaUsername ? String(otaUsername) : String();
    _otaPassword = otaPassword ? String(otaPassword) : String();

    const char* requestHeaders[] = {"Origin", "Host"};
    _server->collectHeaders(requestHeaders, 2);
    setupRoutes();
    _server->begin();
    _enabled = true;
}

void SystemWeb::loop()
{
    if (_enabled && _server) _server->handleClient();
}

bool SystemWeb::originAllowed()
{
    // Modern browsers send Origin on POSTs. Reject cross-origin browser POSTs
    // before processing state-changing update actions. Non-browser clients that
    // omit Origin remain supported and must still authenticate.
    const String origin = _server ? _server->header("Origin") : String();
    if (origin.isEmpty()) return true;

    const String host = _server->header("Host");
    const String expected = String("http://") + host;
    if (!host.isEmpty() && origin.equalsIgnoreCase(expected)) return true;

    _server->send(403, "text/plain; charset=utf-8", "Cross-origin update request rejected.");
    return false;
}

bool SystemWeb::authenticateUpdate()
{
    if (!_server) return false;
    if (_otaUsername.isEmpty() || _otaPassword.isEmpty()) {
        _server->send(503, "text/plain; charset=utf-8",
                      "OTA management is unavailable: configure otaUsername and otaPassword.");
        return false;
    }
    if (_server->authenticate(_otaUsername.c_str(), _otaPassword.c_str())) return true;
    _server->requestAuthentication(BASIC_AUTH, "ESP System OTA");
    return false;
}

void SystemWeb::setupRoutes()
{
    _server->on("/", HTTP_GET, [this]() { handleRoot(); });
    _server->on("/update", HTTP_GET, [this]() { handleUpdatePage(); });
    _server->on("/api/system", HTTP_GET, [this]() { handleStatus(); });
    _server->on("/api/update/check", HTTP_POST, [this]() {
        if (!originAllowed() || !authenticateUpdate()) return;
        const bool found = _ota.checkForUpdate();
        _server->send(found ? 200 : 204, "application/json", found ? "{\"available\":true}" : "");
    });
    _server->on("/api/update/install", HTTP_POST, [this]() {
        if (!originAllowed() || !authenticateUpdate()) return;
        const bool ok = _ota.installAvailable();
        if (!ok) {
            JsonDocument doc;
            doc["ok"] = false;
            doc["message"] = _ota.status().message;
            String body;
            serializeJson(doc, body);
            _server->send(409, "application/json", body);
            return;
        }
        _server->send(200, "application/json", "{\"ok\":true}");
    });

    // Browser upload is multipart/form-data POST /api/update/upload.
    _server->on("/api/update/upload", HTTP_POST,
               [this]() { handleUpdateRequest(); },
               [this]() { handleUpload(); });

    _server->onNotFound([this]() { handleNotFound(); });
}

void SystemWeb::handleRoot()
{
    _server->send(200, "text/html; charset=utf-8", htmlPage());
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
    doc["management_port"] = _port;

    String body;
    serializeJson(doc, body);
    _server->send(200, "application/json", body);
}

void SystemWeb::handleUpdatePage()
{
    if (!authenticateUpdate()) return;

    const OtaStatus& st = _ota.status();
    String body;
    body.reserve(5000);
    body += "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>ESP System Update</title>";
    body += "<style>body{font-family:system-ui;max-width:720px;margin:40px auto;padding:0 18px}pre{background:#f4f4f4;padding:12px;overflow:auto}button{padding:8px 14px;margin:4px 0}input{margin:6px 0}code{font-family:ui-monospace}</style></head><body>";
    body += "<h1>ESP System</h1><p><b>" + htmlEscape(_firmware.product) + "</b> &mdash; " + htmlEscape(_firmware.version) + "</p>";
    body += "<p>Management interface on port <code>" + String(_port) + "</code>.</p>";
    body += "<p>Hardware: <code>" + htmlEscape(_firmware.hardware) + "</code><br>Build: <code>" + htmlEscape(_firmware.build) + "</code><br>Git: <code>" + htmlEscape(_firmware.git) + "</code></p>";
    body += "<h2>Firmware update</h2>";
    body += "<form method='POST' action='/api/update/check'><button>Check for remote update</button></form>";
    if (st.state == OtaState::UpdateAvailable) {
        body += "<p>Update available: <b>" + htmlEscape(st.candidateVersion) + "</b></p>";
        if (!st.candidateNotes.isEmpty()) body += "<p>" + htmlEscape(st.candidateNotes) + "</p>";
        body += "<form method='POST' action='/api/update/install'><button>Install remote update</button></form>";
    }
    body += "<h3>Local firmware upload</h3>";
    body += "<form method='POST' enctype='multipart/form-data' action='/api/update/upload'>";
    body += "<input type='file' name='firmware' accept='.bin,application/octet-stream' required><br><button type='submit'>Upload firmware</button></form>";
    body += "<h3>Status</h3><pre>" + htmlEscape(st.message) + "</pre>";
    body += "</body></html>";
    _server->send(200, "text/html; charset=utf-8", body);
}

void SystemWeb::handleUpdateRequest()
{
    // The multipart callback records the result. Do not infer success from the
    // OTA manager's previous state: that state may reflect an earlier update.
    if (!_uploadAuthorized) return;
    _server->send(_uploadSucceeded ? 200 : _uploadHttpStatus,
                  "text/plain; charset=utf-8", _uploadMessage);
    _uploadAuthorized = false;
}

void SystemWeb::handleUpload()
{
    HTTPUpload& upload = _server->upload();
    if (upload.status == UPLOAD_FILE_START) {
        _uploadStarted = false;
        _uploadSucceeded = false;
        _uploadHttpStatus = 400;
        _uploadMessage = "Firmware upload did not complete.";
        _uploadAuthorized = originAllowed() && authenticateUpdate();
        if (!_uploadAuthorized) return;
        if (!upload.filename.endsWith(".bin")) {
            _uploadMessage = "Choose an application .bin file.";
            return;
        }
        _uploadStarted = _ota.beginUpload();
        if (!_uploadStarted) {
            _uploadHttpStatus = 409;
            _uploadMessage = _ota.status().message;
        } else {
            _uploadMessage = "Receiving firmware image.";
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (!_uploadStarted || !_uploadAuthorized) return;
        const size_t written = _ota.writeUpload(upload.buf, upload.currentSize);
        if (written != upload.currentSize) {
            _ota.abortUpload();
            _uploadStarted = false;
            _uploadHttpStatus = 500;
            _uploadMessage = _ota.status().message;
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (!_uploadStarted || !_uploadAuthorized) return;
        _uploadSucceeded = _ota.endUpload(true);
        _uploadStarted = false;
        _uploadHttpStatus = _uploadSucceeded ? 200 : 500;
        _uploadMessage = _ota.status().message;
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        if (_uploadStarted) _ota.abortUpload();
        _uploadStarted = false;
        _uploadSucceeded = false;
        _uploadHttpStatus = 400;
        _uploadMessage = _ota.status().message.isEmpty()
                           ? String("Firmware upload was aborted.") : _ota.status().message;
    }
}

void SystemWeb::handleNotFound()
{
    _server->send(404, "text/plain", "Not found");
}

String SystemWeb::htmlPage() const
{
    String html;
    html.reserve(3000);
    html += "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>";
    html += htmlEscape(_firmware.product);
    html += "</title><style>body{font-family:system-ui;max-width:720px;margin:40px auto;padding:0 18px}a{display:inline-block;margin:8px 0}pre{background:#f4f4f4;padding:12px}</style></head><body>";
    html += "<h1>" + htmlEscape(_firmware.product) + "</h1>";
    html += "<p>Firmware <b>" + htmlEscape(_firmware.version) + "</b><br>Hardware <code>" + htmlEscape(_firmware.hardware) + "</code><br>IP <code>" + htmlEscape(_wifi.address().toString()) + "</code></p>";
    html += "<p><a href='/api/system'>System JSON</a></p>";
    if (!_otaUsername.isEmpty() && !_otaPassword.isEmpty()) html += "<p><a href='/update'>Firmware update</a></p>";
    html += "</body></html>";
    return html;
}

} // namespace EspSystem
