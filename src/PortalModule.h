#pragma once
#include <Arduino.h>
#include "ConfigHandler.h"

class PortalModule {
public:
    PortalModule();
    void begin();

    bool runFirstBoot();
    bool runReconfigure();

    String getDeviceName();
    String getMqttServer();
    void   saveToNVS();

private:
    String _deviceName;
    String _mqttServer;

    static String _apSsid();
    bool _runPortal(bool resetSettings);
};
