import re, json
F='/home/user/workspace/MW3SAMP/gamemodes/CODMW3.pwn'
s=open(F,encoding='latin1').read()
num=r'(-?[\d.]+)'
gz={m.group(1):[float(m.group(i)) for i in range(2,6)] for m in re.finditer(r'Zone\[(\w+)\]\s*=\s*GangZoneCreate\(\s*'+num+r'\s*,\s*'+num+r'\s*,\s*'+num+r'\s*,\s*'+num,s)}
cp={m.group(1):[float(m.group(i)) for i in range(2,5)] for m in re.finditer(r'\bCP\[(\w+)\]\s*=\s*CreateDynamicCP\(\s*'+num+r'\s*,\s*'+num+r'\s*,\s*'+num,s)}
names={m.group(1):m.group(2) for m in re.finditer(r'case\s+(\w+)\s*:\s*zname\s*=\s*"([^"]*)"',s)}
# zone order = enum order: use names dict order
zones=[]
for k,n in names.items():
    if k in gz and k in cp: zones.append((k,n,gz[k],cp[k]))
print(len(zones), [z[0] for z in zones])
missing=[k for k in names if k not in gz or k not in cp]; print('missing',missing)
def arr(name):
    m=re.search(r'new Float:'+name+r'\s*\[\s*\]\s*\[\s*\]\s*=\s*\{(.*?)\};',s,re.S)
    return [[float(x) for x in re.findall(num,g)] for g in re.findall(r'\{([^{}]*)\}',m.group(1))]
sp={}
for t in ['EurasiaSpawn','ArabSpawn','RussiaSpawn','USSpawn','AusSpawn','CHspawn','IDspawn','FranceSpawn']:
    sp[t]=arr(t)
print({k:len(v) for k,v in sp.items()})
ar={}
for a in ['DMS','SDMS','MGDMS','CHDMS','CBDMS','HSDMS']:
    try: ar[a]=arr(a)
    except Exception as e: print('fail',a,e)
print({k:len(v) for k,v in ar.items()})
objs=re.findall(r'Create(?:Dynamic)?Object\(\s*(\d+)\s*,\s*'+num+r'\s*,\s*'+num+r'\s*,\s*'+num+r'\s*,\s*'+num+r'\s*,\s*'+num+r'\s*,\s*'+num,s)
objs=[(int(o[0]),)+tuple(float(x) for x in o[1:]) for o in objs]
objs=[o for o in objs if o[0] not in (348,349,350,351,353,1245) and not (o[1]==0 and o[2]==0)]
print(len(objs))
cmd=[(m.group(1),[float(m.group(i)) for i in range(2,5)]) for m in re.finditer(r'CMDCP\[(\w+)\]\s*=\s*CreateDynamicCP\(\s*'+num+r'\s*,\s*'+num+r'\s*,\s*'+num,s)]
print(cmd)
json.dump(dict(zones=zones,sp=sp,ar=ar,objs=objs,cmd=cmd),open('/tmp/mw3/data.json','w'))
