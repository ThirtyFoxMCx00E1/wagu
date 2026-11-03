#!/usr/bin/env python3
"""3.58b: Project Day Z textures (built into the .so with tools/embed_asset.py):
  dz_inv.png   - the grungy dark inventory panel (concrete / worn metal, like the real DayZ inventory)
  dz_bag.png   - the bag panel (the same grunge + a faint backpack silhouette)
  dz_items.png - item icons, 8 x 8 cells of 96 px, cell = item id (src/net/dayz.h)
  dz_icon.png  - the server icon (launcher + website): the hand print badge, yellow
  python3 tools/dz_textures.py [out-dir]   (default assets/MP)"""
import os, sys, math, random
from PIL import Image, ImageDraw, ImageFilter, ImageChops

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', 'assets', 'MP')
os.makedirs(OUT, exist_ok=True)
R = random.Random(358)

def grunge(w, h, base=(30, 31, 30), alpha=236, seed=1):
    rnd = random.Random(seed)
    img = Image.new('RGB', (w, h), base)
    # blotches
    lay = Image.new('L', (w // 8, h // 8))
    lay.putdata([rnd.randint(0, 255) for _ in range((w // 8) * (h // 8))])
    lay = lay.resize((w, h), Image.BICUBIC).filter(ImageFilter.GaussianBlur(6))
    lay2 = Image.new('L', (w // 2, h // 2)); lay2.putdata([rnd.randint(0, 255) for _ in range((w // 2) * (h // 2))])
    lay2 = lay2.resize((w, h), Image.BILINEAR)
    px = img.load(); a = lay.load(); b = lay2.load()
    for y in range(h):
        for x in range(w):
            v = (a[x, y] - 128) * 0.16 + (b[x, y] - 128) * 0.07
            r, g, bb = base
            px[x, y] = (max(0, min(255, int(r + v))), max(0, min(255, int(g + v + 1))), max(0, min(255, int(bb + v - 1))))
    d = ImageDraw.Draw(img)
    # scratches + stains
    for _ in range(int(w * h / 2600)):
        x, y = rnd.randint(0, w), rnd.randint(0, h); ang = rnd.uniform(0, math.pi); ln = rnd.randint(6, 40)
        c = rnd.randint(48, 70)
        d.line([(x, y), (x + math.cos(ang) * ln, y + math.sin(ang) * ln)], fill=(c, c, c - 2), width=1)
    for _ in range(int(w * h / 18000)):
        x, y, r = rnd.randint(0, w), rnd.randint(0, h), rnd.randint(8, 34)
        c = rnd.randint(14, 22)
        d.ellipse([x - r, y - r, x + r, y + r * 0.7], fill=(c, c + 1, c))
    img = img.filter(ImageFilter.GaussianBlur(0.6))
    # dark vignette + worn border
    vig = Image.new('L', (w, h), 0); vd = ImageDraw.Draw(vig)
    for i in range(24):
        vd.rectangle([i, i, w - 1 - i, h - 1 - i], outline=int(150 - i * 6))
    img = ImageChops.subtract(img, Image.merge('RGB', [vig] * 3).point(lambda v: v // 3))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, w - 1, h - 1], outline=(70, 70, 66), width=2)
    d.rectangle([3, 3, w - 4, h - 4], outline=(16, 16, 16), width=1)
    out = img.convert('RGBA'); out.putalpha(alpha)
    return out

def backpack_silhouette(img):
    w, h = img.size
    sil = Image.new('L', (w, h), 0); d = ImageDraw.Draw(sil)
    cx, cy, bw, bh = w * 0.5, h * 0.52, w * 0.62, h * 0.58
    d.rounded_rectangle([cx - bw / 2, cy - bh / 2, cx + bw / 2, cy + bh / 2], radius=int(w * 0.12), fill=255)
    d.rounded_rectangle([cx - bw * 0.36, cy - bh / 2 - h * 0.06, cx + bw * 0.36, cy - bh * 0.3], radius=int(w * 0.08), fill=255)
    d.rounded_rectangle([cx - bw * 0.4, cy + bh * 0.08, cx + bw * 0.4, cy + bh * 0.42], radius=int(w * 0.05), fill=200)
    d.arc([cx - bw * 0.22, cy - bh / 2 - h * 0.12, cx + bw * 0.22, cy - bh / 2 + h * 0.02], 180, 360, fill=255, width=int(w * 0.03))
    sil = sil.filter(ImageFilter.GaussianBlur(5)).point(lambda v: v * 52 // 255)
    lift = Image.new('RGBA', (w, h), (120, 124, 110, 0)); lift.putalpha(sil)
    return Image.alpha_composite(img, lift)

# ---------------------------------------------------------------- item icons
S = 4          # supersampling
C = 96         # cell
def cell():
    im = Image.new('RGBA', (C * S, C * S), (0, 0, 0, 0)); return im, ImageDraw.Draw(im)
def P(pts): return [(x * S, y * S) for x, y in pts]
def rect(d, x0, y0, x1, y1, fill, r=0, outline=None, w=1):
    if r: d.rounded_rectangle([x0 * S, y0 * S, x1 * S, y1 * S], radius=r * S, fill=fill, outline=outline, width=w * S)
    else: d.rectangle([x0 * S, y0 * S, x1 * S, y1 * S], fill=fill, outline=outline, width=w * S)
def ell(d, x0, y0, x1, y1, fill, outline=None, w=1): d.ellipse([x0 * S, y0 * S, x1 * S, y1 * S], fill=fill, outline=outline, width=w * S)
def poly(d, pts, fill, outline=None): d.polygon(P(pts), fill=fill, outline=outline)
def line(d, pts, fill, w=2): d.line(P(pts), fill=fill, width=int(w * S))

STEEL, DARK, WOOD, BLACK = (150, 152, 150, 255), (58, 60, 58, 255), (128, 84, 46, 255), (34, 34, 34, 255)
OLIVE, TAN, GOLD = (92, 104, 58, 255), (176, 152, 102, 255), (222, 176, 52, 255)

def gun(d, kind, body=DARK, stock=WOOD, gold=False):
    if gold: body, stock = GOLD, (180, 130, 30, 255)
    if kind == 'pistol':
        poly(d, [(28, 36), (70, 36), (70, 46), (44, 46), (42, 64), (30, 64), (32, 46), (28, 46)], body)
        rect(d, 30, 36, 70, 40, STEEL); rect(d, 33, 50, 40, 62, stock)
    elif kind == 'deagle':
        poly(d, [(22, 34), (76, 34), (76, 46), (44, 46), (42, 66), (28, 66), (30, 46), (22, 46)], (120, 122, 120, 255))
        rect(d, 24, 34, 76, 39, (190, 190, 186, 255)); rect(d, 31, 50, 39, 64, BLACK)
    elif kind == 'shotgun':
        rect(d, 8, 42, 30, 50, stock); poly(d, [(8, 42), (2, 52), (12, 54), (22, 48)], stock)
        rect(d, 28, 40, 54, 50, body); rect(d, 50, 40, 92, 44, STEEL); rect(d, 54, 46, 80, 50, stock)
    elif kind == 'spas':
        poly(d, [(4, 44), (26, 40), (28, 52), (8, 56)], BLACK); rect(d, 26, 38, 58, 52, body)
        rect(d, 54, 38, 92, 43, STEEL); rect(d, 56, 45, 84, 51, BLACK); rect(d, 36, 52, 42, 62, BLACK)
    elif kind == 'uzi':
        rect(d, 26, 36, 68, 48, body); rect(d, 66, 40, 76, 44, STEEL); rect(d, 42, 48, 50, 72, BLACK); rect(d, 30, 48, 36, 58, BLACK)
    elif kind == 'tec9':
        rect(d, 28, 38, 66, 47, body); rect(d, 62, 40, 80, 44, STEEL); rect(d, 40, 47, 47, 72, BLACK); poly(d, [(30, 47), (37, 47), (34, 60), (28, 60)], BLACK)
    elif kind == 'mp5':
        rect(d, 10, 40, 24, 46, BLACK); rect(d, 22, 36, 66, 48, body); rect(d, 64, 39, 84, 45, BLACK)
        poly(d, [(44, 48), (52, 48), (58, 68), (50, 70)], BLACK); rect(d, 32, 48, 38, 60, BLACK)
    elif kind == 'ak':
        poly(d, [(2, 42), (24, 38), (26, 50), (6, 56)], stock); rect(d, 24, 37, 60, 48, body)
        rect(d, 58, 39, 72, 46, stock); rect(d, 70, 40, 94, 43, body)
        poly(d, [(44, 48), (52, 48), (60, 70), (52, 73)], body if not gold else (200, 150, 40, 255)); rect(d, 32, 48, 38, 60, stock)
    elif kind == 'm4':
        poly(d, [(4, 40), (22, 38), (22, 50), (6, 54)], BLACK); rect(d, 22, 36, 62, 48, body)
        rect(d, 34, 32, 52, 36, BLACK); rect(d, 60, 38, 80, 46, BLACK); rect(d, 78, 40, 94, 43, body)
        poly(d, [(44, 48), (51, 48), (54, 68), (47, 69)], BLACK); rect(d, 30, 48, 36, 60, BLACK)
    elif kind == 'rifle':
        poly(d, [(2, 44), (34, 40), (36, 50), (8, 56)], stock); rect(d, 32, 40, 62, 48, body); rect(d, 58, 41, 94, 44, STEEL); rect(d, 40, 48, 46, 56, stock)
    elif kind == 'sniper':
        poly(d, [(2, 44), (30, 40), (32, 50), (6, 57)], OLIVE); rect(d, 28, 40, 60, 49, BLACK); rect(d, 58, 42, 95, 45, BLACK)
        rect(d, 34, 30, 60, 36, BLACK, 2); ell(d, 32, 29, 38, 37, DARK); ell(d, 56, 28, 63, 38, DARK); rect(d, 44, 36, 48, 40, BLACK)
        rect(d, 38, 49, 44, 58, OLIVE)
    elif kind == 'knife':
        poly(d, [(18, 56), (66, 40), (74, 40), (70, 46), (24, 62)], (200, 202, 205, 255)); line(d, [(24, 58), (68, 43)], (240, 240, 240, 255), 1)
        rect(d, 72, 37, 78, 49, BLACK); poly(d, [(76, 40), (92, 36), (94, 42), (78, 47)], WOOD)
    elif kind == 'katana':
        line(d, [(6, 72), (70, 26)], (215, 218, 222, 255), 4); line(d, [(8, 70), (68, 28)], (250, 250, 250, 255), 1)
        ell(d, 66, 20, 78, 32, GOLD); line(d, [(74, 25), (92, 12)], BLACK, 6)

def draw_item(i):
    im, d = cell()
    if i == 1: gun(d, 'knife')
    elif i == 2: gun(d, 'katana')
    elif i == 3: gun(d, 'pistol')
    elif i == 4: gun(d, 'deagle')
    elif i == 5: gun(d, 'shotgun')
    elif i == 6: gun(d, 'spas')
    elif i == 7: gun(d, 'uzi')
    elif i == 8: gun(d, 'tec9')
    elif i == 9: gun(d, 'mp5')
    elif i == 10: gun(d, 'ak')
    elif i == 11: gun(d, 'm4')
    elif i == 12: gun(d, 'rifle')
    elif i == 13: gun(d, 'sniper')
    elif i == 14: gun(d, 'ak', gold=True)
    elif i == 15:   # pistol magazine
        poly(d, [(40, 20), (54, 20), (58, 76), (44, 76)], BLACK); rect(d, 41, 20, 53, 25, GOLD); rect(d, 43, 72, 59, 78, DARK)
    elif i == 16:   # shotgun shells
        for k in range(3):
            x = 28 + k * 14; rect(d, x, 30, x + 11, 66, (180, 30, 30, 255), 2); rect(d, x, 60, x + 11, 70, GOLD, 1)
    elif i == 17:   # SMG mag
        poly(d, [(40, 16), (54, 16), (56, 80), (42, 80)], BLACK); rect(d, 42, 16, 52, 21, GOLD)
    elif i == 18:   # STANAG
        poly(d, [(36, 16), (54, 16), (62, 78), (44, 82)], (90, 92, 88, 255)); rect(d, 38, 16, 52, 21, GOLD)
        d.text((40 * S, 40 * S), 'S', fill=(30, 30, 30, 255))
    elif i == 19:   # sniper rounds
        for k in range(4):
            x = 26 + k * 12; rect(d, x, 34, x + 8, 70, GOLD, 1); poly(d, [(x, 34), (x + 8, 34), (x + 4, 22)], (190, 120, 60, 255))
    elif i == 20:   # baked beans can
        rect(d, 28, 22, 68, 76, (170, 172, 168, 255), 4); rect(d, 28, 34, 68, 64, (200, 70, 30, 255)); ell(d, 38, 40, 58, 58, (240, 200, 90, 255))
    elif i == 21:   # burger
        ell(d, 20, 26, 76, 52, (196, 132, 58, 255)); rect(d, 20, 48, 76, 56, (90, 50, 30, 255), 3); rect(d, 22, 44, 74, 49, (90, 170, 60, 255), 2); rect(d, 22, 54, 74, 66, (196, 132, 58, 255), 5)
    elif i == 22:   # pizza slice
        poly(d, [(20, 26), (76, 26), (48, 78)], (240, 190, 80, 255)); rect(d, 18, 22, 78, 30, (190, 120, 50, 255), 4)
        for x, y in [(36, 36), (56, 38), (48, 54)]: ell(d, x - 5, y - 5, x + 5, y + 5, (190, 40, 40, 255))
    elif i == 23:   # soda can
        rect(d, 34, 20, 62, 76, (200, 30, 40, 255), 6); rect(d, 34, 20, 62, 26, (200, 200, 200, 255), 3); rect(d, 36, 42, 60, 50, (250, 250, 250, 255))
    elif i == 24:   # water bottle
        rect(d, 36, 30, 60, 80, (120, 190, 240, 200), 6); rect(d, 42, 18, 54, 30, (60, 120, 220, 255), 2); rect(d, 36, 46, 60, 58, (40, 110, 210, 255))
    elif i == 25:   # beer bottle
        rect(d, 36, 40, 60, 82, (120, 70, 20, 255), 6); poly(d, [(40, 40), (56, 40), (52, 20), (44, 20)], (120, 70, 20, 255)); rect(d, 38, 52, 58, 66, (240, 220, 160, 255))
    elif i == 26:   # medkit
        rect(d, 18, 28, 78, 74, (235, 235, 230, 255), 6); rect(d, 38, 22, 58, 30, DARK, 2); rect(d, 44, 36, 52, 66, (210, 30, 30, 255)); rect(d, 33, 47, 63, 55, (210, 30, 30, 255))
    elif i == 27:   # bandage
        rect(d, 22, 36, 74, 62, (225, 220, 200, 255), 4); line(d, [(30, 40), (30, 58)], (190, 185, 160, 255), 1); line(d, [(66, 40), (66, 58)], (190, 185, 160, 255), 1)
        rect(d, 40, 42, 56, 56, (150, 170, 110, 255))
    elif i == 28:   # antibiotics (pill bottle)
        rect(d, 34, 30, 62, 78, (230, 120, 30, 220), 5); rect(d, 32, 20, 64, 30, (240, 240, 240, 255), 3); rect(d, 36, 46, 60, 62, (250, 250, 250, 255))
    elif i == 29:   # painkillers (blister)
        rect(d, 22, 28, 74, 70, (200, 205, 210, 255), 4)
        for r_ in range(2):
            for c_ in range(4): ell(d, 27 + c_ * 12, 34 + r_ * 18, 36 + c_ * 12, 44 + r_ * 18, (250, 250, 250, 255), (150, 150, 150, 255))
    elif i in (30, 31, 32):   # bags
        col = [(110, 92, 60, 255), OLIVE, (70, 80, 50, 255)][i - 30]; sz = [0, 6, 12][i - 30]
        rect(d, 26 - sz // 2, 26 - sz, 70 + sz // 2, 80, col, 10); rect(d, 32 - sz // 2, 54, 64 + sz // 2, 76, tuple(max(0, v - 25) for v in col[:3]) + (255,), 5)
        d.arc([38 * S, 14 * S, 58 * S, 34 * S], 180, 360, fill=BLACK, width=3 * S); line(d, [(30, 40), (66, 40)], BLACK, 2)
    elif i == 33:   # helmet
        d.chord([18 * S, 22 * S, 78 * S, 82 * S], 180, 360, fill=OLIVE); rect(d, 14, 50, 82, 56, (70, 80, 46, 255), 2); line(d, [(30, 56), (34, 70)], BLACK, 2); line(d, [(66, 56), (62, 70)], BLACK, 2)
    elif i == 34:   # chest plate
        poly(d, [(24, 20), (40, 26), (56, 26), (72, 20), (76, 72), (48, 82), (20, 72)], (90, 96, 100, 255)); poly(d, [(30, 30), (66, 30), (68, 66), (48, 74), (28, 66)], (120, 126, 130, 255))
    elif i == 35:   # armor vest
        poly(d, [(26, 18), (40, 18), (48, 30), (56, 18), (70, 18), (76, 78), (20, 78)], (60, 72, 48, 255)); rect(d, 26, 42, 70, 52, (44, 54, 34, 255)); rect(d, 26, 58, 70, 68, (44, 54, 34, 255))
    elif i == 36:   # gloves
        rect(d, 30, 40, 66, 78, BLACK, 8); [rect(d, 30 + k * 9, 20 + abs(k - 1.5) * 4, 37 + k * 9, 46, BLACK, 3) for k in range(4)]; rect(d, 62, 44, 76, 54, BLACK, 4)
    elif i == 37:   # boots
        poly(d, [(30, 16), (52, 16), (54, 62), (80, 66), (82, 80), (28, 80)], (70, 52, 34, 255)); rect(d, 26, 76, 84, 82, BLACK, 2)
    elif i == 38:   # GPS
        rect(d, 32, 16, 64, 80, (40, 40, 40, 255), 6); rect(d, 36, 22, 60, 50, (120, 200, 90, 255), 2); line(d, [(40, 30), (50, 40), (56, 34)], (30, 80, 30, 255), 2)
        for k in range(3): ell(d, 38 + k * 8, 58, 44 + k * 8, 64, (90, 90, 90, 255))
    elif i == 39:   # fuel canister
        poly(d, [(24, 30), (60, 30), (72, 42), (72, 82), (24, 82)], (190, 30, 30, 255)); rect(d, 60, 20, 70, 32, (190, 30, 30, 255)); line(d, [(30, 40), (66, 76)], (140, 20, 20, 255), 2); line(d, [(66, 40), (30, 76)], (140, 20, 20, 255), 2)
        rect(d, 26, 22, 50, 30, (190, 30, 30, 255), 3)
    elif i == 40:   # toolbox
        rect(d, 16, 38, 80, 76, (200, 50, 30, 255), 4); d.arc([36 * S, 24 * S, 60 * S, 46 * S], 180, 360, fill=DARK, width=4 * S); rect(d, 16, 50, 80, 54, (150, 30, 20, 255))
    elif i == 41:   # map
        poly(d, [(16, 26), (36, 20), (58, 28), (80, 22), (80, 74), (58, 80), (36, 72), (16, 78)], (210, 200, 160, 255))
        line(d, [(36, 20), (36, 72)], (160, 150, 110, 255), 1); line(d, [(58, 28), (58, 80)], (160, 150, 110, 255), 1); line(d, [(22, 60), (44, 44), (70, 52)], (200, 40, 40, 255), 2)
    elif i == 42:   # watch
        rect(d, 40, 12, 56, 84, BLACK, 4); ell(d, 28, 28, 68, 68, (180, 180, 176, 255)); ell(d, 32, 32, 64, 64, (20, 24, 20, 255)); line(d, [(48, 48), (48, 36)], (220, 220, 220, 255), 2); line(d, [(48, 48), (58, 50)], (220, 220, 220, 255), 2)
    elif i == 43:   # empty can (crushed)
        poly(d, [(34, 30), (62, 26), (66, 46), (58, 60), (62, 74), (36, 76), (40, 56), (32, 46)], (150, 150, 150, 255))
    elif i == 44:   # empty bottle
        rect(d, 36, 40, 60, 82, (150, 200, 170, 140), 6); poly(d, [(40, 40), (56, 40), (52, 20), (44, 20)], (150, 200, 170, 140))
    elif i == 45:   # cardboard box
        poly(d, [(18, 36), (48, 26), (78, 36), (48, 46)], (200, 160, 100, 255)); poly(d, [(18, 36), (48, 46), (48, 82), (18, 70)], (170, 130, 80, 255)); poly(d, [(48, 46), (78, 36), (78, 70), (48, 82)], (150, 110, 64, 255))
    elif i == 46:   # morphine auto-injector
        rect(d, 22, 42, 70, 54, (230, 230, 225, 255), 4); rect(d, 24, 42, 34, 54, (40, 110, 210, 255), 3); rect(d, 68, 46, 80, 50, STEEL); d.text((40 * S, 44 * S), 'M', fill=(40, 40, 40, 255))
        line(d, [(80, 48), (88, 48)], (200, 200, 200, 255), 1)
    elif i == 47:   # epinephrine pen
        rect(d, 20, 42, 72, 54, (250, 170, 40, 255), 6); rect(d, 20, 42, 32, 54, (40, 40, 40, 255), 4); rect(d, 70, 45, 80, 51, (200, 30, 30, 255), 2)
    elif i == 48:   # blood bag
        rect(d, 28, 18, 68, 68, (200, 220, 230, 160), 8); rect(d, 31, 30, 65, 65, (170, 10, 20, 255), 6); line(d, [(48, 68), (48, 80), (62, 88)], (170, 10, 20, 255), 3); rect(d, 40, 12, 56, 20, (200, 220, 230, 200), 2)
    elif i == 49:   # compass
        ell(d, 20, 20, 76, 76, (120, 112, 70, 255)); ell(d, 26, 26, 70, 70, (230, 230, 210, 255)); poly(d, [(48, 30), (53, 48), (48, 66), (43, 48)], (60, 60, 60, 255)); poly(d, [(48, 30), (53, 48), (43, 48)], (200, 40, 40, 255))
    else:
        ell(d, 30, 30, 66, 66, (120, 120, 120, 255))
    im = im.resize((C, C), Image.LANCZOS)
    # soft drop shadow so the icon reads on the grunge
    sh = Image.new('RGBA', im.size, (0, 0, 0, 0)); sh.putalpha(im.getchannel('A').point(lambda v: v * 150 // 255))
    sh = Image.merge('RGBA', (Image.new('L', im.size, 0),) * 3 + (sh.getchannel('A').filter(ImageFilter.GaussianBlur(2)),))
    base = Image.new('RGBA', im.size, (0, 0, 0, 0)); base.alpha_composite(sh, (2, 2)); base.alpha_composite(im)
    return base

atlas = Image.new('RGBA', (C * 8, C * 8), (0, 0, 0, 0))
for i in range(1, 50):
    atlas.alpha_composite(draw_item(i), ((i % 8) * C, (i // 8) * C))
atlas.save(os.path.join(OUT, 'dz_items.png'), optimize=True)

inv = grunge(512, 512, (29, 30, 29), 238, 11); inv.save(os.path.join(OUT, 'dz_inv.png'), optimize=True)
bag = backpack_silhouette(grunge(256, 512, (27, 28, 27), 240, 23)); bag.save(os.path.join(OUT, 'dz_bag.png'), optimize=True)

# server icon: yellow ring badge, red human hand print + green zombie hand
ic, d = Image.new('RGBA', (512, 512), (0, 0, 0, 0)), None
d = ImageDraw.Draw(ic)
d.ellipse([16, 16, 496, 496], fill=(16, 13, 6, 255), outline=(250, 204, 21, 255), width=26)
d.ellipse([62, 62, 450, 450], outline=(250, 204, 21, 90), width=8)
def hand(cx, cy, s, col, lean, claws):
    d.ellipse([cx - s * .36, cy + s * .18 - s * .36, cx + s * .36, cy + s * .18 + s * .36], fill=col)
    d.rounded_rectangle([cx - s * .33, cy - s * .05, cx + s * .33, cy + s * .30], radius=int(s * .12), fill=col)
    fx = [-.30, -.12, .06, .24, .44]; fl = [.42, .55, .58, .50, .30]
    for k in range(5):
        bx, by, ln = cx + fx[k] * s, cy - s * .05 + (s * .18 if k == 4 else 0), fl[k] * s
        a = (0.9 if k == 4 else (k - 1.5) * .16) + lean
        tx, ty = bx + math.sin(a) * ln, by - math.cos(a) * ln
        d.line([(bx, by), (tx, ty)], fill=col, width=int(s * .15)); d.ellipse([tx - s * .075, ty - s * .075, tx + s * .075, ty + s * .075], fill=col)
        if claws: d.polygon([(tx - s * .05, ty), (tx + s * .05, ty), (tx + math.sin(a) * s * .16, ty - math.cos(a) * s * .16)], fill=(230, 230, 210, 255))
hand(256 - 52, 256 + 18, 170, (170, 18, 18, 255), -.15, False)
hand(256 + 58, 256 + 30, 152, (74, 150, 52, 255), .22, True)
for k in range(3): d.ellipse([256 - 92 + k * 35 - 10, 362 + k * 14 - 10, 256 - 92 + k * 35 + 10, 362 + k * 14 + 10], fill=(170, 18, 18, 255))
ic.resize((256, 256), Image.LANCZOS).save(os.path.join(OUT, 'dz_icon.png'), optimize=True)
for n in ('dz_items.png', 'dz_inv.png', 'dz_bag.png', 'dz_icon.png'): print(n, os.path.getsize(os.path.join(OUT, n)))

# ---------------------------------------------------------------- 3.58b: radar map + monitor frame
# dz_map.png: San Andreas from the real road nodes + water (tools/radar), the orange TOP-GTA DayZ style.
# u = (x + 3000) / 6000, v = (3000 - y) / 6000.
import json
RD = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'radar')
M = 1536; SS = 2; W_ = M * SS
def mp(x, y): return ((x + 3000.0) / 6000.0 * W_, (3000.0 - y) / 6000.0 * W_)
mapimg = Image.new('RGB', (W_, W_), (201, 116, 62))
md = ImageDraw.Draw(mapimg)
# land grain (big blotches)
grain = Image.new('L', (96, 96)); grain.putdata([R.randint(0, 255) for _ in range(96 * 96)])
grain = grain.resize((W_, W_), Image.BICUBIC).filter(ImageFilter.GaussianBlur(10))
dark = Image.new('RGB', (W_, W_), (160, 86, 44)); mapimg = Image.composite(dark, mapimg, grain.point(lambda v: max(0, v - 120)))
md = ImageDraw.Draw(mapimg)
for line_ in open(os.path.join(RD, 'water.dat')):
    p = line_.split()
    if len(p) not in (22, 29): continue
    n = 4 if len(p) == 29 else 3
    v = [(float(p[i * 7]), float(p[i * 7 + 1])) for i in range(n)]
    if n == 4: v = [v[0], v[1], v[3], v[2]]
    md.polygon([mp(x, y) for x, y in v], fill=(46, 84, 112))
nodes = json.load(open(os.path.join(RD, 'sa_nodes.json')))
byid = {n_['id']: n_ for n_ in nodes}
seen = set()
for n_ in nodes:
    for t, _w in n_['edges']:
        if t not in byid: continue
        k = (min(n_['id'], t), max(n_['id'], t))
        if k in seen: continue
        seen.add(k)
        o = byid[t]
        md.line([mp(n_['x'], n_['y']), mp(o['x'], o['y'])], fill=(244, 196, 160), width=3 * SS)
# towns: pale blocks along the roads (the pink town look of the screenshots)
mapimg = mapimg.resize((M, M), Image.LANCZOS)
mapimg = mapimg.quantize(colors=96, method=Image.MEDIANCUT, dither=Image.NONE).convert('RGB')
mapimg.save(os.path.join(OUT, 'dz_map.png'), optimize=True)

# dz_radar.png: the old field monitor around the map (screen hole transparent), 512 x 600
FW, FH = 512, 600
fr = grunge(FW, FH, (52, 56, 44), 255, 41)
fd = ImageDraw.Draw(fr)
fd.rounded_rectangle([0, 0, FW - 1, FH - 1], radius=26, outline=(24, 26, 20, 255), width=8)
fd.rounded_rectangle([10, 10, FW - 11, FH - 11], radius=20, outline=(88, 94, 74, 255), width=3)
for x, y in [(22, 22), (FW - 22, 22), (22, FH - 22), (FW - 22, FH - 22)]:
    fd.ellipse([x - 7, y - 7, x + 7, y + 7], fill=(30, 30, 26, 255)); fd.line([x - 4, y, x + 4, y], fill=(90, 90, 80, 255), width=2)
# bottom strip for the COORDS | MAP tabs (the code writes the labels)
fd.rectangle([34, FH - 92, FW - 35, FH - 40], fill=(26, 28, 22, 255), outline=(90, 96, 76, 255), width=2)
fd.line([FW // 2, FH - 90, FW // 2, FH - 42], fill=(90, 96, 76, 255), width=2)
# screen hole: 34..478 x 34..478 (rounded) -> alpha 0
mask = Image.new('L', (FW, FH), 255); mk = ImageDraw.Draw(mask)
mk.rounded_rectangle([36, 36, FW - 37, FH - 120], radius=14, fill=0)
fr.putalpha(ImageChops.multiply(fr.getchannel('A'), mask))
# inner bevel shadow
sh = Image.new('RGBA', (FW, FH), (0, 0, 0, 0)); sd = ImageDraw.Draw(sh)
sd.rounded_rectangle([30, 30, FW - 31, FH - 114], radius=16, outline=(0, 0, 0, 200), width=8)
fr = Image.alpha_composite(fr, sh.filter(ImageFilter.GaussianBlur(3)))
fr.save(os.path.join(OUT, 'dz_radar.png'), optimize=True)
for n in ('dz_map.png', 'dz_radar.png'): print(n, os.path.getsize(os.path.join(OUT, n)))
