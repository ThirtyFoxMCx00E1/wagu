#!/usr/bin/env python3
"""MP 4.13.28: the textures of the neon title menu (files/TYFMC/MP/neon/).

The neon title menu is drawn like PCMenu: at the end of the game's own menu pass, with the game's
MobileMenu::DrawSprite / DrawQuad, out of PNG textures read with RtPNGImageRead. Nothing is painted at runtime:
backgrounds, shades, cards, icons and the text glyphs (font atlases) are all pictures made here.

usage: gen_neon_assets.py <out dir> <neon_bg.jpg> <title.png> <gothic.ttf> <caps.ttf> <ui.ttf>
Font atlases use PCMenu's format (tools/gen_fonts.py): <name>.png (white glyphs, alpha) + <name>.txt
    FONT size ascent descent capHeight atlasW atlasH
    G code x y w h xoff yoff advance
"""
import sys, os
from PIL import Image, ImageDraw, ImageFont, ImageFilter

PAD = 3

def font_atlas(ttf, size, out_png, out_txt, atlas_w=1024):
    font = ImageFont.truetype(ttf, size)
    glyphs = []
    for code in range(32, 127):
        ch = chr(code)
        x0, y0, x1, y1 = font.getbbox(ch, anchor='ls')
        glyphs.append((code, ch, x0, y0, x1, y1, font.getlength(ch)))
    x = y = row_h = 0
    placed = []
    for g in glyphs:
        code, ch, x0, y0, x1, y1, adv = g
        w, h = max(0, x1 - x0), max(0, y1 - y0)
        cw, chh = w + PAD * 2, h + PAD * 2
        if x + cw > atlas_w:
            x, y, row_h = 0, y + row_h, 0
        placed.append((g, x, y, w, h))
        x += cw
        row_h = max(row_h, chh)
    atlas_h = 1
    while atlas_h < y + row_h:
        atlas_h *= 2
    img = Image.new('L', (atlas_w, atlas_h), 0)
    d = ImageDraw.Draw(img)
    lines = []
    for (code, ch, x0, y0, x1, y1, adv), px, py, w, h in placed:
        if w and h:
            d.text((px + PAD - x0, py + PAD - y0), ch, font=font, fill=255, anchor='ls')
        lines.append("G %d %d %d %d %d %d %d %.3f" % (code, px + PAD, py + PAD, w, h, x0, y0, adv))
    rgba = Image.new('RGBA', img.size, (255, 255, 255, 0))
    rgba.putalpha(img)
    rgba.save(out_png, optimize=True)
    cap = font.getbbox('H', anchor='ls')
    asc, desc = font.getmetrics()
    with open(out_txt, 'w') as f:
        f.write("FONT %d %d %d %d %d %d\n" % (size, asc, desc, cap[3] - cap[1], atlas_w, atlas_h))
        f.write("\n".join(lines) + "\n")
    print(out_png, img.size)

def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(len(a)))

def shade_h(out, c0, c1, w=256, h=4):
    """horizontal gradient: c0 on the left -> c1 on the right (RGBA)"""
    im = Image.new('RGBA', (w, h))
    px = im.load()
    for x in range(w):
        c = lerp(c0, c1, x / (w - 1))
        for y in range(h):
            px[x, y] = c
    im.save(out)

def shade_v(out, c0, c1, w=4, h=256):
    im = Image.new('RGBA', (w, h))
    px = im.load()
    for y in range(h):
        c = lerp(c0, c1, y / (h - 1))
        for x in range(w):
            px[x, y] = c
    im.save(out)

def card(out, w, h, border, bar=(184, 154, 90, 255), fill=(12, 12, 14, 236), s=2):
    W, H = w * s, h * s
    im = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, W - 1, H - 1], radius=2 * s, fill=fill, outline=border, width=max(1, s))
    d.rectangle([0, 0, int(2.5 * s), H - 1], fill=bar)
    im.save(out)

def discord_icon(out, s=4):
    n = 37 * s
    im = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, n - 1, n - 1], radius=2 * s, fill=(26, 27, 31, 255), outline=(70, 72, 78, 255), width=s)
    c = n / 2
    col = (225, 228, 236, 255)
    d.rounded_rectangle([c - 10 * s, c - 7 * s, c + 10 * s, c + 7 * s], radius=6 * s, fill=col)
    d.polygon([(c - 10 * s, c + 2 * s), (c - 6 * s, c + 7 * s), (c - 12 * s, c + 9 * s)], fill=col)
    d.polygon([(c + 10 * s, c + 2 * s), (c + 6 * s, c + 7 * s), (c + 12 * s, c + 9 * s)], fill=col)
    for ex in (-4, 4):
        d.ellipse([c + ex * s - 2.2 * s, c - 2.2 * s, c + ex * s + 2.2 * s, c + 2.2 * s], fill=(26, 27, 31, 255))
    im.save(out)

def tri(out, n=64, direction='right'):
    im = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    if direction == 'right':
        d.polygon([(n * 0.2, n * 0.1), (n * 0.9, n * 0.5), (n * 0.2, n * 0.9)], fill=(255, 255, 255, 255))
    else:
        d.polygon([(n * 0.1, n * 0.2), (n * 0.9, n * 0.2), (n * 0.5, n * 0.9)], fill=(255, 255, 255, 255))
    im = im.resize((n // 2, n // 2), Image.LANCZOS)
    im.save(out)

def dot(out, n=32):
    im = Image.new('RGBA', (n * 4, n * 4), (0, 0, 0, 0))
    ImageDraw.Draw(im).ellipse([2, 2, n * 4 - 3, n * 4 - 3], fill=(255, 255, 255, 255))
    im.resize((n, n), Image.LANCZOS).save(out)

def white(out):
    Image.new('RGBA', (8, 8), (255, 255, 255, 255)).save(out)

def main():
    out, bg, title, gothic, caps, ui = sys.argv[1:7]
    os.makedirs(out, exist_ok=True)
    # the art: the right side, its left edge fading into the dark (alpha baked in)
    art = Image.open(bg).convert('RGBA')
    if art.width > 1024:
        art = art.resize((1024, int(art.height * 1024 / art.width)), Image.LANCZOS)
    a = Image.new('L', art.size, 255)
    pa = a.load()
    fade = int(art.width * 0.55)
    for x in range(fade):
        v = int(255 * (x / fade) ** 1.2)
        for y in range(art.height):
            pa[x, y] = v
    art.putalpha(a)
    art.save(os.path.join(out, 'art.png'), optimize=True)
    t = Image.open(title).convert('RGBA')
    t = t.resize((1024, int(round(t.height * 1024 / t.width))), Image.LANCZOS)
    t.save(os.path.join(out, 'logo.png'), optimize=True)
    shade_h(os.path.join(out, 'shade_left.png'), (4, 4, 6, 235), (4, 4, 6, 60))
    shade_v(os.path.join(out, 'shade_bottom.png'), (0, 0, 0, 0), (0, 0, 0, 190))
    shade_v(os.path.join(out, 'shade_top.png'), (0, 0, 0, 150), (0, 0, 0, 0))
    card(os.path.join(out, 'card_identity.png'), 377, 63, (40, 40, 44, 255))
    card(os.path.join(out, 'card_featured.png'), 387, 80, (64, 60, 52, 255))
    card(os.path.join(out, 'dialog.png'), 520, 230, (184, 154, 90, 255), fill=(10, 10, 12, 245))
    card(os.path.join(out, 'button.png'), 150, 40, (184, 154, 90, 255), bar=(184, 154, 90, 0), fill=(26, 24, 20, 250))
    card(os.path.join(out, 'pill.png'), 230, 46, (184, 154, 90, 255), bar=(184, 154, 90, 0), fill=(10, 10, 12, 235))
    discord_icon(os.path.join(out, 'discord.png'))
    tri(os.path.join(out, 'arrow.png'), 64, 'right')
    tri(os.path.join(out, 'arrow_down.png'), 64, 'down')
    dot(os.path.join(out, 'dot.png'))
    white(os.path.join(out, 'white.png'))
    font_atlas(gothic, 64, os.path.join(out, 'font_gothic.png'), os.path.join(out, 'font_gothic.txt'))
    font_atlas(caps, 48, os.path.join(out, 'font_caps.png'), os.path.join(out, 'font_caps.txt'))
    font_atlas(ui, 40, os.path.join(out, 'font_ui.png'), os.path.join(out, 'font_ui.txt'))
    pot_all(out)


def pot_all(folder):
    """4.13.28: every texture is stretched to power-of-two sides (the game's texture loader, like PCMenu's
    1024x512 PNGs). The menu draws each image into its own rectangle, so the stretch does not show. The font
    atlases are already 1024x256 (their glyph table uses pixel coordinates)."""
    def p2(n):
        v = 1
        while v < n:
            v *= 2
        return v
    for f in os.listdir(folder):
        if not f.endswith('.png'):
            continue
        p = os.path.join(folder, f)
        im = Image.open(p).convert('RGBA')
        w, h = im.size
        if (w, h) != (min(p2(w), 1024), min(p2(h), 1024)):
            im.resize((min(p2(w), 1024), min(p2(h), 1024)), Image.LANCZOS).save(p, optimize=True)


if __name__ == '__main__':
    main()
