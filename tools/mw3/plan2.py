import numpy as np, random, json, math
from PIL import Image, ImageDraw
h=np.load('/tmp/hmap.npy')
def H(x,y): return h[int(round(3000-y)),int(round(x+3000))]
X0,X1,Y0,Y1=2360,2820,-2710,-2310
sp=json.load(open('sp.json'))
def corners(cx,cy,hw,hh,a):
    ca,sa=math.cos(a),math.sin(a)
    return [(cx+u*ca-v*sa,cy+u*sa+v*ca) for u,v in ((-hw,-hh),(hw,-hh),(hw,hh),(-hw,hh))]
def flat(cx,cy,hw,hh,a):
    ca,sa=math.cos(a),math.sin(a)
    for u in np.arange(-hw,hw+0.01,1.0):
        for v in np.arange(-hh,hh+0.01,0.7):
            x=cx+u*ca-v*sa; y=cy+u*sa+v*ca
            if not (X0<x<X1 and Y0<y<Y1): return False
            z=H(x,y)
            if not (12.3<=z<=12.95): return False
    return True
def sat(A,B):
    for poly in (A,B):
        for i in range(4):
            x1,y1=poly[i];x2,y2=poly[(i+1)%4]
            nx,ny=y2-y1,x1-x2
            pa=[nx*x+ny*y for x,y in A]; pb=[nx*x+ny*y for x,y in B]
            if max(pa)<min(pb) or max(pb)<min(pa): return False
    return True
random.seed(11)
HW,HH=6.1,1.3
placed=[]
models=[2932,2934,2935]
tries=0
while len(placed)<56 and tries<200000:
    tries+=1
    cx=random.uniform(X0,X1); cy=random.uniform(Y0,Y1)
    a=random.choice([0,math.pi/2,math.pi/4,3*math.pi/4]) if random.random()<0.85 else random.uniform(0,math.pi)
    if any((cx-s[0])**2+(cy-s[1])**2<24**2 for s in sp): continue
    if not flat(cx,cy,HW+0.5,HH+0.5,a): continue
    mine=corners(cx,cy,HW+2.6,HH+2.6,a)   # 5.2 m corridors
    if any(sat(mine,corners(p[0],p[1],HW,HH,p[2])) for p in placed): continue
    placed.append((cx,cy,a))
print(len(placed),tries)
out=[]
for i,(cx,cy,a) in enumerate(placed):
    z=float(np.median([H(x,y) for x,y in corners(cx,cy,HW,HH,a)]))
    m=models[i%3]
    rz=math.degrees(a)
    out.append((m,round(cx,2),round(cy,2),round(z+1.3,2),round(rz,1)))
    if i%3==0: out.append((models[(i+1)%3],round(cx,2),round(cy,2),round(z+1.3+2.85,2),round(rz,1)))
# sandbag cover at each spawn (toward arena centre) + barrels
cxm,cym=2590,-2510
for (x,y) in sp:
    dx,dy=cxm-x,cym-y; l=math.hypot(dx,dy); dx/=l; dy/=l
    for off in (-3.5,0,3.5):
        px=x+dx*12-dy*off; py=y+dy*12+dx*off
        out.append((2060,round(px,2),round(py,2),round(H(px,py)+0.3,2),round(math.degrees(math.atan2(dy,dx))+90,1)))
json.dump(out,open('objs.json','w'))
im=Image.open('/tmp/docks.png'); d=ImageDraw.Draw(im)
S=1600/800; 
def P(x,y): return ((x-2100)*S,(-2150-y)*S)
for (cx,cy,a) in placed:
    d.polygon([P(*c) for c in corners(cx,cy,HW,HH,a)],fill=(255,140,0))
for i,(x,y) in enumerate(sp):
    q=P(x,y); d.ellipse([q[0]-14,q[1]-14,q[0]+14,q[1]+14],outline=(0,255,0),width=4); d.text((q[0]-4,q[1]-6),str(i),fill=(0,255,0))
d.rectangle([P(X0,Y1),P(X1,Y0)],outline=(255,255,0),width=3)
im.save('/tmp/mw3/plan.png')
print(len(out))
