#!/usr/bin/env python3
"""4.03: server list backgrounds icon_1 .. icon_250 (CS:GO style rows).

Real photos from Wikimedia Commons "Quality images" (CC0 / public domain / CC BY / CC BY-SA), no AI:
25 themes like the CS:GO maps (desert town, Italian village, nuclear plant, rail yard ...) x 10 photos.
Each one is cropped to 2:1 and saved 512x256 JPEG with a light "game screenshot" grade
(a bit less saturation, a bit more contrast). Writes assets/MP/bg_server.jar (+ Host/Assets/bg_server.jar)
and assets/MP/ICONS_CREDITS.txt with author, license and source page for every picture.

  python3 tools/make_server_bgs.py            # download + build
  python3 tools/make_server_bgs.py --reuse    # rebuild the jar from tools/bg_cache
"""
import io, json, os, re, sys, time, zipfile, urllib.parse, urllib.request, html
from concurrent.futures import ThreadPoolExecutor
from PIL import Image, ImageEnhance, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(ROOT, "tools", "bg_cache")
UA = "MPLauncherBackgrounds/4.03 (https://github.com/ThirtyFoxMCx00E1/wagu)"
W, H = 512, 256
OK_LIC = re.compile(r"^(CC0|Public domain|PD|CC BY(-SA)? [1-4]\.0|CC BY(-SA)? 2\.5|CC BY(-SA)? 3\.0.*)$", re.I)

THEMES = [  # (label, search words) - 10 photos each
    ("dust",        "desert village"),
    ("inferno",     "Italian village narrow street"),
    ("nuke",        "nuclear power plant"),
    ("train",       "railway yard freight wagons"),
    ("mirage",      "medina Morocco street"),
    ("overpass",    "canal bridge city"),
    ("pripyat",     "Pripyat abandoned"),
    ("vertigo",     "skyscraper construction site crane"),
    ("office",      "snow covered street winter town"),
    ("ancient",     "Maya ruins temple"),
    ("anubis",      "Egyptian temple columns"),
    ("aztec",       "pyramid Teotihuacan"),
    ("cobblestone", "castle courtyard"),
    ("militia",     "farmhouse barn field"),
    ("warehouse",   "industrial building"),
    ("harbor",      "container terminal cranes port"),
    ("airport",     "airport apron aircraft"),
    ("highway",     "highway interchange"),
    ("downtown",    "downtown street night city"),
    ("beach",       "beach promenade palm"),
    ("forest",      "forest road"),
    ("mountain",    "mountain pass road"),
    ("military",    "military vehicles tank museum"),
    ("canyon",      "desert canyon road"),
    ("favela",      "hillside village"),
]
FEATURED = {  # extra backgrounds for the featured rows (not in the 250)
    "bg_default.jpg": "Las Vegas Strip night",
    "bg_bcw.jpg":     "abandoned factory ruins",
    "bg_mw3.jpg":     "military exercise soldiers desert",
    "bg_rp.jpg":      "futuristic city skyline night",
    "bg_dz.jpg":      "abandoned village overgrown",
}


def api(params):
    params = dict(params, format="json")
    url = "https://commons.wikimedia.org/w/api.php?" + urllib.parse.urlencode(params)
    for t in range(5):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": UA})
            return json.load(urllib.request.urlopen(req, timeout=40))
        except Exception as e:
            time.sleep(2 + t * 3)
    return {}


def strip(s):
    return html.unescape(re.sub(r"<[^>]+>", "", s or "")).strip()


def search(words, want, used, offset=0):
    """Landscape quality photos with a free licence, not used yet."""
    out = []
    for cat in ("Quality_images", "Featured_pictures_on_Wikimedia_Commons", None):
        q = words + " filetype:bitmap" + (f" incategory:{cat}" if cat else "")
        d = api({"action": "query", "generator": "search", "gsrsearch": q, "gsrnamespace": 6, "gsrlimit": 50,
                 "gsroffset": offset, "prop": "imageinfo", "iiprop": "url|size|extmetadata|mime", "iiurlwidth": 900})
        pages = sorted(d.get("query", {}).get("pages", {}).values(), key=lambda p: p.get("index", 0))
        for p in pages:
            ii = (p.get("imageinfo") or [{}])[0]
            if ii.get("mime") not in ("image/jpeg", "image/png"):
                continue
            w, h = ii.get("width", 0), ii.get("height", 0)
            if w < 1200 or h < 600 or w / max(h, 1) < 1.3:
                continue
            m = ii.get("extmetadata", {})
            lic = strip(m.get("LicenseShortName", {}).get("value"))
            if not OK_LIC.match(lic):
                continue
            t = p["title"]
            if t in used:
                continue
            used.add(t)
            out.append({"title": t, "thumb": ii.get("thumburl"), "page": ii.get("descriptionurl"),
                        "author": strip(m.get("Artist", {}).get("value"))[:80] or "unknown", "license": lic})
            if len(out) >= want:
                return out
    return out


def grade(im):
    im = im.convert("RGB")
    w, h = im.size
    tw = w
    th = int(w / 2)
    if th > h:
        th = h; tw = h * 2
    x = (w - tw) // 2; y = int((h - th) * 0.55)
    im = im.crop((x, y, x + tw, y + th)).resize((W, H), Image.LANCZOS)
    im = ImageEnhance.Color(im).enhance(0.86)
    im = ImageEnhance.Contrast(im).enhance(1.08)
    im = im.filter(ImageFilter.UnsharpMask(radius=1.2, percent=40, threshold=2))
    return im


def fetch(item, name):
    path = os.path.join(CACHE, name)
    if os.path.exists(path):
        return True
    for t in range(4):
        try:
            req = urllib.request.Request(item["thumb"], headers={"User-Agent": UA})
            data = urllib.request.urlopen(req, timeout=60).read()
            grade(Image.open(io.BytesIO(data))).save(path, "JPEG", quality=84, optimize=True, progressive=False)
            return True
        except Exception as e:
            print("  fetch fail", name, repr(e)[:120], flush=True)
            time.sleep(2 + t * 3)
    return False


def main():
    os.makedirs(CACHE, exist_ok=True)
    meta_path = os.path.join(CACHE, "meta.json")
    meta = json.load(open(meta_path)) if os.path.exists(meta_path) else {}
    if "--reuse" not in sys.argv:
        used = {v["title"] for v in meta.values()}
        n = 1
        for label, words in THEMES:
            have = [k for k, v in meta.items() if v.get("theme") == label]
            need = 10 - len(have)
            got = search(words, need + 6, used) if need > 0 else []
            if need > 0: print('  found', label, len(got), flush=True)
            for it in got:
                while f"icon_{n}.jpg" in meta:
                    n += 1
                if need <= 0:
                    break
                it["theme"] = label
                if fetch(it, f"icon_{n}.jpg"):
                    meta[f"icon_{n}.jpg"] = it; need -= 1; n += 1
            print(label, "missing" if need > 0 else "ok", need)
            json.dump(meta, open(meta_path, "w"), indent=1)
        for fn, words in FEATURED.items():
            if fn in meta:
                continue
            got = search(words, 3, used)
            for it in got:
                it["theme"] = "featured"
                if fetch(it, fn):
                    meta[fn] = it; break
        json.dump(meta, open(meta_path, "w"), indent=1)
    names = [f"icon_{i}.jpg" for i in range(1, 251)]
    missing = [x for x in names if x not in meta]
    if missing:
        print("missing", missing[:10], len(missing)); sys.exit(1)
    # 4.04: own pack files/MP/bg_server.jar, entries bg_1.jpg .. bg_250.jpg (+ bg_default/bcw/mw3/rp/dz.jpg,
    # flags.png). Icons.jar stays the old icon_N.png pack (fallback).
    for dst in (os.path.join(ROOT, "assets", "MP", "bg_server.jar"), os.path.join(ROOT, "Host", "Assets", "bg_server.jar")):
        with zipfile.ZipFile(dst, "w", zipfile.ZIP_STORED) as z:   # JPEG: stored, not deflated
            for fn in names + sorted(FEATURED):
                if fn in meta:
                    z.write(os.path.join(CACHE, fn), fn.replace("icon_", "bg_"))
            flags = os.path.join(ROOT, "assets", "MP", "flags.png")   # 4.03: country flag atlas (tools/make_flags.py)
            if os.path.exists(flags):
                z.write(flags, "flags.png")
        print(dst, os.path.getsize(dst))
    with open(os.path.join(ROOT, "assets", "MP", "ICONS_CREDITS.txt"), "w", encoding="utf-8") as f:
        f.write("MP server list backgrounds (4.03): real photos from Wikimedia Commons (https://commons.wikimedia.org),\n"
                "each under its own free license (CC0 / public domain / CC BY / CC BY-SA). Cropped to 512x256 (2:1),\n"
                "slightly less saturation + more contrast. Author, license and source page for every picture:\n\n")
        for fn in names + sorted(FEATURED):
            v = meta.get(fn)
            if v:
                f.write(f"{fn.replace('icon_', 'bg_'):<18} {v['theme']:<12} {v['title'][5:]} | by {v['author']} | {v['license']} | {v['page']}\n")


if __name__ == "__main__":
    main()
