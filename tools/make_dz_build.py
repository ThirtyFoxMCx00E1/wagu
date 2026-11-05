#!/usr/bin/env python3
"""4.08: Project Day Z Legacy base building - the SA-MP wall pieces (samp.img) for the Day Z model pack.

Takes the wall sections and the door from the PC SA-MP 0.3.7 samp.img (SAMP/SAMP.img of
sa-mp-0.3.7-R5-1-install.exe) and writes them into Host/DayZ/models/buildings/samp/ with a TXD that
only has the texture the piece uses, then adds them to Host/DayZ/models/manifest.txt as 'o' models on
GTA pickup models that are never placed on the map (the Day Z model loader swaps them in on the
Day Z server only). The collision of each piece is set by the client (game/dzmodels.cpp
SetBoxCollision) from the boxes in src/net/dz_build.h, because the GTA collision of the host model
does not fit a wall.

usage: python3 tools/make_dz_build.py /path/to/SAMP.img
"""
import os, sys, struct, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'Host', 'DayZ', 'models', 'buildings', 'samp')
MANIFEST = os.path.join(ROOT, 'Host', 'DayZ', 'models', 'manifest.txt')

# host model -> (samp id, samp model name, txd, texture)   keep in sync with src/net/dz_build.h
PIECES = [
    (1253, 19356, 'wall004', 'all_walls', 'cj_lightwood'),        # wood wall / foundation / ceiling
    (1254, 19386, 'wall034', 'all_walls', 'cj_lightwood'),        # wood doorway
    (1272, 19402, 'wall050', 'all_walls', 'cj_lightwood'),        # wood window
    (1273, 19365, 'wall013', 'all_walls', 'mp_carter_wall'),      # concrete wall / foundation / ceiling
    (1274, 19394, 'wall042', 'all_walls', 'mp_carter_wall'),      # concrete doorway
    (1275, 19411, 'wall059', 'all_walls', 'mp_carter_wall'),      # concrete window (wall059, the one in the screenshot)
    (1313, 19857, 'mihouse1door1', 'mihouse1', None),             # door (all of its textures)
]

def read_img(path):
    d = open(path, 'rb').read()
    if d[:4] != b'VER2': raise SystemExit('not a VER2 img: ' + path)
    n = struct.unpack_from('<I', d, 4)[0]; r = {}
    for i in range(n):
        off, sz, _, name = struct.unpack_from('<IHH24s', d, 8 + i * 32)
        r[name.split(b'\0')[0].decode().lower()] = d[off * 2048:(off + sz) * 2048]
    return r

def chunk_len(b, o): return 12 + struct.unpack_from('<I', b, o + 4)[0]

def trim_dff(b):
    # the img entry is padded to 2048: keep only the clump chunk
    return b[:chunk_len(b, 0)]

def dff_textures(b):
    out = set(); o = 0
    while True:
        i = b.find(b'\x06\x00\x00\x00', o)
        if i < 0 or i + 40 > len(b): break
        t2, s2 = struct.unpack_from('<II', b, i + 12)
        if t2 == 1 and s2 == 4:
            t3, s3 = struct.unpack_from('<II', b, i + 28)
            if t3 == 2 and s3 < 64: out.add(b[i + 40:i + 40 + s3].split(b'\0')[0].decode(errors='ignore').lower())
        o = i + 4
    return out

def filter_txd(b, keep):
    # TexDictionary: struct (count u16, device u16), TextureNative * count, extension
    assert struct.unpack_from('<I', b, 0)[0] == 0x16
    ver = struct.unpack_from('<I', b, 8)[0]
    o = 12; sl = chunk_len(b, o)
    cnt, dev = struct.unpack_from('<HH', b, o + 12)
    o += sl; natives = []
    for _ in range(cnt):
        L = chunk_len(b, o); nat = b[o:o + L]
        name = nat[12 + 20:12 + 52].split(b'\0')[0].decode(errors='ignore').lower()
        if keep is None or name in keep: natives.append(nat)
        o += L
    ext = b[o:o + chunk_len(b, o)] if o + 12 <= len(b) and struct.unpack_from('<I', b, o)[0] == 3 else struct.pack('<III', 3, 0, ver)
    st = struct.pack('<III', 1, 4, ver) + struct.pack('<HH', len(natives), dev)
    body = st + b''.join(natives) + ext
    return struct.pack('<III', 0x16, len(body), ver) + body, len(natives)

def main():
    if len(sys.argv) < 2: raise SystemExit(__doc__)
    im = read_img(sys.argv[1])
    os.makedirs(os.path.join(OUT, 'dff'), exist_ok=True); os.makedirs(os.path.join(OUT, 'txd'), exist_ok=True)
    lines = []
    for host, sid, name, txd, tex in PIECES:
        dff = trim_dff(im[name + '.dff'])
        keep = dff_textures(dff) if tex is None else {tex}
        t, n = filter_txd(im[txd + '.txd'], keep)
        if n == 0: raise SystemExit('no texture for ' + name)
        fd = os.path.join(OUT, 'dff', name + '.dff'); ft = os.path.join(OUT, 'txd', name + '.txd')
        open(fd, 'wb').write(dff); open(ft, 'wb').write(t)
        rel = 'buildings/samp/'
        lines.append('%d o %sdff/%s.dff %stxd/%s.txd %d %d %08x %08x' % (host, rel, name, rel, name, len(dff), len(t),
                     zlib.crc32(dff) & 0xffffffff, zlib.crc32(t) & 0xffffffff))
        print('%5d <- %5d %-14s %6d + %6d bytes, %d texture(s)' % (host, sid, name, len(dff), len(t), n))
    text = open(MANIFEST).read().splitlines()
    tag = '# base building (4.08, tools/make_dz_build.py)'
    hosts = {str(p[0]) for p in PIECES}
    text = [l for l in text if l != tag and not (l.split(' ')[0] in hosts and ' buildings/samp/' in l)]
    while text and not text[-1].strip(): text.pop()
    text += [tag + ': SA-MP samp.img wall pieces on GTA pickup models (Day Z only)'] if False else [tag] + lines
    open(MANIFEST, 'w', newline='\n').write('\n'.join(text) + '\n')

if __name__ == '__main__':
    main()
