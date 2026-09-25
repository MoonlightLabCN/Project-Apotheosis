#!/usr/bin/env python3
"""
Apotheosis web-platform test server (0.2.0).

Python standard library only, on purpose: this has to be runnable on the build box
with zero setup, and the thing under test is a phone browser, not this server.

    python tests/web-platform/server.py            # binds 0.0.0.0:8099
    python tests/web-platform/server.py --port 9000

Then on the Lumia, open   http://<build-host-ip>:8099/
The landing page runs every check in-page and prints a big PASS/FAIL, because the
device browser has no console and no devtools — if a result is not on screen, it
does not exist.

Endpoints (all under /api/):
  cookie/set?name=&value=&...   Set-Cookie with the requested attributes
  cookie/echo                   JSON of the Cookie header the browser sent
  cookie/set-then-echo          Set-Cookie on a 302, echo on the target
  redirect/301?to=              301 to `to`
  redirect/302?to=              302 to `to`
  redirect/chain?n=3            n hops, then lands on redirect/done
  redirect/done                 terminal page, reports its own URL
  upload                        multipart/form-data receiver, echoes what arrived
  slow?ms=2000                  response delayed by `ms`
  abrupt                        headers, partial body, then closes the connection
  csp/script.js                 same-origin script that sets window.EXTERNAL_OK
  csp/echo-connect              tiny JSON, for connect-src checks

0.2.0 additions:
  /ws                           RFC 6455 endpoint on the SAME host/port as the
                                pages (text echo "echo:<msg>", binary echo
                                reversed, ping/pong, "!close"/"!ping"/"!cookie")
  ws-last                       what the most recent WS handshake carried
  frame/echo                    JSON a child frame can use to prove its identity
  frame/xfo-deny                X-Frame-Options: DENY
  frame/ancestors-none          CSP frame-ancestors 'none'

A second listener (--alt-port, default 8100) serves the same handler, giving a
different ORIGIN for SOP/postMessage checks. It is still the same SITE, so the
SameSite page reports SKIP unless you reach the two listeners under two different
hostnames and pass the second as ?alt=<host:port>.
"""

import argparse
import base64
import email.parser
import hashlib
import html
import json
import os
import struct
import sys
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
PAGES = os.path.join(HERE, "pages")

# Last WebSocket handshake seen, for /api/ws-last. Written from request threads.
WS_LOCK = threading.Lock()
WS_LAST = {"cookie": "", "origin": "", "protocol": "", "count": 0}

# Every request path we have served, for /api/served. See that route for why.
SERVED_LOCK = threading.Lock()
SERVED = {}


class Handler(BaseHTTPRequestHandler):
    server_version = "ApotheosisTest/0.2.0"
    protocol_version = "HTTP/1.1"

    # ---- plumbing --------------------------------------------------------

    def log_message(self, fmt, *args):
        sys.stderr.write("%s  %s\n" % (self.log_date_time_string(), fmt % args))

    def _send(self, code, body=b"", ctype="text/plain; charset=utf-8", headers=()):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        # No caching anywhere: a stale 200 would silently turn a real failure green.
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
        for name, value in headers:
            self.send_header(name, value)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _json(self, obj, headers=()):
        self._send(200, json.dumps(obj, ensure_ascii=False),
                   "application/json; charset=utf-8", headers)

    def _query(self):
        return urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)

    def _q1(self, name, default=""):
        return self._query().get(name, [default])[0]

    def _origin(self):
        host = self.headers.get("Host") or ("127.0.0.1:%d" % self.server.server_port)
        return "http://%s" % host

    def _cors(self):
        origin = self.headers.get("Origin")
        if not origin:
            return []
        return [("Access-Control-Allow-Origin", origin),
                ("Access-Control-Allow-Credentials", "true"),
                ("Vary", "Origin")]

    # ---- routing ---------------------------------------------------------

    def _note_served(self):
        with SERVED_LOCK:
            SERVED[self.path] = SERVED.get(self.path, 0) + 1

    def do_GET(self):
        self._note_served()
        path = urllib.parse.urlparse(self.path).path
        if path == "/ws":
            # RFC 6455 upgrade. Handled inline on this connection rather than on a
            # separate port so the WebSocket shares the page's host, which is what
            # makes the "cookies ride on the handshake" check meaningful.
            return self._websocket()
        if path == "/" or path == "/index.html":
            return self._serve_page("index.html")
        if path.startswith("/pages/"):
            return self._serve_page(path[len("/pages/"):])
        if path.startswith("/api/"):
            return self._api(path[len("/api/"):])
        self._send(404, "not found\n")

    def do_HEAD(self):
        self.do_GET()

    def do_POST(self):
        self._note_served()
        path = urllib.parse.urlparse(self.path).path
        if path == "/api/upload":
            return self._upload()
        if path.startswith("/api/"):
            # Drain the body so keep-alive stays in sync even for routes that
            # ignore it (the SameSite page POSTs to cookie/echo).
            length = int(self.headers.get("Content-Length", "0") or 0)
            if length:
                self.rfile.read(length)
            return self._api(path[len("/api/"):])
        self._send(404, "not found\n")

    def do_OPTIONS(self):
        # CORS preflight. Only the credentialed cross-site checks need it.
        headers = self._cors() + [
            ("Access-Control-Allow-Methods", "GET, POST, OPTIONS"),
            ("Access-Control-Allow-Headers",
             self.headers.get("Access-Control-Request-Headers", "Content-Type")),
            ("Access-Control-Max-Age", "0"),
        ]
        self._send(204, b"", "text/plain", headers)

    def _serve_page(self, name):
        # No traversal: only files that actually live in pages/.
        safe = os.path.normpath(os.path.join(PAGES, name))
        if not safe.startswith(PAGES) or not os.path.isfile(safe):
            return self._send(404, "no such page: %s\n" % name)

        with open(safe, "rb") as f:
            body = f.read()

        ctype = "text/html; charset=utf-8"
        extra = []
        if safe.endswith(".js"):
            ctype = "application/javascript; charset=utf-8"
        elif safe.endswith(".css"):
            ctype = "text/css; charset=utf-8"

        # CSP fixtures need their policy in a real response header (a <meta> policy
        # cannot express everything, and header-vs-meta is itself worth testing).
        policy_file = safe + ".csp"
        if os.path.isfile(policy_file):
            with open(policy_file, "r", encoding="utf-8") as f:
                policy = f.read().strip()
            if policy:
                extra.append(("Content-Security-Policy", policy))
        self._send(200, body, ctype, extra)

    # ---- API -------------------------------------------------------------

    def _api(self, route):
        q = self._query

        if route == "cookie/set":
            name = self._q1("name", "t")
            value = self._q1("value", "1")
            parts = ["%s=%s" % (name, value)]
            path = self._q1("path", "/")
            parts.append("Path=%s" % path)
            if self._q1("domain"):
                parts.append("Domain=%s" % self._q1("domain"))
            if self._q1("maxage"):
                parts.append("Max-Age=%s" % self._q1("maxage"))
            if self._q1("httponly") == "1":
                parts.append("HttpOnly")
            if self._q1("secure") == "1":
                parts.append("Secure")
            if self._q1("samesite"):
                parts.append("SameSite=%s" % self._q1("samesite"))
            return self._json({"setCookie": "; ".join(parts)},
                              headers=[("Set-Cookie", "; ".join(parts))])

        if route == "cookie/echo":
            # Credentialed CORS so the SameSite page can read the result of a real
            # cross-site request. Echoing the caller's Origin (rather than "*") is
            # required whenever Allow-Credentials is true.
            return self._json({"cookie": self.headers.get("Cookie", "")},
                              headers=self._cors())

        if route == "cookie/set-then-echo":
            # The classic login shape: the session cookie rides on the 302 itself,
            # so a client that only reads Set-Cookie off the final 200 loses it.
            target = "%s/api/cookie/echo" % self._origin()
            self.send_response(302)
            self.send_header("Location", target)
            self.send_header("Set-Cookie", "redirect_session=ok; Path=/")
            self.send_header("Content-Length", "0")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            return

        if route in ("redirect/301", "redirect/302"):
            code = 301 if route.endswith("301") else 302
            target = self._q1("to") or ("%s/api/redirect/done" % self._origin())
            self.send_response(code)
            self.send_header("Location", target)
            self.send_header("Content-Length", "0")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            return

        if route == "redirect/chain":
            try:
                n = int(self._q1("n", "3"))
            except ValueError:
                n = 3
            if n <= 0:
                target = "%s/api/redirect/done" % self._origin()
            else:
                target = "%s/api/redirect/chain?n=%d" % (self._origin(), n - 1)
            self.send_response(302)
            self.send_header("Location", target)
            self.send_header("Content-Length", "0")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            return

        if route == "redirect/done":
            return self._send(200, DONE_PAGE, "text/html; charset=utf-8")

        if route == "slow":
            try:
                ms = min(int(self._q1("ms", "2000")), 30000)
            except ValueError:
                ms = 2000
            time.sleep(ms / 1000.0)
            return self._json({"sleptMs": ms})

        if route == "abrupt":
            # Headers promise more body than we send, then the socket drops. Exercises
            # the loader's "connection closed mid-response" path.
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", "1000")
            self.end_headers()
            self.wfile.write(b"partial")
            self.close_connection = True
            return

        # ---- 0.2.0: cross-frame / storage / socket fixtures -----------------

        if route == "served":
            # How many times a path containing `q` has been requested. Lets a page
            # tell "the frame loaded but stayed silent" (an engine bug) apart from
            # "the frame was never fetched at all" (the embedder refused to load
            # it) — those look identical from the parent, and only one of them is
            # this browser's fault.
            needle = self._q1("q")
            with SERVED_LOCK:
                n = sum(count for path, count in SERVED.items() if needle and needle in path)
            return self._json({"q": needle, "count": n})

        if route == "ws-last":
            # What the most recent WebSocket handshake carried. The page checks
            # `cookie` here rather than over the socket so a failure to *send*
            # cookies on the Upgrade is distinguishable from a failure to echo.
            with WS_LOCK:
                return self._json(dict(WS_LAST))

        if route == "frame/echo":
            # Everything a child frame needs to prove who it thinks it is, and what
            # the network layer sent on its behalf.
            return self._json({
                "url": self.path,
                "host": self.headers.get("Host", ""),
                "cookie": self.headers.get("Cookie", ""),
                "referer": self.headers.get("Referer", ""),
                "secFetchDest": self.headers.get("Sec-Fetch-Dest", ""),
            })

        if route == "frame/xfo-deny":
            # X-Frame-Options must keep this out of any frame at all.
            return self._send(200, "<!doctype html><title>xfo</title><h1>XFO DENY BODY</h1>",
                              "text/html; charset=utf-8",
                              [("X-Frame-Options", "DENY")])

        if route == "frame/ancestors-none":
            # frame-ancestors is the CSP spelling of the same thing.
            return self._send(200, "<!doctype html><title>fa</title><h1>FRAME-ANCESTORS BODY</h1>",
                              "text/html; charset=utf-8",
                              [("Content-Security-Policy", "frame-ancestors 'none'")])

        if route == "csp/script.js":
            return self._send(200, "window.EXTERNAL_OK = true;\n",
                              "application/javascript; charset=utf-8")

        if route == "csp/echo-connect":
            return self._json({"connect": "ok"})

        self._send(404, "no such api: %s\n" % route)

    # ---- WebSocket (RFC 6455) -------------------------------------------
    #
    # Stdlib only, same as the rest of this file. Just enough of the protocol to
    # tell a working client from a broken one: handshake, text and binary echo,
    # ping/pong, and a clean close handshake. No extensions are negotiated — the
    # engine must cope with a server that declines permessage-deflate.

    WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

    def _websocket(self):
        key = self.headers.get("Sec-WebSocket-Key")
        upgrade = (self.headers.get("Upgrade") or "").lower()
        if not key or upgrade != "websocket":
            return self._send(400, "not a websocket handshake\n")

        accept = base64.b64encode(
            hashlib.sha1((key + self.WS_GUID).encode("ascii")).digest()).decode("ascii")

        # Record what the handshake carried; /api/ws-last reports it so the page can
        # assert that cookies really rode along on the Upgrade request.
        with WS_LOCK:
            WS_LAST["cookie"] = self.headers.get("Cookie", "")
            WS_LAST["origin"] = self.headers.get("Origin", "")
            WS_LAST["protocol"] = self.headers.get("Sec-WebSocket-Protocol", "")
            WS_LAST["count"] = WS_LAST.get("count", 0) + 1

        self.close_connection = True   # we own the socket from here on
        self.wfile.write(
            b"HTTP/1.1 101 Switching Protocols\r\n"
            b"Upgrade: websocket\r\n"
            b"Connection: Upgrade\r\n"
            b"Sec-WebSocket-Accept: " + accept.encode("ascii") + b"\r\n\r\n")
        self.wfile.flush()

        try:
            self._ws_loop()
        except (OSError, ConnectionError):
            pass

    def _ws_read_exact(self, n):
        data = b""
        while len(data) < n:
            chunk = self.rfile.read(n - len(data))
            if not chunk:
                raise ConnectionError("peer closed")
            data += chunk
        return data

    def _ws_send(self, opcode, payload=b""):
        header = bytes([0x80 | opcode])
        length = len(payload)
        if length < 126:
            header += bytes([length])
        elif length < 65536:
            header += bytes([126]) + struct.pack(">H", length)
        else:
            header += bytes([127]) + struct.pack(">Q", length)
        self.wfile.write(header + payload)
        self.wfile.flush()

    def _ws_loop(self):
        while True:
            b0, b1 = self._ws_read_exact(2)
            opcode = b0 & 0x0F
            masked = bool(b1 & 0x80)
            length = b1 & 0x7F
            if length == 126:
                length = struct.unpack(">H", self._ws_read_exact(2))[0]
            elif length == 127:
                length = struct.unpack(">Q", self._ws_read_exact(8))[0]
            if length > 8 * 1024 * 1024:
                return self._ws_send(0x8, struct.pack(">H", 1009) + b"too big")

            mask = self._ws_read_exact(4) if masked else b""
            payload = self._ws_read_exact(length) if length else b""
            if masked:
                payload = bytes(byte ^ mask[i % 4] for i, byte in enumerate(payload))

            if opcode == 0x8:                       # close
                code = struct.unpack(">H", payload[:2])[0] if len(payload) >= 2 else 1000
                self._ws_send(0x8, struct.pack(">H", code))
                return
            if opcode == 0x9:                       # ping -> pong
                self._ws_send(0xA, payload)
                continue
            if opcode == 0xA:                       # pong, ignore
                continue

            if opcode == 0x1:                       # text
                text = payload.decode("utf-8", "replace")
                # A couple of commands so the page can drive server-initiated cases.
                if text == "!close":
                    self._ws_send(0x8, struct.pack(">H", 1000) + b"server close")
                    return
                if text == "!ping":
                    self._ws_send(0x9, b"srv")
                    continue
                if text == "!cookie":
                    with WS_LOCK:
                        self._ws_send(0x1, ("cookie:" + WS_LAST.get("cookie", "")).encode("utf-8"))
                    continue
                self._ws_send(0x1, ("echo:" + text).encode("utf-8"))
                continue
            if opcode == 0x2:                       # binary: echo bytes reversed
                self._ws_send(0x2, payload[::-1])
                continue
            # Unknown opcode: protocol error.
            self._ws_send(0x8, struct.pack(">H", 1002))
            return

    def _upload(self):
        ctype = self.headers.get("Content-Type", "")
        length = int(self.headers.get("Content-Length", "0") or 0)
        body = self.rfile.read(length) if length else b""

        if "multipart/form-data" not in ctype.lower():
            return self._json({"ok": False, "error": "not multipart", "contentType": ctype})

        # email.parser over a synthesized MIME document: no cgi module (removed in
        # Python 3.13) and no third-party dependency.
        raw = b"Content-Type: " + ctype.encode("latin-1") + b"\r\nMIME-Version: 1.0\r\n\r\n" + body
        message = email.parser.BytesParser().parsebytes(raw)

        files, fields = [], {}
        for part in message.walk():
            if part.is_multipart():
                continue
            disposition = part.get("Content-Disposition", "")
            if "form-data" not in disposition:
                continue
            name = part.get_param("name", header="content-disposition")
            filename = part.get_param("filename", header="content-disposition")
            payload = part.get_payload(decode=True) or b""
            if filename:
                files.append({
                    "field": name,
                    "filename": filename,
                    "contentType": part.get_content_type(),
                    "bytes": len(payload),
                    # First bytes as hex so a binary upload is still verifiable by eye.
                    "head": payload[:16].hex(),
                })
            else:
                fields[name or ""] = payload.decode("utf-8", "replace")

        return self._json({"ok": bool(files), "files": files, "fields": fields})


DONE_PAGE = """<!doctype html><meta charset="utf-8">
<title>redirect done</title>
<body style="font:16px sans-serif;padding:16px">
<h1 id="ok" style="color:#0a0">REDIRECT LANDED</h1>
<p>Final URL as this document sees it:</p>
<pre id="u" style="white-space:pre-wrap;word-break:break-all"></pre>
<script>document.getElementById('u').textContent = location.href;</script>
</body>"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8099)
    ap.add_argument("--alt-port", type=int, default=8100,
                    help="second listener, used as the 'other origin' for iframe/"
                         "postMessage/SameSite checks (0 disables it)")
    ap.add_argument("--host", default="0.0.0.0")
    args = ap.parse_args()

    servers = [ThreadingHTTPServer((args.host, args.port), Handler)]
    print("Apotheosis web-platform tests on http://%s:%d/" % (args.host, args.port))
    if args.alt_port:
        servers.append(ThreadingHTTPServer((args.host, args.alt_port), Handler))
        print("second origin (same code, different port) on :%d" % args.alt_port)
    print("On the device, use the build host's LAN IP, not localhost.")
    print("")
    print("NOTE on cross-SITE vs cross-ORIGIN: a second port gives a different")
    print("ORIGIN but the same SITE, which is enough for SOP/postMessage but not")
    print("for SameSite (that is site-based). For real cross-site, reach the two")
    print("listeners under two different hostnames and pass the second one to the")
    print("index page as ?alt=<host:port> — e.g. localhost:8099 vs 127.0.0.1:8100")
    print("on the build box. The SameSite page reports SKIP when it detects that")
    print("the alternate origin is merely same-site.")

    threads = []
    for server in servers[1:]:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        threads.append(thread)
    try:
        servers[0].serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")


if __name__ == "__main__":
    main()
