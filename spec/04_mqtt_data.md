# Module 04 — MQTT Topics, Payload Format & Offline Backfill

## Overview

All device-to-server communication uses MQTT over TLS (port 8883) to `mqtt.hvht.net`. Each device publishes to its own topic namespace using its assigned device ID.

---

## Topic Structure

```
hivis/[device_id]/sensors/temp
hivis/[device_id]/sensors/humidity
hivis/[device_id]/sensors/iaq
hivis/[device_id]/sensors/co2
hivis/[device_id]/sensors/bvoc
hivis/[device_id]/sensors/noise
hivis/[device_id]/system/battery_pct
hivis/[device_id]/system/battery_mv
hivis/[device_id]/system/rssi
hivis/[device_id]/system/accuracy
hivis/[device_id]/system/status

hivis/register                           ← Device registration (first boot)
hivis/provision/[mac_no_colons]          ← Server → device credential delivery
```

**Device ID format:** `hivis-[mac_hex]`  
Example: `hivis-aabbccddeeff`

---

## Standard Sensor Payload

All sensor topics use a **single JSON payload** published to a base topic:

```
hivis/[device_id]/data
```

This simplifies Node-RED routing (one handler vs seven separate subscriptions).

### Payload Format

```json
{
  "device_id": "hivis-aabbccddeeff",
  "device_name": "classroom-3",
  "group": "saskpoly",
  "ts": 1714000000,
  "ts_accurate": true,
  "fw_version": "2.0.0",
  "temp": 21.5,
  "hum": 45.2,
  "iaq": 82.3,
  "co2": 654,
  "bvoc": 0.51,
  "db": 48.2,
  "bat_pct": 87,
  "bat_mv": 3850,
  "rssi": -62,
  "accuracy": 3,
  "backfill": false
}
```

### Field Reference

| Field | Type | Unit | Notes |
|-------|------|------|-------|
| `device_id` | string | — | MAC-derived ID |
| `device_name` | string | — | Human-readable name |
| `group` | string | — | Organization group (future use) |
| `ts` | int | Unix seconds | 0 if no NTP time available |
| `ts_accurate` | bool | — | false if no NTP during recording |
| `fw_version` | string | — | Semantic version |
| `temp` | float | °C | 1 decimal place |
| `hum` | float | % RH | 1 decimal place |
| `iaq` | float | 0–500 | BSEC IAQ scale |
| `co2` | float | ppm | BSEC equivalent CO2 |
| `bvoc` | float | ppm | Breath VOC equivalent |
| `db` | float | dB SPL | Calibrated sound level |
| `bat_pct` | int | % | Battery percentage (0–100) |
| `bat_mv` | int | mV×10 | e.g. 3850 = 3.85V (×100 actually) |
| `rssi` | int | dBm | WiFi signal strength |
| `accuracy` | int | 0–3 | BSEC accuracy level |
| `backfill` | bool | — | true if this is a buffered offline record |

**Note:** `bat_mv` is stored as integer millivolts (e.g. 3850 for 3.850V). Multiply raw ADC voltage × 1000 before storing.

---

## MqttModule Changes vs v1

The `MqttModule` is updated to:
- Use `device_id` from NVS (not hardcoded)
- Publish to `hivis/[device_id]/data` with full JSON payload
- Subscribe to `hivis/provision/[mac]` for credential provisioning response
- Subscribe to `hivis/[device_id]/cmd` for future server commands (reserved, not implemented)
- Use `setInsecure()` for TLS (same as v1, CA pinning is a future upgrade)

### MQTT Keepalive

Set via `cfg.mqttKeepaliveS` (default 60s). PubSubClient `setKeepAlive()` must be called before `connect()`.

### Reconnect Logic

On `publish()` call:
1. If WiFi disconnected → `setupWifi()` → if fails, buffer locally
2. If MQTT disconnected → `connectMqtt()` (3 retries, 1s apart)
3. If still disconnected → skip publish, buffer locally

---

## Offline Backfill

### During Offline Period

```cpp
// In offline mode, store to LittleFS
void OfflineBuffer::store(const DisplayData& data) {
    JsonDocument doc;
    doc["ts"] = 0;
    doc["ts_accurate"] = false;
    doc["temp"] = data.temp;
    // ... all fields
    doc["backfill"] = true;

    String line;
    serializeJson(doc, line);

    File f = LittleFS.open("/offline_buffer.ndjson", "a");
    if (f) {
        f.println(line);
        f.close();
        _count++;
    }

    if (_count >= MAX_BUFFER_RECORDS) {
        _dropOldest(); // Remove first line from file
    }
}
```

File format: newline-delimited JSON (NDJSON) for easy append and line-by-line reading.

### On Reconnect: Backfill Flush

```cpp
void OfflineBuffer::flush(MqttModule* mqtt) {
    if (!LittleFS.exists("/offline_buffer.ndjson")) return;

    File f = LittleFS.open("/offline_buffer.ndjson", "r");
    if (!f) return;

    // Count lines
    int count = 0;
    while (f.available()) {
        f.readStringUntil('\n');
        count++;
    }
    f.seek(0);

    // Get current time
    time_t now = time(nullptr);
    int intervalS = cfg.mqttIntervalMs / 1000;

    // Read and publish with approximate timestamps
    int index = count - 1;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.isEmpty()) { index--; continue; }

        JsonDocument doc;
        deserializeJson(doc, line);

        // Assign approximate timestamp (older records first)
        doc["ts"] = (int)(now - (index * intervalS));
        doc["ts_accurate"] = false;
        doc["backfill"] = true;

        String payload;
        serializeJson(doc, payload);

        mqtt->publishRaw("hivis/" + deviceId + "/data", payload);
        index--;
        delay(50); // Don't flood broker
    }

    f.close();
    LittleFS.remove("/offline_buffer.ndjson");
    _count = 0;
    Serial.println("Offline buffer flushed.");
}
```

### Node-RED Handling of Backfill

Node-RED receives all `hivis/+/data` messages. When `backfill == true`:
- Use `msg.payload.ts` as the InfluxDB timestamp (not `Date.now()`)
- Tag the measurement with `backfill: true` in InfluxDB for visibility

InfluxDB 2.x accepts historical writes with past timestamps natively. No special configuration needed.

---

## MQTT Security

| Setting | Value |
|---------|-------|
| Port | 8883 (TLS) |
| Auth | Username + password (provisioned per device) |
| TLS | `setInsecure()` — accepts self-signed cert |
| Future upgrade | CA cert pinning (replace `setInsecure()` with `setCACert()`) |

Each device has its own MQTT username/password, provisioned by the server after MAC authorization. This allows per-device revocation by disabling the Mosquitto user.

### Mosquitto ACL

Each device user can only publish to its own namespace:

```
# /etc/mosquitto/acl
user device_aabbccddeeff
topic write hivis/aabbccddeeff/#
topic read  hivis/provision/aabbccddeeff

user device_112233445566
topic write hivis/112233445566/#
topic read  hivis/provision/112233445566

# Node-RED internal user (full access)
user nodered_bridge
topic readwrite hivis/#
```

Note: Topic in ACL uses MAC without hyphens to match the provisioned user naming convention. Adjust naming to be consistent.

---

## QoS Levels

| Topic | QoS | Retained | Notes |
|-------|-----|----------|-------|
| `hivis/[id]/data` (live) | 0 | false | Fire and forget, high frequency |
| `hivis/[id]/data` (backfill) | 1 | false | At-least-once for reliability |
| `hivis/register` | 1 | false | Registration must be received |
| `hivis/provision/[mac]` | 1 | true | Retained so device catches it on reconnect |
