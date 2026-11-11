#!/usr/bin/env python3
"""4.13.1: Moquil / MLG / app artwork, drawn by hand as flat vector shapes + real game screenshots
(no generated pictures). Run from the wagu repo root: python3 tools/art/make_art.py [MLG-Production dir]
Needs playwright + chromium (CHROME env) and Pillow."""
import os, sys, base64
from playwright.sync_api import sync_playwright
from PIL import Image, ImageFilter, ImageEnhance
ROOT = os.getcwd(); ART = os.path.join(ROOT, 'tools/art'); WWW = os.path.join(ROOT, 'moquil/assets/www/img')
SS = os.path.join(ROOT, 'Screenshots'); MLG = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, '../MLG-Production')
CHROME = os.environ.get('CHROME', '/home/user/.cache/ms-playwright/chromium-1217/chrome-linux64/chrome')
WOLF = open(os.path.join(ART, 'wolf.svg')).read()
def uri(path):
    ext = path.rsplit('.', 1)[1].lower(); ext = 'jpeg' if ext == 'jpg' else ext
    return 'data:image/%s;base64,%s' % (ext, base64.b64encode(open(path, 'rb').read()).decode())
FONT = "font-family:'Lato',sans-serif;"
CSS = "<style>*{box-sizing:border-box}html,body{margin:0;overflow:hidden}body{%s}</style>" % FONT

def logo_html(w, h):
    s = h / 410.0
    return f'''<div style="width:{w}px;height:{h}px;background:#121318;position:relative;overflow:hidden">
<div style="position:absolute;right:0;top:0;bottom:0;width:{int(w*.5)}px;background:repeating-linear-gradient(135deg,rgba(226,70,143,.07) 0 {int(18*s)}px,transparent {int(18*s)}px {int(36*s)}px)"></div>
<div style="position:absolute;left:{int(40*s)}px;top:{int(35*s)}px;width:{int(340*s)}px;height:{int(340*s)}px">{WOLF.replace('<svg ', '<svg width="100%" height="100%" ')}</div>
<div style="position:absolute;left:{int(420*s)}px;top:{int(70*s)}px;color:#fff">
<div style="font-weight:900;font-size:{int(200*s)}px;line-height:.9;letter-spacing:{int(4*s)}px">MLG</div>
<div style="height:{int(8*s)}px;width:{int(520*s)}px;background:#e2468f;margin:{int(18*s)}px 0 {int(16*s)}px"></div>
<div style="font-weight:800;font-size:{int(58*s)}px;letter-spacing:{int(15*s)}px;color:#e9ebf0">PRODUCTION</div></div></div>'''

BTN = [('#2b6cb0', 'cam'), ('#6f45b8', 'bone'), ('#2c8a66', 'ped'), ('#1f9e3a', 'chroma'), ('#b4632b', 'scene'), ('#2a87ad', 'sky'), ('#555b68', 'gear'), ('#b3262f', 'rec')]
def strip_html(col, kind, w=1024, h=76):
    pat = {'chroma': 'repeating-linear-gradient(90deg,rgba(0,0,0,.10) 0 2px,transparent 2px 24px)',
           'rec': 'repeating-linear-gradient(90deg,rgba(0,0,0,.18) 0 40px,transparent 40px 80px)'}.get(kind, 'repeating-linear-gradient(135deg,rgba(255,255,255,.07) 0 14px,transparent 14px 28px)')
    return f'<div style="width:{w}px;height:{h}px;background:{col};position:relative;overflow:hidden"><div style="position:absolute;inset:0;background:{pat}"></div><div style="position:absolute;left:0;right:0;bottom:0;height:6px;background:rgba(0,0,0,.25)"></div><div style="position:absolute;left:0;right:0;top:0;height:2px;background:rgba(255,255,255,.18)"></div></div>'

def atlas_html():
    strips = ''.join(f'<div style="position:absolute;left:0;top:{412 + i*76}px">{strip_html(c, k)}</div>' for i, (c, k) in enumerate(BTN))
    return f'<div style="width:1024px;height:1024px;background:#000;position:relative">{logo_html(1024, 410)}{strips}</div>'

LABELS = ['Free Cam', 'Entity Tools', 'Ped Tools', 'Chroma Key', 'Scene', 'Weather & Time', 'Settings', 'Recording']
def panel_html():   # 1000x1000 preview of the in-game panel (what the mod draws with this atlas)
    rows = ''.join(f'<div style="position:relative;height:74px;margin-top:12px;border:2px solid #000">{strip_html(c, k, 856, 70)}<div style="position:absolute;inset:0;display:flex;align-items:center;justify-content:center;color:#fff;font-weight:900;font-size:34px;text-shadow:0 2px 0 #000,0 0 4px #000">{LABELS[i]}</div></div>' for i, (c, k) in enumerate(BTN))
    return f'<div style="width:1000px;height:1000px;background:#1b1d22;display:flex;align-items:center;justify-content:center"><div style="width:900px;padding:20px;background:#101217;border:2px solid #464c5c;border-radius:8px">{logo_html(856, 342)}{rows}</div></div>'

def photo(name, box, size, dark=0.0, blur=0):
    im = Image.open(os.path.join(SS, name)).convert('RGB').crop(box).resize(size, Image.LANCZOS)
    if blur: im = im.filter(ImageFilter.GaussianBlur(blur))
    if dark: im = ImageEnhance.Brightness(im).enhance(1 - dark)
    return im

def camfx_html(bg):
    br = 'position:absolute;width:90px;height:90px;border-color:#fff;border-style:solid;'
    return f'''<div style="width:1200px;height:800px;background:url({bg}) center/cover;position:relative">
<div style="position:absolute;inset:0;background:linear-gradient(0deg,rgba(0,0,0,.75),rgba(0,0,0,0) 55%)"></div>
<div style="{br}left:40px;top:40px;border-width:6px 0 0 6px"></div><div style="{br}right:40px;top:40px;border-width:6px 6px 0 0"></div>
<div style="{br}left:40px;bottom:40px;border-width:0 0 6px 6px"></div><div style="{br}right:40px;bottom:40px;border-width:0 6px 6px 0"></div>
<div style="position:absolute;left:90px;bottom:90px;color:#fff"><div style="font-weight:900;font-size:130px;line-height:1">Cam<span style="color:#ffb21e">FX</span></div>
<div style="font-weight:700;font-size:34px;letter-spacing:3px;margin-top:6px;color:#e6e6e6">CAMERA FOV EFFECTS &middot; 2.1</div></div></div>'''

def icon_html(kind, size):
    r = int(size * .22)
    glyph = {
     'moquil': ('#3a63e6', '<path d="M40 52c0-8 6-14 14-14h92c8 0 14 6 14 14v64c0 8-6 14-14 14H92l-30 24v-24h-8c-8 0-14-6-14-14z" fill="#fff"/><path d="M66 112V62h12l22 26 22-26h12v50h-14V84l-20 23-20-23v28z" fill="#3a63e6"/>'),
     'voice': ('#1f9d55', '<rect x="78" y="34" width="44" height="82" rx="22" fill="#fff"/><path d="M58 96c0 24 19 40 42 40s42-16 42-40" fill="none" stroke="#fff" stroke-width="12" stroke-linecap="round"/><path d="M100 136v24M76 162h48" stroke="#fff" stroke-width="12" stroke-linecap="round"/>'),
     'tde': ('#f2b400', '<path d="M52 50h96v24h-12v-10h-24v76h14v14H74v-14h14V64H64v10H52z" fill="#1b1d22"/><path d="M40 40v20M40 40h20M160 40v20M160 40h-20M40 160v-20M40 160h20M160 160v-20M160 160h-20" stroke="#1b1d22" stroke-width="7"/>'),
     'mp': ('#1b1d22', '<path d="M34 140V60h18l22 34 22-34h18v80H96V92l-22 32-22-32v48z" fill="#fff"/><path d="M124 140V60h26c16 0 26 10 26 25s-10 25-26 25h-10v30zm16-44h9c7 0 11-4 11-11s-4-11-11-11h-9z" fill="#ffb21e"/>'),
     'mlg': ('#121318', WOLF.replace('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 200">', '<g transform="translate(22 18) scale(.78)">').replace('</svg>', '</g>') + '<rect x="40" y="176" width="120" height="6" fill="#e2468f"/>'),
     'camfx': ('#1b1d22', '<path d="M36 36h30M36 36v30M164 36h-30M164 36v30M36 164h30M36 164v-30M164 164h-30M164 164v-30" stroke="#fff" stroke-width="9"/><text x="100" y="124" text-anchor="middle" font-family="Lato" font-weight="900" font-size="68" fill="#ffb21e">FX</text>'),
    }[kind]
    return f'<div style="width:{size}px;height:{size}px;border-radius:{r}px;background:{glyph[0]};overflow:hidden"><svg viewBox="0 0 200 200" width="{size}" height="{size}">{glyph[1]}</svg></div>'

def main():
    os.makedirs('/tmp/art', exist_ok=True)
    # real screenshots
    photo('Screenshot_20261025-212621.png', (155, 40, 1435, 680), (1400, 700), dark=.12).save(os.path.join(WWW, 'hero.jpg'), quality=86)
    photo('Screenshot_20261025-212621.png', (600, 0, 1080, 720), (900, 1350), dark=.2).save(os.path.join(WWW, 'login.jpg'), quality=84)
    photo('Screenshot_20261025-212621.png', (155, 0, 1435, 720), (1024, 512), blur=18, dark=.35).save(os.path.join(WWW, 'ui_bg.jpg'), quality=80)
    photo('Screenshot_20261104-114839.png', (260, 0, 1340, 720), (1200, 800)).save('/tmp/art/camfx_bg.jpg', quality=90)
    jobs = [('atlas', atlas_html(), 1024, 1024), ('mlgcover', logo_html(1200, 480), 1200, 480), ('panel', panel_html(), 1000, 1000),
            ('camfx', camfx_html(uri('/tmp/art/camfx_bg.jpg')), 1200, 800)]
    for k in ('moquil', 'voice', 'tde', 'mp', 'mlg', 'camfx'):
        jobs.append(('ic_' + k, icon_html(k, 256), 256, 256))
    with sync_playwright() as p:
        b = p.chromium.launch(executable_path=CHROME)
        for name, html, w, h in jobs:
            pg = b.new_page(viewport={'width': w, 'height': h}); open('/tmp/art/_r.html', 'w').write('<!doctype html>' + CSS + '<body>' + html)
            pg.goto('file:///tmp/art/_r.html'); pg.wait_for_timeout(200)
            pg.screenshot(path='/tmp/art/%s.png' % name, omit_background=name.startswith('ic_')); pg.close()
        b.close()
    J = lambda n: Image.open('/tmp/art/%s.png' % n).convert('RGB')
    J('atlas').save(os.path.join(WWW, 'mods/mlg_atlas.jpg'), quality=88)
    J('mlgcover').save(os.path.join(WWW, 'mods/mlg_cover.jpg'), quality=88)
    J('panel').save(os.path.join(WWW, 'mods/mlg_buttons.jpg'), quality=88)
    J('camfx').save(os.path.join(WWW, 'mods/camfx_cover.jpg'), quality=86)
    for k, out in (('moquil', 'logo.png'), ('mp', 'mods/mp_icon.png'), ('tde', 'mods/tde_icon.png'), ('voice', 'mods/voice_icon.png'), ('mlg', 'mods/mlg_icon.png'), ('camfx', 'mods/camfx_icon.png')):
        Image.open('/tmp/art/ic_%s.png' % k).save(os.path.join(WWW, out))
    for k, d in (('moquil', 'moquil'), ('voice', 'voicehelper'), ('tde', 'textdraweditor')):
        Image.open('/tmp/art/ic_%s.png' % k).resize((192, 192), Image.LANCZOS).save(os.path.join(ROOT, d, 'res/drawable/icon.png'))
    if os.path.isdir(MLG):   # the MLG Production texture atlas (built into the .so)
        J('atlas').save(os.path.join(MLG, 'assets/mlg_atlas.jpg'), quality=90)
        data = open(os.path.join(MLG, 'assets/mlg_atlas.jpg'), 'rb').read()
        with open(os.path.join(MLG, 'src/mlg_atlas.h'), 'w') as f:
            f.write('// MLG Production texture atlas (1024x1024 JPEG): flat wolf logo 1024x410 at y 0, 8 button textures 1024x76 at y 412 + i*76\n// made by tools/art/make_art.py in the wagu repo (flat shapes, no photos)\n#pragma once\n')
            f.write('static const unsigned char kMlgAtlas[%d] = {\n' % len(data))
            for i in range(0, len(data), 24): f.write(','.join(str(x) for x in data[i:i+24]) + ',\n')
            f.write('};\n')
    print('ok')
if __name__ == "__main__": main()
