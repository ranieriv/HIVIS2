#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>

#define NVS_NAMESPACE "hivis"

struct DeviceConfig {
    // --- NVS (loaded via loadNVS, never in config.json) ---
    // Note: WiFi SSID/pass are owned by WiFiManager (espwifimgr namespace).
    // We do not store or read them manually.
    String mqttServer;    // MQTT broker address
    String mqttUser;      // Provisioned by server
    String mqttPass;      // Provisioned by server
    String deviceName;    // Human-readable, set via portal
    String deviceId;      // Assigned by server; local fallback is MAC-derived

    // --- From config.json ---
    int    mqttPort;
    String mqttTopicPrefix;
    int    mqttIntervalMs;
    int    mqttKeepaliveS;

    uint8_t bmeAddr;
    float   iaqWarn;
    float   iaqAlert;
    int     bsecSaveIntervalH;

    int    micWS, micSCK, micSD, micLR;
    double micCal;

    int   refreshMs;
    int   batteryPin;
    int   batteryMinMv;
    int   batteryMaxMv;
    float batteryMultiplier;

    int   otaCheckIntervalH;
    int   displayTimeoutMs;

    bool  buzzerEnabled;
    int   buzzerPin;

    int   buttonPin;
    int   longPressMs;
    int   doublePressMs;
};

inline String deriveDeviceId() {
    return "hivis-" + String((uint32_t)ESP.getEfuseMac(), HEX);
}

inline bool loadConfig(DeviceConfig &cfg) {
    cfg.mqttPort          = 8883;
    cfg.mqttTopicPrefix   = "hivis/sensors";
    cfg.mqttIntervalMs    = 3000;
    cfg.mqttKeepaliveS    = 60;
    cfg.bmeAddr           = 119;
    cfg.iaqWarn           = 100.0f;
    cfg.iaqAlert          = 200.0f;
    cfg.bsecSaveIntervalH = 6;
    cfg.micWS = 12; cfg.micSCK = 14; cfg.micSD = 26; cfg.micLR = 13;
    cfg.micCal            = 115.0;
    cfg.refreshMs         = 1000;
    cfg.batteryPin        = 36;
    cfg.batteryMinMv      = 330;   // units: centivolt (0.01V) — 330 = 3.30V
    cfg.batteryMaxMv      = 420;   // units: centivolt (0.01V) — 420 = 4.20V (1S LiPo)
    cfg.batteryMultiplier = 2.2f;  // voltage divider ratio (ADC sees V/2.2)
    cfg.otaCheckIntervalH = 24;
    cfg.displayTimeoutMs  = 10000;
    cfg.buzzerEnabled     = true;
    cfg.buzzerPin         = 25;
    cfg.buttonPin         = 4;
    cfg.longPressMs       = 2000;
    cfg.doublePressMs     = 400;

    if (!LittleFS.begin(true)) {
        Serial.println("Config: LittleFS mount failed.");
        return false;
    }

    File f = LittleFS.open("/config.json", "r");
    if (!f) {
        Serial.println("Config: config.json not found, using defaults.");
        return true;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err != DeserializationError::Ok) {
        Serial.println("Config: parse error, using defaults.");
        return true;
    }

    cfg.mqttPort          = doc["mqtt"]["port"]                | cfg.mqttPort;
    cfg.mqttTopicPrefix   = doc["mqtt"]["topic_prefix"]        | cfg.mqttTopicPrefix;
    cfg.mqttIntervalMs    = doc["mqtt"]["interval_ms"]         | cfg.mqttIntervalMs;
    cfg.mqttKeepaliveS    = doc["mqtt"]["keepalive_s"]         | cfg.mqttKeepaliveS;

    cfg.bmeAddr           = doc["bme"]["addr"]                 | (int)cfg.bmeAddr;
    cfg.iaqWarn           = doc["bme"]["iaq_warn"]             | cfg.iaqWarn;
    cfg.iaqAlert          = doc["bme"]["iaq_alert"]            | cfg.iaqAlert;
    cfg.bsecSaveIntervalH = doc["bme"]["bsec_save_interval_h"] | cfg.bsecSaveIntervalH;

    cfg.micWS  = doc["mic"]["pin_ws"]  | cfg.micWS;
    cfg.micSCK = doc["mic"]["pin_sck"] | cfg.micSCK;
    cfg.micSD  = doc["mic"]["pin_sd"]  | cfg.micSD;
    cfg.micLR  = doc["mic"]["pin_lr"]  | cfg.micLR;
    cfg.micCal = doc["mic"]["cal"]     | cfg.micCal;

    cfg.refreshMs         = doc["misc"]["refresh_ms"]          | cfg.refreshMs;
    cfg.batteryPin        = doc["misc"]["battery_pin"]         | cfg.batteryPin;
    cfg.batteryMinMv      = doc["misc"]["battery_min_mv"]      | cfg.batteryMinMv;
    cfg.batteryMaxMv      = doc["misc"]["battery_max_mv"]      | cfg.batteryMaxMv;
    cfg.batteryMultiplier = doc["misc"]["battery_multiplier"]  | cfg.batteryMultiplier;

    cfg.otaCheckIntervalH = doc["ota"]["check_interval_h"]     | cfg.otaCheckIntervalH;
    cfg.displayTimeoutMs  = doc["display"]["timeout_ms"]       | cfg.displayTimeoutMs;
    cfg.buzzerEnabled     = doc["buzzer"]["enabled"]           | cfg.buzzerEnabled;
    cfg.buzzerPin         = doc["buzzer"]["pin"]               | cfg.buzzerPin;
    cfg.buttonPin         = doc["button"]["pin"]               | cfg.buttonPin;
    cfg.longPressMs       = doc["button"]["long_press_ms"]     | cfg.longPressMs;
    cfg.doublePressMs     = doc["button"]["double_press_ms"]   | cfg.doublePressMs;

    return true;
}

inline void loadNVS(DeviceConfig &cfg) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    cfg.mqttServer = prefs.getString("mqtt_server", "mqtt.hvht.net");
    cfg.mqttUser   = prefs.getString("mqtt_user",   "");
    cfg.mqttPass   = prefs.getString("mqtt_pass",   "");
    cfg.deviceName = prefs.getString("device_name", "");
    cfg.deviceId   = prefs.getString("device_id",   "");
    prefs.end();

    if (cfg.deviceId.isEmpty()) cfg.deviceId = deriveDeviceId();
    if (cfg.deviceName.isEmpty()) cfg.deviceName = cfg.deviceId;
}

inline void saveNVSIdentity(const String &server, const String &name) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString("mqtt_server", server);
    prefs.putString("device_name", name);
    prefs.end();
}

inline void saveNVSProvisioned(const String &user, const String &pass, const String &id) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString("mqtt_user", user);
    prefs.putString("mqtt_pass", pass);
    prefs.putString("device_id", id);
    prefs.putInt("provisioned",  1);
    prefs.end();
}

inline bool isProvisioned() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    int p = prefs.getInt("provisioned", 0);
    prefs.end();
    return p == 1;
}

inline void saveOtaLastCheck(uint32_t ts) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putULong("ota_last_chk", ts);
    prefs.end();
}

inline uint32_t loadOtaLastCheck() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    uint32_t ts = prefs.getULong("ota_last_chk", 0);
    prefs.end();
    return ts;
}

inline void clearNVS() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.clear();
    prefs.end();
    Serial.println("NVS: cleared.");
}

// ── MQTT auth-fail reboot counter ────────────────────────────────────────────
// Prevents infinite reboot loops when broker credentials are permanently wrong.
// Counter is stored in NVS and cleared on each successful MQTT connection.
inline int loadAuthRebootCount() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    int c = prefs.getInt("auth_rbt_cnt", 0);
    prefs.end();
    return c;
}

inline void incrementAuthRebootCount() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putInt("auth_rbt_cnt", prefs.getInt("auth_rbt_cnt", 0) + 1);
    prefs.end();
}

inline void clearAuthRebootCount() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putInt("auth_rbt_cnt", 0);
    prefs.end();
}
