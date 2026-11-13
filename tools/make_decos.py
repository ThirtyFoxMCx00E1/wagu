#!/usr/bin/env python3
"""4.13.18: Moquil avatar decorations (Discord style). Each SVG is 120x120 = 1.2x the avatar; the avatar is the
circle c=(60,60) r=50, so the art hugs the avatar edge like Discord's decorations. Output: moquil/assets/www/img/deco/"""
import math, os
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'moquil', 'assets', 'www', 'img', 'deco')
os.makedirs(OUT, exist_ok=True)

def P(r, deg, cx=60, cy=60):
    a = math.radians(deg - 90)          # 0 deg = top, clockwise
    return cx + r * math.cos(a), cy + r * math.sin(a)

def svg(body, defs=''):
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 120 120" width="120" height="120">'
            '<defs>' + defs + '</defs>' + body + '</svg>')

def star4(x, y, s, fill, anim=None):
    d = f"M{x},{y-s} Q{x+s*.18},{y-s*.18} {x+s},{y} Q{x+s*.18},{y+s*.18} {x},{y+s} Q{x-s*.18},{y+s*.18} {x-s},{y} Q{x-s*.18},{y-s*.18} {x},{y-s}Z"
    a = ''
    if anim:
        dur, beg = anim
        a = (f'<animate attributeName="opacity" values="1;.15;1" dur="{dur}s" begin="{beg}s" repeatCount="indefinite"/>'
             f'<animateTransform attributeName="transform" type="scale" additive="sum" values="1;.55;1" dur="{dur}s" begin="{beg}s" repeatCount="indefinite"/>')
        return f'<g transform="translate({x},{y})"><g><path d="{d}" transform="translate({-x},{-y})" fill="{fill}"/>{a}</g></g>'
    return f'<path d="{d}" fill="{fill}"/>'

def heart(x, y, s, fill, rot=0, extra=''):
    d = (f"M0,{s*.35} C{-s*.05},{s*.3} {-s},{-s*.15} {-s*.5},{-s*.55} C{-s*.2},{-s*.8} 0,{-s*.45} 0,{-s*.3} "
         f"C0,{-s*.45} {s*.2},{-s*.8} {s*.5},{-s*.55} C{s},{-s*.15} {s*.05},{s*.3} 0,{s*.35}Z")
    return f'<g transform="translate({x},{y}) rotate({rot})"><path d="{d}" fill="{fill}"/>{extra}</g>'

def ring(r1, r2, fill):
    return (f'<path fill-rule="evenodd" fill="{fill}" d="M{60-r2},60 a{r2},{r2} 0 1,0 {2*r2},0 a{r2},{r2} 0 1,0 {-2*r2},0Z '
            f'M{60-r1},60 a{r1},{r1} 0 1,0 {2*r1},0 a{r1},{r1} 0 1,0 {-2*r1},0Z"/>')

def arc(r, a0, a1):
    x0, y0 = P(r, a0); x1, y1 = P(r, a1)
    large = 1 if (a1 - a0) % 360 > 180 else 0
    return f"M{x0:.2f},{y0:.2f} A{r},{r} 0 {large},1 {x1:.2f},{y1:.2f}"

D = {}
GLOW = '<filter id="gl" x="-30%" y="-30%" width="160%" height="160%"><feGaussianBlur stdDeviation="1.6" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>'
SH = '<filter id="sh" x="-20%" y="-20%" width="140%" height="140%"><feDropShadow dx="0" dy="1.2" stdDeviation="1.1" flood-color="#000" flood-opacity=".55"/></filter>'

# ---------------- samurai kabuto (helmet, gold crescent, side wings, neck guard, katana) ----------------
def samurai():
    defs = SH + ('<linearGradient id="lac" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#3a3a46"/><stop offset=".5" stop-color="#16161d"/><stop offset="1" stop-color="#07070a"/></linearGradient>'
                 '<linearGradient id="gold" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#fff2b0"/><stop offset=".45" stop-color="#f2b53a"/><stop offset="1" stop-color="#9a5b10"/></linearGradient>'
                 '<linearGradient id="red" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#e2382f"/><stop offset="1" stop-color="#7d0f12"/></linearGradient>'
                 '<linearGradient id="blade" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#ffffff"/><stop offset=".55" stop-color="#c7d0dc"/><stop offset="1" stop-color="#6d7889"/></linearGradient>')
    b = '<g filter="url(#sh)">'
    # katana behind the neck guard, along the lower right edge
    b += '<g transform="rotate(-24 78 106)">'
    b += '<path d="M44,104 L118,104 Q121,106 118,108 L44,108Z" fill="url(#blade)" stroke="#4b5563" stroke-width=".5"/>'
    b += '<path d="M50,106.4 Q70,105.2 90,106.4 T117,106" fill="none" stroke="#fff" stroke-width=".7" opacity=".8"/>'
    b += '<ellipse cx="44" cy="106" rx="2.4" ry="5.6" fill="url(#gold)" stroke="#5c3a08" stroke-width=".5"/>'
    b += '<rect x="22" y="103.6" width="21" height="4.8" rx="1.6" fill="#141418"/>'
    for i in range(5):
        x = 24 + i * 4
        b += f'<path d="M{x},103.8 l2,2.2 l-2,2.2 l-2,-2.2Z" fill="#b8202a"/>'
    b += '<rect x="19.5" y="103.4" width="3" height="5.2" rx="1" fill="url(#gold)"/></g>'
    # neck guard (shikoro): 3 lacquered plates each side with red lacing
    for side in (-1, 1):
        for i in range(3):
            r0, r1 = 49 + i * 0.5, 57 + i * 1.6
            a0, a1 = (62 + i * 11, 96 + i * 13) if side > 0 else (-96 - i * 13, -62 - i * 11)
            if side < 0: a0, a1 = 360 + a0, 360 + a1
            x0, y0 = P(r1, a0); x1, y1 = P(r1, a1); x2, y2 = P(r0, a1); x3, y3 = P(r0, a0)
            sw = 1 if side > 0 else 1
            b += (f'<path d="M{x0:.1f},{y0:.1f} A{r1},{r1} 0 0,{sw} {x1:.1f},{y1:.1f} L{x2:.1f},{y2:.1f} A{r0},{r0} 0 0,{1-sw} {x3:.1f},{y3:.1f}Z" '
                  f'fill="url(#lac)" stroke="url(#gold)" stroke-width=".8"/>')
            for k in range(3):
                aa = a0 + (a1 - a0) * (k + 1) / 4
                xa, ya = P(r0 + 1, aa); xb, yb = P(r1 - 1, aa)
                b += f'<line x1="{xa:.1f}" y1="{ya:.1f}" x2="{xb:.1f}" y2="{yb:.1f}" stroke="#d3252c" stroke-width="1.4" stroke-linecap="round"/>'
    # helmet bowl (hachi) sitting on the head
    b += '<path d="M8,60 C8,24 30,3 60,3 C90,3 112,24 112,60 L103,60 C102,35 84,15 60,15 C36,15 18,35 17,60Z" fill="url(#lac)" stroke="url(#gold)" stroke-width="1.4"/>'
    for a in (-60, -40, -20, 0, 20, 40, 60):   # ribs
        x0, y0 = P(56.5, a); x1, y1 = P(45.5, a)
        b += f'<line x1="{x0:.1f}" y1="{y0:.1f}" x2="{x1:.1f}" y2="{y1:.1f}" stroke="#f2b53a" stroke-width=".7" opacity=".75"/>'
    b += '<path d="M17,60 C18,35 36,15 60,15 C84,15 102,35 103,60" fill="none" stroke="url(#red)" stroke-width="2.4"/>'
    # side wings (fukigaeshi)
    b += '<path d="M10,46 C2,40 -1,30 4,26 C10,32 16,38 22,40 Z" fill="url(#red)" stroke="url(#gold)" stroke-width="1"/>'
    b += '<path d="M110,46 C118,40 121,30 116,26 C110,32 104,38 98,40 Z" fill="url(#red)" stroke="url(#gold)" stroke-width="1"/>'
    b += '<circle cx="9" cy="34" r="2" fill="url(#gold)"/><circle cx="111" cy="34" r="2" fill="url(#gold)"/>'
    # gold crescent crest (maedate) + mon
    b += '<path d="M22,17 C30,-4 90,-4 98,17 C86,4 34,4 22,17Z" fill="url(#gold)" stroke="#7a4708" stroke-width=".7"/>'
    b += '<path d="M60,4 L64,11 L60,18 L56,11Z" fill="url(#gold)" stroke="#7a4708" stroke-width=".6"/>'
    b += '<circle cx="60" cy="11" r="2.6" fill="#c81d25"/>'
    b += '</g>'
    # petals
    for x, y, r in ((106, 70, 20), (12, 84, -30), (100, 22, 60)):
        b += f'<ellipse cx="{x}" cy="{y}" rx="3.4" ry="2" transform="rotate({r} {x} {y})" fill="#ffb7d0" opacity=".95"/>'
    return svg(b, defs)
D['deco-samurai'] = samurai()

# ---------------- ronin (straw kasa hat + katana hilt over the shoulder) ----------------
def ronin():
    defs = SH + ('<radialGradient id="straw" cx=".5" cy=".3" r=".8"><stop offset="0" stop-color="#f4dc9a"/><stop offset=".7" stop-color="#c99a4a"/><stop offset="1" stop-color="#8a6326"/></radialGradient>'
                 '<linearGradient id="gold" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#fff2b0"/><stop offset=".5" stop-color="#f2b53a"/><stop offset="1" stop-color="#9a5b10"/></linearGradient>')
    b = '<g filter="url(#sh)" transform="rotate(-10 60 30)">'
    b += '<path d="M-2,34 L60,-4 L122,34 Q60,44 -2,34Z" fill="url(#straw)" stroke="#6b4a18" stroke-width="1"/>'
    for i in range(-5, 6):
        b += f'<line x1="60" y1="-3" x2="{60 + i * 12}" y2="{36 - abs(i) * .6:.1f}" stroke="#8a6326" stroke-width=".6" opacity=".7"/>'
    for t in (0.35, 0.65):
        y = -4 + 38 * t
        b += f'<path d="M{60 - 62 * t:.1f},{y + 1:.1f} Q60,{y + 4 * t:.1f} {60 + 62 * t:.1f},{y + 1:.1f}" fill="none" stroke="#8a6326" stroke-width=".7" opacity=".8"/>'
    b += '<path d="M14,36 Q60,44 106,36" fill="none" stroke="#5a1d14" stroke-width="2"/></g>'
    # cords
    b += '<path d="M24,40 Q20,70 30,96" fill="none" stroke="#b8202a" stroke-width="1.6"/><path d="M96,40 Q100,70 90,96" fill="none" stroke="#b8202a" stroke-width="1.6"/>'
    # katana hilt over right shoulder
    b += '<g filter="url(#sh)" transform="rotate(38 100 94)"><rect x="84" y="91" width="34" height="6" rx="2" fill="#141418"/>'
    for i in range(7):
        b += f'<path d="M{86 + i * 4.4},91.2 l2.2,2.8 l-2.2,2.8 l-2.2,-2.8Z" fill="#e7e1d0"/>'
    b += '<ellipse cx="83" cy="94" rx="2.6" ry="7" fill="url(#gold)"/><rect x="66" y="91.6" width="16" height="4.8" fill="#2b2b33"/></g>'
    return svg(b, defs)
D['deco-ronin'] = ronin()

# ---------------- oni mask on the side + horns ----------------
def oni():
    defs = SH + ('<radialGradient id="ored" cx=".45" cy=".35" r=".75"><stop offset="0" stop-color="#ff6a4d"/><stop offset=".6" stop-color="#d1202a"/><stop offset="1" stop-color="#7a0b12"/></radialGradient>'
                 '<linearGradient id="horn" x1="0" y1="1" x2="0" y2="0"><stop offset="0" stop-color="#e8dcc0"/><stop offset="1" stop-color="#fff"/></linearGradient>'
                 '<linearGradient id="gold" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#fff2b0"/><stop offset=".5" stop-color="#f2b53a"/><stop offset="1" stop-color="#9a5b10"/></linearGradient>')
    b = '<g filter="url(#sh)">' + ring(49.5, 53, 'url(#gold)')
    b += '<g transform="translate(24 86) rotate(-18)">'
    b += '<path d="M-10,-14 L-16,-30 L-6,-17Z" fill="url(#horn)"/><path d="M10,-14 L16,-30 L6,-17Z" fill="url(#horn)"/>'
    b += '<path d="M-18,-6 C-18,-20 18,-20 18,-6 C18,10 10,20 0,20 C-10,20 -18,10 -18,-6Z" fill="url(#ored)" stroke="#3a0508" stroke-width="1"/>'
    b += '<path d="M-14,-8 Q-8,-14 -3,-7" fill="none" stroke="#1a0204" stroke-width="2.2"/><path d="M14,-8 Q8,-14 3,-7" fill="none" stroke="#1a0204" stroke-width="2.2"/>'
    b += '<ellipse cx="-7" cy="-3" rx="3.6" ry="2.6" fill="#ffd34d"/><ellipse cx="7" cy="-3" rx="3.6" ry="2.6" fill="#ffd34d"/>'
    b += '<circle cx="-7" cy="-3" r="1.2" fill="#000"/><circle cx="7" cy="-3" r="1.2" fill="#000"/>'
    b += '<path d="M-10,8 Q0,16 10,8 Q0,12 -10,8Z" fill="#fff"/><path d="M-9,8 L-7,4 L-5,9Z M9,8 L7,4 L5,9Z" fill="#fff"/></g>'
    b += '<path d="M42,98 C50,108 62,112 74,112" fill="none" stroke="#b8202a" stroke-width="1.6"/></g>'
    return svg(b, defs)
D['deco-oni'] = oni()

# ---------------- leather jacket (collar + lapels on the shoulders, zip, studs) ----------------
def jacket():
    defs = SH + ('<linearGradient id="lth" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#4b4b55"/><stop offset=".35" stop-color="#1c1c22"/><stop offset="1" stop-color="#050507"/></linearGradient>'
                 '<linearGradient id="lth2" x1="1" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#4b4b55"/><stop offset=".35" stop-color="#1c1c22"/><stop offset="1" stop-color="#050507"/></linearGradient>'
                 '<linearGradient id="silver" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#ffffff"/><stop offset=".5" stop-color="#b9c0ca"/><stop offset="1" stop-color="#5b6370"/></linearGradient>')
    b = '<g filter="url(#sh)">'
    # body / shoulders
    b += '<path d="M0,120 L0,98 C4,86 16,80 26,82 L46,108 L60,120Z" fill="url(#lth)"/>'
    b += '<path d="M120,120 L120,98 C116,86 104,80 94,82 L74,108 L60,120Z" fill="url(#lth2)"/>'
    # standing collar
    b += '<path d="M24,80 L14,70 C22,66 30,70 34,78 L44,104 Z" fill="url(#lth)" stroke="#000" stroke-width=".6"/>'
    b += '<path d="M96,80 L106,70 C98,66 90,70 86,78 L76,104 Z" fill="url(#lth2)" stroke="#000" stroke-width=".6"/>'
    # lapels
    b += '<path d="M26,82 L44,104 L50,118 L36,104 L22,96Z" fill="#2a2a32" stroke="#000" stroke-width=".6"/>'
    b += '<path d="M94,82 L76,104 L70,118 L84,104 L98,96Z" fill="#2a2a32" stroke="#000" stroke-width=".6"/>'
    # highlights
    b += '<path d="M6,100 C10,90 18,86 24,86" fill="none" stroke="#8d8d99" stroke-width="1.2" opacity=".55"/>'
    b += '<path d="M114,100 C110,90 102,86 96,86" fill="none" stroke="#8d8d99" stroke-width="1.2" opacity=".55"/>'
    # zipper
    b += '<line x1="60" y1="110" x2="60" y2="120" stroke="url(#silver)" stroke-width="2"/>'
    for y in range(111, 120, 2):
        b += f'<line x1="58.4" y1="{y}" x2="61.6" y2="{y}" stroke="#9aa3ad" stroke-width=".8"/>'
    b += '<rect x="57.6" y="106" width="4.8" height="6" rx="1" fill="url(#silver)"/>'
    # studs on the collar
    for x, y in ((18, 72), (24, 75), (102, 72), (96, 75), (30, 96), (90, 96)):
        b += f'<circle cx="{x}" cy="{y}" r="1.5" fill="url(#silver)"/>'
    b += '</g>'
    return svg(b, defs)
D['deco-jacket'] = jacket()

# ---------------- Grove Street jacket (green varsity) ----------------
def varsity():
    defs = SH + ('<linearGradient id="g1" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#4fd16a"/><stop offset=".5" stop-color="#1f8b37"/><stop offset="1" stop-color="#0b4a1b"/></linearGradient>'
                 '<linearGradient id="g2" x1="1" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#4fd16a"/><stop offset=".5" stop-color="#1f8b37"/><stop offset="1" stop-color="#0b4a1b"/></linearGradient>')
    b = '<g filter="url(#sh)">'
    b += '<path d="M0,120 L0,98 C4,86 16,80 26,82 L46,108 L60,120Z" fill="url(#g1)"/><path d="M120,120 L120,98 C116,86 104,80 94,82 L74,108 L60,120Z" fill="url(#g2)"/>'
    b += '<path d="M24,82 C34,96 44,108 60,114 C76,108 86,96 96,82" fill="none" stroke="#f4f4f4" stroke-width="5"/>'
    b += '<path d="M24,82 C34,96 44,108 60,114 C76,108 86,96 96,82" fill="none" stroke="#1d1d1d" stroke-width="1.6"/>'
    for x in (52, 68):
        b += f'<circle cx="{x}" cy="{116}" r="1.6" fill="#f4f4f4"/>'
    b += '<text x="16" y="110" font-family="Arial Black,Arial" font-weight="900" font-size="13" fill="#f4f4f4" stroke="#0b4a1b" stroke-width=".6">G</text>'
    b += '<text x="94" y="110" font-family="Arial Black,Arial" font-weight="900" font-size="13" fill="#f4f4f4" stroke="#0b4a1b" stroke-width=".6">S</text></g>'
    return svg(b, defs)
D['deco-grove'] = varsity()

# ---------------- hoodie (hood around the face + drawstrings) ----------------
def hoodie():
    defs = SH + '<linearGradient id="hd" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#8f6bff"/><stop offset=".6" stop-color="#5b3fd6"/><stop offset="1" stop-color="#2e1d7e"/></linearGradient>'
    b = '<g filter="url(#sh)">'
    b += '<path fill-rule="evenodd" fill="url(#hd)" d="M60,0 C96,0 118,26 118,62 C118,86 106,104 92,116 L80,104 C94,94 102,80 102,62 C102,36 84,14 60,14 C36,14 18,36 18,62 C18,80 26,94 40,104 L28,116 C14,104 2,86 2,62 C2,26 24,0 60,0Z"/>'
    b += f'<path d="{arc(52, -120, 120)}" fill="none" stroke="#c9b8ff" stroke-width="1" stroke-dasharray="2 2.4" opacity=".8"/>'
    b += '<path d="M44,104 Q42,112 46,120" fill="none" stroke="#f1ecff" stroke-width="2"/><path d="M76,104 Q78,112 74,120" fill="none" stroke="#f1ecff" stroke-width="2"/>'
    b += '<rect x="44" y="114" width="3.4" height="6" rx="1" fill="#d6d6e0"/><rect x="72.6" y="114" width="3.4" height="6" rx="1" fill="#d6d6e0"/></g>'
    return svg(b, defs)
D['deco-hoodie'] = hoodie()

# ---------------- golden sparkles (Wumpus card) ----------------
def sparkle():
    defs = GLOW + '<linearGradient id="gold" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#fff6c2"/><stop offset=".5" stop-color="#f4c13d"/><stop offset="1" stop-color="#c17a0e"/></linearGradient>'
    b = '<g filter="url(#gl)">' + ring(49.6, 52.4, 'url(#gold)')
    import random
    random.seed(7)
    for i in range(46):
        a = random.uniform(0, 360); r = random.uniform(50.5, 57); x, y = P(r, a)
        b += f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{random.uniform(.35, 1.05):.2f}" fill="#fff3b0"><animate attributeName="opacity" values="1;.2;1" dur="{random.uniform(1.2, 2.6):.1f}s" begin="{random.uniform(0, 2):.1f}s" repeatCount="indefinite"/></circle>'
    for (a, r, s, d, bg) in ((300, 55, 7, 1.8, 0), (40, 56, 5, 2.2, .6), (140, 55, 6, 2.0, 1.1), (215, 57, 4.5, 1.6, .3), (95, 57, 3.5, 2.4, 1.4)):
        x, y = P(r, a); b += star4(round(x, 1), round(y, 1), s, '#fffbe0', (d, bg))
    b += '</g>'
    return svg(b, defs)
D['deco-sparkle'] = sparkle()

# ---------------- string lights (Phibi card) ----------------
def lights():
    defs = GLOW
    pts = []
    for i in range(0, 361, 6):
        sag = 3.2 * abs(math.sin(math.radians(i * 3)))
        pts.append(P(52 + sag, i))
    d = 'M' + ' L'.join(f'{x:.1f},{y:.1f}' for x, y in pts)
    b = f'<path d="{d}" fill="none" stroke="#e9d9ff" stroke-width="1.1" opacity=".9"/>'
    cols = ['#ff4fd8', '#3ff0ff', '#7dff6a', '#b26bff', '#ffd44f']
    for k, a in enumerate(range(0, 360, 30)):
        x, y = P(52 + 3.2 * abs(math.sin(math.radians(a * 3 + 30))) + 0.5, a + 15)
        c = cols[k % len(cols)]
        b += (f'<g transform="translate({x:.1f} {y:.1f}) rotate({a + 15})"><rect x="-1.6" y="-1" width="3.2" height="2.6" rx=".6" fill="#cfd3dc"/>'
              f'<ellipse cx="0" cy="5" rx="3.4" ry="4.8" fill="{c}" filter="url(#gl)"><animate attributeName="opacity" values="1;.35;1" dur="1.6s" begin="{(k % 3) * .53:.2f}s" repeatCount="indefinite"/></ellipse>'
              f'<ellipse cx="-1" cy="3.6" rx="1" ry="1.6" fill="#fff" opacity=".7"/></g>')
    return svg(b, defs)
D['deco-lights'] = lights()

# ---------------- love hearts (Mallow card) ----------------
def hearts():
    defs = GLOW + '<linearGradient id="pk" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#ff9ccc"/><stop offset="1" stop-color="#ff3d8b"/></linearGradient>'
    b = ring(49.8, 51.6, '#ff7ab8')
    for (a, r, s, rot, d) in ((315, 56, 9, -18, 0), (30, 57, 6, 14, .5), (250, 57, 6.5, -24, 1), (120, 57, 5, 20, .3), (180, 56, 4, 0, .8), (75, 58, 4, 10, 1.2)):
        x, y = P(r, a)
        anim = f'<animateTransform attributeName="transform" type="scale" additive="sum" values="1;1.18;1" dur="1.3s" begin="{d}s" repeatCount="indefinite"/>'
        b += heart(round(x, 1), round(y, 1), s, 'url(#pk)', rot, anim)
    # speech bubble with heart eyes
    b += '<g transform="translate(14 10)"><rect x="-10" y="-8" width="20" height="15" rx="7" fill="#fff"/><path d="M-3,6 L-6,11 L2,6Z" fill="#fff"/>'
    b += heart(-4, -1, 3.4, '#ff3d8b') + heart(4, -1, 3.4, '#ff3d8b') + '</g>'
    return svg(b, defs)
D['deco-hearts'] = hearts()

# ---------------- bubbles + lightning (Locke card) ----------------
def bubbles():
    defs = GLOW + ('<radialGradient id="bb" cx=".35" cy=".3" r=".8"><stop offset="0" stop-color="#ffffff" stop-opacity=".95"/><stop offset=".35" stop-color="#7ff3ff" stop-opacity=".35"/><stop offset="1" stop-color="#22c4ff" stop-opacity=".75"/></radialGradient>')
    b = '<g filter="url(#gl)">' + ring(49.6, 52, '#53e3ff')
    import random
    random.seed(3)
    for i in range(16):
        a = random.uniform(0, 360); r = random.uniform(52, 58); x, y = P(r, a); s = random.uniform(1.4, 4.2)
        b += f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{s:.1f}" fill="url(#bb)" stroke="#bff8ff" stroke-width=".4"><animate attributeName="cy" values="{y:.1f};{y - 3:.1f};{y:.1f}" dur="{random.uniform(2, 3.4):.1f}s" repeatCount="indefinite"/></circle>'
    for (a, rot) in ((320, -20), (60, 25), (200, 160)):
        x, y = P(56, a)
        b += f'<path transform="translate({x:.1f} {y:.1f}) rotate({rot})" d="M-1,-7 L3,-1 L0,-1 L2,7 L-3,0 L0,0Z" fill="#dffbff"><animate attributeName="opacity" values="1;0;1;1" dur="2s" repeatCount="indefinite"/></path>'
    b += '</g>'
    return svg(b, defs)
D['deco-bubbles'] = bubbles()

# ---------------- sakura branch ----------------
def sakura():
    defs = SH + '<radialGradient id="pt" cx=".5" cy=".5" r=".6"><stop offset="0" stop-color="#fff"/><stop offset=".5" stop-color="#ffc6dd"/><stop offset="1" stop-color="#ff7fb0"/></radialGradient>'
    b = '<g filter="url(#sh)"><path d="M120,30 C100,18 84,6 60,4 C44,3 30,8 20,16" fill="none" stroke="#5b3424" stroke-width="3.2" stroke-linecap="round"/>'
    b += '<path d="M96,18 C100,8 104,4 110,2" fill="none" stroke="#5b3424" stroke-width="2" stroke-linecap="round"/><path d="M48,5 C44,-1 38,-2 34,0" fill="none" stroke="#5b3424" stroke-width="1.6" stroke-linecap="round"/>'
    def flower(x, y, s):
        o = f'<g transform="translate({x} {y})">'
        for k in range(5):
            o += f'<ellipse cx="0" cy="{-s * .55:.1f}" rx="{s * .38:.1f}" ry="{s * .55:.1f}" fill="url(#pt)" transform="rotate({k * 72})"/>'
        return o + f'<circle r="{s * .2:.1f}" fill="#ffd34d"/></g>'
    for x, y, s in ((112, 26, 8), (98, 14, 9), (82, 7, 7), (64, 4, 8.5), (46, 6, 6.5), (30, 10, 7.5), (108, 4, 6), (20, 18, 6)):
        b += flower(x, y, s)
    b += '</g>'
    for x, y, r in ((110, 52, 30), (102, 76, -20), (8, 40, 50)):
        b += f'<ellipse cx="{x}" cy="{y}" rx="3.6" ry="2.1" transform="rotate({r} {x} {y})" fill="#ffc6dd"><animate attributeName="opacity" values="1;.4;1" dur="2.4s" repeatCount="indefinite"/></ellipse>'
    return svg(b, defs)
D['deco-sakura'] = sakura()

# ---------------- headphones ----------------
def headphones():
    defs = SH + ('<linearGradient id="hp" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#3b3f4a"/><stop offset="1" stop-color="#0d0f14"/></linearGradient>'
                 '<linearGradient id="ac" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#ff5ad1"/><stop offset="1" stop-color="#7a5cff"/></linearGradient>')
    b = '<g filter="url(#sh)">'
    b += f'<path d="{arc(55, -78, 78)}" fill="none" stroke="url(#hp)" stroke-width="7" stroke-linecap="round"/>'
    b += f'<path d="{arc(55, -60, 60)}" fill="none" stroke="url(#ac)" stroke-width="1.6" stroke-linecap="round"/>'
    for side in (-1, 1):
        cx = 60 + side * 52
        b += f'<rect x="{cx - 9}" y="48" width="18" height="30" rx="8" fill="url(#hp)" stroke="#000" stroke-width=".6"/>'
        b += f'<rect x="{cx - 5}" y="53" width="10" height="20" rx="5" fill="url(#ac)"/>'
    b += '</g>'
    return svg(b, defs)
D['deco-headphones'] = headphones()

# ---------------- cat ears ----------------
def catears():
    defs = SH
    b = '<g filter="url(#sh)">'
    for side in (-1, 1):
        t = f'rotate({side * 32} 60 60)'
        b += f'<g transform="{t}"><path d="M44,16 L60,-6 L76,16 Q60,10 44,16Z" fill="#2b2b33" stroke="#111" stroke-width=".8"/><path d="M50,13 L60,0 L70,13 Q60,9 50,13Z" fill="#ff9ccc"/></g>'
    b += '</g>'
    return svg(b, defs)
D['deco-catears'] = catears()

# ---------------- remade classics (same ids, now hugging the avatar) ----------------
def simple_ring(c1, c2, glow=True, anim=''):
    defs = GLOW + f'<linearGradient id="g" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="{c1}"/><stop offset="1" stop-color="{c2}"/></linearGradient>'
    return svg(f'<g {"filter=\"url(#gl)\"" if glow else ""}>' + ring(49.5, 53.5, 'url(#g)') + anim + '</g>', defs)
D['deco-ring'] = simple_ring('#fff1b0', '#d08a12')
D['deco-neon'] = simple_ring('#9fe0ff', '#4a6bff')
D['deco-hd'] = simple_ring('#e2a8ff', '#7a2cff')
def fire():
    defs = GLOW + '<linearGradient id="fl" x1="0" y1="1" x2="0" y2="0"><stop offset="0" stop-color="#ffde4a"/><stop offset=".5" stop-color="#ff6a1a"/><stop offset="1" stop-color="#d4140a" stop-opacity="0"/></linearGradient>'
    b = '<g filter="url(#gl)">' + ring(49.5, 52.5, '#ff7a1a')
    for k, a in enumerate(range(0, 360, 20)):
        h = 7 + (k % 3) * 2.5
        b += (f'<path transform="rotate({a} 60 60)" d="M55.5,9 Q57,{8 - h * .5:.1f} 60,{9 - h:.1f} Q63,{8 - h * .5:.1f} 64.5,9Z" fill="url(#fl)">'
              f'<animate attributeName="opacity" values="1;.55;1" dur="{0.7 + (k % 4) * .15:.2f}s" repeatCount="indefinite"/></path>')
    return svg(b + '</g>', defs)
D['deco-fire'] = fire()
def crown():
    defs = SH + '<linearGradient id="gold" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#fff2b0"/><stop offset=".5" stop-color="#f2b53a"/><stop offset="1" stop-color="#9a5b10"/></linearGradient>'
    b = '<g filter="url(#sh)" transform="rotate(-14 60 14)"><path d="M38,24 L34,4 L46,14 L53,0 L60,12 L67,0 L74,14 L86,4 L82,24Z" fill="url(#gold)" stroke="#7a4708" stroke-width=".8"/>'
    b += '<rect x="37" y="22" width="46" height="6" rx="2" fill="url(#gold)" stroke="#7a4708" stroke-width=".8"/>'
    b += '<circle cx="60" cy="25" r="2.2" fill="#e0263b"/><circle cx="48" cy="25" r="1.7" fill="#2f7bff"/><circle cx="72" cy="25" r="1.7" fill="#22c55e"/>'
    b += '<circle cx="34" cy="4" r="1.6" fill="#fff6c2"/><circle cx="53" cy="0.5" r="1.6" fill="#fff6c2"/><circle cx="67" cy="0.5" r="1.6" fill="#fff6c2"/><circle cx="86" cy="4" r="1.6" fill="#fff6c2"/></g>'
    return svg(b, defs)
D['deco-crown'] = crown()
def halo():
    defs = GLOW
    b = '<g filter="url(#gl)"><ellipse cx="60" cy="6" rx="26" ry="6" fill="none" stroke="#fff6b0" stroke-width="3.2"><animate attributeName="cy" values="6;3;6" dur="2.6s" repeatCount="indefinite"/></ellipse></g>'
    b += ring(49.8, 51.2, 'rgba(255,246,176,.75)')
    return svg(b, defs)
D['deco-halo'] = halo()

for k, v in D.items():
    with open(os.path.join(OUT, k + '.svg'), 'w') as f:
        f.write(v)
print(len(D), 'decorations ->', os.path.normpath(OUT))

# ---------------- profile effects (full-card overlays, 300x500, stretched with preserveAspectRatio=none) ----------------
FX = os.path.join(OUT, '..', 'fx'); os.makedirs(FX, exist_ok=True)
import random
def fxsvg(body, defs='', w=300, h=500):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}" preserveAspectRatio="none">'
            f'<defs>{defs}</defs>{body}</svg>')
def petal(x, y, s, dur, beg, drift):
    return (f'<ellipse cx="0" cy="0" rx="{s}" ry="{s * .6:.1f}" fill="#ffc0d8" opacity=".9">'
            f'<animateMotion path="M{x},{y} q{drift},260 {drift * .4:.0f},560" dur="{dur}s" begin="{beg}s" repeatCount="indefinite"/>'
            f'<animateTransform attributeName="transform" type="rotate" values="0;360" dur="{dur / 2:.1f}s" repeatCount="indefinite"/></ellipse>')
random.seed(11)
b = ''.join(petal(random.uniform(0, 300), -30, random.uniform(3, 5.5), random.uniform(6, 11), -random.uniform(0, 10), random.uniform(-60, 60)) for _ in range(18))
br = '<path d="M300,0 C270,30 250,40 214,46 M262,32 C254,58 246,70 236,84" fill="none" stroke="#5b3424" stroke-width="4" stroke-linecap="round"/>'
fl = ''
for x, y, s in ((290, 10, 9), (268, 28, 10), (244, 42, 8), (220, 46, 9), (252, 64, 8), (238, 84, 7)):
    fl += f'<g transform="translate({x} {y})">' + ''.join(f'<ellipse cx="0" cy="{-s * .55:.1f}" rx="{s * .38:.1f}" ry="{s * .55:.1f}" fill="#ffc6dd" transform="rotate({k * 72})"/>' for k in range(5)) + f'<circle r="{s * .2:.1f}" fill="#ffd34d"/></g>'
open(os.path.join(FX, 'fx-sakura.svg'), 'w').write(fxsvg(b + br + fl))
b = ''
for _ in range(22):
    x = random.uniform(0, 300); s = random.uniform(4, 14); d = random.uniform(5, 10)
    b += (f'<circle cx="{x:.0f}" cy="540" r="{s:.1f}" fill="url(#bb)" stroke="#bff8ff" stroke-width=".8">'
          f'<animate attributeName="cy" from="540" to="-30" dur="{d:.1f}s" begin="{-random.uniform(0, d):.1f}s" repeatCount="indefinite"/></circle>')
open(os.path.join(FX, 'fx-bubbles.svg'), 'w').write(fxsvg(b, '<radialGradient id="bb" cx=".35" cy=".3" r=".8"><stop offset="0" stop-color="#fff" stop-opacity=".9"/><stop offset=".4" stop-color="#7ff3ff" stop-opacity=".15"/><stop offset="1" stop-color="#22c4ff" stop-opacity=".6"/></radialGradient>'))
b = '<rect x="3" y="3" width="294" height="494" rx="14" fill="none" stroke="url(#gd)" stroke-width="4"/>'
for _ in range(70):
    side = random.random()
    if side < .5: x, y = random.choice((random.uniform(2, 14), random.uniform(286, 298))), random.uniform(0, 500)
    else: x, y = random.uniform(0, 300), random.choice((random.uniform(2, 14), random.uniform(486, 498)))
    b += f'<circle cx="{x:.0f}" cy="{y:.0f}" r="{random.uniform(.6, 1.8):.1f}" fill="#fff3b0"><animate attributeName="opacity" values="1;.1;1" dur="{random.uniform(1, 2.6):.1f}s" begin="{random.uniform(0, 2):.1f}s" repeatCount="indefinite"/></circle>'
for x, y, s in ((24, 22, 10), (278, 140, 8), (18, 300, 7), (282, 470, 11), (150, 488, 6)):
    b += star4(x, y, s, '#fffbe0', (random.uniform(1.4, 2.4), random.uniform(0, 1)))
open(os.path.join(FX, 'fx-sparkle.svg'), 'w').write(fxsvg(b, '<linearGradient id="gd" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#fff2b0"/><stop offset=".5" stop-color="#f2b53a"/><stop offset="1" stop-color="#b8740e"/></linearGradient>'))
cols = ['#ff4fd8', '#3ff0ff', '#7dff6a', '#b26bff', '#ffd44f']
b = '<path d="M0,6 Q37,30 75,6 T150,6 T225,6 T300,6" fill="none" stroke="#e9d9ff" stroke-width="1.4"/>'
b += '<path d="M6,0 Q30,62 6,125 T6,250 T6,375 T6,500 M294,0 Q270,62 294,125 T294,250 T294,375 T294,500" fill="none" stroke="#e9d9ff" stroke-width="1.4"/>'
k = 0
for x, y in [(37.5, 17), (112.5, 17), (187.5, 17), (262.5, 17)] + [(17, 62.5 + 125 * i) for i in range(4)] + [(283, 62.5 + 125 * i) for i in range(4)]:
    c = cols[k % 5]
    b += f'<ellipse cx="{x}" cy="{y + 6}" rx="5" ry="7" fill="{c}" filter="url(#gl)"><animate attributeName="opacity" values="1;.3;1" dur="1.6s" begin="{(k % 3) * .53:.2f}s" repeatCount="indefinite"/></ellipse>'
    k += 1
open(os.path.join(FX, 'fx-lights.svg'), 'w').write(fxsvg(b, GLOW))
print('4 effects ->', os.path.normpath(FX))
