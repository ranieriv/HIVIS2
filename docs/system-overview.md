# HIVIS Monitor 2.0 — System Documentation

**Version:** 2.0.1
**Hardware:** ESP32 DOIT DevKit V1 + BME688 + INMP441
**Server:** Lenovo ThinkPad X200 · Ubuntu · 172.16.1.156

---

## 1. Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│  ESP32 Device (Battery-powered, indoor)                     │
│  BME688 (IAQ/CO2/VOC/Temp/Hum) + INMP441 (Noise) + OLED   │
└──────────────────┬──────────────────────────────────────────┘
                   │ MQTT TLS :8883
                   │ HTTP     :8090 (OTA)
                   ▼
┌─────────────────────────────────────────────────────────────┐
│  Server — 172.16.1.156 / mqtt.hvht.net                      │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  │
│  │  Mosquitto   │  │   Node-RED   │  │    InfluxDB 2    │  │
│  │  :8883 TLS   │→ │  :1880       │→ │    :8086         │  │
│  └──────────────┘  └──────────────┘  └────────┬─────────┘  │
│  ┌──────────────┐                    ┌─────────▼─────────┐  │
│  │  OTA Server  │                    │    Grafana        │  │
│  │  :8090       │                    │    :3000          │  │
│  └──────────────┘                    └───────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

---

## 2. Server Stack

| Service | Image | Port | Purpose |
|---------|-------|------|---------|
| `hivis-mosquitto` | eclipse-mosquitto:latest | **8883** | MQTT broker (TLS only) |
| `hivis-nodered` | nodered/node-red:latest | **1880** | Data routing & provisioning |
| `hivis-influxdb` | influxdb:2 | **8086** | Time-series database |
| `hivis-grafana` | grafana/grafana:latest | **3000** | Dashboards |
| `hivis-ota` | python:3.11-slim | **8090** | Firmware update server |

**Manage all services:**
```bash
cd /opt/hivis
docker compose ps           # status
docker compose restart      # restart all
docker compose logs -f      # live logs
```

---

## 3. Device Boot Sequence

```
1. Load config.json from LittleFS
2. Init hardware (I2C 21/22, OLED, buzzer, button)
3. Show splash, load NVS credentials
4. If no WiFi/server saved → Captive portal (HIVIS-Setup-XXXX)
5. Init BME688 (I2C 0x77) + INMP441 microphone
6. Connect WiFi (15s timeout)
7. Sync NTP (pool.ntp.org / time.nist.gov)
8. If not provisioned → Register via MQTT bootstrap
9. Connect MQTT with provisioned credentials
10. Flush offline buffer (if any)
11. Check OTA (every 24h)
12. Main loop: read sensors → display → publish every 3s
```

---

## 4. Device Provisioning Flow

```
Device (unprovisioned)
  │ CONNECT  user=hivis_bootstrap  pass=hivishitech2026
  ▼
Mosquitto ──→ Node-RED
  │              │ Check whitelist.json
  │              │ Generate random 20-char password
  │              │ Call POST http://ota-server:8090/provision
  │              │   └─ mosquitto_passwd -b passwd device_XXXX PASS
  │              │   └─ Append ACL entry
  │              │   └─ SIGHUP Mosquitto
  │              │ Publish to hivis/provision/[mac] (retain=true)
  ◄─────────────────────────────────────────────────────────
Device receives: {"status":"approved","mqtt_user":"device_XXXX",
                  "mqtt_pass":"...","device_id":"hivis-XXXX"}
  │ Save to NVS
  │ CONNECT  user=device_XXXX  pass=...
  ▼
Mosquitto: authenticated ✓
```

---

## 5. MQTT Topics

| Topic | Direction | Purpose |
|-------|-----------|---------|
| `hivis/hivis-[mac]/data` | Device → Server | Sensor data (3s interval) |
| `hivis/register` | Device → Server | Registration request |
| `hivis/provision/[mac]` | Server → Device | Provisioning response (retained) |

**Sensor payload (JSON):**
```json
{
  "device_id":   "hivis-4cc382c32764",
  "device_name": "Room 101",
  "ts":          1710518400,
  "ts_accurate": true,
  "fw_version":  "2.0.1",
  "temp":        22.5,
  "hum":         45.3,
  "iaq":         75.2,
  "co2":         412,
  "bvoc":        0.52,
  "db":          65.3,
  "bat_pct":     85,
  "bat_mv":      3850,
  "rssi":        -45,
  "accuracy":    3,
  "backfill":    false
}
```

**IAQ Accuracy (BSEC2):**
| Value | Meaning |
|-------|---------|
| 0 | Calibrating — values unreliable (~30 min on first boot) |
| 1 | Low accuracy |
| 2 | Medium accuracy |
| 3 | High accuracy — fully calibrated |

---

## 6. MQTT Users & Access Control

| User | Password | Access |
|------|----------|--------|
| `hivis_bootstrap` | `hivishitech2026` | Write hivis/register, Read hivis/provision/+ |
| `nodered_bridge` | (encrypted in Node-RED) | Read/Write hivis/# |
| `device_[mac]` | Auto-generated | Write hivis/hivis-[mac]/#, Read hivis/provision/[mac] |

**Add a new device to whitelist** (required before registration is approved):
```bash
# On server:
nano /opt/hivis/ota/whitelist.json
# Also update Node-RED's copy:
docker cp /opt/hivis/ota/whitelist.json hivis-nodered:/data/whitelist.json
```

---

## 7. OTA Update Process

**OTA server endpoints:**
```
GET  http://mqtt.hvht.net:8090/ota/version?mac=[mac]   → "2.0.1"
GET  http://mqtt.hvht.net:8090/ota/firmware?mac=[mac]  → latest.bin
POST http://ota-server:8090/provision                  → Add Mosquitto user
```

**Deploy a new firmware version:**
```bash
# 1. Bump version in platformio.ini: -DFIRMWARE_VERSION='"2.0.2"'
# 2. Build:
pio run
# 3. Upload binary:
scp .pio/build/esp32doit-devkit-v1/firmware.bin mqttadmin@172.16.1.156:/opt/hivis/ota/firmware/latest.bin
# 4. Update version file:
ssh mqttadmin@172.16.1.156 "echo '2.0.2' > /opt/hivis/ota/firmware/version.txt"
```

Device checks OTA on every boot (if last check > 24h ago). Update applies automatically and device reboots.

---

## 8. Hardware Pin Assignments

| Component | Pin | Notes |
|-----------|-----|-------|
| I2C SDA | GPIO 21 | BME688 + OLED |
| I2C SCL | GPIO 22 | BME688 + OLED |
| BME688 | I2C 0x77 | IAQ/CO2/VOC/Temp/Hum |
| INMP441 WS | GPIO 12 | Microphone clock |
| INMP441 SCK | GPIO 14 | Microphone bit clock |
| INMP441 SD | GPIO 26 | Microphone data |
| INMP441 LR | GPIO 13 | Left/Right select |
| Button | GPIO 0 | Short/Long/Double/Factory reset |
| Buzzer | GPIO 25 | LEDC PWM, channel 0 |
| Battery ADC | GPIO 36 | Voltage divider ×2.2 |

---

## 9. Button Functions

| Press | Duration | Action |
|-------|----------|--------|
| Short | < 400ms | Next display page |
| Double | 2× < 400ms apart | Stop alert buzzer |
| Long | > 2s | Launch reconfigure portal |
| Factory reset | > 5s | Wipe NVS → full reboot |

---

## 10. Flash Partition Layout

| Partition | Offset | Size | Contents |
|-----------|--------|------|----------|
| nvs | 0x9000 | 20 KB | WiFi creds, MQTT creds, device ID |
| otadata | 0xE000 | 8 KB | Active OTA slot tracker |
| app0 | 0x10000 | 1.75 MB | Primary firmware |
| app1 | 0x1D0000 | 1.75 MB | OTA update slot |
| spiffs | 0x390000 | 448 KB | LittleFS: config.json, bsec_state.bin |

**Erase NVS only** (wipes credentials, keeps firmware):
```bash
python -m esptool --port COM7 erase_region 0x9000 0x5000
```

---

## 11. Grafana Dashboards

| Dashboard | URL path | Purpose |
|-----------|----------|---------|
| Fleet Overview | `/d/hivis-fleet` | All devices — status table, IAQ comparison, battery |
| Per Device | `/d/hivis-perdevice` | Single device — live gauges + history |
| Historical | `/d/hivis-historical` | 7-day trends, backfill annotation |

Login: `admin` / `hivishitech2026`
URL: `http://172.16.1.156:3000`

---

## 12. Key File Locations

### Server (`/opt/hivis/`)
```
/opt/hivis/
├── docker-compose.yml
├── mosquitto/
│   ├── config/
│   │   ├── mosquitto.conf
│   │   ├── passwd          ← Mosquitto user hashes
│   │   └── acl             ← Topic access control
│   ├── certs/              ← TLS cert (mqtt.hvht.net)
│   └── log/mosquitto.log
└── ota/
    ├── server.py
    ├── devices.json        ← OTA-approved MACs
    ├── whitelist.json      ← Registration whitelist
    └── firmware/
        ├── latest.bin
        └── version.txt
```

### Device (LittleFS)
```
/littlefs/
├── config.json             ← All tunable parameters
├── bsec_state.bin          ← BSEC2 calibration (auto-saved every 6h)
└── offline_buffer.ndjson   ← Offline sensor records (max 500)
```

### Device (NVS — namespace "hivis")
| Key | Content |
|-----|---------|
| `mqtt_server` | `mqtt.hvht.net` |
| `mqtt_user` | `device_[mac]` |
| `mqtt_pass` | Auto-generated password |
| `device_id` | `hivis-[mac]` |
| `device_name` | Human-readable name |
| `provisioned` | 1 (set after successful registration) |
| `ota_last_chk` | Unix timestamp of last OTA check |
