import json, math
d=json.load(open('data.json')); o2=json.load(open('objs.json')); sp=json.load(open('sp.json'))
L=[]
w=L.append
w('// MP 3.23 - MW3 data (generated from MW3SAMP CODMW3.pwn: zones, team spawns, command centers, DM arenas,')
w('// map objects) + the LS Docks container arena (Cargo). Do not edit by hand: tools/mw3/gen.py')
w('#pragma once')
w('struct Mw3Zone { const char* key; const char* name; float minX, minY, maxX, maxY; float cp[3]; uint8_t special; };')
w('static const Mw3Zone g_Mw3Zones[] = {')
special={'BIG','MT','A52','OPP'}
for k,n,g,c in d['zones']:
    w('    { "%s", "%s", %.2ff, %.2ff, %.2ff, %.2ff, { %.4ff, %.4ff, %.4ff }, %d },'%(k,n,min(g[0],g[2]),min(g[1],g[3]),max(g[0],g[2]),max(g[1],g[3]),c[0],c[1],c[2],1 if k in special else 0))
w('};')
w('#define MW3_ZONE_COUNT %d'%len(d['zones']))
order=['EurasiaSpawn','ArabSpawn','RussiaSpawn','USSpawn','AusSpawn','CHspawn','IDspawn','FranceSpawn']
for t in order:
    v=d['sp'][t]
    w('static const float g_Mw3Sp_%s[][4] = { %s };'%(t,', '.join('{ %.4ff, %.4ff, %.4ff, %.2ff }'%(p[0],p[1],p[2],p[3] if len(p)>3 else 0.0) for p in v)))
w('struct Mw3SpawnSet { const float (*p)[4]; int n; };')
w('static const Mw3SpawnSet g_Mw3TeamSpawns[8] = { %s };'%', '.join('{ g_Mw3Sp_%s, %d }'%(t,len(d['sp'][t])) for t in order))
w('static const float g_Mw3CmdCP[8][3] = { %s };'%', '.join('{ %.4ff, %.4ff, %.4ff }'%tuple(c[1]) for c in d['cmd']))
for a in ['DMS','SDMS','MGDMS','CHDMS','CBDMS','HSDMS']:
    v=d['ar'][a]
    w('static const float g_Mw3Ar_%s[][3] = { %s };'%(a,', '.join('{ %.4ff, %.4ff, %.4ff }'%tuple(p[:3]) for p in v)))
objs=[x for x in d['objs'] if abs(x[1])<3000 and abs(x[2])<3000 and x[0]<18000]
w('struct Mw3Obj { uint16_t model; float x, y, z, rx, ry, rz; };')
w('// MW3SAMP map objects (bases / zones) + the LS Docks Cargo arena (shipping containers, sandbags)')
w('static const Mw3Obj g_Mw3Objects[] = {')
for x in objs: w('    { %d, %.4ff, %.4ff, %.4ff, %.2ff, %.2ff, %.2ff },'%tuple(x))
w('    // ---- LS Docks (Ocean Docks) Cargo arena ----')
for m,x,y,z,rz in o2: w('    { %d, %.2ff, %.2ff, %.2ff, 0.0f, 0.0f, %.1ff },'%(m,x,y,z,rz))
w('};')
w('#define MW3_OBJECT_COUNT ((int)(sizeof(g_Mw3Objects) / sizeof(g_Mw3Objects[0])))')
w('// Cargo arena spawn points (events, duels): open ground, 12.6 m docks level')
w('static const float g_Mw3DockSpawns[8][3] = { %s };'%', '.join('{ %.1ff, %.1ff, 13.6f }'%(p[0],p[1]) for p in sp))
open('/home/user/workspace/wagu/src/net/mw3_data.h','w').write('\n'.join(L)+'\n')
print(len(objs)+len(o2))
