# HIVIS Monitor 2.0 — Master Specification

**Version:** 2.0.0  
**Date:** 2026-03-13  
**Author:** Rani  
**Supervisor:** Aaron  
**Institution:** Saskatchewan Polytechnic  
**Showcase:** Applied Research Student Showcase 2026 — May 4, Prairieland Park, Saskatoon

---

## Project Overview

HIVIS Monitor 2.0 is a multi-device, self-hosted indoor air quality and acoustic monitoring system. It is a privacy-respecting alternative to commercial IoT monitors, targeting schools, offices, healthcare, and industrial facilities.

The system consists of:
- One or more ESP32-based sensor devices
- A self-hosted server stack (Lenovo ThinkPad X200, Ubuntu Server)
- A Grafana dashboard accessible via `hvht.net`

The target scale is **up to 10 devices**. The showcase demonstration uses **3 devices**.

---

## Module Spec Files

| File | Contents |
|------|----------|
| `spec/01_device_firmware.md` | ESP32 firmware — boot flow, sensors, display, button, buzzer, offline buffering |
| `spec/02_provisioning.md` | First-boot captive portal, NVS credential storage, device registration |
| `spec/03_ota.md` | OTA firmware update system — device polling, server approval, HTTP delivery |
| `spec/04_mqtt_data.md` | MQTT topic structure, payload format, offline backfill |
| `spec/05_server_infrastructure.md` | Docker stack — Mosquitto, Node-RED, InfluxDB 2.x, Grafana |
| `spec/06_grafana_dashboards.md` | Dashboard definitions — per-device, fleet overview, historical |

---

## System Architecture

```
[ESP32 Device(s)]
       │
       │  MQTT over TLS (port 8883)
       ▼
[Mosquitto Broker]  ←── mqtt.hvht.net (DNS-only, no Cloudflare proxy)
       │
       │  Subscribe all hivis/#
       ▼
[Node-RED]  ──────────────────────────────────────────────┐
       │                                                   │
       │  node-red-contrib-influxdb                        │  Device registration
       ▼                                                   │  MAC whitelist check
[InfluxDB 2.x]  (bucket: hivis)                           │  MQTT credential provisioning
       │                                                   │
       ▼
[Grafana]  ──── dashboard.hvht.net (port 3000)
```

All services run in **Docker** on the X200 (`mqttserver`), managed by a single `docker-compose.yml`.

---

## Technology Stack

### Device (ESP32)
| Component | Library / Tool |
|-----------|---------------|
| Framework | Arduino (PlatformIO) |
| Sensor (IAQ) | Bosch BSEC2 + BME68x |
| Display | U8g2 |
| JSON | ArduinoJson |
| MQTT | PubSubClient |
| WiFi provisioning | WiFiManager (tzapu) |
| NVS credentials | ESP32 Preferences API |
| OTA update | ESP32 HTTPUpdate (built-in) |
| Offline storage | LittleFS |
| Filesystem | LittleFS |

### Server
| Service | Image |
|---------|-------|
| MQTT Broker | eclipse-mosquitto:latest |
| Flow engine | nodered/node-red:latest |
| Time-series DB | influxdb:2 |
| Dashboards | grafana/grafana:latest |
| OTA HTTP server | Python `http.server` (simple container or host) |

---

## Network & Access

| Endpoint | Address | Notes |
|----------|---------|-------|
| MQTT (TLS) | `mqtt.hvht.net:8883` | DNS-only Cloudflare, existing cert |
| Grafana | `dashboard.hvht.net:3000` | Tailscale or direct LAN |
| InfluxDB UI | `mqttserver:8086` | LAN only |
| Node-RED UI | `mqttserver:1880` | LAN only |
| OTA server | `mqttserver:8090` | LAN + Tailscale |
| Local IP | `&lt;SERVER_IP&gt;` | X200 static LAN IP |
| Tailscale IP | `&lt;TAILSCALE_IP&gt;` | Remote access |

---

## Device Hardware

| Component | Part | Notes |
|-----------|------|-------|
| MCU | ESP32 DoIt DevKit V1 | Or equivalent 4MB flash module |
| Air quality | Bosch BME688 | I2C, addr 0x77 (119) |
| Microphone | INMP441 | I2S |
| Display | SSD1306 0.96" OLED (or 1.3") | I2C, 128×64 |
| Button | 1× tactile button | GPIO, active LOW |
| Buzzer | Passive piezo + 100Ω resistor | PWM via LEDC |
| Battery | 18650 Li-ion or LiPo | Single cell |
| Charger/Boost | J5019 | 5V regulated output |
| Battery monitor | Voltage divider → GPIO 36 | Taps raw cell voltage |

**Custom PCB:** Targeted for JLCPCB fabrication. Schematic and layout are outside this spec scope.

**Partition table:** A custom `partitions.csv` is required to fit OTA (dual app partitions) + LittleFS on 4MB flash:

```csv
# Name,   Type, SubType, Offset,   Size,    Flags
nvs,      data, nvs,     0x9000,   0x5000,
otadata,  data, ota,     0xe000,   0x2000,
app0,     app,  ota_0,   0x10000,  0x1C0000,
app1,     app,  ota_1,   ,         0x1C0000,
spiffs,   data, spiffs,  ,         0x3F0000,
```

---

## Security Model

| Concern | Approach |
|---------|----------|
| MQTT transport | TLS (port 8883), `setInsecure()` initially, CA pinning future upgrade |
| Credentials on device | ESP32 NVS (Preferences API), not files |
| Device authorization | MAC address whitelist on server |
| MQTT credentials | Server-provisioned after MAC auth, not user-entered |
| OTA authenticity | Per-device approval flag on server before binary is served |
| Config (non-secret) | Plain-text `config.json` in LittleFS |

---

## Versioning

Firmware uses semantic versioning: `MAJOR.MINOR.PATCH` (e.g. `2.0.0`).  
Version string is defined as a build flag in `platformio.ini`:

```ini
build_flags = -DFIRMWARE_VERSION='"2.0.0"'
```

---

## Key Design Decisions & Rationale

- **NVS over encrypted file:** NVS is hardware-backed, cleaner, and integrates naturally with WiFiManager. SecureVault (AES-128 CBC) from v1 is removed.
- **WiFiManager (tzapu):** Handles captive portal, custom parameters, and NVS storage natively with PlatformIO.
- **Docker for server:** Portability, clean separation of services, easy migration to new hardware.
- **InfluxDB 2.x + Grafana:** Industry standard for IoT time-series. Replaces Node-RED dashboard.
- **Node-RED retained:** Acts as routing glue between MQTT and InfluxDB. Handles device registration logic. Replaceable with Telegraf later if needed.
- **Offline backfill:** Same MQTT topic, `ts_accurate: false` flag, approximate timestamps spaced backwards from reconnect time.
- **1 button:** Short press / long press / double press covers all UI needs.
- **Passive piezo:** Tone control for distinct alert levels.
- **No DS3231 RTC:** Keeps form factor small. Offline timestamps are approximate and flagged.
- **OTA polling:** Device polls on every boot + every 24h. Server has per-device `ota_approved` flag.
- **Group field:** Present in data model from day one. UI support deferred.
