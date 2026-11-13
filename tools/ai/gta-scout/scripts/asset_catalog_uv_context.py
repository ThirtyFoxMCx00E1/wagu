#!/usr/bin/env python3
"""Plot actual DFF material UV triangles over a native texture for context review.
This diagnostic never assigns a material identity, semantic description or verdict.
The caller must verify the model→TXD→pixel binding separately.
"""
import os
import argparse, hashlib, json, math, sys
from pathlib import Path
from PIL import Image, ImageDraw
from asset_catalog_dragon import clumps
ROOT=Path(__file__).resolve().parents[1]
DRAGONFF_ROOT=Path(os.environ.get('DRAGONFF_PATH', str(ROOT/'references/DragonFF')))
sys.path.insert(0,str(DRAGONFF_ROOT))


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def material_uvs(data, name):
    """Return source UVs, including out-of-tile coordinates without wrapping them."""
    result=[]
    geometries=[g for c in clumps(data) for g in c.geometry_list]
    for gi,g in enumerate(geometries):
        selected={i for i,m in enumerate(g.materials) if any(t.name.casefold()==name.casefold() for t in m.textures)}
        if not selected:continue
        if not g.uv_layers:raise ValueError('Matched material has no UV layer')
        for ti,t in enumerate(g.triangles):
            if t.material not in selected:continue
            vertices=[t.a,t.b,t.c]
            uv=[[float(g.uv_layers[0][i].u),float(g.uv_layers[0][i].v)] for i in vertices]
            if not all(math.isfinite(v) for xy in uv for v in xy):raise ValueError('Non-finite UV')
            result.append({'geometry':gi,'triangle':ti,'material':t.material,'vertexIndices':vertices,'uv':uv})
    if not result:raise ValueError('No UV triangles reference the requested texture name')
    return result


def export(dff_path, texture_name, image_path, out, size=768):
    if not 128<=size<=2048:raise ValueError('Diagnostic size outside bounds')
    dff_path=Path(dff_path).resolve();image_path=Path(image_path).resolve();out=Path(out).resolve()
    before={'dff':sha(dff_path),'textureImage':sha(image_path)}
    from gtaLib.dff import dff
    model=dff();model.load_file(str(dff_path));triangles=material_uvs(model,texture_name)
    with Image.open(image_path) as native:
        native.load();dimensions=list(native.size);image=native.convert('RGBA').resize((size,size),Image.Resampling.NEAREST)
    # Gray checkerboard shows source alpha; bright green lines are synthetic UV edges.
    base=Image.new('RGBA',(size,size));draw=ImageDraw.Draw(base)
    cell=24
    for y in range(0,size,cell):
        for x in range(0,size,cell):draw.rectangle((x,y,x+cell-1,y+cell-1),fill=(180,180,180,255) if (x//cell+y//cell)%2 else (225,225,225,255))
    base=Image.alpha_composite(base,image);draw=ImageDraw.Draw(base)
    for triangle in triangles:
        pts=[(u*size,v*size) for u,v in triangle['uv']]
        draw.line(pts+[pts[0]],fill=(0,255,70,255),width=1)
    if before!={'dff':sha(dff_path),'textureImage':sha(image_path)}:raise ValueError('Source changed during UV diagnostic')
    out.mkdir(parents=True,exist_ok=True);target=out/'uv-overlay.png';base.convert('RGB').save(target)
    coords=[uv for t in triangles for uv in t['uv']]
    report={'version':'dff-uv-context-v1','generatorSha256':sha(__file__),'uvLayer':0,'dff_path':str(dff_path),'dff_sha256':before['dff'],'image_path':str(image_path),'image_sha256':before['textureImage'],'nativeDimensions':dimensions,'textureName':texture_name,'triangleCount':len(triangles),'uvBounds':[[min(v[i] for v in coords),max(v[i] for v in coords)] for i in (0,1)],'triangles':triangles,'overlay_path':str(target),'overlay_sha256':sha(target),'interpretation':'Synthetic green UV edges from DFF over supplied native texture; checkerboard denotes alpha. Unit-square plot changes non-square image aspect; consult native image for appearance. Only base tile shown, out-of-range UVs clipped, not wrapped. Texture binding and semantic use require separate verification. Skinned vertex pose does not change these source UV coordinates.'}
    (out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n');return report


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--dff',required=True);p.add_argument('--texture-name',required=True);p.add_argument('--image',required=True);p.add_argument('--out',required=True);a=p.parse_args();r=export(a.dff,a.texture_name,a.image,a.out);print(json.dumps({k:v for k,v in r.items() if k!='triangles'},indent=2))
if __name__=='__main__':main()
