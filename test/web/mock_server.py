"""Simulador da API do ESP32-CAM (portas 80 e 81) para testar a interface web.

Uso: sudo python mock_server.py <pasta web> <pasta @vladmandic/human>
"""
import json, os, sys, threading, time, mimetypes
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

WEB = sys.argv[1]; HUMAN = sys.argv[2]; HERE = os.path.dirname(os.path.abspath(__file__))
PASSWORD = "senha-teste-123"
state = {"scene": "sceneA.jpg", "known": 0, "faces": None, "settings": {
    "cam": {"framesize": 8, "quality": 12, "brightness": 0, "contrast": 0, "saturation": 0, "aeLevel": 0,
            "vflip": False, "hmirror": False, "awb": True},
    "motion": {"enabled": True, "capture": True, "pixelThreshold": 22, "minAreaPercent": 1.0, "confirmFrames": 2, "cooldownS": 5},
    "ledBrightness": 25, "knownHoldMs": 5000}, "events": [], "settings_posts": 0}
TOKEN = "a" * 32

def frame():
    with open(os.path.join(HERE, state["scene"]), "rb") as f: return f.read()

class Api(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def authed(self): return f"sid={TOKEN}" in (self.headers.get("Cookie") or "")
    def send(self, code, body=b"", ctype="application/json", extra=None):
        if isinstance(body, (dict, list)): body = json.dumps(body).encode()
        if isinstance(body, str): body = body.encode()
        self.send_response(code); self.send_header("Content-Type", ctype); self.send_header("Content-Length", str(len(body)))
        for k, v in (extra or {}).items(): self.send_header(k, v)
        self.end_headers(); self.wfile.write(body)
    def body(self):
        n = int(self.headers.get("Content-Length") or 0); return self.rfile.read(n)
    def do_GET(self):
        u = urlparse(self.path); p = u.path
        if p == "/": return self.static("index.html" if self.authed() else "login.html")
        if p.startswith("/human/"):
            fp = os.path.join(HUMAN, p[len("/human/"):])
            with open(fp, "rb") as f: data = f.read()
            ct = "application/javascript" if fp.endswith(".js") else "application/json" if fp.endswith(".json") else "application/octet-stream"
            return self.send(200, data, ct, {"Access-Control-Allow-Origin": "*"})
        if p.startswith("/mock/scene"):
            state["scene"] = parse_qs(u.query)["s"][0]; return self.send(200, {"ok": True})
        if p == "/mock/state": return self.send(200, {"known": state["known"], "faces": state["faces"], "settings": state["settings"], "posts": state["settings_posts"]})
        if p.startswith("/api/") and not self.authed(): return self.send(401, {"error": "nao autorizado"})
        if p == "/api/status":
            return self.send(200, {"fw": "2.0.0-mock", "sensor": "OV2640", "uptime": 3725, "time": int(time.time()), "fps": 14.8, "rssi": -58,
                "ip": "127.0.0.1", "heap": 180000, "psram": 7000000, "temp": 41.5, "clients": 1, "sd": {"ok": False, "used": 0, "total": 0},
                "motion": {"enabled": True, "capture": True, "active": True, "light": False, "percent": 4.2, "events": 3, "box": [0.2, 0.1, 0.6, 0.7]},
                "lastEvent": len(state["events"])})
        if p == "/api/events": return self.send(200, [{"id": i + 1, "time": int(time.time()), "percent": 4.2, "manual": m} for i, m in reversed(list(enumerate(state["events"])))])
        if p in ("/api/snapshot", "/api/frame.jpg"): return self.send(200, frame(), "image/jpeg")
        if p == "/api/settings": return self.send(200, state["settings"])
        if p == "/api/faces": return self.send(200, state["faces"] or {"v": 2, "faces": []})
        name = p.lstrip("/")
        if os.path.exists(os.path.join(WEB, name)) and name != "index.html": return self.static(name)
        self.send(404, "nao encontrado", "text/plain")
    def static(self, name):
        with open(os.path.join(WEB, name), "rb") as f: data = f.read()
        self.send(200, data, mimetypes.guess_type(name)[0] or "application/octet-stream")
    def do_POST(self):
        p = urlparse(self.path).path; b = self.body()
        if p == "/api/login":
            pw = json.loads(b).get("password")
            if pw != PASSWORD: return self.send(401, {"error": "senha incorreta"})
            return self.send(200, {"ok": True}, extra={"Set-Cookie": f"sid={TOKEN}; Path=/; HttpOnly; SameSite=Strict"})
        if not self.authed(): return self.send(401, {"error": "nao autorizado"})
        if p == "/api/known": state["known"] += 1; return self.send(200, {"ok": True})
        if p == "/api/faces": state["faces"] = json.loads(b); return self.send(200, {"ok": True})
        if p == "/api/snapshot": state["events"].append(True); return self.send(200, {"id": len(state["events"])})
        if p == "/api/settings":
            d = json.loads(b); state["settings_posts"] += 1
            for k, v in d.items():
                if isinstance(v, dict): state["settings"][k].update(v)
                else: state["settings"][k] = v
            return self.send(200, state["settings"])
        if p == "/api/logout": return self.send(200, {"ok": True}, extra={"Set-Cookie": "sid=; Path=/; Max-Age=0"})
        self.send(404, {"error": "?"})

class Stream(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_GET(self):
        if f"sid={TOKEN}" not in (self.headers.get("Cookie") or ""):
            self.send_response(401); self.end_headers(); return
        self.send_response(200)
        self.send_header("Content-Type", "multipart/x-mixed-replace;boundary=f")
        o = self.headers.get("Origin")
        if o: self.send_header("Access-Control-Allow-Origin", o); self.send_header("Access-Control-Allow-Credentials", "true")
        self.end_headers()
        try:
            while True:
                j = frame()
                self.wfile.write(b"--f\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n" % len(j) + j + b"\r\n")
                time.sleep(0.08)
        except Exception: pass

for port, h in ((80, Api), (81, Stream)):
    s = ThreadingHTTPServer(("127.0.0.1", port), h); s.daemon_threads = True
    threading.Thread(target=s.serve_forever, daemon=True).start()
print("mock pronto", flush=True)
while True: time.sleep(3600)
