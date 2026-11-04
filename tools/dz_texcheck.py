#!/usr/bin/env python3
"""DayZ model pack texture check (4.02).

For every model in Host/DayZ/models/manifest.txt it lists:
  - textures the DFF asks for that are NOT in its own TXD (they come from the parent / GTA texture DB),
  - TXD texture names that ALSO exist in the stock mobile texture DBs (gta3 / txd from the
    "txd+gta3.zip" release asset). Before 4.02 the mobile DB lookup could win for those names, so the
    model showed the GTA texture (or white) instead of the MTA one.
Usage: tools/dz_texcheck.py <models dir> <texdb dir with gta3/gta3.txt + txd/txd.txt>
"""
import os, re, struct, sys

def db_names(root):
    s = set()
    for sub in ("gta3/gta3.txt", "txd/txd.txt"):
        p = os.path.join(root, sub)
        if os.path.exists(p):
            for ln in open(p, encoding="latin-1"):
                m = re.match(r'\s*"([^"]+)"', ln)
                if m: s.add(m.group(1).lower())
    return s

def chunks(d, o, end):
    while o + 12 <= end:
        t, sz, _ = struct.unpack_from("<III", d, o)
        yield t, o + 12, min(o + 12 + sz, end)
        o += 12 + sz

def txd_names(path):
    d = open(path, "rb").read(); out = []
    for t, a, b in chunks(d, 0, len(d)):
        if t != 0x16: continue
        for t2, a2, b2 in chunks(d, a, b):
            if t2 == 0x15:
                for t3, a3, b3 in chunks(d, a2, b2):
                    if t3 == 1 and b3 - a3 >= 40:
                        out.append(d[a3 + 8:a3 + 40].split(b"\0")[0].decode("latin-1"))
                    break
    return out

def dff_names(path):
    d = open(path, "rb").read(); out = set()
    def walk(a, b, depth):
        for t, x, y in chunks(d, a, b):
            if t == 0x06:   # texture: struct, string(name), string(mask)
                strs = [d[x2:y2].split(b"\0")[0].decode("latin-1") for t2, x2, y2 in chunks(d, x, y) if t2 == 2]
                if strs: out.add(strs[0])
            elif t in (0x10, 0x1A, 0x0E, 0x08, 0x07, 0x0F, 0x14) and depth < 8:
                walk(x, y, depth + 1)
    walk(0, len(d), 0)
    return out

def main():
    mdir, tdb = sys.argv[1], sys.argv[2]
    db = db_names(tdb); print(f"texture DB names: {len(db)}")
    tot_c = tot_m = 0
    for ln in open(os.path.join(mdir, "manifest.txt")):
        if ln.startswith("#") or not ln.strip(): continue
        f = ln.split(); mid, kind, dff, txd = f[0], f[1], f[2], f[3]
        try: tn = txd_names(os.path.join(mdir, txd)); dn = dff_names(os.path.join(mdir, dff))
        except OSError as e: print(mid, "missing", e); continue
        tl = {n.lower() for n in tn}
        coll = sorted(n for n in tn if n.lower() in db)
        miss = sorted(n for n in dn if n.lower() not in tl)
        tot_c += len(coll); tot_m += len(miss)
        if coll or miss:
            print(f"{mid:>5} {kind} txd={len(tn):3} dff={len(dn):3}  DB-collide={coll[:6]}{'...' if len(coll)>6 else ''}  not-in-txd={miss[:6]}")
    print(f"total: {tot_c} TXD names also in the GTA DB, {tot_m} DFF textures not in their own TXD")

if __name__ == "__main__": main()
