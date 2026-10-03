"""Reconhecimento facial para o stream da ESP32-S3-CAM.

Lê o MJPEG (porta 81), detecta rostos (YuNet), identifica (SFace) e escreve o nome na imagem.
Uso:  python recognize.py --host 192.168.0.50 --password SUA_SENHA [--serve]
"""
import argparse, re, sys, threading, time, urllib.request
from pathlib import Path

import cv2
import numpy as np
import requests

HERE = Path(__file__).parent
MODELS = {
    "face_detection_yunet_2023mar.onnx":
        "https://github.com/opencv/opencv_zoo/raw/main/models/face_detection_yunet/face_detection_yunet_2023mar.onnx",
    "face_recognition_sface_2021dec.onnx":
        "https://github.com/opencv/opencv_zoo/raw/main/models/face_recognition_sface/face_recognition_sface_2021dec.onnx",
}
COSINE_THRESHOLD = 0.363  # valor recomendado para o SFace


def ensure_models():
    d = HERE / "models"
    d.mkdir(exist_ok=True)
    for name, url in MODELS.items():
        p = d / name
        if not p.exists():
            print(f"Baixando {name} ...")
            urllib.request.urlretrieve(url, p)
    return [str(d / n) for n in MODELS]


class Recognizer:
    def __init__(self):
        det, rec = ensure_models()
        self.det = cv2.FaceDetectorYN.create(det, "", (320, 320), 0.8, 0.3, 5000)
        self.rec = cv2.FaceRecognizerSF.create(rec, "")
        self.names, self.feats = [], []

    def enroll(self, folder):
        for f in sorted(Path(folder).glob("*")):
            if f.suffix.lower() not in (".jpg", ".jpeg", ".png"):
                continue
            img = cv2.imread(str(f))
            faces = self._detect(img)
            if faces is None:
                print(f"  sem rosto em {f.name}, ignorado")
                continue
            best = max(faces, key=lambda r: r[2] * r[3])
            self.feats.append(self.rec.feature(self.rec.alignCrop(img, best)))
            self.names.append(re.sub(r"[_-]?\d+$", "", f.stem))
        print(f"{len(self.names)} foto(s) cadastrada(s): {sorted(set(self.names))}")

    def _detect(self, img):
        h, w = img.shape[:2]
        self.det.setInputSize((w, h))
        _, faces = self.det.detect(img)
        return faces

    def annotate(self, img):
        faces = self._detect(img)
        for r in (faces if faces is not None else []):
            x, y, w, h = map(int, r[:4])
            name, score = "Desconhecido", 0.0
            if self.feats:
                f = self.rec.feature(self.rec.alignCrop(img, r))
                scores = [self.rec.match(f, k, cv2.FaceRecognizerSF_FR_COSINE) for k in self.feats]
                i = int(np.argmax(scores))
                score = scores[i]
                if score >= COSINE_THRESHOLD:
                    name = self.names[i]
            ok = name != "Desconhecido"
            color = (0, 200, 0) if ok else (0, 0, 255)
            cv2.rectangle(img, (x, y), (x + w, y + h), color, 2)
            label = f"{name} {score:.2f}" if self.feats else name
            cv2.rectangle(img, (x, y - 24), (x + 9 * len(label) + 6, y), color, -1)
            cv2.putText(img, label, (x + 3, y - 7), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 255, 255), 1, cv2.LINE_AA)
        return img


def frames(host, password):
    """Gera quadros BGR do stream MJPEG autenticado."""
    while True:
        try:
            s = requests.Session()
            r = s.post(f"http://{host}/api/login", json={"password": password}, timeout=8)
            if r.status_code == 429:
                wait = int(r.headers.get("Retry-After", "10"))
                print(f"Muitas tentativas; aguardando {wait} s")
                time.sleep(wait)
                continue
            if r.status_code != 200 or "sid" not in s.cookies:
                sys.exit("Senha incorreta.")
            resp = s.get(f"http://{host}:81/stream", stream=True, timeout=10)
            buf = b""
            for chunk in resp.iter_content(4096):
                buf += chunk
                a = buf.find(b"\xff\xd8")
                b = buf.find(b"\xff\xd9", a + 2) if a >= 0 else -1
                if a >= 0 and b >= 0:
                    jpg, buf = buf[a:b + 2], buf[b + 2:]
                    img = cv2.imdecode(np.frombuffer(jpg, np.uint8), cv2.IMREAD_COLOR)
                    if img is not None:
                        yield img
        except requests.RequestException as e:
            print("Conexao perdida, tentando de novo:", e)
            time.sleep(2)


latest = {"jpg": None}


def serve(port, password):
    from flask import Flask, Response, request
    app = Flask(__name__)

    def ok():
        a = request.authorization
        return a and a.password == password

    @app.route("/")
    def index():
        if not ok():
            return Response("Senha", 401, {"WWW-Authenticate": 'Basic realm="Reconhecimento"'})
        return '<body style="margin:0;background:#111"><img src="/video" style="width:100%;max-width:900px"></body>'

    @app.route("/video")
    def video():
        if not ok():
            return Response("Senha", 401, {"WWW-Authenticate": 'Basic realm="Reconhecimento"'})

        def gen():
            last = None
            while True:
                j = latest["jpg"]
                if j is None or j is last:
                    time.sleep(0.03)
                    continue
                last = j
                yield b"--f\r\nContent-Type: image/jpeg\r\n\r\n" + j + b"\r\n"
        return Response(gen(), mimetype="multipart/x-mixed-replace; boundary=f")

    threading.Thread(target=lambda: app.run(host="0.0.0.0", port=port, threaded=True), daemon=True).start()
    print(f"Video anotado em http://<ip-do-pc>:{port}/  (usuario qualquer, senha igual)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True, help="IP da ESP32-CAM")
    ap.add_argument("--password", required=True)
    ap.add_argument("--faces", default=str(HERE / "known_faces"))
    ap.add_argument("--serve", action="store_true", help="serve o video anotado via Flask")
    ap.add_argument("--port", type=int, default=5000)
    ap.add_argument("--no-window", action="store_true")
    a = ap.parse_args()

    rec = Recognizer()
    rec.enroll(a.faces)
    if a.serve:
        serve(a.port, a.password)
    for img in frames(a.host, a.password):
        img = rec.annotate(img)
        if a.serve:
            latest["jpg"] = cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, 80])[1].tobytes()
        if not a.no_window:
            cv2.imshow("Reconhecimento (q para sair)", img)
            if cv2.waitKey(1) & 0xFF == ord("q"):
                break


if __name__ == "__main__":
    main()
