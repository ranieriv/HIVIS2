# Module 05 — Server Infrastructure

## Overview

All server services run in Docker on the Lenovo ThinkPad X200 (`mqttserver`).  
A single `docker-compose.yml` defines the entire stack.

**Host:** Ubuntu Server, 4GB RAM  
**Static LAN IP:** `172.16.1.156`  
**Tailscale IP:** `100.120.194.59`  
**External domain:** `mqtt.hvht.net` (Cloudflare DNS-only, no proxy)

---

## Services

| Service | Container | Port (host) | Purpose |
|---------|-----------|-------------|---------|
| Mosquitto | `hivis-mosquitto` | 8883 | MQTT broker (TLS) |
| Node-RED | `hivis-nodered` | 1880 | MQTT → InfluxDB routing + registration |
| InfluxDB 2.x | `hivis-influxdb` | 8086 | Time-series storage |
| Grafana | `hivis-grafana` | 3000 | Dashboards |
| OTA Server | `hivis-ota` | 8090 | Firmware delivery |

---

## docker-compose.yml

```yaml
version: "3.8"

services:

  mosquitto:
    image: eclipse-mosquitto:latest
    container_name: hivis-mosquitto
    restart: unless-stopped
    ports:
      - "8883:8883"
    volumes:
      - ./mosquitto/config:/mosquitto/config
      - ./mosquitto/data:/mosquitto/data
      - ./mosquitto/log:/mosquitto/log
      - ./mosquitto/certs:/mosquitto/certs
    networks:
      - hivis-net

  nodered:
    image: nodered/node-red:latest
    container_name: hivis-nodered
    restart: unless-stopped
    ports:
      - "1880:1880"
    volumes:
      - nodered-data:/data
    depends_on:
      - mosquitto
      - influxdb
    environment:
      - TZ=America/Regina
    networks:
      - hivis-net

  influxdb:
    image: influxdb:2
    container_name: hivis-influxdb
    restart: unless-stopped
    ports:
      - "8086:8086"
    volumes:
      - influxdb-data:/var/lib/influxdb2
      - influxdb-config:/etc/influxdb2
    environment:
      - DOCKER_INFLUXDB_INIT_MODE=setup
      - DOCKER_INFLUXDB_INIT_USERNAME=admin
      - DOCKER_INFLUXDB_INIT_PASSWORD=hivishitech2026
      - DOCKER_INFLUXDB_INIT_ORG=hivis
      - DOCKER_INFLUXDB_INIT_BUCKET=hivis
      - DOCKER_INFLUXDB_INIT_ADMIN_TOKEN=REPLACE_WITH_GENERATED_TOKEN
    networks:
      - hivis-net

  grafana:
    image: grafana/grafana:latest
    container_name: hivis-grafana
    restart: unless-stopped
    ports:
      - "3000:3000"
    volumes:
      - grafana-data:/var/lib/grafana
      - ./grafana/provisioning:/etc/grafana/provisioning
    environment:
      - GF_SECURITY_ADMIN_PASSWORD=hivishitech2026
      - GF_USERS_ALLOW_SIGN_UP=false
    depends_on:
      - influxdb
    networks:
      - hivis-net

  ota-server:
    image: python:3.11-slim
    container_name: hivis-ota
    restart: unless-stopped
    working_dir: /app
    volumes:
      - /opt/hivis/ota:/app
    command: python server.py
    ports:
      - "8090:8090"
    networks:
      - hivis-net

networks:
  hivis-net:
    driver: bridge

volumes:
  nodered-data:
  influxdb-data:
  influxdb-config:
  grafana-data:
```

**Important:** Change `DOCKER_INFLUXDB_INIT_ADMIN_TOKEN` and all default passwords before deploying.

---

## Mosquitto Configuration

File: `./mosquitto/config/mosquitto.conf`

```conf
listener 8883
protocol mqtt

# TLS
cafile /mosquitto/certs/ca.crt
certfile /mosquitto/certs/server.crt
keyfile /mosquitto/certs/server.key
require_certificate false

# Auth
allow_anonymous false
password_file /mosquitto/config/passwd
acl_file /mosquitto/config/acl

# Persistence
persistence true
persistence_location /mosquitto/data/

# Logging
log_dest file /mosquitto/log/mosquitto.log
log_type error
log_type warning
log_type notice
```

**TLS certs:** Existing self-signed cert for `mqtt.hvht.net` (already in use in v1). Mount into container at `/mosquitto/certs/`.

**Password file:** Managed with `mosquitto_passwd`. Each device gets its own user. Node-RED gets a `nodered_bridge` user with full access.

```bash
# Create password file inside container
docker exec -it hivis-mosquitto mosquitto_passwd -c /mosquitto/config/passwd nodered_bridge
docker exec -it hivis-mosquitto mosquitto_passwd /mosquitto/config/passwd device_aabbccddeeff
```

**ACL file:** `./mosquitto/config/acl` — see Module 04 for content.

---

## Node-RED Flows

Install required nodes in Node-RED:
```
node-red-contrib-influxdb
```

### Flow 1: Sensor Data → InfluxDB

```
[MQTT in: hivis/+/data]
  → [JSON parse]
  → [Function: build InfluxDB point]
  → [InfluxDB out: hivis bucket]
```

InfluxDB point builder function:
```javascript
const payload = msg.payload;
const isBackfill = payload.backfill === true;

// Use payload timestamp if backfill, otherwise now
const timestamp = isBackfill && payload.ts > 0
    ? new Date(payload.ts * 1000)
    : new Date();

msg.payload = [
    {
        measurement: "air_quality",
        tags: {
            device_id:   payload.device_id,
            device_name: payload.device_name,
            group:       payload.group || "default",
            backfill:    String(isBackfill)
        },
        fields: {
            temp:     payload.temp,
            hum:      payload.hum,
            iaq:      payload.iaq,
            co2:      payload.co2,
            bvoc:     payload.bvoc,
            db:       payload.db,
            bat_pct:  payload.bat_pct,
            bat_mv:   payload.bat_mv,
            rssi:     payload.rssi,
            accuracy: payload.accuracy
        },
        timestamp: timestamp
    }
];

return msg;
```

### Flow 2: Device Registration

```
[MQTT in: hivis/register]
  → [JSON parse]
  → [Function: check whitelist]
  → [Branch: approved / rejected]
  → (approved) [Function: generate credentials]
               [MQTT out: hivis/provision/[mac]]
               [File: update devices.json]
  → (rejected) [MQTT out: hivis/provision/[mac]] (rejected payload)
```

Whitelist check function:
```javascript
const reg = msg.payload;
const mac = reg.mac.toLowerCase().replace(/:/g, "");
const fs = require("fs");
const whitelist = JSON.parse(fs.readFileSync("/data/whitelist.json", "utf8"));

const device = whitelist.devices.find(d =>
    d.mac.toLowerCase().replace(/:/g, "") === mac
);

if (device && device.authorized) {
    msg.approved = true;
    msg.mac = mac;
    msg.device_name = reg.device_name;
} else {
    msg.approved = false;
    msg.mac = mac;
}
return msg;
```

Credential generation function:
```javascript
// Generate a secure random password
const chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
let pass = "";
for (let i = 0; i < 20; i++) {
    pass += chars[Math.floor(Math.random() * chars.length)];
}

const user = "device_" + msg.mac;
const deviceId = "hivis-" + msg.mac;

msg.topic = "hivis/provision/" + msg.mac;
msg.payload = JSON.stringify({
    status: "approved",
    mqtt_user: user,
    mqtt_pass: pass,
    device_id: deviceId
});
msg.retain = true;

// Store credentials for reference (Node-RED context or file)
// Operator must also add user to Mosquitto passwd manually or via exec node
return msg;
```

**Note:** Automatically adding users to Mosquitto's passwd file from Node-RED requires a shell `exec` node running `mosquitto_passwd`. This is acceptable for a managed deployment. The operator can also do this step manually.

### Flow 3: System Status

```
[MQTT in: hivis/+/data]
  → [Function: extract system fields]
  → [InfluxDB out: hivis bucket, measurement: "device_status"]
```

System fields to track separately: `bat_pct`, `bat_mv`, `rssi`, `accuracy`, `fw_version`.

---

## InfluxDB 2.x

**Organization:** `hivis`  
**Bucket:** `hivis`  
**Retention:** Unlimited (or set 90 days for auto-cleanup)

Measurements:
- `air_quality` — all sensor readings
- `device_status` — system fields (battery, RSSI, accuracy)

Tags on all measurements:
- `device_id`
- `device_name`
- `group`
- `backfill`

---

## File Structure on Host

```
/opt/hivis/
  docker-compose.yml
  mosquitto/
    config/
      mosquitto.conf
      passwd
      acl
    certs/
      ca.crt           ← existing cert
      server.crt       ← existing cert
      server.key       ← existing key
    data/
    log/
  grafana/
    provisioning/
      datasources/
        influxdb.yml
  ota/
    server.py
    firmware/
      latest.bin
      version.txt
    devices.json
  server/
    whitelist.json
```

---

## Grafana Data Source Provisioning

File: `./grafana/provisioning/datasources/influxdb.yml`

```yaml
apiVersion: 1

datasources:
  - name: InfluxDB-hivis
    type: influxdb
    access: proxy
    url: http://influxdb:8086
    jsonData:
      version: Flux
      organization: hivis
      defaultBucket: hivis
      tlsSkipVerify: true
    secureJsonData:
      token: REPLACE_WITH_INFLUXDB_TOKEN
    isDefault: true
```

---

## Security Notes

- All services are on an internal Docker bridge network (`hivis-net`). Only the ports listed are exposed to the host.
- Grafana and Node-RED UIs are LAN/Tailscale only. Do not expose port 1880 or 3000 to the internet.
- Mosquitto port 8883 is the only port that needs to be internet-accessible (via `mqtt.hvht.net`).
- Existing Tailscale + fail2ban setup on the X200 continues to apply.
- Change all default passwords in `docker-compose.yml` before first run.
