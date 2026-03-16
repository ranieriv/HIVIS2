#pragma once
#include <Arduino.h>

class OTAModule {
public:
    // serverUrl      — base URL of OTA server, e.g. "http://172.16.1.156:8090"
    // deviceId       — e.g. "hivis-aabbccddeeff"
    // mac            — full MAC, lowercase no colons, e.g. "aabbccddeeff"
    // currentVersion — current firmware version string, e.g. "2.0.0"
    OTAModule(const String& serverUrl, const String& deviceId,
              const String& mac,       const String& currentVersion);

    void begin();

    // Check server for an update and apply if available.
    // Step 1: GET /ota/version?mac=[mac]  → server version string or 304
    // Step 2: If newer, display OLED message, then GET /ota/firmware?mac=[mac]
    // Returns true if update was applied (device reboots automatically).
    bool checkAndUpdate();

private:
    String _serverUrl;
    String _deviceId;
    String _mac;
    String _currentVersion;

    // Returns true if enough time has elapsed since last check
    bool _shouldCheck();

    // Save current time as last check timestamp
    void _saveLastCheckTime();

    // Compare version strings: returns true if newVer > currentVer
    static bool _isNewer(const String& newVer, const String& currentVer);
};
