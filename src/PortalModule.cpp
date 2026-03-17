#include "PortalModule.h"
#include <WiFiManager.h>
#include <WiFi.h>

static const char* PARAM_DEVICE_NAME = "device_name";
static const char* PARAM_MQTT_SERVER = "mqtt_server";

PortalModule::PortalModule() : _deviceName(""), _mqttServer("mqtt.hvht.net") {}

void PortalModule::begin() {
    // Pre-load existing NVS values so the portal form shows current settings
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    _deviceName = prefs.getString("device_name", "");
    _mqttServer = prefs.getString("mqtt_server", "mqtt.hvht.net");
    prefs.end();
}

bool PortalModule::runFirstBoot() {
    return _runPortal(true);
}

bool PortalModule::runReconfigure() {
    return _runPortal(false);
}

String PortalModule::getDeviceName() { return _deviceName; }
String PortalModule::getMqttServer() { return _mqttServer; }
String PortalModule::getApSsid()     { return _apSsid(); }

void PortalModule::saveToNVS() {
    saveNVSIdentity(_mqttServer, _deviceName);
}

// ── Private ───────────────────────────────────────────────────────────────────

String PortalModule::_apSsid() {
    uint64_t mac = ESP.getEfuseMac();
    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%04X", (uint16_t)(mac & 0xFFFF));
    return "HIVIS-Setup-" + String(suffix);
}

bool PortalModule::_runPortal(bool resetSettings) {
    WiFiManager wm;
    wm.setTitle("HIVIS Monitor 2.0");
    wm.setConfigPortalTimeout(180); // 3-minute timeout

    if (resetSettings) wm.resetSettings();

    // Use current values as defaults in the form
    if (_deviceName.isEmpty()) _deviceName = deriveDeviceId();
    if (_mqttServer.isEmpty()) _mqttServer = "mqtt.hvht.net";

    WiFiManagerParameter pName(PARAM_DEVICE_NAME, "Device Name (max 20 chars)",
                               _deviceName.c_str(), 20);
    WiFiManagerParameter pServer(PARAM_MQTT_SERVER, "Server Address",
                                 _mqttServer.c_str(), 64);

    wm.addParameter(&pName);
    wm.addParameter(&pServer);

    String apSsid = _apSsid();
    Serial.printf("Portal: starting AP '%s'\n", apSsid.c_str());

    bool connected;
    if (resetSettings) {
        connected = wm.autoConnect(apSsid.c_str());
    } else {
        connected = wm.startConfigPortal(apSsid.c_str());
    }

    if (!connected) {
        Serial.println("Portal: timed out or user cancelled.");
        return false;
    }

    _deviceName = String(pName.getValue());
    _mqttServer = String(pServer.getValue());

    _deviceName.trim();
    _mqttServer.trim();

    Serial.printf("Portal: OK — server=%s  name=%s\n",
                  _mqttServer.c_str(), _deviceName.c_str());
    return true;
}
