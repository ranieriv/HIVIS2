#!/usr/bin/env python3
"""
HIVIS OTA Server — serves firmware updates to approved devices.
Listens on port 8090.

Endpoints:
  GET /ota/version?mac=[mac]   — returns version string (200) or 304 if not approved
  GET /ota/firmware?mac=[mac]  — serves latest.bin if approved, else 403
"""

import json
import os
import sys
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlparse

import docker as docker_sdk

PORT             = 8090
BASE_DIR         = os.path.dirname(os.path.abspath(__file__))
PROVISION_SECRET = os.environ.get("PROVISION_SECRET", "")
DEVICES_FILE  = os.path.join(BASE_DIR, "devices.json")
FIRMWARE_FILE = os.path.join(BASE_DIR, "firmware", "latest.bin")
VERSION_FILE  = os.path.join(BASE_DIR, "firmware", "version.txt")
ACL_FILE      = "/mosquitto/config/acl"
PASSWD_CONTAINER = "hivis-mosquitto"


def log(mac: str, method: str, path: str, status: int, note: str = ""):
    ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    extra = f"  [{note}]" if note else ""
    print(f"{ts}  {method} {path}  mac={mac or '-'}  → {status}{extra}", flush=True)


def read_devices() -> dict:
    try:
        with open(DEVICES_FILE) as f:
            return json.load(f).get("devices", {})
    except FileNotFoundError:
        return {}
    except json.JSONDecodeError as e:
        print(f"ERROR: devices.json parse error: {e}", flush=True)
        return {}


def read_version() -> str | None:
    try:
        with open(VERSION_FILE) as f:
            return f.read().strip()
    except FileNotFoundError:
        return None


def is_approved(mac: str) -> bool:
    devices = read_devices()
    entry = devices.get(mac, {})
    return entry.get("ota_approved", False)


def provision_mqtt_user(user: str, password: str):
    """Add/update device user in Mosquitto passwd + ACL, then reload."""
    mac = user.replace("device_", "")          # e.g. 4cc382c32764
    device_id = "hivis-" + mac

    client = docker_sdk.from_env()
    container = client.containers.get(PASSWD_CONTAINER)

    # Update passwd file (mosquitto_passwd handles create-or-update)
    exit_code, output = container.exec_run(
        ["mosquitto_passwd", "-b", "/mosquitto/config/passwd", user, password]
    )
    if exit_code != 0:
        raise RuntimeError(f"mosquitto_passwd failed: {output.decode()}")

    # Append ACL entry only if not already present
    try:
        with open(ACL_FILE) as f:
            acl_content = f.read()
    except FileNotFoundError:
        acl_content = ""

    if f"user {user}" not in acl_content:
        entry = (
            f"\n# auto-provisioned\n"
            f"user {user}\n"
            f"topic write hivis/{device_id}/#\n"
            f"topic write hivis/register\n"
            f"topic read  hivis/provision/{mac}\n"
        )
        with open(ACL_FILE, "a") as f:
            f.write(entry)

    # Reload Mosquitto config (SIGHUP — no restart needed)
    container.kill(signal="SIGHUP")
    print(f"Provisioned {user} → {device_id}", flush=True)


class OTAHandler(BaseHTTPRequestHandler):

    def do_POST(self):
        parsed = urlparse(self.path)
        if parsed.path == "/provision":
            self._handle_provision()
        else:
            self.send_response(404)
            self.end_headers()

    def _handle_provision(self):
        if not PROVISION_SECRET or self.headers.get("X-Provision-Token") != PROVISION_SECRET:
            self.send_response(401)
            self.end_headers()
            log("", "POST", "/provision", 401, "missing or invalid X-Provision-Token")
            return

        try:
            length = int(self.headers.get("Content-Length", 0))
            body = json.loads(self.rfile.read(length))
            user = body["user"]
            password = body["password"]
        except Exception as e:
            self.send_response(400)
            self.end_headers()
            log("", "POST", "/provision", 400, str(e))
            return

        try:
            provision_mqtt_user(user, password)
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b"OK")
            log("", "POST", "/provision", 200, f"user={user}")
        except Exception as e:
            self.send_response(500)
            self.end_headers()
            self.wfile.write(str(e).encode())
            log("", "POST", "/provision", 500, str(e))

    def do_GET(self):
        parsed = urlparse(self.path)
        params = parse_qs(parsed.query)

        mac = self._get_mac(params)

        if parsed.path == "/ota/version":
            self._handle_version(mac)
        elif parsed.path == "/ota/firmware":
            self._handle_firmware(mac)
        else:
            self.send_response(404)
            self.end_headers()
            log(mac, "GET", self.path, 404)

    def _get_mac(self, params: dict) -> str:
        """MAC from x-ESP32-STA-MAC header first, then ?mac= query param."""
        mac = self.headers.get("x-ESP32-STA-MAC", "")
        if not mac:
            mac = params.get("mac", [""])[0]
        return mac.lower().replace(":", "").strip()

    def _handle_version(self, mac: str):
        if not is_approved(mac):
            self.send_response(304)
            self.end_headers()
            log(mac, "GET", self.path, 304, "not approved")
            return

        version = read_version()
        if not version:
            self.send_response(503)
            self.end_headers()
            log(mac, "GET", self.path, 503, "version.txt missing")
            return

        body = version.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        log(mac, "GET", self.path, 200, f"version={version}")

    def _handle_firmware(self, mac: str):
        if not is_approved(mac):
            self.send_response(403)
            self.end_headers()
            log(mac, "GET", self.path, 403, "not approved")
            return

        if not os.path.exists(FIRMWARE_FILE):
            self.send_response(404)
            self.end_headers()
            log(mac, "GET", self.path, 404, "latest.bin missing")
            return

        size = os.path.getsize(FIRMWARE_FILE)
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(size))
        self.send_header("Content-Disposition", "attachment; filename=firmware.bin")
        self.end_headers()

        with open(FIRMWARE_FILE, "rb") as f:
            while chunk := f.read(65536):
                self.wfile.write(chunk)

        log(mac, "GET", self.path, 200, f"served {size} bytes")

    def log_message(self, fmt, *args):
        # Suppress default BaseHTTPRequestHandler logging — we handle it ourselves
        pass


if __name__ == "__main__":
    os.makedirs(os.path.join(BASE_DIR, "firmware"), exist_ok=True)

    if not os.path.exists(DEVICES_FILE):
        with open(DEVICES_FILE, "w") as f:
            json.dump({"devices": {}}, f, indent=2)
        print(f"Created empty {DEVICES_FILE}", flush=True)

    print(f"HIVIS OTA server starting on port {PORT}", flush=True)
    print(f"  devices.json : {DEVICES_FILE}", flush=True)
    print(f"  firmware     : {FIRMWARE_FILE}", flush=True)
    print(f"  version.txt  : {VERSION_FILE}", flush=True)

    try:
        httpd = HTTPServer(("", PORT), OTAHandler)
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("Shutting down.", flush=True)
        sys.exit(0)
