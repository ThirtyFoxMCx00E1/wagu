import numpy as np, random, json
from PIL import Image, ImageDraw
h=np.load('/tmp/hmap.npy')
def H(x,y): return h[int(round(3000-y)),int(round(x+3000))]
X0,X1,Y0,Y1=2360,2820,-2710,-2310
def flat(cx,cy,hw,hh,ang=0):
    import math
    ca,sa=math.cos(ang),math.sin(ang)
    for u in np.arange(-hw,hw+0.1,1.0):
        for v in np.arange(-hh,hh+0.1,1.0):
            x=cx+u*ca-v*sa; y=cy+u*sa+v*ca
            z=H(x,y)
            if not (12.3<=z<=12.95): return False
    return True
# spawn candidates
cands=[]
for x in range(X0+12,X1-12,4):
    for y in range(Y0+12,Y1-12,4):
        if flat(x,y,11,11): cands.append((x,y))
print(len(cands))
# farthest point sampling for 8 spawns
random.seed(3)
sp=[min(cands,key=lambda p:(p[0]-2380)**2+(p[1]+2690)**2)]
while len(sp)<8:
    sp.append(max(cands,key=lambda p:min((p[0]-q[0])**2+(p[1]-q[1])**2 for q in sp)))
print(sp)
json.dump(sp,open('sp.json','w'))
