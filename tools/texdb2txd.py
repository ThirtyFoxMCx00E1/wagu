#!/usr/bin/env python3
# 4.05.5: GTA SA mobile texture database (texdb) -> standard PC (Direct3D 9) TXD files.
#
#   python3 tools/texdb2txd.py <texdb dir> <out dir> [--cap 512] [--per-txd 24] [--png N]
#
# <texdb dir> holds name.txt + name.<dxt|etc|pvr|RENAME>.dat / .toc (the .tmb thumbnails are not
# needed). Format (gtamods.com/wiki/Mobile_textures_(SA/VC)):
#   .txt  one line per texture: "name" width= height= format= alphamode= ... or "affiliate=other"
#   .toc  u32 size of the .dat, then one i32 offset per .txt line (-1 = affiliate, no data)
#   .dat  per texture: u16 name hash, u16 encoding, u16 width, u16 height (bit 15 = mip flag),
#         u32 size (counted from byte 12, so it includes the next field), u32 RLE escape byte
#         (0 = not packed), then the data (all mip levels, biggest first).
#   RLE   [esc][count][unit] = unit repeated count times, anything else = one literal unit.
#         unit = one DXT block (8 / 16 bytes) or 4 bytes for the 32 / 16 bit formats.
# Encodings seen in the dxt databases: 0x83f0 DXT1, 0x83f3 DXT5, 0x1401 RGBA8888, 0x8363 RGB565.
#
# The output TXDs are plain RenderWare 3.6 (GTA SA PC) texture dictionaries: DXT1 / DXT5 stay
# compressed, 8888 becomes A8R8G8B8 and 565 becomes R5G6B5. Only the biggest mip level that fits
# --cap is kept (the MP model loader uses one level). Textures are packed --per-txd MB per file
# (world_000.txd, world_001.txd ...) and world.idx lists "name file offset size" for every texture
# (and "alias name target" for affiliates), so the game can read one texture without the rest.
# RLE is undone here, so the game's own RLEDecompress is never used for these textures.
import os, re, struct, sys, glob, argparse, zlib
from PIL import Image

FMT = {0x83f0: ('DXT1', 8, True), 0x83f3: ('DXT5', 16, True), 0x1401: ('8888', 4, False), 0x8363: ('565', 2, False)}
RLE_UNIT = {0x83f0: 8, 0x83f3: 16, 0x1401: 4, 0x8363: 4}

def level_size(enc, w, h):
    _, unit, blk = FMT[enc]
    return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * unit if blk else w * h * unit

def levels(enc, w, h, mips):
    out = []
    while True:
        out.append((w, h, level_size(enc, w, h)))
        if not mips or (w == 1 and h == 1): break
        w = max(1, w // 2); h = max(1, h // 2)
    return out

def rle_decode(src, unit, esc, want):
    out = bytearray(); i = 0; n = len(src)
    while i < n and len(out) < want:
        if src[i] == esc:
            c = src[i + 1]; out += src[i + 2:i + 2 + unit] * c; i += 2 + unit
        else:
            out += src[i:i + unit]; i += unit
    return bytes(out)

def name_hash(n):
    h = 0
    for b in n.encode('latin1', 'replace'): h = (h + ((h << 5) + b)) & 0xffffffff
    return (h + (h >> 5)) & 0xffff

def read_db(d):
    txts = glob.glob(os.path.join(d, '*.txt'))
    if not txts: sys.exit('no .txt in ' + d)
    base = os.path.splitext(os.path.basename(txts[0]))[0]
    dats = [p for p in glob.glob(os.path.join(d, base + '.*.dat'))]
    if not dats: sys.exit('no %s.*.dat in %s' % (base, d))
    dat = dats[0]; toc = dat[:-4] + '.toc'
    ents = []
    for l in open(txts[0], encoding='latin1'):
        m = re.match(r'^"([^"]+)"(.*)', l.strip())
        if not m: continue
        nm = m.group(1); rest = m.group(2)
        aff = re.match(r'^affiliate=(.*)$', nm)
        if aff: ents.append(('', dict(), aff.group(1))); continue
        a = re.search(r'"affiliate=([^"]+)"', rest)
        ents.append((nm, dict(re.findall(r'(\w+)=([^\s"]+)', rest)), a.group(1) if a else None))
    f = open(dat, 'rb'); dsize = os.path.getsize(dat)
    offs = None
    if os.path.isfile(toc):
        t = open(toc, 'rb').read()
        if len(t) >= 4 and struct.unpack_from('<I', t)[0] == dsize: offs = list(struct.unpack_from('<%di' % (len(t) // 4 - 1), t, 4))
    if offs is None or len(offs) < len(ents):   # no / old .toc: walk the .dat
        offs = []; p = 0
        for nm, pr, aff in ents:
            if aff: offs.append(-1); continue
            offs.append(p); f.seek(p + 8); sz = struct.unpack('<I', f.read(4))[0]; p += 12 + sz
    return base, ents, offs, f

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src'); ap.add_argument('out')
    ap.add_argument('--cap', type=int, default=512, help='biggest width/height kept (a mip level that fits)')
    ap.add_argument('--per-txd', type=float, default=24, help='MB per output txd')
    ap.add_argument('--prefix', default='world')
    ap.add_argument('--png', type=int, default=0, help='also write the first N textures as PNG (check)')
    ap.add_argument('--keep-raw', dest='raw_ok', action='store_true', help='keep 8888 / 565 as they are (bigger)')
    a = ap.parse_args()
    base, ents, offs, f = read_db(a.src)
    os.makedirs(a.out, exist_ok=True)
    natives = []   # (name, bytes of the texture native chunk)
    stats = {'ok': 0, 'aff': 0, 'bad': 0, 'rle': 0}
    aliases = []
    for (nm, pr, aff), o in zip(ents, offs):
        if aff or o < 0:
            if nm and aff: aliases.append((nm, aff))
            stats['aff'] += 1; continue
        f.seek(o); hd = f.read(16)
        hs, enc, w, hm, sz, esc = struct.unpack('<HHHHII', hd)
        h = hm & 0x7fff
        if enc not in FMT or hs != name_hash(nm) or w == 0 or h == 0:
            stats['bad'] += 1; print('skip', nm, hex(enc), w, h); continue
        raw = f.read(sz - 4)
        lv = None
        for mips in (True, False):
            L = levels(enc, w, h, mips); want = sum(x[2] for x in L)
            data = rle_decode(raw, RLE_UNIT[enc], esc & 0xff, want) if esc else raw
            if len(data) >= want and (esc or len(data) == want): lv = L; break
        if lv is None: stats['bad'] += 1; print('size?', nm, hex(enc), w, h, len(raw)); continue
        if esc: stats['rle'] += 1
        # biggest level that fits the cap
        p = 0; k = 0
        while k + 1 < len(lv) and (lv[k][0] > a.cap or lv[k][1] > a.cap): p += lv[k][2]; k += 1
        lw, lh, ls = lv[k]; px = data[p:p + ls]
        kind = FMT[enc][0]
        if kind == '8888':   # RGBA -> BGRA (D3DFMT_A8R8G8B8)
            b = bytearray(px); b[0::4], b[2::4] = px[2::4], px[0::4]; px = bytes(b)
        # no mip level that fits (single-level textures), or a 16 / 32 bit format: decode, shrink to
        # the cap and store as DXT1 (opaque) / DXT5 (alpha) - 4 to 8 times smaller than 8888
        if a.raw_ok is False and (kind in ('8888', '565') or lw > a.cap or lh > a.cap):
            im = to_image(kind, lw, lh, px)
            if im is not None:
                im = im.convert('RGBA')
                while im.size[0] > a.cap or im.size[1] > a.cap: im = im.resize((max(1, im.size[0] // 2), max(1, im.size[1] // 2)), Image.LANCZOS)
                lw, lh = im.size
                rgba = im.tobytes()
                has_a = min(rgba[3::4]) < 250
                kind = 'DXT5' if has_a else 'DXT1'
                px = dxt_encode(rgba, lw, lh, has_a)
        alpha = kind in ('DXT5', '8888') or pr.get('alphamode', '0') != '0'
        d3d = {'DXT1': b'DXT1', 'DXT5': b'DXT5', '8888': struct.pack('<I', 21), '565': struct.pack('<I', 23)}[kind]
        rfmt = {'DXT1': 0x0200 if not alpha else 0x0100, 'DXT5': 0x0300, '8888': 0x0500, '565': 0x0200}[kind]
        depth = {'DXT1': 16, 'DXT5': 16, '8888': 32, '565': 16}[kind]
        flags = (1 if alpha else 0) | (8 if kind.startswith('DXT') else 0)
        st = struct.pack('<II', 9, 0x1106)   # platform d3d9, filter linear-mip + wrap/wrap
        st += nm.encode('latin1')[:31].ljust(32, b'\0') + b''.ljust(32, b'\0')
        st += struct.pack('<I', rfmt) + d3d + struct.pack('<HHBBBB', lw, lh, depth, 1, 4, flags)
        st += struct.pack('<I', len(px)) + px
        chunk = struct.pack('<III', 1, len(st), 0x1803FFFF) + st
        ext = struct.pack('<III', 3, 0, 0x1803FFFF)
        natives.append((nm, struct.pack('<III', 0x15, len(chunk) + len(ext), 0x1803FFFF) + chunk + ext, (kind, lw, lh, px)))
        stats['ok'] += 1
    # pack into txds
    lim = int(a.per_txd * 1024 * 1024)
    groups = [[]]; cur = 0
    for n in natives:
        if cur + len(n[1]) > lim and groups[-1]: groups.append([]); cur = 0
        groups[-1].append(n); cur += len(n[1])
    idx = ['# %s world textures (from texdb %s, cap %d) - name<TAB>file<TAB>offset<TAB>size  (offset/size = the texture native struct data)' % (a.prefix, base, a.cap)]
    files = []
    for gi, g in enumerate(groups):
        fn = '%s_%03d.txd' % (a.prefix, gi)
        body = b''; pos = []
        hdr_len = 12 + 12 + 4   # txd chunk + struct chunk + count
        for nm, ch, _ in g: pos.append((nm, hdr_len + len(body) + 24, struct.unpack_from('<I', ch, 16)[0])); body += ch
        st = struct.pack('<III', 1, 4, 0x1803FFFF) + struct.pack('<HH', len(g), 9)
        ext = struct.pack('<III', 3, 0, 0x1803FFFF)
        out = struct.pack('<III', 0x16, len(st) + len(body) + len(ext), 0x1803FFFF) + st + body + ext
        open(os.path.join(a.out, fn), 'wb').write(out)
        files.append((fn, len(out), zlib.crc32(out) & 0xffffffff))
        for nm, off, sz in pos: idx.append('%s\t%s\t%d\t%d' % (nm, fn, off, sz))
    for nm, tgt in aliases: idx.append('alias\t%s\t%s' % (nm, tgt))
    for fn, sz, crc in files: idx.insert(1, 'file\t%s\t%d\t%08x' % (fn, sz, crc))
    open(os.path.join(a.out, a.prefix + '.idx'), 'w', newline='\n').write('\n'.join(idx) + '\n')
    if a.png:
        os.makedirs(os.path.join(a.out, 'png'), exist_ok=True)
        for nm, _, (kind, lw, lh, px) in natives[:a.png]:
            im = to_image(kind, lw, lh, px)
            if im: im.save(os.path.join(a.out, 'png', nm + '.png'))
    tot = sum(x[1] for x in files)
    print('%d textures, %d affiliates, %d RLE unpacked, %d skipped -> %d txd (%.1f MB)' % (stats['ok'], len(aliases), stats['rle'], stats['bad'], len(files), tot / 1048576.0))

_dxt = None
def dxt_encode(rgba, w, h, alpha):
    global _dxt
    if _dxt is None:   # tools/dxtenc (stb_dxt), built once
        import ctypes, subprocess, tempfile
        here = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'dxtenc')
        so = os.path.join(tempfile.gettempdir(), 'mp_dxtenc.so')
        if not os.path.isfile(so) or os.path.getmtime(so) < os.path.getmtime(os.path.join(here, 'dxtenc.c')):
            subprocess.check_call(['gcc', '-O2', '-shared', '-fPIC', '-o', so, os.path.join(here, 'dxtenc.c')])
        _dxt = ctypes.CDLL(so)
    import ctypes
    out = ctypes.create_string_buffer(((w + 3) // 4) * ((h + 3) // 4) * 16)
    n = _dxt.encode(rgba, w, h, 1 if alpha else 0, out)
    return out.raw[:n]

def to_image(kind, w, h, px):
    if kind == '8888': return Image.frombytes('RGBA', (w, h), px, 'raw', 'BGRA')
    if kind == '565': return Image.frombytes('RGB', (w, h), px, 'raw', 'BGR;16')
    # DXT via a DDS header
    import io
    fourcc = b'DXT1' if kind == 'DXT1' else b'DXT5'
    hdr = b'DDS ' + struct.pack('<7I', 124, 0x1007, h, w, len(px), 0, 1) + b'\0' * 44
    hdr += struct.pack('<2I', 32, 4) + fourcc + b'\0' * 20 + struct.pack('<I', 0x1000) + b'\0' * 16
    try: return Image.open(io.BytesIO(hdr + px)).convert('RGBA')
    except Exception: return None

if __name__ == '__main__': main()
