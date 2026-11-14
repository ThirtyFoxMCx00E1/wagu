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
# 4.13.34: a release whose zips are not committed to release/ (they are too big to keep adding) gets them built
# here from bin/, assets/MP, the APKs in release/ and HiveMQ/ (the same layout as tools/make_release.py)
def _build_missing():
    import zipfile, tempfile
    tmp = tempfile.mkdtemp(prefix="mpzip_")
    def tree(z, src, dst, skip=()):
        for d, _, fs in os.walk(src):
            for f in sorted(fs):
                p = os.path.join(d, f); rel = os.path.relpath(p, src)
                if any(rel.startswith(x) for x in skip): continue
                z.write(p, dst + "/" + rel.replace(os.sep, "/"))
    inst = ("MP Multiplayer %s - install\n\n1. lib/arm64-v8a/libMP64.so (64-bit game) or lib/armeabi-v7a/libMP32.so (32-bit)\n"
            "   into Android/data/com.rockstargames.gtasa/mods/ (AML). Delete any separate libmonetloader.so there.\n"
            "2. files/TYFMC into Android/data/com.rockstargames.gtasa/files/.\n"
            "3. Main menu: TYFMC.MP.ini [MP] ServerBrowserMode = neon / mp / default (or MP Settings > Display > Main menu).\n") % V
    apks = [a for a in ("MP-Voice.apk", "MP-Textdraw-Editor.apk") if os.path.isfile("release/" + a)]
    out = {}
    if not os.path.isfile(files["MP-client.zip"]):
        o = os.path.join(tmp, "MP-%s-client.zip" % V); c = "MP-%s-client" % V
        with zipfile.ZipFile(o, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr(c + "/INSTALL.txt", inst)
            for a in apks: z.write("release/" + a, c + "/" + a)
            z.write("bin/libMP64.so", c + "/lib/arm64-v8a/libMP64.so"); z.write("bin/libMP32.so", c + "/lib/armeabi-v7a/libMP32.so")
            tree(z, "assets/MP", c + "/files/TYFMC/MP", ("MP.zip",))
        out["MP-client.zip"] = o
    if not os.path.isfile(files["MP-full.zip"]):
        if os.path.isfile("assets/MP/MP.zip"): out["MP-full.zip"] = "assets/MP/MP.zip"   # 4.13.20: MP.zip = the full zip
        else:
            o = os.path.join(tmp, "MP-%s-full.zip" % V); f = "MP-%s" % V
            with zipfile.ZipFile(o, "w", zipfile.ZIP_DEFLATED) as z:
                z.writestr(f + "/INSTALL.txt", inst)
                for a in apks + (["Moquil.apk"] if os.path.isfile("release/Moquil.apk") else []): z.write("release/" + a, f + "/" + a)
                z.write("bin/libMP64.so", f + "/lib/arm64-v8a/libMP64.so"); z.write("bin/libMP32.so", f + "/lib/armeabi-v7a/libMP32.so")
                tree(z, "assets/MP", f + "/assets/MP", ("MP.zip",))
            out["MP-full.zip"] = o
    if not os.path.isfile(files["HiveMQ-kit.zip"]) and os.path.isdir("HiveMQ"):
        o = os.path.join(tmp, "HiveMQ-kit-%s.zip" % V)
        with zipfile.ZipFile(o, "w", zipfile.ZIP_DEFLATED) as z: tree(z, "HiveMQ", "HiveMQ-kit", ("obj",))
        out["HiveMQ-kit.zip"] = o
    for k, v in out.items(): print("built %s -> %s" % (k, v)); files[k] = v
_build_missing()
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
