#pragma once
#include <Arduino.h>

class OTAModule {
public:
    OTAModule(const String& serverUrl, const String& deviceId,
              const String& mac,       const String& currentVersion);

    void begin();
    bool checkAndUpdate();

private:
    String _serverUrl;
    String _deviceId;
    String _mac;
    String _currentVersion;

    bool _shouldCheck();
    void _saveLastCheckTime();
    static bool _isNewer(const String& newVer, const String& currentVer);
};
