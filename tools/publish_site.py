#!/usr/bin/env python3
# 3.37: builds the website (GitHub Pages of the wagu repo: mpsamp-open.github.io) into one folder:
# docs/ + downloads/ with the release files under stable names (always the latest release in docs/release.json).
# The separate openmp.com repo is no longer used - wagu is the only repo (mod, servers, website).
#   python3 tools/publish_site.py [version] [out-dir]     (defaults: tag in docs/release.json, _site)
# Run by .github/workflows/pages.yml on every push; locally it is only needed to preview the site.
import os, sys, shutil, tarfile, json
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
rel = json.load(open("docs/release.json"))
V = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] else rel["tag_name"].lstrip("v")
OUT = sys.argv[2] if len(sys.argv) > 2 else "_site"
# 1) website files (docs/ = the source of the site)
if os.path.isdir(OUT): shutil.rmtree(OUT)
shutil.copytree("docs", OUT, ignore=shutil.ignore_patterns("_*"))
open(os.path.join(OUT, ".nojekyll"), "w").close()
# 2) downloads/ - stable names, always the latest release
dl = os.path.join(OUT, "downloads"); os.makedirs(dl, exist_ok=True)
files = {
    "MP-client.zip": "release/MP-%s-client.zip" % V, "MP-full.zip": "release/MP-%s-full.zip" % V,
    "libMP32.so": "bin/libMP32.so", "libMP64.so": "bin/libMP64.so",
    "MP-Voice.apk": "release/MP-Voice.apk", "MP-Textdraw-Editor.apk": "release/MP-Textdraw-Editor.apk", "HiveMQ-kit.zip": "release/HiveMQ-kit-%s.zip" % V,
}
for d, s in files.items():
    if not os.path.isfile(s): sys.exit("missing %s (run tools/make_release.py %s first)" % (s, V))
    shutil.copy2(s, os.path.join(dl, d))
# 4.13: optional files (the Moquil APK is built by .github/workflows/moquil.yml and committed to release/)
for d, s in {"Moquil.apk": "release/Moquil.apk"}.items():
    if os.path.isfile(s): shutil.copy2(s, os.path.join(dl, d)); files[d] = s
# server source for the Termux installer (server/ + src/net + mbedtls - no game code)
def keep(ti):
    b = os.path.basename(ti.name)
    if "/obj" in ti.name or b in ("mp_server",) or b.endswith((".o", ".a")): return None
    return ti
with tarfile.open(os.path.join(dl, "mp-server-src.tar.gz"), "w:gz") as t:
    for p in ("server", "src/net", "third_party/mbedtls"): t.add(p, filter=keep)
# 4.12.7: wagu is private, so the website also serves the public files the mod and the VPS scripts use
# (they used raw.githubusercontent.com/.../wagu before): the HiveMQ kit, Day Z models / mapping, media, GIFs
with tarfile.open(os.path.join(dl, "HiveMQ-kit.tar.gz"), "w:gz") as t: t.add("HiveMQ", filter=keep)
for p in ("HiveMQ", "Host/DayZ/models", "Host/DayZ/mapping", "Host/DayZ/pack", "Host/Media"):
    if os.path.isdir(p): shutil.copytree(p, os.path.join(OUT, p), dirs_exist_ok=True, ignore=shutil.ignore_patterns(".git*", "obj"))
if os.path.isdir("_gifs"): shutil.copytree("_gifs", os.path.join(OUT, "gifs"), dirs_exist_ok=True, ignore=shutil.ignore_patterns(".git*"))
shutil.copy2("src/net/protocol.h", os.path.join(OUT, "protocol.h"))
rel["downloads"] = sorted(list(files) + ["mp-server-src.tar.gz", "HiveMQ-kit.tar.gz"])
json.dump(rel, open(os.path.join(OUT, "release.json"), "w"), indent=1)
for n in sorted(os.listdir(dl)): print("downloads/%s %d" % (n, os.path.getsize(os.path.join(dl, n))))
