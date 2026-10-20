# Changelog

## 0.2.0 - unreleased

- Added configurable ESP System management HTTP port, defaulting to 8080.
- Separated the Wi-Fi bootstrap image from the Wi-Fi application template.
- Advertised the management endpoint as a distinct `_esp-system._tcp` mDNS service.
- Added normative OTA requirements and project acceptance checks.
- Tightened remote manifest validation: exact product/hardware targets, positive image size, and hexadecimal SHA-256.
- Avoided invoking the application's pre-update callback twice for one remote install.
- Added partition-table bounds checks to CI.
- Escaped manifest-derived strings in the management HTML to prevent update notes from injecting markup.
- Added Origin checks for cross-origin browser POSTs to management update actions.
- Made mDNS initialization configurable for applications that already own the responder.
- Made browser upload responses report the current upload result rather than a previous OTA state.
- Updated documentation for LAN-only HTTP Basic Auth, partition compatibility, and the actual OTA workflow.

## 0.1.0 - initial implementation

- Added system-level Wi-Fi service and persistent credentials.
- Added local browser firmware upload and HTTP(S) manifest-based device-pull updates.
- Added dual-slot OTA support, rollback confirmation hooks and firmware metadata.
- Added bootstrap example, application template and local manifest tools.
