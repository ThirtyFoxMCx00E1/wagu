#!/usr/bin/env python3
"""3.61: Day Z model pack hosting (Project Day Z Legacy).

Copies every dff / txd / col of MTA DayZ (github.com/NullSystemWorks/mtadayz, DayZ/models) into
Host/DayZ/models of this repo and writes Host/DayZ/models/manifest.txt, the list the game reads:

    <model id> <kind> <dff path> <txd path> <dff bytes> <txd bytes> <dff crc32> <txd crc32>

kind: p = player skin, z = zombie, w = weapon, o = object / item, v = vehicle (texture size cap).
Paths are relative to the manifest. The game downloads the files it needs from
https://mpsamp-open.github.io/Host/DayZ/models/ (fallback: the
mtadayz fork) into its own cache - nothing goes into the MP folder.

The id mapping is read from MTA DayZ's DayZ/models/loadskins.lua (engineImportTXD /
engineReplaceModel), plus one fixed DayZ weapon per GTA gun model (MTA swaps them per weapon in
weaponswitch.lua; our Day Z has one model per gun). Buildings and animals are hosted but not
listed (they would change the GTA map / animals are not in our Day Z).

usage: python3 tools/make_dz_models.py <mtadayz checkout>/DayZ/models
"""
import os, re, shutil, sys, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEST = os.path.join(ROOT, 'Host', 'DayZ', 'models')

# GTA gun model -> MTA DayZ weapon file (weapons/dff + weapons/txd)
WEAPONS = {
    335: 'knife', 339: 'hatchet', 346: 'm1911', 348: 'revolver', 349: 'winchester', 351: 'remington',
    352: 'pdw', 353: 'mp5a5', 355: 'ak74', 356: 'm4a1', 357: 'enfield', 358: 'm24', 367: 'binoculars',
}
# files named in loadskins.lua that are not in the repo
RENAME = {'players/dff/standard_female.dff': 'players/dff/survivor_female.dff',
          'players/txd/standard_female.txd': 'players/txd/survivor_female.txd'}


def kind_of(path, mid):
    if path.startswith('players/'): return 'p'
    if path.startswith('zombies/'): return 'z'
    if path.startswith('weapons/'): return 'w'
    if path.startswith('vehicles/'): return 'v'
    return 'o'


def parse_loadskins(src):
    lua = open(os.path.join(src, 'loadskins.lua'), encoding='latin-1').read().replace('\r', '')
    var = {}
    txd, dff = {}, {}
    for line in lua.split('\n'):
        line = line.split('--')[0].strip()
        m = re.match(r'(?:local\s+)?(\w+)\s*=\s*engineLoad(TXD|DFF)\(\s*"[^"]*DayZ/models/([^"]+)"', line)
        if m:
            var[m.group(1)] = m.group(3); continue
        m = re.match(r'engineImportTXD\(\s*(\w+)\s*,\s*(\d+)\s*\)', line)
        if m and m.group(1) in var: txd[int(m.group(2))] = var[m.group(1)]; continue
        m = re.match(r'engineReplaceModel\(\s*(\w+)\s*,\s*(\d+)\s*\)', line)
        if m and m.group(1) in var: dff[int(m.group(2))] = var[m.group(1)]
    return txd, dff


def crc(path):
    return zlib.crc32(open(path, 'rb').read()) & 0xffffffff


def main():
    if len(sys.argv) < 2: print(__doc__); return 1
    src = sys.argv[1]
    # 1) mirror everything (the whole MTA model set is hosted)
    n = 0
    for d, _, files in os.walk(src):
        for f in files:
            if not f.lower().endswith(('.dff', '.txd', '.col')): continue
            rel = os.path.relpath(os.path.join(d, f), src)
            out = os.path.join(DEST, rel)
            os.makedirs(os.path.dirname(out), exist_ok=True)
            shutil.copyfile(os.path.join(d, f), out); n += 1
    # 2) the list the game uses
    txd, dff = parse_loadskins(src)
    for mid, name in WEAPONS.items():
        dff[mid] = 'weapons/dff/%s.dff' % name; txd[mid] = 'weapons/txd/%s.txd' % name
    rows = []
    for mid in sorted(set(txd) & set(dff)):
        dp, tp = RENAME.get(dff[mid], dff[mid]), RENAME.get(txd[mid], txd[mid])
        if dp.startswith(('buildings/', 'animals/')): continue
        fd, ft = os.path.join(DEST, dp), os.path.join(DEST, tp)
        if not (os.path.isfile(fd) and os.path.isfile(ft)):
            print('skip %d: missing %s / %s' % (mid, dp, tp)); continue
        rows.append('%d %s %s %s %d %d %08x %08x' % (mid, kind_of(dp, mid), dp, tp, os.path.getsize(fd),
                                                    os.path.getsize(ft), crc(fd), crc(ft)))
    with open(os.path.join(DEST, 'manifest.txt'), 'w', newline='\n') as f:
        f.write('# Project Day Z Legacy model pack (MTA DayZ models, github.com/NullSystemWorks/mtadayz)\n')
        f.write('# id kind dff txd dffBytes txdBytes dffCrc txdCrc\n')
        f.write('\n'.join(rows) + '\n')
    print('%d files hosted, %d models in manifest.txt' % (n, len(rows)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
