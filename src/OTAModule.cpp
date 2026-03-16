#include "OTAModule.h"
#include "ConfigHandler.h"  // for saveOtaLastCheck / loadOtaLastCheck
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClient.h>
#include <time.h>

OTAModule::OTAModule(const String& serverUrl, const String& deviceId,
                     const String& mac,       const String& currentVersion)
    : _serverUrl(serverUrl), _deviceId(deviceId),
      _mac(mac), _currentVersion(currentVersion) {}

void OTAModule::begin() {
    // Nothing to initialise — checks are on-demand
}

bool OTAModule::checkAndUpdate() {
    Serial.println("OTA: checking for update...");

    // ── Step 1: GET /ota/version?mac=[mac] ───────────────────────────────────
    String versionUrl = _serverUrl + "/ota/version?mac=" + _mac;

    HTTPClient http;
    http.begin(versionUrl);
    http.setTimeout(5000);

    int code = http.GET();
    if (code == 304) {
        Serial.println("OTA: firmware is up to date (304).");
        http.end();
        _saveLastCheckTime();
        return false;
    }
    if (code != 200) {
        Serial.printf("OTA: version check returned %d, skipping.\n", code);
        http.end();
        _saveLastCheckTime();
        return false;
    }

    String serverVersion = http.getString();
    http.end();
    serverVersion.trim();

    Serial.printf("OTA: device=%s  server=%s\n",
                  _currentVersion.c_str(), serverVersion.c_str());

    if (!_isNewer(serverVersion, _currentVersion)) {
        Serial.println("OTA: no newer version available.");
        _saveLastCheckTime();
        return false;
    }

    // ── Step 2: Apply update via GET /ota/firmware?mac=[mac] ─────────────────
    String firmwareUrl = _serverUrl + "/ota/firmware?mac=" + _mac;
    Serial.printf("OTA: applying update from %s\n", firmwareUrl.c_str());

    WiFiClient client;
    httpUpdate.setLedPin(LED_BUILTIN, LOW);

    // Pass current version so the server can do a final check
    t_httpUpdate_return ret = httpUpdate.update(client, firmwareUrl, _currentVersion);

    _saveLastCheckTime();

    switch (ret) {
        case HTTP_UPDATE_FAILED:
            Serial.printf("OTA: update failed (%d): %s\n",
                          httpUpdate.getLastError(),
                          httpUpdate.getLastErrorString().c_str());
            return false;
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("OTA: server says no update.");
            return false;
        case HTTP_UPDATE_OK:
            Serial.println("OTA: update applied — rebooting.");
            return true;  // device will reboot automatically
    }
    return false;
}

bool OTAModule::_shouldCheck() {
    uint32_t lastCheck = loadOtaLastCheck();
    if (lastCheck == 0) return true;

    // Use wall-clock time if available, else millis-based offset
    uint32_t now = (time(nullptr) > 1000000000UL)
                       ? (uint32_t)time(nullptr)
                       : (uint32_t)(millis() / 1000);

    uint32_t intervalS = (uint32_t)24 * 3600; // default 24h; caller should pass cfg value
    return (now - lastCheck) >= intervalS;
}

void OTAModule::_saveLastCheckTime() {
    uint32_t now = (time(nullptr) > 1000000000UL)
                       ? (uint32_t)time(nullptr)
                       : (uint32_t)(millis() / 1000);
    saveOtaLastCheck(now);
}

bool OTAModule::_isNewer(const String& newVer, const String& currentVer) {
    // Simple semantic version comparison: split on '.' and compare integers
    int nMaj = 0, nMin = 0, nPat = 0;
    int cMaj = 0, cMin = 0, cPat = 0;
    sscanf(newVer.c_str(),     "%d.%d.%d", &nMaj, &nMin, &nPat);
    sscanf(currentVer.c_str(), "%d.%d.%d", &cMaj, &cMin, &cPat);

    if (nMaj != cMaj) return nMaj > cMaj;
    if (nMin != cMin) return nMin > cMin;
    return nPat > cPat;
}
