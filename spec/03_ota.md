# Module 03 — OTA Firmware Update System

## Overview

Devices poll an HTTP server for firmware updates. The server controls which devices receive an update via a per-device approval flag. Devices never update without server authorization.

---

## OTA Flow

```
Device boots
  → Check NVS: time since last OTA check > otaCheckIntervalH (or first boot)
  → GET http://[ota_server]/ota/version?mac=[device_mac]
  → Server checks:
      - Is this MAC in the whitelist?
      - Is ota_approved = true for this device?
      - Is server version > device FIRMWARE_VERSION?
  → If yes: respond with latest version string
  → Device compares: server_version > FIRMWARE_VERSION?
  → If yes: display "Updating firmware..." on OLED, perform HTTPUpdate
  → On success: reboot into new firmware
  → On failure: log error, continue with current firmware
  → Update NVS ota_last_check timestamp
```

### Check Interval

- On every boot (cold start)
- Every `cfg.otaCheckIntervalH` hours during operation (default 24h)
- Checked via `millis()` comparison against stored `ota_last_check` offset

---

## OTAModule

```cpp
class OTAModule {
public:
    OTAModule(const String& serverUrl, const String& deviceId,
              const String& mac, const String& currentVersion);
    void begin();

    // Call on boot and periodically
    // Returns: true if update was applied (device will reboot)
    bool checkAndUpdate();

private:
    String _serverUrl;  // e.g. "http://172.16.1.156:8090"
    String _deviceId;
    String _mac;
    String _currentVersion;

    bool _shouldCheck();
    void _saveLastCheckTime();
};
```

### HTTPUpdate Usage

```cpp
#include <HTTPUpdate.h>

WiFiClient client;
String url = _serverUrl + "/ota/firmware?mac=" + _mac;

httpUpdate.setLedPin(LED_BUILTIN, LOW);
t_httpUpdate_return ret = httpUpdate.update(client, url, _currentVersion);

switch (ret) {
    case HTTP_UPDATE_FAILED:
        Serial.printf("OTA failed: %s\n", httpUpdate.getLastErrorString().c_str());
        break;
    case HTTP_UPDATE_NO_UPDATES:
        Serial.println("OTA: firmware is up to date.");
        break;
    case HTTP_UPDATE_OK:
        Serial.println("OTA: update applied, rebooting.");
        // Device reboots automatically
        break;
}
```

The ESP32 HTTPUpdate library sends the following headers automatically:
- `x-ESP32-STA-MAC` — device MAC (used by server for per-device approval)
- `x-ESP32-free-space` — available flash
- `x-ESP32-sketch-size` — current firmware size

---

## OTA Server

A lightweight Python HTTP server running in Docker (or as a systemd service) on the X200.

### Directory Structure

```
/opt/hivis/ota/
  server.py           ← HTTP server
  firmware/
    latest.bin        ← Current approved firmware binary
    version.txt       ← Current version string, e.g. "2.0.1"
  devices.json        ← Per-device approval flags
```

### devices.json

```json
{
  "devices": {
    "aabbccddeeff": {
      "ota_approved": true,
      "notes": "Classroom unit 1"
    },
    "112233445566": {
      "ota_approved": false,
      "notes": "Test device — not yet approved"
    }
  }
}
```

Setting `ota_approved: true` for a device means it will receive the update on its next check. After a successful update, the operator should set it back to `false` (or leave it `true` for auto-updates — operator's choice).

### server.py — Endpoints

**GET `/ota/version?mac=[mac]`**

Checks whitelist + approval flag. Returns version string or 304.

Response if update available:
```
HTTP 200
Content-Type: text/plain
Body: 2.0.1
```

Response if not approved or up to date:
```
HTTP 304 Not Modified
```

**GET `/ota/firmware?mac=[mac]`**

Validates MAC header from ESP32 HTTPUpdate. If approved, serves `firmware/latest.bin`.

```python
# server.py pseudocode
from http.server import BaseHTTPRequestHandler, HTTPServer
import json, os

DEVICES_FILE = "devices.json"
FIRMWARE_FILE = "firmware/latest.bin"
VERSION_FILE = "firmware/version.txt"

class OTAHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        mac = self._get_mac()
        if not self._is_approved(mac):
            self.send_response(304)
            self.end_headers()
            return

        if self.path.startswith("/ota/version"):
            version = open(VERSION_FILE).read().strip()
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(version.encode())

        elif self.path.startswith("/ota/firmware"):
            data = open(FIRMWARE_FILE, "rb").read()
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    def _get_mac(self):
        # From URL param or ESP32 header
        mac = self.headers.get("x-ESP32-STA-MAC", "").lower().replace(":", "")
        if not mac:
            from urllib.parse import urlparse, parse_qs
            params = parse_qs(urlparse(self.path).query)
            mac = params.get("mac", [""])[0].replace(":", "").lower()
        return mac

    def _is_approved(self, mac):
        with open(DEVICES_FILE) as f:
            data = json.load(f)
        device = data.get("devices", {}).get(mac, {})
        return device.get("ota_approved", False)
```

### Docker Service

```yaml
# In docker-compose.yml
ota-server:
  image: python:3.11-slim
  container_name: hivis-ota
  working_dir: /app
  volumes:
    - /opt/hivis/ota:/app
  command: python server.py
  ports:
    - "8090:8090"
  restart: unless-stopped
```

---

## Deployment Workflow

```
1. Build new firmware in PlatformIO → .pio/build/esp32doit-devkit-v1/firmware.bin
2. Manually flash test device via USB
3. Validate — confirm new version appears on serial / display
4. Copy firmware.bin to /opt/hivis/ota/firmware/latest.bin on server
5. Update /opt/hivis/ota/firmware/version.txt to new version string
6. Edit devices.json: set ota_approved = true for test device
7. Device polls → downloads → reboots → confirm correct version
8. Set ota_approved = true for production devices
9. Devices update on next check (boot or 24h interval)
```

---

## Partition Table Requirement

OTA requires dual app partitions. Use this `partitions.csv` in the project root:

```csv
# Name,   Type, SubType, Offset,   Size,     Flags
nvs,      data, nvs,     0x9000,   0x5000,
otadata,  data, ota,     0xe000,   0x2000,
app0,     app,  ota_0,   0x10000,  0x1C0000,
app1,     app,  ota_1,   ,         0x1C0000,
spiffs,   data, spiffs,  ,         0x3F0000,
```

Reference in `platformio.ini`:
```ini
board_build.partitions = partitions.csv
```

**Note:** The `app0` + `app1` partitions are each 1.75MB. Total firmware binary must stay under 1.75MB. Monitor with `pio run` output. The BSEC library is large — if binary exceeds limit, reduce LittleFS size or optimize.
