#!/usr/bin/env python3
# MP 4.13.13: the Rocket League ball for the League arena.
# assets/MP/ball_-_rocket_league.glb (the real RL ball: mesh + 1024x1024 texture) ->
# src/game/lgball_data.h: a GTA SA dff (RenderWare 3.6, one atomic, the ball 7.42 m wide, centred on its origin)
# and the texture as a 512x512 JPEG. libMP puts this model in place of object 1946 (baskt_ball_hi) while you
# are in the League arena (src/game/lgball.cpp), so nothing has to be copied to the phone.
import struct, json, io, os, sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GLB = os.path.join(ROOT, 'assets', 'MP', 'ball_-_rocket_league.glb')
OUT = os.path.join(ROOT, 'src', 'game', 'lgball_data.h')
RADIUS = 0.9275 * 4.0          # LG_BALL_R (league.h)
VER = 0x1803FFFF               # RW 3.6.0.3 (GTA SA)

def load_glb(p):
    d = open(p, 'rb').read()
    jl = struct.unpack('<I', d[12:16])[0]
    j = json.loads(d[20:20 + jl])
    b = d[20 + jl + 8:]
    def acc(i):
        a = j['accessors'][i]; v = j['bufferViews'][a['bufferView']]
        off = v.get('byteOffset', 0) + a.get('byteOffset', 0)
        n = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3}[a['type']]
        fmt = {5126: 'f', 5125: 'I', 5123: 'H'}[a['componentType']]
        sz = struct.calcsize(fmt)
        stride = v.get('byteStride', sz * n)
        out = []
        for k in range(a['count']):
            vals = struct.unpack_from('<' + fmt * n, b, off + k * stride)
            out.append(vals if n > 1 else vals[0])
        return out
    bv = j['bufferViews'][j['images'][0]['bufferView']]
    png = b[bv.get('byteOffset', 0):bv.get('byteOffset', 0) + bv['byteLength']]
    return j, acc, png

def mat4(m):  # glTF column-major -> rows
    return [[m[c * 4 + r] for c in range(4)] for r in range(4)]
def mul(a, b): return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]
def xf(m, v, w): return tuple(m[r][0] * v[0] + m[r][1] * v[1] + m[r][2] * v[2] + m[r][3] * w for r in range(3))

j, acc, png = load_glb(GLB)
nodes = j['nodes']
# the model's own nodes (fbx + bola); the Sketchfab root only turns Z-up into glTF's Y-up, GTA is Z-up
M = mul(mat4(nodes[1]['matrix']), mat4(nodes[3]['matrix']))
pos, nrm, uv, tris = [], [], [], []
for mi, mesh in enumerate(j['meshes']):
    pr = mesh['primitives'][0]; base = len(pos)
    P = acc(pr['attributes']['POSITION']); N = acc(pr['attributes']['NORMAL']); T = acc(pr['attributes']['TEXCOORD_0'])
    I = acc(pr['indices'])
    for p, n, t in zip(P, N, T):
        pos.append(xf(M, p, 1)); q = xf(M, n, 0); l = (q[0] ** 2 + q[1] ** 2 + q[2] ** 2) ** 0.5 or 1; nrm.append(tuple(c / l for c in q))
        uv.append((t[0], t[1] - (t[1] // 1.0)))
    for k in range(0, len(I), 3):
        a, b2, c = I[k] + base, I[k + 1] + base, I[k + 2] + base
        tris.append((a, b2, c, mi)); tris.append((a, c, b2, mi))   # both sides (the glTF materials are double sided)
cx = [(min(p[i] for p in pos) + max(p[i] for p in pos)) / 2 for i in range(3)]
r0 = max(((p[0] - cx[0]) ** 2 + (p[1] - cx[1]) ** 2 + (p[2] - cx[2]) ** 2) ** 0.5 for p in pos)
s = RADIUS / r0
pos = [tuple((p[i] - cx[i]) * s for i in range(3)) for p in pos]
nv, nt = len(pos), len(tris)
assert nv < 65536

def chunk(t, data): return struct.pack('<III', t, len(data), VER) + data
def struct_(data): return chunk(0x01, data)
def string(sv):
    b = sv.encode() + b'\0'; b += b'\0' * ((4 - len(b) % 4) % 4); return chunk(0x02, b)
ext = lambda data=b'': chunk(0x03, data)

# frames: a root and the atomic's own frame (named like the GTA model)
fr = b''
for parent in (-1, 0):
    fr += struct.pack('<9f3fiI', 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, parent, 0)
frame_list = chunk(0x0E, struct_(struct.pack('<i', 2) + fr) + ext(chunk(0x0253F2FE, b'lgball_root')) + ext(chunk(0x0253F2FE, b'baskt_ball_hi')))

flags = 0x02 | 0x04 | 0x08 | 0x10 | 0x20 | 0x40     # positions, textured, prelit, normals, light, modulate material colour
g = struct.pack('<Iiii', flags | (1 << 16), nt, nv, 1)
g += b''.join(struct.pack('<4B', 255, 255, 255, 255) for _ in range(nv))
g += b''.join(struct.pack('<2f', *t) for t in uv)
g += b''.join(struct.pack('<4H', t[1], t[0], t[3], t[2]) for t in tris)
g += struct.pack('<4f', 0, 0, 0, RADIUS * 1.01) + struct.pack('<ii', 1, 1)
g += b''.join(struct.pack('<3f', *p) for p in pos)
g += b''.join(struct.pack('<3f', *n) for n in nrm)
# materials: 0 = the textured shell (texture set by libMP after loading), 1 = the glowing teal strips
mats = b''
for col in ((255, 255, 255, 255), (70, 225, 255, 255)):
    mats += chunk(0x07, struct_(struct.pack('<i4Bii3f', 0, *col, 0, 0, 1.0, 1.0, 1.0)) + ext())
mat_list = chunk(0x08, struct_(struct.pack('<iii', 2, -1, -1)) + mats)
# bin mesh PLG: one triangle list per material
bm = b''; per = [[t for t in tris if t[3] == m] for m in (0, 1)]
for m, ts in enumerate(per):
    bm += struct.pack('<II', len(ts) * 3, m) + b''.join(struct.pack('<3I', t[0], t[1], t[2]) for t in ts)
bin_mesh = chunk(0x050E, struct.pack('<III', 0, 2, nt * 3) + bm)
geometry = chunk(0x0F, struct_(g) + mat_list + ext(bin_mesh))
geo_list = chunk(0x1A, struct_(struct.pack('<i', 1)) + geometry)
atomic = chunk(0x14, struct_(struct.pack('<iiii', 1, 0, 5, 0)) + ext())
clump = chunk(0x10, struct_(struct.pack('<iii', 1, 0, 0)) + frame_list + geo_list + atomic + ext())

im = Image.open(io.BytesIO(png)).convert('RGB').resize((512, 512), Image.LANCZOS)
jb = io.BytesIO(); im.save(jb, 'JPEG', quality=88); jpg = jb.getvalue()

def arr(name, data):
    lines = [', '.join(str(x) for x in data[i:i + 32]) for i in range(0, len(data), 32)]
    return 'static const unsigned char %s[] = {\n%s\n};\n' % (name, ',\n'.join(lines))
with open(OUT, 'w') as f:
    f.write('// generated by tools/make_league_ball.py from assets/MP/ball_-_rocket_league.glb - do not edit\n#pragma once\n')
    f.write('static const int kLgBallVerts = %d, kLgBallTris = %d;\n' % (nv, nt))
    f.write(arr('kLgBallDff', clump)); f.write(arr('kLgBallJpg', jpg))
print('dff %d bytes (%d verts, %d tris), jpg %d bytes -> %s' % (len(clump), nv, nt, len(jpg), OUT))
