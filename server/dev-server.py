#!/usr/bin/env python3
"""
HIVIS local dev server — serves the website on http://localhost:8080
and proxies /api/v2/query to InfluxDB on the X200 (172.16.1.156:8086).

Run from the server/ directory:
    python dev-server.py
"""

import http.server
import urllib.request
import urllib.error
import os
import sys

PORT        = 8080
WEBSITE_DIR = os.path.join(os.path.dirname(__file__), "website")
INFLUX_HOST  = "172.16.1.156"
INFLUX_PORT  = 8086
INFLUX_TOKEN = os.environ.get("INFLUX_TOKEN", "")


class HIVISHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=WEBSITE_DIR, **kwargs)

    def do_POST(self):
        if self.path.startswith("/api/v2/query"):
            self._proxy_influx()
        else:
            self.send_error(404, "Not Found")

    def do_OPTIONS(self):
        # CORS preflight
        self.send_response(204)
        self._cors_headers()
        self.end_headers()

    def _proxy_influx(self):
        length   = int(self.headers.get("Content-Length", 0))
        body     = self.rfile.read(length) if length else b""
        ct       = self.headers.get("Content-Type", "application/vnd.flux")
        accept   = self.headers.get("Accept", "application/csv")
        qs       = "?" + self.path.split("?", 1)[1] if "?" in self.path else ""
        url      = f"http://{INFLUX_HOST}:{INFLUX_PORT}/api/v2/query{qs}"

        req = urllib.request.Request(url, data=body, method="POST")
        req.add_header("Authorization", f"Token {INFLUX_TOKEN}")
        req.add_header("Content-Type", ct)
        req.add_header("Accept", accept)

        try:
            with urllib.request.urlopen(req, timeout=30) as resp:
                data = resp.read()
                self.send_response(resp.status)
                self._cors_headers()
                self.send_header("Content-Type", resp.headers.get("Content-Type", "text/csv"))
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)
        except urllib.error.HTTPError as e:
            data = e.read()
            self.send_response(e.code)
            self._cors_headers()
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        except Exception as e:
            msg = f"Proxy error: {e}".encode()
            self.send_response(502)
            self._cors_headers()
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(msg)))
            self.end_headers()
            self.wfile.write(msg)

    def _cors_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, Accept")
        self.send_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS")

    def log_message(self, fmt, *args):
        # Quieter logging
        if "200" in str(args) or "POST" in str(args):
            print(f"  {self.address_string()} {fmt % args}")


if __name__ == "__main__":
    if not os.path.isdir(WEBSITE_DIR):
        print(f"ERROR: website directory not found: {WEBSITE_DIR}")
        sys.exit(1)

    if not INFLUX_TOKEN:
        print("  WARNING: INFLUX_TOKEN env var not set — InfluxDB queries will fail.")
        print("  Run:  INFLUX_TOKEN=<token> python dev-server.py\n")
    print(f"\n  HIVIS dev server")
    print(f"  Website : {WEBSITE_DIR}")
    print(f"  InfluxDB: http://{INFLUX_HOST}:{INFLUX_PORT}")
    print(f"\n  Open: http://localhost:{PORT}")
    print(f"  Press Ctrl+C to stop.\n")

    server = http.server.ThreadingHTTPServer(("", PORT), HIVISHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n  Stopped.")
