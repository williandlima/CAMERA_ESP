"""Compacta web/* (gzip) e gera src/generated/web_assets.h.

Executado automaticamente pelo PlatformIO antes de compilar (extra_scripts = pre:...).
Também pode rodar sozinho:  python tools/embed_web.py
"""
import gzip
import hashlib
import os

MIME = {
    ".html": "text/html; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".svg": "image/svg+xml",
    ".ico": "image/x-icon",
    ".json": "application/json",
}


def generate(project_dir):
    web = os.path.join(project_dir, "web")
    out_dir = os.path.join(project_dir, "src", "generated")
    out = os.path.join(out_dir, "web_assets.h")
    os.makedirs(out_dir, exist_ok=True)
    files = sorted(f for f in os.listdir(web) if os.path.splitext(f)[1] in MIME)
    lines = [
        "// GERADO por tools/embed_web.py a partir de web/. Não edite.",
        "#pragma once",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "struct WebAsset {",
        "  const char *path;",
        "  const char *mime;",
        "  const uint8_t *data;",
        "  size_t len;",
        "  const char *etag;",
        "};",
        "",
    ]
    table = []
    total_raw = total_gz = 0
    for i, name in enumerate(files):
        with open(os.path.join(web, name), "rb") as fh:
            raw = fh.read()
        gz = gzip.compress(raw, compresslevel=9, mtime=0)  # mtime=0: saída reproduzível
        total_raw += len(raw)
        total_gz += len(gz)
        etag = '"%s"' % hashlib.sha1(raw).hexdigest()[:16]
        hexdata = ",".join("0x%02x" % b for b in gz)
        lines.append("static const uint8_t WEB_ASSET_%d[] = {%s};" % (i, hexdata))
        table.append('  {"/%s", "%s", WEB_ASSET_%d, %d, "%s"},' % (
            name, MIME[os.path.splitext(name)[1]], i, len(gz), etag.replace('"', '\\"')))
    lines += ["", "static const WebAsset WEB_ASSETS[] = {"] + table + ["};"]
    lines.append("static const size_t WEB_ASSETS_COUNT = %d;" % len(files))
    content = "\n".join(lines) + "\n"
    old = open(out).read() if os.path.exists(out) else ""
    if old != content:  # evita recompilar à toa
        with open(out, "w") as fh:
            fh.write(content)
    print("web assets: %d arquivos, %d -> %d bytes (gzip)" % (len(files), total_raw, total_gz))


try:
    Import("env")  # noqa: F821  (contexto do PlatformIO/SCons)
    generate(env.subst("$PROJECT_DIR"))  # noqa: F821
except NameError:
    generate(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
