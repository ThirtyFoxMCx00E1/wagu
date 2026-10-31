#!/usr/bin/env python3
# 3.37: builds the website (GitHub Pages of the wagu repo: thirtyfoxmcx00e1.github.io/wagu) into one folder:
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
    "MP-Voice.apk": "release/MP-Voice.apk", "HiveMQ-kit.zip": "release/HiveMQ-kit-%s.zip" % V,
}
for d, s in files.items():
    if not os.path.isfile(s): sys.exit("missing %s (run tools/make_release.py %s first)" % (s, V))
    shutil.copy2(s, os.path.join(dl, d))
# server source for the Termux installer (server/ + src/net + mbedtls - no game code)
def keep(ti):
    b = os.path.basename(ti.name)
    if "/obj" in ti.name or b in ("mp_server",) or b.endswith((".o", ".a")): return None
    return ti
with tarfile.open(os.path.join(dl, "mp-server-src.tar.gz"), "w:gz") as t:
    for p in ("server", "src/net", "third_party/mbedtls"): t.add(p, filter=keep)
rel["downloads"] = sorted(list(files) + ["mp-server-src.tar.gz"])
json.dump(rel, open(os.path.join(OUT, "release.json"), "w"), indent=1)
for n in sorted(os.listdir(dl)): print("downloads/%s %d" % (n, os.path.getsize(os.path.join(dl, n))))
