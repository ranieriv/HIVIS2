# HIVIS Monitor 2.0 — Operations Manual

**Version:** 2.0.1
**Server:** 172.16.1.156 (ssh mqttadmin@172.16.1.156)
**Grafana:** http://172.16.1.156:3000 (admin / hivishitech2026)

---

## Quick Check (2–3 minutes)

Run this whenever you want to confirm the system is alive.

### QC-1 · Docker containers running

```bash
ssh mqttadmin@172.16.1.156 "cd /opt/hivis && docker compose ps"
```

**Expected:** All 5 containers show `Up` (or `running`).

| Container | Status |
|-----------|--------|
| hivis-mosquitto | Up |
| hivis-nodered | Up |
| hivis-influxdb | Up |
| hivis-grafana | Up |
| hivis-ota | Up |

**If a container is down:**
```bash
cd /opt/hivis && docker compose up -d <service-name>
```

---

### QC-2 · Grafana shows live data

1. Open http://172.16.1.156:3000
2. Go to **Fleet Overview** dashboard (`/d/hivis-fleet`)
3. Confirm your device appears in the table with a timestamp within the last 10 seconds

**If the table is empty:** jump to Full Check → FC-5.

---

### QC-3 · Device serial output (if device is connected to PC)

Open PlatformIO serial monitor at 115200 baud. You should see lines like:

```
IAQ:75.2 Hum:45.3% Temp:22.5°C CO2:412 BVOC:0.52 dB:65.3 Acc:3 Bat:85%(3.85V) ONLINE
```

**Green flags:**
- `ONLINE` at end of each line
- `Acc:` is 1, 2, or 3 (not 0 — 0 = still calibrating)
- No `MQTT state=-1` or `state=5` errors

**If you see `OFFLINE`:** check WiFi and proceed to FC-3.

---

### QC-4 · OLED display

The device OLED should show sensor readings continuously (no blank screen).
Press the button (GPIO 0) briefly to cycle through the 3 pages:
- Page 0: IAQ + CO₂ + noise
- Page 1: Temp + Humidity + BVOC
- Page 2: WiFi SSID + battery + server connection status

If the screen is blank: `timeout_ms` in config.json must be 0 (always-on). See Full Check → FC-7.

---

## Full Check

Run this after making changes, after a server reboot, or when something isn't working.

---

### FC-1 · Server health — all services

```bash
ssh mqttadmin@172.16.1.156 "cd /opt/hivis && docker compose ps && docker compose logs --tail=20"
```

Check for error lines in each service log. Common problems:

| Log pattern | Meaning | Fix |
|-------------|---------|-----|
| `Connection refused` in nodered | InfluxDB/Mosquitto not ready | `docker compose restart nodered` |
| `address already in use` | Port conflict | `docker compose down && docker compose up -d` |
| `permission denied /mosquitto/config` | Volume mount issue | Check ownership: `ls -la /opt/hivis/mosquitto/config/` |

---

### FC-2 · Mosquitto MQTT broker

**Test broker is accepting TLS connections:**
```bash
ssh mqttadmin@172.16.1.156 \
  "docker exec hivis-mosquitto mosquitto_sub \
     --cafile /mosquitto/certs/ca.crt \
     -h mqtt.hvht.net -p 8883 \
     -u nodered_bridge -P <nodered_bridge_password> \
     -t 'hivis/#' -C 1 --quiet -W 5"
```

**Expected:** Receives one message within 5 seconds (if a device is publishing).

**Check Mosquitto log for recent connections:**
```bash
ssh mqttadmin@172.16.1.156 "tail -50 /opt/hivis/mosquitto/log/mosquitto.log"
```

Look for:
- `New client connected` lines for your device (e.g., `device_4cc382c32764`)
- No `CONNREFUSED` for `nodered_bridge`

**Check active ACL (who can connect):**
```bash
ssh mqttadmin@172.16.1.156 "cat /opt/hivis/mosquitto/config/acl"
```

Your provisioned device should have an entry like:
```
user device_4cc382c32764
topic write hivis/hivis-4cc382c32764/#
topic read  hivis/provision/4cc382c32764
```

---

### FC-3 · WiFi / network path from device

On device serial monitor, immediately after boot, confirm:
```
6. Connecting WiFi... OK (192.168.x.x)
```

If WiFi fails:
1. Long-press GPIO 0 (> 2 s) to launch reconfigure portal
2. Connect phone/PC to `HIVIS-Setup-XXXX` hotspot
3. Browse to 192.168.4.1 — enter correct WiFi SSID/password and MQTT server
4. Save and wait for device reboot

---

### FC-4 · Device provisioning check

On device serial, confirm provisioning completes:
```
7. Registering device with server...
Registration: waiting for server response (30s)...
Registration: approved — user=device_XXXX  id=hivis-XXXX
```

If you see `REJECTED` or no response after 30 s:

1. Check whitelist:
```bash
ssh mqttadmin@172.16.1.156 "docker exec hivis-nodered cat /data/whitelist.json"
```

2. Add device MAC if missing (whitelist lives inside the Node-RED container):
```bash
ssh mqttadmin@172.16.1.156 "docker exec hivis-nodered nano /data/whitelist.json"
# Add to the "devices" array:
# { "mac": "AA:BB:CC:DD:EE:FF", "authorized": true, "group": "saskpoly", "notes": "Room X" }
```

3. Erase NVS to force re-registration (closes serial monitor first!):
```bash
python -m esptool --port COM7 erase_region 0x9000 0x5000
```

---

### FC-5 · InfluxDB data pipeline

**Step A — Check InfluxDB is receiving data:**
```bash
ssh mqttadmin@172.16.1.156 \
  "docker exec hivis-influxdb influx query \
     --org hivis \
     --token <influx-admin-token> \
     'from(bucket:\"hivis\") |> range(start:-5m) |> limit(n:3)'"
```

**Expected:** 3 rows with fields like `iaq`, `temp`, `co2`.

**Step B — Check Node-RED is routing data:**

1. Open http://172.16.1.156:1880
2. Look at the `MQTT In` node — the green dot should be `connected`
3. Click **Deploy** if any nodes show orange (pending changes)
4. Look for the debug panel on the right — it shows warnings/errors from function nodes

Common Node-RED errors and fixes:

| Error | Cause | Fix |
|-------|-------|-----|
| `MQTT: connection refused` | nodered_bridge password wrong | Re-check Mosquitto passwd file |
| `global.get('http') is null` | settings.js missing http entry | Add `http: require("http")` to functionGlobalContext and restart |
| `No measurement specified` | InfluxDB function node using wrong format | `msg.measurement` must be set before InfluxDB out node |

---

### FC-6 · Grafana datasource health

1. Open http://172.16.1.156:3000
2. Go to **Connections → Data sources → InfluxDB**
3. Scroll to bottom → click **Save & Test**

**Expected:** `datasource connected and 3 buckets found`

If it fails:
- Check InfluxDB token in the datasource config matches the one in InfluxDB
- Verify InfluxDB container is up: `docker compose ps`
- Check InfluxDB URL is `http://hivis-influxdb:8086` (Docker network name, not localhost)

---

### FC-7 · OTA server

**Check version endpoint:**
```bash
curl http://172.16.1.156:8090/ota/version?mac=4cc382c32764
```

**Expected output:** `2.0.1` (or current firmware version)

**Check firmware file exists:**
```bash
ssh mqttadmin@172.16.1.156 "ls -lh /opt/hivis/ota/firmware/"
```

**Expected:**
```
latest.bin    (> 500 KB)
version.txt
```

**Check OTA log from server:**
```bash
ssh mqttadmin@172.16.1.156 "docker logs hivis-ota --tail=30"
```

Look for `GET /ota/version` and `POST /provision` entries.

---

### FC-8 · Display / OLED always-on check

Verify `config.json` has `timeout_ms` set to 0:

In PlatformIO serial monitor, after boot:
```
1. Loading config... OK
```

Check LittleFS config with:
```bash
# Open serial monitor, trigger a factory reset to re-upload config.json
# Or verify the file directly in the project:
cat data/config.json | grep timeout
```

Expected: `"timeout_ms": 0`

If screen keeps going off: rebuild and re-upload filesystem:
```bash
pio run --target uploadfs
```

---

### FC-9 · BSEC2 calibration status

BSEC2 takes ~30 minutes to reach accuracy 3 on first boot. After that, calibration is saved to `/littlefs/bsec_state.bin` every 6 hours.

On serial monitor:
```
IAQ:75.2 ... Acc:3 ...    ← fully calibrated, values reliable
IAQ:25.0 ... Acc:0 ...    ← still warming up, IAQ not meaningful yet
```

**If `bsec_state.bin does not exist` error appears in serial:**
- The LittleFS filesystem may not have been uploaded
- Fix: `pio run --target uploadfs` (uploads `data/` folder to device)

After calibration file is saved, reboots skip the 30-minute warmup.

---

### FC-10 · End-to-end data flow verification

This confirms data flows all the way from sensor to Grafana.

1. **Device** publishes to `hivis/hivis-[mac]/data` every 3 seconds
2. **Mosquitto** receives and routes to `nodered_bridge` subscriber
3. **Node-RED** parses JSON → sets `msg.measurement`, `msg.tags`, `msg.payload` → writes to InfluxDB
4. **InfluxDB** stores in `hivis` bucket, `air_quality` and `device_status` measurements
5. **Grafana** queries InfluxDB, displays on Fleet Overview dashboard

**Verify each hop:**

```bash
# Hop 1-2: Mosquitto receiving device data
ssh mqttadmin@172.16.1.156 "tail -f /opt/hivis/mosquitto/log/mosquitto.log"
# Should see publish lines every 3s

# Hop 3: Node-RED processing (check debug panel in UI at :1880)

# Hop 4: InfluxDB storing data
ssh mqttadmin@172.16.1.156 \
  "docker exec hivis-influxdb influx query --org hivis --token <token> \
   'from(bucket:\"hivis\") |> range(start:-1m) |> count()'"

# Hop 5: Grafana (open browser)
```

---

## Deploying a New Firmware Version

```bash
# 1. Edit platformio.ini — bump version number
#    -DFIRMWARE_VERSION='"2.0.2"'

# 2. Build
pio run

# 3. Upload binary to server
scp .pio/build/esp32doit-devkit-v1/firmware.bin \
    mqttadmin@172.16.1.156:/opt/hivis/ota/firmware/latest.bin

# 4. Update version file on server
ssh mqttadmin@172.16.1.156 "echo '2.0.2' > /opt/hivis/ota/firmware/version.txt"

# 5. Verify OTA endpoint
curl http://172.16.1.156:8090/ota/version?mac=4cc382c32764
# → 2.0.2

# Device will pick up the new version on next boot (if > 24h since last OTA check)
# Or erase NVS ota_last_chk key to force immediate check:
#   python -m esptool --port COM7 erase_region 0x9000 0x5000
```

---

## Adding a New Device

```bash
# 1. Get the device MAC from serial monitor (printed at boot):
#    MAC=4C:C3:82:C3:27:64  → use lowercase no-colon form: 4cc382c32764

# 2. Add to whitelist inside the Node-RED container
ssh mqttadmin@172.16.1.156 "docker exec hivis-nodered nano /data/whitelist.json"
# Add to the "devices" array:
# { "mac": "4C:C3:82:C3:27:64", "authorized": true, "group": "saskpoly", "notes": "Room 102" }

# 3. Add to OTA approved list
ssh mqttadmin@172.16.1.156 "nano /opt/hivis/ota/devices.json"
# Add to the "devices" object:
# "4cc382c32764": { "ota_approved": true, "notes": "Room 102 — 4C:C3:82:C3:27:64" }

# 4. Power on device — it will auto-register and appear in Grafana within 1 minute
```

---

## Common Problems — Quick Reference

| Symptom | Most Likely Cause | Fix |
|---------|-------------------|-----|
| Device shows `MQTT state=5` on every boot | Stale retained provisioning message | Erase NVS (0x9000) and reboot |
| Device shows `MQTT state=-1` | WiFi OK but MQTT broker unreachable | Check Mosquitto container, TLS cert |
| Device loops in captive portal | `mqttServer` empty in NVS | Complete portal setup, save |
| Grafana shows no data | InfluxDB write failing | Check Node-RED debug, check FC-5 |
| `No measurement specified` in Node-RED | InfluxDB node format wrong | Set `msg.measurement` in function node |
| OTA check returns version `-1` | OTA server down or MAC not in devices.json | Check FC-7, add MAC to devices.json |
| `bsec_state.bin` error | LittleFS not uploaded | `pio run --target uploadfs` |
| IAQ always shows 0, Acc=0 | BSEC2 warming up | Wait 30 minutes, accuracy will reach 1+ |
| Screen blank / display off | `timeout_ms` non-zero | Set `timeout_ms: 0` in config.json, re-upload FS |
| Factory reset triggered accidentally | GPIO 0 held > 5s during boot | Avoid pressing GPIO 0 at boot |
| `fetch is not defined` in Node-RED | Node-RED doesn't expose global fetch | Use `global.get('http')` with functionGlobalContext |

---

## Key Credentials

| Service | Username | Password |
|---------|----------|----------|
| Grafana | `admin` | `hivishitech2026` |
| MQTT bootstrap (unregistered devices) | `hivis_bootstrap` | `hivishitech2026` |
| SSH server | `mqttadmin` | (your SSH key) |
| InfluxDB admin | `admin` | (set during InfluxDB setup) |

---

*Last updated: 2026-03-15 · HIVIS Monitor 2.0.1*
