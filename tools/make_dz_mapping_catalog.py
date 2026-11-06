#!/usr/bin/env python3
"""4.10: Project Day Z Legacy dynamic mapping - the model catalog.

Builds Host/DayZ/mapping/catalog.txt: every GTA San Andreas object model (+ the SA-MP 0.3.7 / 0.3.DL
objects) with its id, dff, txd, the IDE it comes from, draw distance and search tags.

Sources (the same ones listed for the mapping feature):
  * IDE files: github.com/Fernando-A-Rocha/mta-ide-search (ide-search/files/*.ide, incl. SAMP.ide)
  * tags / descriptions: github.com/Dryxio/gta-scout (data/annotations/sa-*.json, MIT)
  * id lookups for players: dev.prineside.com/gtasa_samp_model_id/, gtastuff.com/models/ (links only)

Line format (space separated, one model per line):
  id dff txd ide drawdist flags kind tags
    kind  sa   = in the phone's GTA (gta3.img / gta_int.img) - always works
          samp = a SA-MP object (samp.img) - works only when its dff/txd is in the Day Z model pack
    tags  comma separated (gta-scout), "-" when none

Usage:  python3 tools/make_dz_mapping_catalog.py [ide_dir] [gta_scout_json]
        (no arguments: downloads both from GitHub)
"""
import json, os, re, sys, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "Host", "DayZ", "mapping", "catalog.txt")
IDE_API = "https://api.github.com/repos/Fernando-A-Rocha/mta-ide-search/contents/ide-search/files"
IDE_RAW = "https://raw.githubusercontent.com/Fernando-A-Rocha/mta-ide-search/HEAD/ide-search/files/"
SCOUT = "https://raw.githubusercontent.com/Dryxio/gta-scout/HEAD/data/annotations/sa-2026-10-03.json"


def fetch(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "MP-catalog"}), timeout=60) as r:
        return r.read()


def load_ides(ide_dir):
    files = {}
    if ide_dir:
        for n in sorted(os.listdir(ide_dir)):
            if n.lower().endswith(".ide"):
                files[n] = open(os.path.join(ide_dir, n), "rb").read().decode("latin-1")
    else:
        for e in json.loads(fetch(IDE_API)):
            if e["name"].lower().endswith(".ide"):
                files[e["name"]] = fetch(IDE_RAW + e["name"]).decode("latin-1")
    return files


def parse_ide(name, text, out):
    sect = None
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        low = line.lower()
        if low in ("objs", "tobj", "anim", "weap", "cars", "peds", "path", "2dfx", "txdp", "hier"):
            sect = low
            continue
        if low == "end":
            sect = None
            continue
        if sect not in ("objs", "tobj", "anim"):
            continue
        p = [x.strip() for x in line.split(",")]
        if len(p) < 4 or not p[0].isdigit():
            continue
        mid, dff, txd = int(p[0]), p[1], p[2]
        if sect == "anim":   # id, model, txd, anim, drawdist, flags
            dd, fl = (p[4] if len(p) > 4 else "0"), (p[5] if len(p) > 5 else "0")
        elif len(p) >= 5 and len(p) <= 6:   # id, model, txd, drawdist, flags (+ times for tobj)
            dd, fl = p[3], p[4]
        else:                # old type: id, model, txd, meshCount, dd..., flags
            dd, fl = p[4] if len(p) > 4 else "0", p[-1]
            if sect == "tobj" and len(p) > 6:
                fl = p[-3]
        try:
            dd = "%g" % float(dd)
        except ValueError:
            dd = "0"
        kind = "samp" if name.lower() == "samp.ide" else "sa"
        out.setdefault(mid, (dff, txd, os.path.splitext(name)[0], dd, re.sub(r"[^0-9]", "", fl) or "0", kind))


def load_tags(path):
    try:
        data = json.load(open(path, encoding="utf-8")) if path else json.loads(fetch(SCOUT))
    except Exception as e:  # tags are optional
        print("gta-scout tags skipped:", e)
        return {}
    tags = {}
    for e in data.get("entries", []):
        sel = e.get("selector", {})
        dff = (sel.get("dff") or sel.get("name") or "").lower()
        if dff and e.get("tags"):
            tags.setdefault(dff, [])
            for t in e["tags"]:
                t = re.sub(r"[^a-z0-9_-]", "", t.lower().replace(" ", "_"))
                if t and t not in tags[dff]:
                    tags[dff].append(t)
    return tags


def main():
    ide_dir = sys.argv[1] if len(sys.argv) > 1 else None
    scout = sys.argv[2] if len(sys.argv) > 2 else None
    models = {}
    for n, t in load_ides(ide_dir).items():
        parse_ide(n, t, models)
    tags = load_tags(scout)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", newline="\n") as f:
        f.write("# Project Day Z Legacy - dynamic mapping catalog (MP 4.10, tools/make_dz_mapping_catalog.py)\n")
        f.write("# IDE: github.com/Fernando-A-Rocha/mta-ide-search  tags: github.com/Dryxio/gta-scout (MIT)\n")
        f.write("# look models up: dev.prineside.com/gtasa_samp_model_id/  gtastuff.com/models/  gtastuff.com/viewer/\n")
        f.write("# id dff txd ide drawdist flags kind tags\n")
        for mid in sorted(models):
            dff, txd, ide, dd, fl, kind = models[mid]
            tg = ",".join(tags.get(dff.lower(), [])[:6]) or "-"
            f.write("%d %s %s %s %s %s %s %s\n" % (mid, dff, txd, ide, dd, fl, kind, tg))
    sa = sum(1 for m in models.values() if m[5] == "sa")
    print("%s: %d models (%d GTA SA, %d SA-MP), %d with tags" % (OUT, len(models), sa, len(models) - sa,
          sum(1 for m in models.values() if m[0].lower() in tags)))


if __name__ == "__main__":
    main()
