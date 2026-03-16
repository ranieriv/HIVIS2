#pragma once
#include <Arduino.h>
#include "ConfigHandler.h"

class PortalModule {
public:
    PortalModule();
    void begin();

    // First-boot flow. Blocks until user submits the portal form or 3-min timeout.
    // On success: WiFi connected, device_name and mqtt_server saved to NVS.
    // Returns true on success.
    bool runFirstBoot();

    // Reconfigure flow (triggered by long press).
    // Re-shows portal. Does NOT wipe MQTT credentials.
    // Returns true on success.
    bool runReconfigure();

    // Values captured by the portal form
    String getDeviceName();
    String getMqttServer();

    // Persist device_name and mqtt_server to the "hivis" NVS namespace.
    // Call after runFirstBoot() or runReconfigure() succeeds.
    void saveToNVS();

private:
    String _deviceName;
    String _mqttServer;

    // Builds the AP SSID: "HIVIS-Setup-XXXX" where XXXX = last 4 hex of MAC
    static String _apSsid();

    // Internal: run WiFiManager with custom params
    // resetSettings: true for first boot, false for reconfigure
    bool _runPortal(bool resetSettings);
};
