#!/usr/bin/env python3
# 3.35: publishes the website to the openmp.com repo (GitHub Pages: thirtyfoxmcx00e1.github.io/openmp.com)
# with the release files in downloads/ - the site links to its own files, never to the wagu repo.
#   python3 tools/publish_site.py 3.35 [path-to-openmp.com-checkout]
import os, sys, shutil, tarfile, subprocess, json
V = sys.argv[1]
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(ROOT), "openmp.com")
os.chdir(ROOT)
if not os.path.isdir(OUT): sys.exit("clone the openmp.com repo to %s first" % OUT)
# 1) website files (docs/ = the source of the site)
for n in os.listdir(OUT):
    if n in (".git", "README.md", "LICENSE"): continue
    p = os.path.join(OUT, n)
    shutil.rmtree(p) if os.path.isdir(p) else os.remove(p)
for n in os.listdir("docs"):
    if n.startswith("_"): continue
    s = os.path.join("docs", n); d = os.path.join(OUT, n)
    shutil.copytree(s, d) if os.path.isdir(s) else shutil.copy2(s, d)
open(os.path.join(OUT, ".nojekyll"), "w").close()
# 2) downloads/ - stable names, always the latest release
dl = os.path.join(OUT, "downloads"); os.makedirs(dl, exist_ok=True)
files = {
    "MP-client.zip": "release/MP-%s-client.zip" % V, "MP-full.zip": "release/MP-%s-full.zip" % V,
    "libMP32.so": "bin/libMP32.so", "libMP64.so": "bin/libMP64.so",
    "MP-Voice.apk": "release/MP-Voice.apk", "HiveMQ-kit.zip": "release/HiveMQ-kit-%s.zip" % V,
}
for d, s in files.items(): shutil.copy2(s, os.path.join(dl, d))
# server source for the Termux installer (server/ + src/net + mbedtls - no game code)
def keep(ti):
    b = os.path.basename(ti.name)
    if "/obj" in ti.name or b in ("mp_server",) or b.endswith((".o", ".a")): return None
    return ti
with tarfile.open(os.path.join(dl, "mp-server-src.tar.gz"), "w:gz") as t:
    for p in ("server", "src/net", "third_party/mbedtls"): t.add(p, filter=keep)
rel = json.load(open("docs/release.json"))
rel["downloads"] = sorted(list(files) + ["mp-server-src.tar.gz"])
json.dump(rel, open(os.path.join(OUT, "release.json"), "w"), indent=1)
for n in sorted(os.listdir(dl)): print("downloads/%s %d" % (n, os.path.getsize(os.path.join(dl, n))))
