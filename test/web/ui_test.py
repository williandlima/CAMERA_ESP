"""Teste ponta a ponta da interface (Playwright + simulador). Ver .github/workflows/ci.yml."""
import json, os, sys, time, urllib.request
from playwright.sync_api import sync_playwright

BASE = "http://127.0.0.1"
SHOTS = os.environ.get("SHOTS_DIR", os.path.dirname(os.path.abspath(__file__)))
def mock(path): return json.loads(urllib.request.urlopen(BASE + path).read())
errors = []
ok = lambda c, m: print(("PASS " if c else "FAIL ") + m) or (c or errors.append(m))

with sync_playwright() as p:
    b = p.chromium.launch(executable_path=os.environ.get("CHROMIUM_PATH") or None, args=["--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--ignore-gpu-blocklist"])
    page = b.new_page(viewport={"width": 1280, "height": 860})
    logs = []
    page.on("console", lambda m: logs.append(f"{m.type}: {m.text}"))
    page.on("pageerror", lambda e: logs.append(f"PAGEERROR: {e}"))

    page.goto(BASE + "/")
    ok(page.locator("#pw").is_visible(), "pagina de login exibida sem sessao")
    page.fill("#pw", "errada"); page.click("#btn")
    page.wait_for_selector("#err:not([hidden])")
    ok("incorreta" in page.inner_text("#err"), "senha errada mostra erro")
    page.fill("#pw", "senha-teste-123"); page.click("#btn")
    page.wait_for_url(BASE + "/"); page.wait_for_selector("#live")
    logs.clear()  # erros esperados (401 da senha errada) ficam de fora
    # humanBase aponta para modelos locais (sandbox sem acesso ao CDN)
    page.goto(BASE + "/?humanBase=" + BASE + "/human", wait_until="domcontentloaded")
    page.wait_for_function("document.querySelector('#live').naturalWidth > 0", timeout=10000)
    ok(True, "stream MJPEG com cookie + CORS carregou")
    page.wait_for_function("document.querySelector('#c-conn').textContent === 'online'", timeout=5000)
    ok("14.8 fps" in page.inner_text("#c-fps"), "chips de status atualizados")
    ok("movimento" in page.inner_text("#c-motion"), "chip de movimento ativo")

    # ajustes: muda resolução e verifica POST agrupado
    page.click("button[data-tab=camera]")
    page.select_option("select[data-k='cam.framesize']", "5")
    page.check("input[data-k='cam.vflip']")
    time.sleep(1)
    st = mock("/mock/state")
    ok(st["settings"]["cam"]["framesize"] == 5 and st["settings"]["cam"]["vflip"] is True, "ajustes de camera salvos via API")
    ok(st["posts"] == 1, f"mudancas agrupadas em 1 POST (debounce) -> {st['posts']}")
    page.click("button[data-tab=system]"); time.sleep(1.2)
    ok("OV2640" in page.inner_text("#sys"), "aba sistema mostra dados")
    page.click("button[data-tab=faces]")

    # reconhecimento facial
    t0 = time.time()
    page.wait_for_function("document.querySelector('#fr-status').textContent.startsWith('Pronto')", timeout=180000)
    print("   modelos carregados em %.1fs: %s" % (time.time() - t0, page.inner_text("#fr-status")))
    page.wait_for_function("fr.tracks.length > 0", timeout=150000)
    ok(True, "rosto detectado no stream")
    ok(page.evaluate("trackLabel(fr.tracks[0])") is None, "sem cadastro -> desconhecido")
    page.fill("#fr-name", "Teste")
    page.click("#fr-enroll")
    page.wait_for_function("document.querySelector('#fr-msg').textContent.includes('cadastrado')", timeout=150000)
    faces = mock("/mock/state")["faces"]
    ok(faces["v"] == 2 and len(faces["faces"]) == 5 and faces["faces"][0]["name"] == "Teste", "5 amostras salvas na placa (v2)")
    ok(len(faces["faces"][0]["e"]["q"]) < 2000, "embedding quantizado compacto (%d chars)" % len(faces["faces"][0]["e"]["q"]))
    page.wait_for_function("fr.tracks.some(t => trackLabel(t) === 'Teste')", timeout=120000)
    ok(True, "rosto reconhecido como 'Teste'")
    sim = page.evaluate("fr.tracks[0].similarity")
    print("   similaridade: %.2f  real=%s live=%s" % (sim, page.evaluate("fr.tracks[0].real"), page.evaluate("fr.tracks[0].live")))
    time.sleep(6)
    page.screenshot(path=os.path.join(SHOTS, "known.png"))
    known = mock("/mock/state")["known"]
    print("   sinais /api/known: %d" % known)

    # outra pessoa -> não pode ser reconhecida como Teste
    urllib.request.urlopen(BASE + "/mock/scene?s=sceneB.jpg")
    page.wait_for_function("fr.tracks.length > 0 && fr.tracks.every(t => trackLabel(t) !== 'Teste')", timeout=120000)
    time.sleep(12)
    labels = page.evaluate("fr.tracks.map(t => [trackLabel(t), +t.similarity.toFixed(2)])")
    ok(all(l[0] != "Teste" for l in labels), f"outra pessoa nao confundida: {labels}")
    page.screenshot(path=os.path.join(SHOTS, "unknown.png"))

    # recarrega: cadastro persiste e volta a reconhecer
    urllib.request.urlopen(BASE + "/mock/scene?s=sceneA.jpg")
    page.reload(wait_until="domcontentloaded")
    page.wait_for_function("fr.tracks.some(t => trackLabel(t) === 'Teste')", timeout=180000)
    ok("Teste (5 amostras)" in page.inner_text("#fr-list"), "cadastro persiste apos recarregar")

    # galeria e print manual
    page.click("#shot"); page.wait_for_selector("#gallery figure", timeout=5000)
    ok(page.locator("#gallery figure").count() == 1, "print manual aparece na galeria")

    # sem rosto -> tracks somem
    urllib.request.urlopen(BASE + "/mock/scene?s=sceneEmpty.jpg")
    page.wait_for_function("fr.tracks.length === 0", timeout=60000)
    ok(True, "sem rosto -> sem caixas")

    page.set_viewport_size({"width": 390, "height": 844})
    urllib.request.urlopen(BASE + "/mock/scene?s=sceneA.jpg"); time.sleep(3)
    page.screenshot(path=os.path.join(SHOTS, "mobile.png"), full_page=True)
    sw = page.evaluate("document.documentElement.scrollWidth")
    ok(sw <= 390, f"sem rolagem horizontal no celular (scrollWidth={sw})")

    errs = [l for l in logs if l.startswith(("error", "PAGEERROR"))]
    ok(not errs, "sem erros no console" + ("" if not errs else ": " + " | ".join(errs[:5])))
    b.close()
print("\nRESULTADO:", "OK" if not errors else f"{len(errors)} falha(s)")
sys.exit(1 if errors else 0)
