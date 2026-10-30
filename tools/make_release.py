#!/usr/bin/env python3
# 3.34: builds release/MP-<v>-client.zip, release/MP-<v>-full.zip and release/HiveMQ-kit-<v>.zip
# from bin/, assets/MP, docs/ and the HiveMQ/ folder (same layout as the 3.32 zips).
import os, sys, zipfile
V = sys.argv[1]
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
def tree(z, src, dst, skip=()):
    for d, _, fs in os.walk(src):
        for f in sorted(fs):
            p = os.path.join(d, f); rel = os.path.relpath(p, src)
            if any(rel.startswith(s) for s in skip): continue
            z.write(p, dst + "/" + rel.replace(os.sep, "/"))
def zf(path): return zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9)
old = zipfile.ZipFile("release/MP-3.32-client.zip")
inst = old.read("MP-3.31-client/INSTALL.txt").decode("utf-8", "replace").replace("3.31", V).replace("3.32", V)
admin = zipfile.ZipFile("release/MP-3.32-full.zip").read("MP-3.32/ADMIN_KEY_README.txt")
MOD = """MP moderators (3.35)
====================
Moderators are NOT the admin key (admin.txt). Every server owner picks their own.

Server owner (VPS / Termux / Oracle): make moderator.txt next to mp_server, one line per moderator:
    # name  key (8+ characters, no spaces)
    Juan   juan-secret-key-123
  (or mp_server ... --mods /path/to/moderator.txt; a server hosted in the game reads
   Android/data/com.rockstargames.gtasa/files/MP/moderators_server.txt)

Moderator (phone): put the same key in Android/data/com.rockstargames.gtasa/files/MP/moderator.txt
(just the key on one line) and join with that player name. You get "[MOD] name" in the name tag,
chat and kill feed, and can kick / mute players and switch cheats on that server (ban = admin only).
"""
kit_skip = ("oracle/", "cloud/")   # the kit zip keeps the 3.33 file set; the full repo has everything
with zf("release/MP-%s-client.zip" % V) as z:
    c = "MP-%s-client" % V
    z.write("release/MP-Voice.apk", c + "/MP-Voice.apk"); z.writestr(c + "/INSTALL.txt", inst); z.writestr(c + "/MODERATOR_README.txt", MOD)
    z.write("bin/libMP64.so", c + "/lib/arm64-v8a/libMP64.so"); z.write("bin/libMP32.so", c + "/lib/armeabi-v7a/libMP32.so")
    tree(z, "assets/MP", c + "/files/MP")
with zf("release/HiveMQ-kit-%s.zip" % V) as z: tree(z, "HiveMQ", "HiveMQ-kit", kit_skip)
with zf("release/MP-%s-full.zip" % V) as z:
    f = "MP-%s" % V
    z.write("release/MP-Voice.apk", f + "/MP-Voice.apk"); z.writestr(f + "/ADMIN_KEY_README.txt", admin); z.writestr(f + "/MODERATOR_README.txt", MOD)
    z.write("bin/libMP64.so", f + "/lib/arm64-v8a/libMP64.so"); z.write("bin/libMP32.so", f + "/lib/armeabi-v7a/libMP32.so")
    tree(z, "assets/MP", f + "/assets/MP"); tree(z, "docs", f + "/website"); tree(z, "HiveMQ", f + "/HiveMQ-kit", kit_skip)
    z.write("termux/install.sh", f + "/termux/install.sh")
for n in ("client", "full"): print("release/MP-%s-%s.zip" % (V, n), os.path.getsize("release/MP-%s-%s.zip" % (V, n)))
print("release/HiveMQ-kit-%s.zip" % V, os.path.getsize("release/HiveMQ-kit-%s.zip" % V))
