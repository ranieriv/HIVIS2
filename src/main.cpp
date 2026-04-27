#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "ConfigHandler.h"
#include "BME688Module.h"
#include "MicModule.h"
#include "DisplayModule.h"
#include "MqttModule.h"
#include "ButtonModule.h"
#include "BuzzerModule.h"
#include "OTAModule.h"
#include "PortalModule.h"
#include "OfflineBuffer.h"

// ── Globals ───────────────────────────────────────────────────────────────────
DeviceConfig   cfg;
BME688Module*  bme    = nullptr;
MicModule*     mic    = nullptr;
DisplayModule* oled   = nullptr;
MqttModule*    mqtt   = nullptr;
ButtonModule*  button = nullptr;
BuzzerModule*  buzzer = nullptr;

bool online    = false;
bool wasOnline = false;

int   lastBatPrc = -1;
float lastBatV   = 0.0f;


// ── MAC helpers ───────────────────────────────────────────────────────────────

static String getMacAddress() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

static String getMacHex() {
    // Lowercase, no colons — used for provision topic
    String m = getMacAddress();
    m.toLowerCase();
    m.replace(":", "");
    return m;
}

// ── Battery ───────────────────────────────────────────────────────────────────

static void readBattery() {
    int raw = 0;
    for (int i = 0; i < 16; i++) raw += analogRead(cfg.batteryPin);
    raw /= 16;

    float v   = (raw / 4095.0f) * 3.3f * cfg.batteryMultiplier;
    int   prc = (int)map(
        constrain((int)(v * 100), cfg.batteryMinMv, cfg.batteryMaxMv),
        cfg.batteryMinMv, cfg.batteryMaxMv, 0, 100
    );

    if (lastBatPrc < 0) {
        lastBatPrc = prc;
        lastBatV   = v;
    } else if (abs(prc - lastBatPrc) >= 2) {
        lastBatPrc = prc;
        lastBatV   = v;
    }
}

// ── WiFi ──────────────────────────────────────────────────────────────────────

// WiFiManager stored WiFi creds in its namespace — reconnect with no args
static bool tryConnectWifi(int timeoutMs = 10000) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(); // reconnect to last saved network (WiFiManager's creds)
    unsigned long t = millis();
    while (millis() - t < (unsigned long)timeoutMs) {
        if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0))
            return true;
        delay(200);
    }
    return false;
}

static bool syncNtp(int timeoutMs = 5000) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    unsigned long t = millis();
    while (time(nullptr) < 1000000000UL && millis() - t < (unsigned long)timeoutMs) {
        delay(200);
    }
    return time(nullptr) >= 1000000000UL;
}

// ── Registration (MQTT-based) ─────────────────────────────────────────────────

// Received provision response storage (written by MQTT callback)
static volatile bool provisionReceived = false;
static String        provisionStatus   = "";
static String        provisionUser     = "";
static String        provisionPass     = "";
static String        provisionId       = "";

static void onMqttMessage(const String& topic, const String& payload) {
    Serial.printf("MQTT RX [%s]: %s\n", topic.c_str(), payload.c_str());

    // Check if this is a provision response
    String expected = MqttModule::provisionTopic(getMacHex());
    if (topic != expected) return;

    JsonDocument doc;
    if (deserializeJson(doc, payload) != DeserializationError::Ok) return;

    provisionStatus = doc["status"] | "";
    provisionUser   = doc["mqtt_user"]   | "";
    provisionPass   = doc["mqtt_pass"]   | "";
    provisionId     = doc["device_id"]   | "";
    provisionReceived = true;
}

// Register with server via MQTT. Returns true if provisioned successfully.
static bool registerDevice() {
    Serial.println("Registration: connecting to broker (bootstrap)...");

    // Bootstrap connection — shared low-privilege account for unregistered devices
    // IMPORTANT: Change this password before deploying. Update Mosquitto passwd file to match.
    MqttModule bootstrap(cfg.mqttServer, cfg.mqttPort,
                         "hivis_bootstrap", "CHANGE_ME_BOOTSTRAP_PASS",
                         cfg.deviceId, cfg.deviceName);
    bootstrap.setMessageCallback(onMqttMessage);

    if (!bootstrap.connect()) {
        Serial.println("Registration: broker unreachable.");
        return false;
    }

    // Subscribe to provision response topic
    String provTopic = MqttModule::provisionTopic(getMacHex());
    bootstrap.subscribe(provTopic, 1);
    // Flush any retained message from a previous registration so we don't
    // mistake it for the server's response to THIS registration request.
    for (int i = 0; i < 5; i++) { bootstrap.loop(); delay(100); }
    provisionReceived = false;

    // Publish registration
    bootstrap.publishRegister(getMacAddress(), cfg.deviceName);

    // Wait up to 30 seconds for server response
    Serial.println("Registration: waiting for server response (30s)...");
    unsigned long deadline = millis() + 30000UL;
    while (!provisionReceived && millis() < deadline) {
        bootstrap.loop();
        delay(100);
    }

    if (!provisionReceived) {
        Serial.println("Registration: no response — offline mode, retry next boot.");
        return false;
    }

    if (provisionStatus != "approved") {
        Serial.println("Registration: REJECTED by server.");
        oled->wake();
        // Show rejection on display briefly
        buzzer->beepError();
        return false;
    }

    // Save provisioned credentials
    cfg.mqttUser   = provisionUser;
    cfg.mqttPass   = provisionPass;
    cfg.deviceId   = provisionId.isEmpty() ? cfg.deviceId : provisionId;
    saveNVSProvisioned(cfg.mqttUser, cfg.mqttPass, cfg.deviceId);

    Serial.printf("Registration: approved — user=%s  id=%s\n",
                  cfg.mqttUser.c_str(), cfg.deviceId.c_str());
    buzzer->beepConnected();
    return true;
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== HIVIS Monitor 2.0 ===");
    Serial.flush();

    // 1. LittleFS + config.json
    Serial.print("1. Loading config... ");
    Serial.flush();
    loadConfig(cfg);
    // Verify LittleFS is writable; reformat if not (can happen after crash loop)
    {
        File t = LittleFS.open("/.wcheck", "w");
        if (!t) {
            Serial.println("LittleFS not writable — reformatting...");
            LittleFS.format();
            LittleFS.begin();
            loadConfig(cfg); // reload config.json after reformat
        } else {
            t.close();
            LittleFS.remove("/.wcheck");
        }
    }
    Serial.println("OK");
    Serial.flush();

    // 2. Hardware init
    Serial.print("2. Init hardware... ");
    Wire.begin(21, 22);

    oled = new DisplayModule();
    oled->begin();
    oled->setTimeoutMs(cfg.displayTimeoutMs);

    buzzer = new BuzzerModule(cfg.buzzerPin);
    buzzer->begin();
    if (!cfg.buzzerEnabled) buzzer->setMuted(true);

    button = new ButtonModule(cfg.buttonPin, cfg.longPressMs, cfg.doublePressMs);
    button->begin();
    Serial.println("OK");

    // 3. Splash
    oled->showSplash();
    oled->wake();

    // 4. Load NVS credentials/identity
    loadNVS(cfg);
    Serial.printf("   MAC=%s  deviceId=%s  server=%s\n",
                  getMacAddress().c_str(), cfg.deviceId.c_str(), cfg.mqttServer.c_str());

    // 5. First boot: no WiFi stored yet → run captive portal
    {
        // WiFiManager stores network in its own namespace. Try a quick connect first.
        WiFi.mode(WIFI_STA);
        WiFi.begin();
        delay(1000);
        bool hasSavedNetwork = (WiFi.SSID().length() > 0);

        if (!hasSavedNetwork || cfg.mqttServer.isEmpty()) {
            {
                Serial.println("3. First boot — launching captive portal...");
                PortalModule portal;
                portal.begin();
                oled->showPortal(portal.getApSsid().c_str());
                if (portal.runFirstBoot()) {
                    portal.saveToNVS();
                    cfg.deviceName = portal.getDeviceName();
                    cfg.mqttServer = portal.getMqttServer();
                } else {
                    Serial.println("Portal: timed out — continuing in offline mode.");
                    buzzer->beepError();
                    if (cfg.deviceName.isEmpty()) cfg.deviceName = deriveDeviceId();
                }
            }
        }
    }

    // 6. Init sensors (before WiFi so BSEC starts warming up)
    Serial.print("4. Init BME688... ");
    bme = new BME688Module(cfg.bmeAddr, cfg.bsecSaveIntervalH);
    Serial.println(bme->begin() ? "OK" : "FAILED (no sensor data)");

    Serial.print("5. Init microphone... ");
    mic = new MicModule(cfg.micWS, cfg.micSCK, cfg.micSD, cfg.micLR, cfg.micCal);
    mic->begin();
    Serial.println("OK");

    // 7. Connect WiFi
    Serial.print("6. Connecting WiFi... ");
    online = tryConnectWifi(15000);
    if (online) {
        Serial.printf("OK (%s)\n", WiFi.localIP().toString().c_str());
        buzzer->beepConnected();
        syncNtp();
    } else {
        Serial.println("FAILED — entering offline mode.");
    }

    // 8. Provision MQTT credentials if not yet done
    if (online && !isProvisioned()) {
        Serial.println("7. Registering device with server...");
        if (registerDevice()) {
            // Give Mosquitto time to reload passwd after SIGHUP before we connect
            Serial.println("   Waiting for broker reload...");
            delay(6000);
        }
    }

    // 9. Connect MQTT with real credentials (if provisioned)
    if (online && isProvisioned() && !cfg.mqttUser.isEmpty()) {
        mqtt = new MqttModule(
            cfg.mqttServer, cfg.mqttPort,
            cfg.mqttUser, cfg.mqttPass,
            cfg.deviceId, cfg.deviceName,
            cfg.mqttKeepaliveS
        );
        mqtt->setMessageCallback(onMqttMessage);
        mqtt->connect();

        // Flush any offline data
        int buffered = OfflineBuffer::count();
        if (buffered > 0) {
            Serial.printf("9. Flushing %d offline records...\n", buffered);
            OfflineBuffer::flush(*mqtt, time(nullptr), cfg.mqttIntervalMs,
                                 cfg.deviceId, cfg.deviceName);
        }
    }

    // 10. OTA check (every boot + every 24h)
    if (online) {
        uint32_t lastOtaCheck = loadOtaLastCheck();
        uint32_t now = (time(nullptr) > 1000000000UL)
                           ? (uint32_t)time(nullptr)
                           : (uint32_t)(millis() / 1000);
        uint32_t intervalS = (uint32_t)cfg.otaCheckIntervalH * 3600UL;
        bool doOta = (lastOtaCheck == 0 || (now - lastOtaCheck) >= intervalS);

        if (doOta) {
            Serial.println("8. Checking OTA...");
            OTAModule ota("http://" + cfg.mqttServer + ":8090",
                          cfg.deviceId, getMacHex(), FIRMWARE_VERSION);
            ota.begin();
            ota.checkAndUpdate(); // reboots if update applied
        }
    }

    buzzer->beepReady();
    wasOnline = online; // prevent false "WiFi: reconnected" on first loop
    Serial.println("=== SYSTEM READY ===");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    bme->update();
    button->update();

    if (mqtt) {
        mqtt->loop();

        // If broker keeps rejecting our credentials, re-provision after 5 minutes.
        // Capped at 5 reboots to prevent infinite reboot loops.
        static unsigned long authFailSince = 0;
        if (!mqtt->isConnected() && mqtt->lastState() == 5) {
            if (authFailSince == 0) authFailSince = millis();
            if (millis() - authFailSince >= 5UL * 60UL * 1000UL) {
                int cnt = loadAuthRebootCount();
                if (cnt >= 5) {
                    Serial.printf("MQTT: auth failure — reboot limit reached (%d). "
                                  "Staying offline, long-press to reconfigure.\n", cnt);
                    authFailSince = millis(); // reset timer, keep trying periodically
                } else {
                    Serial.printf("MQTT: persistent auth failure (5 min) — clearing credentials, "
                                  "rebooting... (attempt %d/5)\n", cnt + 1);
                    buzzer->beepError();
                    delay(1000);
                    incrementAuthRebootCount();
                    clearNVS();
                    ESP.restart();
                }
            }
        } else {
            authFailSince = 0;
            if (mqtt->isConnected()) clearAuthRebootCount();
        }
    }

    // WiFi reconnect watchdog
    bool nowOnline = (WiFi.status() == WL_CONNECTED &&
                      WiFi.localIP() != IPAddress(0, 0, 0, 0));
    if (nowOnline && !wasOnline) {
        Serial.println("WiFi: reconnected.");
        buzzer->beepConnected();
        syncNtp();
        if (mqtt) {
            mqtt->connect();
            int buffered = OfflineBuffer::count();
            if (buffered > 0) {
                Serial.printf("Flushing %d buffered records...\n", buffered);
                OfflineBuffer::flush(*mqtt, time(nullptr), cfg.mqttIntervalMs,
                                     cfg.deviceId, cfg.deviceName);
            }
        }
    }
    online    = nowOnline;
    wasOnline = nowOnline;

    // ── Refresh tick ──────────────────────────────────────────────────────────
    static unsigned long lastRefresh = 0;
    if (millis() - lastRefresh >= (unsigned long)cfg.refreshMs) {
        lastRefresh = millis();

        readBattery();

        DisplayData data = {};
        data.temp        = bme->getTemp();
        data.hum         = bme->getHum();
        data.iaq         = bme->getIAQ();
        data.co2         = bme->getCO2();
        data.bvoc        = bme->getVOC();
        data.db          = mic->readDB();
        data.accuracy    = bme->getAccuracy();
        data.batteryPrc  = (lastBatPrc < 0) ? 0 : lastBatPrc;
        data.batteryV    = lastBatV;
        data.rssi        = online ? WiFi.RSSI() : 0;
        data.serverConnected = (mqtt && mqtt->isConnected());
        data.ssid        = WiFi.SSID().c_str();
        data.deviceName  = cfg.deviceName.c_str();

        if (button->factoryReset()) {
            Serial.println("BTN: factory reset — wiping NVS...");
            buzzer->beepError();
            delay(500);
            clearNVS();
            ESP.restart();
        }

        if (button->longPressed()) {
            Serial.println("BTN: long press — reconfiguring...");
            buzzer->beepShort();
            PortalModule portal;
            portal.begin();
            oled->showPortal(portal.getApSsid().c_str());
            portal.runReconfigure();
            portal.saveToNVS();
            cfg.deviceName = portal.getDeviceName();
            cfg.mqttServer = portal.getMqttServer();
        }

        if (button->doublePressed()) {
            Serial.println("BTN: double press — muting alert.");
            buzzer->stopAlert();
        }

        if (button->shortPressed()) {
            Serial.println("BTN: short press — next page.");
            oled->nextPage();
            buzzer->beepShort();
        }

        // Alert logic
        if (data.accuracy > 0) {
            if (data.iaq >= cfg.iaqAlert) {
                if (!buzzer->isMuted()) buzzer->startDanger();
            } else if (data.iaq >= cfg.iaqWarn) {
                if (!buzzer->isMuted()) buzzer->startWarning();
            } else {
                buzzer->stopAlert();
            }
        }

        buzzer->update();
        oled->update(data, cfg.iaqWarn, cfg.iaqAlert);

        // ── MQTT publish tick ──────────────────────────────────────────────────
        static unsigned long lastMqtt        = 0;
        static unsigned long lastOfflineStore = 0;
        time_t ts = time(nullptr);
        bool tsAccurate = (ts >= 1000000000UL);

        if (online && mqtt && mqtt->isConnected()) {
            if (millis() - lastMqtt >= (unsigned long)cfg.mqttIntervalMs) {
                lastMqtt = millis();
                mqtt->publishSensor(data, ts, tsAccurate);
            }
        } else {
            if (millis() - lastOfflineStore >= 60000UL) {
                lastOfflineStore = millis();
                OfflineBuffer::store(data);
            }
        }

        Serial.printf("IAQ:%.1f Hum:%.1f%% Temp:%.1f°C CO2:%.0f BVOC:%.2f "
                      "dB:%.1f Acc:%d Bat:%d%%(%.2fV) %s\n",
                      data.iaq, data.hum, data.temp, data.co2, data.bvoc,
                      data.db, data.accuracy,
                      data.batteryPrc, data.batteryV,
                      online ? "ONLINE" : "OFFLINE");
    }

    delay(1);
}
