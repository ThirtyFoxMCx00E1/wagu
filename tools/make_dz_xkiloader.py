#!/usr/bin/env python3
"""MP 4.12.3: Project Day Z model pack for XkiLoader (release/libXkiLoader.so / libXkiLoader64.so).

XkiLoader swaps a model by its FILE NAME (= the GTA model name) and its textures by TEXTURE NAME, globally,
from <storage>/xkiloader/<folder>/ (see research/xkiloader-findings.md). It only reads Android TXDs, but
PNG files work, so:
  * every model of Host/DayZ/models/manifest.txt -> ProjectDayZ/<gta name>.dff
  * the PC (Direct3D) TXD of that model -> one PNG per texture (decoded here, like src/game/dzmodels.cpp)
  * the texture names are made unique ("dz<id>_<n>") and written into the DFF too: MTA DayZ uses names
    like "body", "1".."5", which would otherwise replace the same names in GTA itself (and each other).

  python3 tools/make_dz_xkiloader.py [out.zip]   (default: build/MP_ProjectDayZ_XkiLoader.zip)
IDE names: /tmp/ide/{peds,default,vehicles}.ide (FSSRepo/cleo-editor-android assets/gtasa) + the mapping catalog.
"""
import io, os, struct, sys, zipfile, hashlib
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DZ = os.path.join(ROOT, 'Host', 'DayZ', 'models')
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'MP_ProjectDayZ_XkiLoader.zip')
FOLDER = 'ProjectDayZ'
IDE = os.environ.get('IDE_DIR', '/tmp/ide')


def gta_names():
    n = {}
    for f in ('peds', 'default', 'vehicles'):
        for l in open(os.path.join(IDE, f + '.ide'), errors='ignore'):
            p = [x.strip() for x in l.split('#')[0].split(',')]
            if len(p) >= 3 and p[0].isdigit():
                n.setdefault(int(p[0]), p[1])
    for l in open(os.path.join(ROOT, 'Host', 'DayZ', 'mapping', 'catalog.txt'), errors='ignore'):
        if l.startswith('#'):
            continue
        p = l.split()
        if len(p) > 2 and p[0].isdigit():
            n.setdefault(int(p[0]), p[1])
    return n


# ---------------------------------------------------------------- PC TXD -> RGBA
def u32(d, o): return struct.unpack_from('<I', d, o)[0]
def u16(d, o): return struct.unpack_from('<H', d, o)[0]


def decode_native(d, cap):
    if len(d) < 88:
        return None
    plat = u32(d, 0)
    if plat not in (8, 9):
        return None
    name = d[8:40].split(b'\0')[0].decode('latin1')
    rf, fmt = u32(d, 72), u32(d, 76)
    w, h, depth, levels, flags = u16(d, 80), u16(d, 82), d[84], d[85] or 1, d[87]
    dxt = 0
    if plat == 9:
        dxt = {0x31545844: 1, 0x32545844: 3, 0x33545844: 3, 0x34545844: 5, 0x35545844: 5}.get(fmt, 0)
    elif 1 <= flags <= 5:
        dxt = 1 if flags == 1 else (3 if flags <= 3 else 5)
    p = 88
    pal8, pal4 = bool(rf & 0x2000), bool(rf & 0x4000)
    pal = None
    if pal8 or pal4:
        cnt = 256 if pal8 else 32
        pal = [tuple(d[p + i * 4:p + i * 4 + 4]) for i in range(cnt)]
        p += cnt * 4
    if not (0 < w <= 4096 and 0 < h <= 4096):
        return None
    lv = 0
    while lv + 1 < levels and ((w >> lv) > cap or (h >> lv) > cap) and (w >> (lv + 1)) >= 4 and (h >> (lv + 1)) >= 4:
        lv += 1
    for _ in range(lv):
        p += 4 + u32(d, p)
    sz = u32(d, p); p += 4
    px = d[p:p + sz]
    lw, lh = max(1, w >> lv), max(1, h >> lv)
    if dxt:
        bw, bh = (lw + 3) // 4, (lh + 3) // 4
        need = bw * bh * (8 if dxt == 1 else 16)
        if len(px) < need:
            return None
        im = Image.frombytes('RGBA', (bw * 4, bh * 4), bytes(px[:need]), 'bcn', {1: 1, 3: 2, 5: 3}[dxt])
        if (bw * 4, bh * 4) != (lw, lh):
            im = im.crop((0, 0, lw, lh))
    elif pal:
        idx = px[:lw * lh]
        if len(idx) < lw * lh:
            return None
        out = bytearray(lw * lh * 4)
        for i, c in enumerate(idx):
            out[i * 4:i * 4 + 4] = bytes(pal[(c & 31) if pal4 else c])
        im = Image.frombytes('RGBA', (lw, lh), bytes(out))
    else:
        f = 0
        if plat == 9:
            f = {21: 1, 22: 2, 23: 3, 25: 4, 26: 5, 50: 6}.get(fmt, 0)
        else:
            f = {0x500: 1, 0x600: 7 if depth == 24 else 2, 0x200: 3, 0x100: 4, 0x300: 5, 0x400: 6}.get(rf & 0x0F00, 0)
        if not f:
            return None
        n = lw * lh
        if f in (1, 2):
            if len(px) < n * 4: return None
            im = Image.frombytes('RGBA', (lw, lh), bytes(px[:n * 4]), 'raw', 'BGRA')
            if f == 2: im.putalpha(255)
        elif f == 7:
            if len(px) < n * 3: return None
            im = Image.frombytes('RGB', (lw, lh), bytes(px[:n * 3]), 'raw', 'BGR').convert('RGBA')
        elif f == 6:
            im = Image.frombytes('L', (lw, lh), bytes(px[:n])).convert('RGBA')
        else:
            if len(px) < n * 2: return None
            out = bytearray(n * 4)
            for i in range(n):
                c = u16(px, i * 2)
                if f == 3: q = (((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31, 255)
                elif f == 4: q = (((c >> 10) & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31, (c & 31) * 255 // 31, 255 if c & 0x8000 else 0)
                else: q = (((c >> 8) & 15) * 17, ((c >> 4) & 15) * 17, (c & 15) * 17, (c >> 12) * 17)
                out[i * 4:i * 4 + 4] = bytes(q)
            im = Image.frombytes('RGBA', (lw, lh), bytes(out))
    while im.width > cap or im.height > cap:
        im = im.resize((max(1, im.width // 2), max(1, im.height // 2)), Image.BOX)
    return name, im


def txd_textures(path, cap):
    d = open(path, 'rb').read()
    if len(d) < 28 or u32(d, 0) != 0x16:
        return []
    count, p, out = u16(d, 24), 28, []
    for _ in range(count):
        if p + 12 > len(d): break
        ct, cs = u32(d, p), u32(d, p + 4); nxt = p + 12 + cs
        if ct != 0x15 or nxt > len(d): break
        q = p + 12
        if u32(d, q) == 0x01:
            ss = u32(d, q + 4)
            r = decode_native(d[q + 12:q + 12 + ss], cap)
            if r: out.append(r)
        p = nxt
    return out


# ---------------------------------------------------------------- DFF texture rename
CONTAINERS = {0x10, 0x0E, 0x1A, 0x0F, 0x08, 0x07, 0x06, 0x14, 0x03, 0x2B}   # clump, frames, geometry, materials, texture, atomic, extension


def rw_string(s, ver):
    b = s.encode('latin1') + b'\0'
    b += b'\0' * ((4 - len(b) % 4) % 4)
    return struct.pack('<III', 0x02, len(b), ver) + b


def rewrite(d, ren, in_tex=False):
    """rebuilds RW chunks; inside a Texture (0x06) the 1st string = name (renamed), the 2nd = mask (emptied)"""
    out, p, strings = bytearray(), 0, 0
    while p + 12 <= len(d):
        t, s, v = struct.unpack_from('<III', d, p)
        body = d[p + 12:p + 12 + s]
        if p + 12 + s > len(d):
            out += d[p:]; break
        if in_tex and t == 0x02:
            strings += 1
            nm = body.split(b'\0')[0].decode('latin1')
            if strings == 1:
                out += rw_string(ren.get(nm.lower(), nm), v)
            else:
                out += rw_string(ren.get(nm.lower(), '') if nm else '', v)
        elif t in CONTAINERS:
            nb = rewrite(body, ren, t == 0x06)
            out += struct.pack('<III', t, len(nb), v) + nb
        else:
            out += d[p:p + 12 + s]
        p += 12 + s
    return bytes(out)


def main():
    names = gta_names()
    rows = [l.split() for l in open(os.path.join(DZ, 'manifest.txt')) if l.strip() and not l.startswith('#')]
    rows = [r for r in rows if r[0].isdigit()]
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    readme = []
    nmodels = ntex = 0
    with zipfile.ZipFile(OUT, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for r in rows:
            mid, kind, dff, txd = int(r[0]), r[1], r[2], r[3]
            gname = names.get(mid)
            if not gname:
                print('no name for', mid); continue
            cap = 1024 if kind == 'p' else (256 if kind in ('o', 'm') else 512)
            texs = txd_textures(os.path.join(DZ, txd), cap) if txd != '-' else []
            ren, seen = {}, set()
            for i, (tn, im) in enumerate(texs):
                k = tn.lower()
                if k in seen: continue
                seen.add(k)
                new = ('dz%d_%s' % (mid, tn))[:31]
                if len(new) > 31 or new.lower() in ren.values():
                    new = 'dz%d_%d' % (mid, i)
                ren[k] = new
                b = io.BytesIO(); im.save(b, 'PNG', optimize=True)
                z.writestr('%s/%s/%s.png' % (FOLDER, gname, new), b.getvalue()); ntex += 1
            d = open(os.path.join(DZ, dff), 'rb').read()
            nd = rewrite(d, ren)
            z.writestr('%s/%s/%s.dff' % (FOLDER, gname, gname), nd); nmodels += 1
            readme.append('%5d  %-14s %s  (%d textures)' % (mid, gname, dff, len(ren)))
        z.writestr(FOLDER + '/README_MP.txt',
                   'MP - Project Day Z model pack for XkiLoader (MP 4.12.3)\n'
                   'Models: MTA DayZ (github.com/NullSystemWorks/mtadayz), textures converted to PNG.\n'
                   'Put the folder "%s" in <game storage>/xkiloader/ (or /sdcard/xkiloader/) - MP does it for you:\n'
                   'Server > Project Day Z > Download pack / Import pack (zip). XkiLoader (libXkiLoader.so /\n'
                   'libXkiLoader64.so in wagu release/) must be installed as an AML mod. Restart the game after.\n'
                   'Note: XkiLoader swaps by model name everywhere (also outside Day Z); turn the folder off in\n'
                   'XkiLoader\'s menu / XkiiGnss.xkiloader.ini or delete it to get the normal models back.\n\n' % FOLDER
                   + '\n'.join(readme) + '\n')
    h = hashlib.sha256(open(OUT, 'rb').read()).hexdigest()
    print('%s: %d models, %d textures, %.1f MB, sha256 %s' % (OUT, nmodels, ntex, os.path.getsize(OUT) / 1048576, h))


if __name__ == '__main__':
    main()
