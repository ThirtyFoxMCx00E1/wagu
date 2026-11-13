#!/usr/bin/env python3
"""4.13.19: the website tab / search icons - the MP app icon (white M + orange P, the same vector glyph
as tools/art/make_art.py 'mp') instead of the San Andreas Multiplayer title logo, which is unreadable
at 16-48 px. Writes into docs/ (published by tools/publish_site.py to both MP sites):
  favicon.ico (16, 32, 48)   at the site root: what browsers and search results ask for first
  favicon-32.png, favicon-48.png, icon-192.png, icon-512.png, apple-touch-icon.png (180)
  site.webmanifest
Run from the wagu repo root: python3 tools/art/make_site_icons.py   (Pillow + playwright chromium; CHROME env)
"""
import io, json, os, base64
from playwright.sync_api import sync_playwright
from PIL import Image

ROOT = os.getcwd(); DOCS = os.path.join(ROOT, 'docs')
CHROME = os.environ.get('CHROME', '/home/user/.cache/ms-playwright/chromium_headless_shell-1217/chrome-headless-shell-linux64/chrome-headless-shell')
BG = '#1b1d22'
GLYPH = ('<path d="M34 140V60h18l22 34 22-34h18v80H96V92l-22 32-22-32v48z" fill="#fff"/>'
         '<path d="M124 140V60h26c16 0 26 10 26 25s-10 25-26 25h-10v30zm16-44h9c7 0 11-4 11-11s-4-11-11-11h-9z" fill="#ffb21e"/>')


def render(size, radius=.22, pad=0.0):
    """the icon at size px; pad zooms the glyph (small favicons: a little bigger so it stays readable)"""
    s = 200 * (1 - pad); o = (200 - s) / 2
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{o} {o} {s} {s}" width="{size}" height="{size}">'
           f'<rect x="{o}" y="{o}" width="{s}" height="{s}" rx="{s * radius}" fill="{BG}"/>{GLYPH}</svg>')
    return svg


def main():
    jobs = {'512': (512, render(512)), '192': (192, render(192)), '180': (180, render(180, radius=0)),
            '48': (48, render(48, pad=.1)), '32': (32, render(32, pad=.14)), '16': (16, render(16, pad=.16, radius=.18))}
    imgs = {}
    with sync_playwright() as p:
        b = p.chromium.launch(executable_path=CHROME)
        for k, (sz, svg) in jobs.items():
            pg = b.new_page(viewport={'width': sz, 'height': sz}, device_scale_factor=1)
            pg.set_content('<!doctype html><style>html,body{margin:0;background:transparent}</style>' + svg)
            imgs[k] = Image.open(io.BytesIO(pg.screenshot(omit_background=True))).convert('RGBA'); pg.close()
        b.close()
    imgs['512'].save(os.path.join(DOCS, 'icon-512.png'), optimize=True)
    imgs['192'].save(os.path.join(DOCS, 'icon-192.png'), optimize=True)
    imgs['180'].convert('RGB').save(os.path.join(DOCS, 'apple-touch-icon.png'), optimize=True)
    imgs['48'].save(os.path.join(DOCS, 'favicon-48.png'), optimize=True)
    imgs['32'].save(os.path.join(DOCS, 'favicon-32.png'), optimize=True)
    imgs['48'].save(os.path.join(DOCS, 'favicon.ico'), sizes=[(16, 16), (32, 32), (48, 48)],
                    append_images=[imgs['16'], imgs['32']])
    json.dump({'name': 'MP Multiplayer', 'short_name': 'MP', 'start_url': './', 'display': 'standalone',
               'background_color': BG, 'theme_color': BG,
               'icons': [{'src': 'icon-192.png', 'sizes': '192x192', 'type': 'image/png'},
                         {'src': 'icon-512.png', 'sizes': '512x512', 'type': 'image/png'}]},
              open(os.path.join(DOCS, 'site.webmanifest'), 'w'), indent=1)
    # a 64 px copy as a data URI, for pages served from elsewhere (HiveMQ cloud panel)
    b64 = io.BytesIO(); imgs['192'].resize((64, 64), Image.LANCZOS).save(b64, 'PNG', optimize=True)
    open('/tmp/mp_icon64.b64', 'w').write(base64.b64encode(b64.getvalue()).decode())
    print('docs/: favicon.ico favicon-32/48.png icon-192/512.png apple-touch-icon.png site.webmanifest')


if __name__ == '__main__':
    main()
