# Module 02 — Provisioning

## Overview

Device provisioning happens in two phases:
1. **WiFi + identity setup** via captive portal (user-initiated, first boot or long press)
2. **MQTT credential provisioning** by the server after MAC authorization (automatic)

Credentials are stored in ESP32 NVS (Preferences API), never in files.

---

## PortalModule

Wraps the `tzapu/WiFiManager` library. Handles first boot and reconfigure flows.

### Library

```
tzapu/WiFiManager @ ^2.0.17
```

Add to `platformio.ini` `lib_deps`.

### Custom Parameters

WiFiManager supports extra form fields via `WiFiManagerParameter`. The portal collects:

| Field | NVS Key | Default | Notes |
|-------|---------|---------|-------|
| Device Name | `device_name` | `hivis-[mac_suffix]` | Max 20 chars |
| Server Address | `mqtt_server` | `mqtt.hvht.net` | MQTT broker hostname |

WiFi SSID and password are handled natively by WiFiManager and stored by it in its own NVS namespace (`espwifimgr`). MQTT credentials are NOT shown in the portal — they are provisioned by the server.

### Portal AP Name

The AP SSID shown to the user is: `HIVIS-Setup-[last4ofMAC]`  
Example: `HIVIS-Setup-A3F2`

### PortalModule API

```cpp
class PortalModule {
public:
    PortalModule();
    void begin();

    // Returns true if portal was triggered and completed successfully
    bool runFirstBoot();   // Blocks until configured or timeout (3 min)
    bool runReconfigure(); // Triggered by long press

    String getDeviceName();
    String getMqttServer();

    // After portal completes, save custom params to NVS
    void saveToNVS();
};
```

### Flow

```
runFirstBoot():
  1. WiFiManager.addParameter(deviceName)
  2. WiFiManager.addParameter(mqttServer)
  3. WiFiManager.autoConnect("HIVIS-Setup-XXXX")
     → If user connects and submits form:
         WiFi creds saved by WiFiManager to its NVS namespace
         Custom params available via getParameter()
  4. saveToNVS() → write device_name, mqtt_server to "hivis" NVS namespace
  5. Return true

runReconfigure():
  Same flow but uses WiFiManager.startConfigPortal() instead of autoConnect()
```

---

## Device Registration

After WiFi connects for the first time (provisioned flag not set), the device sends a registration request to the server.

### Registration MQTT Topic

```
hivis/register
```

### Registration Payload

```json
{
  "mac": "AA:BB:CC:DD:EE:FF",
  "device_name": "classroom-3",
  "fw_version": "2.0.0",
  "group": ""
}
```

### Server Response Topic

Server publishes to:
```
hivis/provision/[mac_lowercase_no_colons]
```

Example: `hivis/provision/aabbccddeeff`

### Server Response Payload

If MAC is in whitelist:
```json
{
  "status": "approved",
  "mqtt_user": "device_aabbccddeeff",
  "mqtt_pass": "generatedSecurePassword123",
  "device_id": "hivis-aabbccddeeff"
}
```

If MAC is NOT in whitelist:
```json
{
  "status": "rejected"
}
```

### Device Behavior on Response

- **Approved:** Save `mqtt_user`, `mqtt_pass`, `device_id` to NVS. Set `provisioned = 1`. Reconnect MQTT with new credentials. Double-beep.
- **Rejected:** Show "NOT AUTHORIZED" on display. Three error beeps. Enter offline mode. Retry registration on next boot.
- **No response in 30s:** Proceed in offline mode, retry on next boot.

---

## NVS Schema

Namespace: `"hivis"` (Preferences)

```cpp
Preferences prefs;
prefs.begin("hivis", false);

// Write
prefs.putString("device_name", name);
prefs.putString("mqtt_server", server);
prefs.putString("mqtt_user", user);
prefs.putString("mqtt_pass", pass);
prefs.putString("device_id", id);
prefs.putInt("provisioned", 1);

// Read
String name   = prefs.getString("device_name", "");
String server = prefs.getString("mqtt_server", "mqtt.hvht.net");
String user   = prefs.getString("mqtt_user", "");
String pass   = prefs.getString("mqtt_pass", "");
String id     = prefs.getString("device_id", "");
int provisioned = prefs.getInt("provisioned", 0);

prefs.end();
```

WiFi credentials (SSID/pass) are stored by WiFiManager in its own namespace and do not need to be read manually — `WiFi.begin()` with no arguments reconnects to the saved network.

---

## Device ID Generation

Device ID is MAC-derived and assigned by the server, but the device also generates a local fallback:

```cpp
String localId = "hivis-" + String((uint32_t)ESP.getEfuseMac(), HEX);
```

The server's assigned ID (from provision response) overwrites this in NVS and is used for all MQTT topics.

---

## Server Side: MAC Whitelist

The MAC whitelist is a simple JSON file on the server, managed manually by the operator.

File: `/opt/hivis/server/whitelist.json`

```json
{
  "devices": [
    {
      "mac": "aa:bb:cc:dd:ee:ff",
      "authorized": true,
      "group": "saskpoly",
      "notes": "Classroom unit 1"
    },
    {
      "mac": "11:22:33:44:55:66",
      "authorized": true,
      "group": "saskpoly",
      "notes": "Lab unit"
    }
  ]
}
```

Node-RED reads this file and checks incoming `hivis/register` messages against it. If authorized, Node-RED generates MQTT credentials and publishes the provision response.

Node-RED also creates the MQTT user in Mosquitto's password file via a shell exec node (or pre-configured). See Module 05 for server-side detail.
