#!/usr/bin/env python3
"""4.05.5: Day Z world textures + map models (Project Day Z Legacy).

    python3 tools/make_dz_world.py <pack folder> [--cap 256]

<pack folder> = the unpacked texture pack of release v4.02 ("GTA V TEXTURES OVERHAUL 2020 FOR
ANDROID BY SWARAJ": com.rockstargames.gtasa/files/texdb/gta3 + "IMPORT TO GTA3.IMG/*.dff").

1. texdb (gta3.txt + .dat + .toc) -> Host/DayZ/models/world/tex/world_NNN.txd + world.idx
   (tools/texdb2txd.py: RLE undone, DXT kept, 8888 / 565 re-encoded as DXT, max --cap pixels).
2. every dff in "IMPORT TO GTA3.IMG" -> Host/DayZ/models/world/roads/<name>.dff + a <name>.txd made
   from the textures that dff uses (taken from the converted texdb), so the MTA-style loader
   (src/game/dzmodels.cpp) can swap it in like any other Day Z model.
3. Host/DayZ/models/manifest.txt gets a "# world" section:
       <id> m world/roads/x.dff world/roads/x.txd <sizes> <crcs>     (m = map model, always wanted)
       tex world/tex/world.idx <size> <crc>
       tex world/tex/world_000.txd <size> <crc> ...
   The game downloads these on the Day Z server and answers the texture-database lookups
   (TextureDatabaseRuntime::GetTexture) from them, one texture at a time.
"""
import os, re, struct, subprocess, sys, zlib, argparse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEST = os.path.join(ROOT, 'Host', 'DayZ', 'models')
# map model ids (GTA SA ide, wiki.multitheftauto.com/wiki/IDE_List)
MAP_IDS = {'cuntwroad22': 17303, 'cuntwroad68': 17279, 'cuntwroad71': 17281}

def crc(b): return zlib.crc32(b) & 0xffffffff

def dff_textures(d):
    names = []
    def walk(p, end):
        while p + 12 <= end:
            t, s, _ = struct.unpack_from('<III', d, p)
            if p + 12 + s > end: break
            if t == 0x06:
                q = p + 12; q += 12 + struct.unpack_from('<I', d, q + 4)[0]
                t2, s2, _ = struct.unpack_from('<III', d, q)
                if t2 == 2:
                    n = d[q + 12:q + 12 + s2].split(b'\0')[0].decode('latin1')
                    if n and n.lower() not in [x.lower() for x in names]: names.append(n)
            elif t in (0x10, 0x1a, 0x14, 0x0f, 0x08, 0x07, 0x0e, 0x03): walk(p + 12, p + 12 + s)
            p += 12 + s
    walk(0, len(d))
    return names

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('pack'); ap.add_argument('--cap', type=int, default=256)
    a = ap.parse_args()
    texdb = None
    for r, ds, fs in os.walk(a.pack):
        if any(f.endswith('.txt') for f in fs) and any(f.endswith('.dat') for f in fs): texdb = r; break
    if not texdb: sys.exit('no texdb folder (.txt + .dat) in ' + a.pack)
    out = os.path.join(DEST, 'world', 'tex'); os.makedirs(out, exist_ok=True)
    for f in os.listdir(out): os.unlink(os.path.join(out, f))
    subprocess.check_call([sys.executable, os.path.join(ROOT, 'tools', 'texdb2txd.py'), texdb, out, '--cap', str(a.cap)])
    # the index: name -> (file, struct offset, struct size)
    idx = {}; alias = {}
    for l in open(os.path.join(out, 'world.idx'), encoding='latin1'):
        p = l.rstrip('\n').split('\t')
        if len(p) == 4 and p[0] != 'file': idx[p[0].lower()] = (p[1], int(p[2]), int(p[3]))
        elif len(p) == 3 and p[0] == 'alias': alias[p[1].lower()] = p[2].lower()
    cache = {}
    def native(name):   # the whole texture native chunk (header 12 + struct 12 + data + extension)
        n = name.lower()
        for _ in range(4):
            if n in idx: break
            n = alias.get(n, n)
        if n not in idx: return None
        fn, off, sz = idx[n]
        if fn not in cache: cache[fn] = open(os.path.join(out, fn), 'rb').read()
        d = cache[fn]; st = off - 24
        t, cs, _ = struct.unpack_from('<III', d, st)
        assert t == 0x15
        return d[st:st + 12 + cs]
    # map models
    roads_src = None
    for r, ds, fs in os.walk(a.pack):
        if any(f.lower().endswith('.dff') for f in fs): roads_src = r; break
    lines = []
    if roads_src:
        rd = os.path.join(DEST, 'world', 'roads'); os.makedirs(rd, exist_ok=True)
        for f in sorted(os.listdir(roads_src)):
            if not f.lower().endswith('.dff'): continue
            nm = f[:-4].lower(); mid = MAP_IDS.get(nm)
            if not mid: print('no model id for', f, '- skipped'); continue
            d = open(os.path.join(roads_src, f), 'rb').read()
            tex = dff_textures(d); ch = [native(t) for t in tex]; missing = [t for t, c in zip(tex, ch) if c is None]
            ch = [c for c in ch if c]
            body = b''.join(ch)
            st = struct.pack('<III', 1, 4, 0x1803FFFF) + struct.pack('<HH', len(ch), 9)
            ext = struct.pack('<III', 3, 0, 0x1803FFFF)
            txd = struct.pack('<III', 0x16, len(st) + len(body) + len(ext), 0x1803FFFF) + st + body + ext
            open(os.path.join(rd, nm + '.dff'), 'wb').write(d); open(os.path.join(rd, nm + '.txd'), 'wb').write(txd)
            lines.append('%d m world/roads/%s.dff world/roads/%s.txd %d %d %08x %08x' % (mid, nm, nm, len(d), len(txd), crc(d), crc(txd)))
            print('%s (%d): %d textures%s' % (nm, mid, len(ch), (' - missing ' + ', '.join(missing)) if missing else ''))
    files = ['world.idx'] + sorted(f for f in os.listdir(out) if f.endswith('.txd'))
    for f in files:
        b = open(os.path.join(out, f), 'rb').read()
        lines.append('tex world/tex/%s %d %08x' % (f, len(b), crc(b)))
    # manifest: replace the old world section
    mp = os.path.join(DEST, 'manifest.txt')
    old = open(mp, encoding='latin1').read().split('\n')
    keep = []; skip = False
    for l in old:
        if l.startswith('# world'): skip = True; continue
        if skip and (l.startswith('tex ') or re.match(r'^\d+ m ', l)): continue
        skip = False
        if l: keep.append(l)
    keep.append('# world (4.05.5, tools/make_dz_world.py): m = map model (always swapped on Day Z), tex = world texture dictionaries + index')
    open(mp, 'w', newline='\n').write('\n'.join(keep + lines) + '\n')
    print('manifest: %d world lines' % len(lines))

if __name__ == '__main__': main()
