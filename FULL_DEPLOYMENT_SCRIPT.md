# HIVIS Monitor — Website Deployment Guide

Deploy the HIVIS live dashboard to **hvht.net** on the X200 server (172.16.1.156).
All website files live in `server/` in this repo — copy them to the X200 and run the steps below.

---

## Pre-Flight Checklist

Run on X200 before starting:

```bash
# InfluxDB must already be running (existing stack)
curl http://localhost:8086/health

# Check current public IP (needed for DNS)
curl -s ifconfig.me && echo

# Docker running?
docker ps

# Note docker-compose version (use 'docker compose' if v2)
docker compose version || docker-compose --version
```

---

## Step 1 — Copy repo files to X200

From your dev machine (adjust SSH target as needed):

```bash
# From repo root
scp -r server/ user@172.16.1.156:/opt/hivis/server/
```

Or if the repo is already cloned on X200, just pull:

```bash
cd /opt/hivis && git pull
```

The `server/` directory should contain:
```
server/
  docker-compose.yml      ← already running (mosquitto/influxdb/grafana/nodered/ota)
  website/
    index.html            ← the live dashboard
  nginx/
    http-only.conf        ← bootstrap: HTTP-only (use during cert request)
    hivis.conf            ← production: HTTPS + InfluxDB proxy
```

---

## Step 2 — Bootstrap: Start Nginx in HTTP-only mode

The nginx config directory (`server/nginx/`) contains two configs.
Nginx loads **all** `.conf` files in `conf.d/`. During the bootstrap we only want
`http-only.conf` loaded, so rename the production config temporarily:

```bash
cd /opt/hivis/server/nginx

# Temporarily disable production config (Nginx can't start without the cert)
mv hivis.conf hivis.conf.disabled

# Verify only http-only.conf is present
ls *.conf
# expected: http-only.conf
```

Start the full stack (nginx + certbot + existing services):

```bash
cd /opt/hivis/server
docker compose up -d

# All containers should be running
docker compose ps
```

Test HTTP is reachable locally:

```bash
curl -I http://172.16.1.156
# Expected: HTTP/1.1 200 OK
```

---

## Step 3 — Configure Router Port Forwarding

**Manual step on Arris NVG448B (192.168.100.1):**

| External Port | Protocol | Internal IP  | Internal Port |
|---------------|----------|--------------|---------------|
| 80            | TCP      | 172.16.1.156 | 80            |
| 443           | TCP      | 172.16.1.156 | 443           |

---

## Step 4 — Configure DNS (Cloudflare)

1. Log in to Cloudflare
2. Go to hvht.net → DNS
3. Set A record:
   - **Name:** `@` (or `hvht.net`)
   - **Type:** A
   - **Value:** your current public IP (from Step 0)
   - **Proxy status:** DNS only (gray cloud — NOT proxied)
4. Set CNAME for www:
   - **Name:** `www`
   - **Type:** CNAME
   - **Value:** `hvht.net`
   - **Proxy status:** DNS only

Wait 2–5 minutes for DNS to propagate, then verify:

```bash
nslookup hvht.net
# Should return your public IP
```

---

## Step 5 — Request SSL Certificate (first time only)

Once DNS is pointing at the server and port 80 is reachable externally:

```bash
docker run -it --rm \
  -v hivis_server_certbot-conf:/etc/letsencrypt \
  -v hivis_server_certbot-www:/var/www/certbot \
  certbot/certbot certonly --webroot \
    -w /var/www/certbot \
    -d hvht.net \
    -d www.hvht.net \
    --email YOUR_EMAIL@example.com \
    --agree-tos \
    --non-interactive

# Verify cert was created
docker run --rm -v hivis_server_certbot-conf:/etc/letsencrypt \
  alpine ls /etc/letsencrypt/live/hvht.net/
# Expected: fullchain.pem  privkey.pem  cert.pem  chain.pem
```

> **Note:** Docker volume names include the compose project prefix.
> If your prefix is different (check with `docker volume ls | grep certbot`), adjust accordingly.

---

## Step 6 — Switch to Production Nginx Config

```bash
cd /opt/hivis/server/nginx

# Enable the production config
mv hivis.conf.disabled hivis.conf

# Remove the bootstrap config
# (or keep it disabled — Nginx ignores .disabled extension)
mv http-only.conf http-only.conf.disabled

# Reload Nginx (no downtime)
docker compose exec nginx nginx -s reload

# If reload fails, check the config is valid first:
docker compose exec nginx nginx -t
```

---

## Step 7 — Verify Full Deployment

```bash
# HTTP redirects to HTTPS
curl -I http://hvht.net
# Expected: 301 to https://

# HTTPS loads the dashboard
curl -I https://hvht.net
# Expected: 200 OK, Content-Type: text/html

# SSL certificate dates
echo | openssl s_client -servername hvht.net -connect hvht.net:443 2>/dev/null \
  | openssl x509 -noout -dates

# InfluxDB proxy works (should return CSV data)
curl -s -o /dev/null -w "%{http_code}" \
  -X POST "https://hvht.net/api/v2/query?org=hivis" \
  -H "Authorization: Token uWjv9PW_WMP83kXETwxu9qjp2dqCp1mQRhtywSRzAhKYshYcER1Pb4WzvMg42-WSLORkNr3gTA4xVaqrZww0Ww==" \
  -H "Content-Type: application/vnd.flux" \
  -d 'from(bucket:"hivis") |> range(start:-1m) |> limit(n:1)'
# Expected: 200
```

Open https://hvht.net in a browser — device cards should appear within 10 seconds if sensors are online.

---

## Ongoing Maintenance

```bash
# Check container status
docker compose -f /opt/hivis/server/docker-compose.yml ps

# View Nginx logs
docker compose -f /opt/hivis/server/docker-compose.yml logs -f nginx

# Check cert renewal (runs automatically every 12h)
docker compose -f /opt/hivis/server/docker-compose.yml logs certbot

# Restart Nginx only
docker compose -f /opt/hivis/server/docker-compose.yml restart nginx

# Reload Nginx config without restart (for config changes)
docker compose -f /opt/hivis/server/docker-compose.yml exec nginx nginx -s reload
```

---

## Updating the Dashboard

Edit `server/website/index.html` in the repo, push/copy to X200 — Nginx serves it
immediately with no restart needed (the volume is mounted read-only, Nginx reads on request).

```bash
# After updating index.html on X200:
# No action needed — browser refresh picks it up instantly.
```

---

## Troubleshooting

### Website not loading

```bash
# Check Nginx error log
docker compose logs nginx | tail -50

# Is port 80/443 open?
sudo ufw status
sudo ufw allow 80/tcp
sudo ufw allow 443/tcp

# DNS pointing here?
curl -s ifconfig.me        # public IP
nslookup hvht.net           # should match
```

### InfluxDB proxy returns 502

```bash
# Is InfluxDB healthy?
curl http://localhost:8086/health

# Can Nginx reach InfluxDB by container name?
docker compose exec nginx wget -qO- http://influxdb:8086/health
```

### SSL certificate issues

```bash
# Check cert validity
docker run --rm -v hivis_server_certbot-conf:/etc/letsencrypt \
  alpine ls -la /etc/letsencrypt/live/hvht.net/

# Force manual renewal
docker run -it --rm \
  -v hivis_server_certbot-conf:/etc/letsencrypt \
  -v hivis_server_certbot-www:/var/www/certbot \
  certbot/certbot renew --webroot -w /var/www/certbot --force-renewal
```

### Dashboard shows "Connection error"

1. Open browser DevTools → Network tab
2. Look for the failed `POST /api/v2/query` request
3. Common causes:
   - InfluxDB not running → `docker compose ps`
   - Wrong InfluxDB token → update `INFLUX_TOKEN` in `server/website/index.html`
   - No sensor data in last 15 min → check Mosquitto + Node-RED

---

## Showcase Checklist (April 15)

- [ ] `docker compose ps` — all 7 containers running (mosquitto, nodered, influxdb, grafana, ota, nginx, certbot)
- [ ] `curl -I https://hvht.net` — 200 OK
- [ ] Browser: device cards populate with live readings
- [ ] Browser: click a card → charts render (1h, 6h, 24h ranges work)
- [ ] Dashboard auto-refreshes every 10 seconds
- [ ] SSL padlock visible in browser
- [ ] Test on phone hotspot (external network) before event

---

## Backup

```bash
# Website files
tar -czf ~/hivis-website-$(date +%Y%m%d).tar.gz /opt/hivis/server/website/

# SSL certificates
docker run --rm \
  -v hivis_server_certbot-conf:/etc/letsencrypt \
  -v ~/backups:/backup \
  alpine tar -czf /backup/hivis-certs-$(date +%Y%m%d).tar.gz /etc/letsencrypt
```
