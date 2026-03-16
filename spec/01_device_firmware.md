# Module 01 — Device Firmware

## Overview

The ESP32 firmware is structured as a set of independent modules, each in its own class. `main.cpp` owns the lifecycle and the main loop. All tunable parameters live in `config.json` (plain text, LittleFS). All credentials live in NVS (Preferences API).

---

## Module Structure

```
src/
  main.cpp
  BME688Module.cpp
  MicModule.cpp
  DisplayModule.cpp
  MqttModule.cpp
  ButtonModule.cpp
  BuzzerModule.cpp
  OTAModule.cpp
  PortalModule.cpp
  ConfigHandler.cpp     (or remain as inline in ConfigHandler.h)

include/
  BME688Module.h
  MicModule.h
  DisplayModule.h
  MqttModule.h
  ButtonModule.h
  BuzzerModule.h
  OTAModule.h
  PortalModule.h
  ConfigHandler.h

data/
  config.json           (plain text, uploaded to LittleFS)
```

**Removed from v1:** `SecureVault.h/.cpp`, `credentials.json` (encrypted file). Replaced by NVS.

---

## ConfigHandler

### config.json (plain text, LittleFS)

```json
{
  "mqtt": {
    "port": 8883,
    "topic_prefix": "hivis/sensors",
    "interval_ms": 3000,
    "keepalive_s": 60
  },
  "bme": {
    "addr": 119,
    "iaq_warn": 100,
    "iaq_alert": 200,
    "bsec_save_interval_h": 6
  },
  "mic": {
    "pin_ws": 12,
    "pin_sck": 14,
    "pin_sd": 26,
    "pin_lr": 13,
    "cal": 115.0
  },
  "misc": {
    "refresh_ms": 1000,
    "battery_pin": 36,
    "battery_min_mv": 330,
    "battery_max_mv": 420,
    "battery_multiplier": 2.2
  },
  "ota": {
    "check_interval_h": 24
  },
  "display": {
    "timeout_ms": 10000
  },
  "buzzer": {
    "enabled": true,
    "pin": 25
  },
  "button": {
    "pin": 0,
    "long_press_ms": 2000,
    "double_press_ms": 400
  }
}
```

### DeviceConfig struct additions vs v1

```cpp
struct DeviceConfig {
    // --- NVS (loaded separately, not from config.json) ---
    String wifiSSID;
    String wifiPass;
    String mqttServer;
    String mqttUser;
    String mqttPass;
    String deviceName;    // Human-readable, set via portal
    String deviceId;      // MAC-derived, e.g. "hivis-a1b2c3d4"

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

    int   otaCheckIntervalH;    // How often to poll OTA server
    int   displayTimeoutMs;     // Screen auto-off delay after wake

    bool  buzzerEnabled;
    int   buzzerPin;

    int   buttonPin;
    int   longPressMs;
    int   doublePressMs;
};
```

NVS keys (Preferences namespace `"hivis"`):

| Key | Value |
|-----|-------|
| `wifi_ssid` | WiFi SSID |
| `wifi_pass` | WiFi password |
| `mqtt_server` | MQTT server address |
| `mqtt_user` | MQTT username (provisioned by server) |
| `mqtt_pass` | MQTT password (provisioned by server) |
| `device_name` | Human-readable name (set via portal) |
| `provisioned` | `1` if server has provisioned MQTT credentials |
| `ota_last_check` | Unix timestamp of last OTA check (millis-based approximation) |

---

## Boot Flow

### First Boot (NVS empty)

```
Power on
  → Mount LittleFS
  → Load config.json
  → Init hardware (I2C, OLED, buzzer, button)
  → Show splash screen
  → NVS check: no wifi_ssid found
  → Launch PortalModule (captive portal)
      User enters: WiFi SSID, WiFi password, device name, server address
  → Save to NVS
  → Connect to WiFi
  → Send registration request to server (MAC + device name)
  → Server responds with MQTT credentials
  → Save MQTT credentials to NVS, set provisioned = 1
  → Double-beep (system ready)
  → Enter main loop
```

### Every Subsequent Boot

```
Power on
  → Mount LittleFS
  → Load config.json
  → Load NVS credentials
  → Init hardware
  → Show splash screen
  → Load BSEC state from LittleFS (/bsec_state.bin)
  → Check OTA (if interval elapsed or first boot of day)
  → Connect to WiFi
      Success → online mode
      Fail    → offline mode
  → Double-beep (system ready)
  → Enter main loop
```

### Long Press During Boot or Operation
Launches captive portal to reconfigure WiFi / device name / server address.  
Does NOT wipe MQTT credentials unless server re-provisions.

### Factory Reset
Long press > 5 seconds → wipe entire NVS namespace `"hivis"` → reboot → first boot flow.

---

## Main Loop

**Refresh interval:** `cfg.refreshMs` (default 1000ms)  
**MQTT publish interval:** `cfg.mqttIntervalMs` (default 3000ms)

Each iteration:
1. `bme->update()` — BSEC tick
2. If `refreshMs` elapsed:
   - Read mic (`mic->readDB()`)
   - Read battery ADC (16-sample average, hysteresis 2%, values only decrease)
   - Bundle `DisplayData` struct
   - `button->update()` — process button state machine
   - `buzzer->update()` — handle tone timers
   - If screen is on: `oled->update(data)`
   - Check alert thresholds → trigger buzzer if needed
   - If online and `mqttIntervalMs` elapsed: `mqtt->publish(data)`
   - If offline: `buffer->store(data)`
3. If just reconnected to WiFi: `buffer->flush()` (backfill publish)
4. `delay(1)`

---

## Display Module

### Screen Behavior
- Screen is **OFF by default** to save battery
- Wakes on: short button press, alert threshold crossed, error condition
- Auto-off after `cfg.displayTimeoutMs` (default 10 seconds) of inactivity
- While screen is off, sensor readings and MQTT publishing continue normally

### Display Pages (cycle with short press)

**Page 1 — Air Quality**
```
Temp: XX.X°C   Hum: XX%  [WiFi][Bat]
─────────────────────────────────────
  [IAQ large font]   IAQ
                     CO2: XXXX ppm
Noise: XX.X dB
─────────────────────────────────────
Status: OK | Acc: X
```

**Page 2 — Environment**
```
[Device Name]           [WiFi][Bat]
─────────────────────────────────────
  Temp:  XX.X °C
  Hum:   XX.X %
  BVOC:  X.XX ppm
─────────────────────────────────────
Status: OK | Acc: X
```

**Page 3 — System**
```
[Device Name]           [WiFi][Bat]
─────────────────────────────────────
  WiFi:   [SSID] XX dBm
  Bat:    XX% (X.XXV)
  Server: [connected/offline]
  Acc:    X/3
─────────────────────────────────────
FW: 2.0.0
```

### IAQ Status Footer (Page 1)
| Condition | Message |
|-----------|---------|
| `accuracy == 0` | `Calibrating BSEC...` |
| `iaq >= iaqAlert` | `!! POOR AIR QUALITY !!` |
| `iaq >= iaqWarn` | `ALERT: VENTILATE REC` |
| default | `Status: OK | Acc: X` |

---

## Button Module

Single button, active LOW (internal pull-up).

| Gesture | Action |
|---------|--------|
| Short press (< `longPressMs`) | Wake screen / cycle to next display page |
| Double press (two presses < `doublePressMs` apart) | Acknowledge alert / silence buzzer |
| Long press (> `longPressMs`, < 5s) | Open settings portal (captive portal) |
| Long press (> 5s) | Factory reset (wipe NVS, reboot) |

**ButtonModule API:**
```cpp
class ButtonModule {
public:
    ButtonModule(int pin, int longPressMs, int doublePressMs);
    void begin();
    void update();  // Call every loop iteration

    bool shortPressed();    // Consumed on read
    bool doublePressed();   // Consumed on read
    bool longPressed();     // Consumed on read
    bool factoryReset();    // Consumed on read
};
```

---

## Buzzer Module

Passive piezo on PWM pin (`cfg.buzzerPin`, default GPIO 25). Uses ESP32 LEDC.

| Event | Tone Pattern | Description |
|-------|-------------|-------------|
| Button press / menu nav | Single short beep 1000Hz 50ms | UI feedback |
| System ready | Two beeps 1200Hz 100ms, 100ms gap | Boot complete |
| WiFi connected | Ascending two-tone (800→1200Hz) | Connection OK |
| IAQ warning | Slow intermittent 800Hz, 500ms on / 1s off | Warning level |
| IAQ danger | Fast urgent 1200Hz, 200ms on / 200ms off | Danger level |
| Alert silenced | Descending tone 1000→600Hz 300ms | Acknowledged |
| Error | Three rapid low beeps 400Hz | System error |

**BuzzerModule API:**
```cpp
class BuzzerModule {
public:
    BuzzerModule(int pin);
    void begin();
    void update();          // Call every loop — handles non-blocking timers

    void beepShort();
    void beepReady();
    void beepConnected();
    void startWarning();    // Continuous until stopAlert()
    void startDanger();     // Continuous until stopAlert()
    void stopAlert();
    void beepError();

    bool isMuted();
    void setMuted(bool muted);  // Permanent silence from settings

private:
    bool _muted;
    // Internal state machine for continuous tones
};
```

---

## Offline Buffer Module

When WiFi is unavailable, readings are stored to LittleFS.

### Storage Format

File: `/offline_buffer.json` (append-style JSON array or newline-delimited JSON)

Each record:
```json
{
  "ts": 0,
  "ts_accurate": false,
  "temp": 21.5,
  "hum": 45.2,
  "iaq": 82.3,
  "co2": 654,
  "bvoc": 0.51,
  "db": 48.2,
  "bat": 87,
  "acc": 3
}
```

`ts` is set to `0` during offline period (no NTP). On reconnect, approximate timestamps are calculated backwards from `now()` using the stored `cfg.mqttIntervalMs` interval.

### Backfill on Reconnect

On WiFi reconnect:
1. Read `/offline_buffer.json`
2. Get current NTP time
3. Count records, assign timestamps backwards: `ts = now - (index * intervalMs / 1000)`
4. Publish each record to MQTT with `"backfill": true` in payload
5. Delete `/offline_buffer.json` after successful flush

### Storage Limits

Maximum buffer size: 500 records (configurable). If full, oldest records are dropped (ring buffer behavior). A warning is logged to serial.

---

## Battery Monitor

- 16-sample ADC averaging on `cfg.batteryPin` (GPIO 36)
- Voltage divider correction: `voltage = (raw / 4095.0) * 3.3 * cfg.batteryMultiplier`
- Percentage: `map(constrain(voltage*100, minMv, maxMv), minMv, maxMv, 0, 100)`
- Hysteresis: reported percentage only decreases (never increases by less than 2%)
- Both percentage and voltage reported in DisplayData and MQTT payload

---

## platformio.ini

```ini
[env:esp32doit-devkit-v1]
platform = espressif32
board = esp32doit-devkit-v1
framework = arduino
monitor_speed = 115200
board_build.filesystem = littlefs
board_build.partitions = partitions.csv

lib_compat_mode = strict
lib_ignore = WiFiNINA, WiFiNINA_-_Adafruit_Fork

lib_deps =
    boschsensortec/bsec2 @ ^1.10.2610
    boschsensortec/BME68x Sensor Library @ ^1.2.40408
    olikraus/U8g2 @ ^2.35.19
    bblanchon/ArduinoJson
    knolleary/PubSubClient @ ^2.8.0
    tzapu/WiFiManager @ ^2.0.17

build_flags =
    -L .pio/libdeps/esp32doit-devkit-v1/bsec2/src/esp32
    -lalgobsec
    -DFIRMWARE_VERSION='"2.0.0"'
```
