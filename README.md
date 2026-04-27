# HIVIS Monitor 2.0

Self-hosted indoor air quality and acoustic monitoring system built on ESP32. Designed for schools, offices, and research environments where privacy and data ownership matter.

**Live dashboard:** [https://hvht.net](https://hvht.net)

---

## What it measures

| Metric | Sensor | Notes |
|--------|--------|-------|
| IAQ (Air Quality Index) | BME688 + BSEC2 | 0–500 scale, accuracy 0–3 |
| CO₂ equivalent | BME688 + BSEC2 | ppm |
| VOC (breath VOC) | BME688 + BSEC2 | ppm |
| Temperature | BME688 | °C |
| Humidity | BME688 | % RH |
| Noise level | INMP441 | dB SPL |
| Battery voltage | ADC (GPIO 36) | % + volts |

---

## Architecture

```
┌─────────────────────────────────────────────────────┐
│  ESP32 Device (battery-powered)                     │
│  BME688 · INMP441 · OLED · Button · Buzzer          │
└──────────────────┬──────────────────────────────────┘
                   │ MQTT TLS :8883 · HTTP :8090 (OTA)
                   ▼
┌─────────────────────────────────────────────────────┐
│  Server (Docker)                                    │
│  Mosquitto → Node-RED → InfluxDB → Grafana          │
│  Nginx (https://hvht.net) · OTA server (:8090)      │
└─────────────────────────────────────────────────────┘
```

---

## Hardware

| Component | Part |
|-----------|------|
| MCU | ESP32 DOIT DevKit V1 |
| Air quality | Bosch BME688 breakout |
| Microphone | INMP441 I2S breakout |
| Display | 0.96" OLED SSD1306 (I2C) |
| Button | Tactile switch (GPIO 4) |
| Buzzer | Passive piezo (GPIO 25) |
| Power | 1S LiPo + USB-C charging module |

KiCad schematic, PCB layout, Gerber files, and BOM are in [`hardware/`](hardware/).

### GPIO Map

| GPIO | Function |
|------|----------|
| 21 | I2C SDA (BME688 + OLED) |
| 22 | I2C SCL (BME688 + OLED) |
| 12 | I2S WS — microphone |
| 13 | I2S LR — microphone channel select |
| 14 | I2S SCK — microphone |
| 26 | I2S SD — microphone data |
| 4  | Button (INPUT_PULLUP, active LOW) |
| 25 | Buzzer (LEDC PWM) |
| 36 | Battery ADC (voltage divider ×2.2) |

---

## Firmware

Built with PlatformIO (Arduino framework).

**Dependencies** (auto-installed via `platformio.ini`):
- `boschsensortec/bsec2` — IAQ/CO₂/VOC
- `olikraus/U8g2` — OLED display
- `bblanchon/ArduinoJson`
- `knolleary/PubSubClient` — MQTT
- `tzapu/WiFiManager` — captive portal provisioning

**Build and flash:**
```bash
# Install PlatformIO CLI or use the VS Code extension
pio run --target upload

# Upload LittleFS (config.json, bsec_state.bin)
pio run --target uploadfs
```

**First boot:** the device launches a captive portal (`HIVIS-Setup-XXXX`). Connect and enter your WiFi credentials and MQTT server address.

Device configuration lives in [`data/config.json`](data/config.json). All pin assignments and thresholds are overridable there without reflashing.

---

## Server

The full server stack runs on Docker. A single Linux machine is enough (tested on a Lenovo ThinkPad X200 with Ubuntu).

**Services:**

| Container | Purpose | Port |
|-----------|---------|------|
| `hivis-mosquitto` | MQTT broker (TLS) | 8883 |
| `hivis-nodered` | Data routing + device provisioning | 1880 |
| `hivis-influxdb` | Time-series storage | 8086 |
| `hivis-grafana` | Dashboards | 3000 |
| `hivis-ota` | Firmware update server | 8090 |
| `hivis-nginx` | Public HTTPS dashboard | 80, 443 |
| `hivis-certbot` | Let's Encrypt auto-renewal | — |

**Deploy:**
```bash
cd server
cp mosquitto/config/passwd.example mosquitto/config/passwd  # create credentials
docker compose up -d
```

See [`docs/system-overview.md`](docs/system-overview.md) for the full setup and [`docs/operations-manual.md`](docs/operations-manual.md) for day-to-day operations.

---

## Button functions

| Press | Duration | Action |
|-------|----------|--------|
| Short | < 400 ms | Next display page |
| Double | 2× within 400 ms | Stop alert buzzer |
| Long | 2–5 s | Launch reconfigure portal |
| Factory reset | > 5 s | Wipe NVS credentials and reboot |

---

## Documentation

| File | Contents |
|------|----------|
| [`docs/system-overview.md`](docs/system-overview.md) | Architecture, pin map, boot sequence, MQTT topics, server stack |
| [`docs/operations-manual.md`](docs/operations-manual.md) | Quick checks, troubleshooting, deploying firmware, adding devices |
| [`spec/01_device_firmware.md`](spec/01_device_firmware.md) | Firmware module details |
| [`spec/02_provisioning.md`](spec/02_provisioning.md) | First-boot captive portal and MQTT registration flow |
| [`spec/03_ota.md`](spec/03_ota.md) | OTA update system |
| [`spec/04_mqtt_data.md`](spec/04_mqtt_data.md) | MQTT topics and JSON payload schema |
| [`spec/05_server_infrastructure.md`](spec/05_server_infrastructure.md) | Docker stack and Mosquitto ACL |
| [`spec/06_grafana_dashboards.md`](spec/06_grafana_dashboards.md) | Dashboard definitions |

---

## Project structure

```
hivis2/
├── src/            # ESP32 firmware (C++/Arduino)
├── data/           # LittleFS filesystem (config.json)
├── hardware/       # KiCad schematic, PCB, Gerbers, BOM
├── server/         # Docker stack, Mosquitto, Node-RED, Grafana, Nginx
├── docs/           # Operations manual + system overview
├── spec/           # Detailed technical specifications
├── partitions.csv  # ESP32 flash partition table
└── platformio.ini  # Build configuration
```
