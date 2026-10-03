"""Gera as cenas de teste a partir da imagem de amostra da biblioteca Human."""
import os, sys
from PIL import Image

human = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "node_modules/@vladmandic/human")
out = os.path.dirname(os.path.abspath(__file__))
im = Image.open(os.path.join(human, "assets/samples.jpg")).convert("RGB")
im.crop((326, 0, 536, 157)).resize((640, 480), Image.LANCZOS).save(os.path.join(out, "sceneA.jpg"), quality=85)  # pessoa cadastrada
w = im.crop((166, 0, 316, 157)); bg = Image.new("RGB", (210, 157), (20, 20, 20)); bg.paste(w, (30, 0))
bg.resize((640, 480), Image.LANCZOS).save(os.path.join(out, "sceneB.jpg"), quality=85)  # outra pessoa
Image.new("RGB", (640, 480), (60, 60, 60)).save(os.path.join(out, "sceneEmpty.jpg"))  # sem rosto
print("cenas geradas")
