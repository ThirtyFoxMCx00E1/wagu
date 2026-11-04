#!/usr/bin/env python3
"""4.03: country flag atlas for the server list.
   git clone --depth 1 https://github.com/lipis/flag-icons.git /tmp/flag-icons   (MIT licence)
   pip install cairosvg pillow
   python3 tools/make_flags.py [/tmp/flag-icons]
Writes assets/MP/flags.png (1024x1024, 64x48 cells, 16 per row, slot 0 = unknown, slot i = kCountries[i - 1])
and src/ui/flags_data.h. Then run tools/make_server_bgs.py --reuse to put flags.png into bg_server.jar."""
import json, io, os, sys
import cairosvg
from PIL import Image, ImageDraw

SRC = sys.argv[1] if len(sys.argv) > 1 else "/tmp/flag-icons"
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
d = json.load(open(os.path.join(SRC, "country.json")))
extra = ["xk", "gb-eng", "gb-sct", "gb-wls", "eu", "un"]
lst = sorted([x for x in d if x.get("iso")], key=lambda x: x["name"])
lst += [x for c in extra for x in d if x["code"] == c]
CW, CH, COLS = 64, 48, 16
atlas = Image.new("RGBA", (1024, 1024), (0, 0, 0, 0))
u = Image.new("RGBA", (CW, CH), (90, 95, 102, 255))
dr = ImageDraw.Draw(u); dr.rectangle([0, 0, CW - 1, CH - 1], outline=(140, 145, 150, 255)); dr.text((CW // 2 - 3, CH // 2 - 6), "?", fill=(220, 220, 220, 255))
atlas.paste(u, (0, 0))
for i, x in enumerate(lst, 1):
    png = cairosvg.svg2png(url=os.path.join(SRC, x["flag_4x3"]), output_width=CW * 2, output_height=CH * 2)
    im = Image.open(io.BytesIO(png)).convert("RGBA").resize((CW, CH), Image.LANCZOS)
    atlas.paste(im, ((i % COLS) * CW, (i // COLS) * CH))
atlas.save(os.path.join(ROOT, "assets", "MP", "flags.png"), optimize=True)
with open(os.path.join(ROOT, "src", "ui", "flags_data.h"), "w") as f:
    f.write("// 4.03: country flags - assets/MP/flags.png is a 1024x1024 atlas of 64x48 cells, 16 per row,\n"
            "// slot 0 = unknown, slot i = kCountries[i - 1]. Flags: lipis/flag-icons (MIT), made by tools/make_flags.py.\n"
            "#pragma once\nstruct MpCountry { const char* code; const char* name; };\nstatic const MpCountry kCountries[] = {\n")
    for x in lst:
        f.write('    { "%s", "%s" },\n' % (x["code"], x["name"].replace('"', "'")))
    f.write("};\nstatic const int kCountryCount = (int)(sizeof(kCountries) / sizeof(kCountries[0]));\n"
            "#define MP_FLAG_CELL_W 64\n#define MP_FLAG_CELL_H 48\n#define MP_FLAG_COLS 16\n#define MP_FLAG_ATLAS 1024\n")
print(len(lst), "flags")
