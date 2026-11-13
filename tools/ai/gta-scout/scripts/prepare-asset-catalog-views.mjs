#!/usr/bin/env node
/** Extract selected stock SA IMG entries and TXD PNGs for render-asset-catalog-views.py.
 * node scripts/prepare-asset-catalog-views.mjs GAME OUTPUT --catalog-root CATALOG ID [ID ...]
 * Reads game only. Records exact archive, entry offsets, hashes and decode recipe.
 */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {TXDReader} from '../shared/TXDReader.js';
import {PNG} from 'pngjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const [gameArg,outArg,...rest]=process.argv.slice(2);
const ids=[],extraArchives=[];let textureCachePath;let catalogRoot;
for(let i=0;i<rest.length;i++){
 if(rest[i]==='--catalog-root'){if(!rest[i+1])throw Error('--catalog-root requires a path');catalogRoot=path.resolve(rest[++i]);}
 else if(rest[i]==='--archive'){if(!rest[i+1])throw Error('--archive requires a path');extraArchives.push(path.resolve(rest[++i]));}
 else if(rest[i]==='--texture-cache'){if(!rest[i+1])throw Error('--texture-cache requires a manifest');textureCachePath=path.resolve(rest[++i]);}
 else ids.push(rest[i]);
}
if(!gameArg||!outArg||!ids.length||!catalogRoot)throw Error('Usage: node scripts/prepare-asset-catalog-views.mjs GAME OUTPUT --catalog-root CATALOG ID [ID ...]');
const game=path.resolve(gameArg),out=path.resolve(outArg),sources=path.join(out,'sources');fs.mkdirSync(sources,{recursive:true});
const digest=b=>crypto.createHash('sha256').update(b).digest('hex');
const nativeCache=new Map(textureCachePath?JSON.parse(fs.readFileSync(textureCachePath)).images.map(i=>[i.pixelSha256,i]):[]),verifiedNative=new Set();
function cachedTexture(t){
 const pixel=digest(Buffer.concat([Buffer.from(`${t.width}x${t.height}:`),Buffer.from(t.imageData)])),entry=nativeCache.get(pixel);if(!entry)return null;
 if(!verifiedNative.has(pixel)){const bytes=fs.readFileSync(entry.imagePath);if(digest(bytes)!==entry.imageSha256)throw Error('Shared PNG cache bytes changed');const im=PNG.sync.read(bytes);if(digest(Buffer.concat([Buffer.from(`${im.width}x${im.height}:`),Buffer.from(im.data)]))!==pixel)throw Error('Shared PNG pixel identity mismatch');verifiedNative.add(pixel);}
 return entry.imagePath;
}
const entries=new Map(),handles=[];
const catalog=JSON.parse(fs.readFileSync(path.join(catalogRoot,'models/data/models.json')));
const models=new Map(catalog.models.map(m=>[String(m.id),m]));
const manifest={assets:[],failures:[],decodeRecipe:{scriptSha256:digest(fs.readFileSync(fileURLToPath(import.meta.url))),txdReaderSha256:digest(fs.readFileSync(path.join(root,'shared/TXDReader.js'))),output:'PNG RGBA native resolution'}};
try{
 const archives=[...fs.readdirSync(path.join(game,'models')).filter(n=>n.toLowerCase().endsWith('.img')).sort().map(n=>path.join(game,'models',n)),...extraArchives];
 for(const archive of [...new Set(archives)]){
  const fd=fs.openSync(archive,'r');handles.push(fd);const h=Buffer.alloc(8);fs.readSync(fd,h,0,8,0);if(h.subarray(0,4).toString()!=='VER2')continue;
  const count=h.readUInt32LE(4);const directory=Buffer.alloc(count*32);if(fs.readSync(fd,directory,0,directory.length,8)!==directory.length)throw Error('Truncated IMG directory: '+archive);
  for(let i=0;i<count;i++){const e=directory.subarray(i*32,i*32+32),name=e.subarray(8,32).toString().split('\0')[0].toLowerCase();if(!entries.has(name))entries.set(name,[]);entries.get(name).push({fd,archive,offset:e.readUInt32LE(0)*2048,length:(e.readUInt16LE(4)||e.readUInt16LE(6))*2048});}
 }
 // Shared vehicle/clothing dictionaries can be loose files, outside IMG archives.
 const loose=new Map();
 function visit(folder){for(const dirent of fs.readdirSync(folder,{withFileTypes:true})){const p=path.join(folder,dirent.name);if(dirent.isDirectory())visit(p);else if(dirent.isFile()&&/\.(txd|dff)$/i.test(dirent.name)){const key=dirent.name.toLowerCase();if(!loose.has(key))loose.set(key,[]);loose.get(key).push(p);}}}
 visit(path.join(game,'models'));
 function extract(name,provenance){
  let candidates=entries.get(name.toLowerCase())||[];
  let looseCandidates=loose.get(name.toLowerCase())||[];
  const explicitLoose=provenance?.toLowerCase().endsWith('.dff')&&name.toLowerCase().endsWith('.dff');
  if(explicitLoose){looseCandidates=looseCandidates.filter(p=>path.relative(game,p).replaceAll('\\','/').toLowerCase()===provenance.replaceAll('\\','/').toLowerCase());candidates=[];if(looseCandidates.length!==1)throw Error('Explicit loose DFF source unresolved: '+provenance);}
  if(provenance?.toLowerCase().endsWith('.img')){
   const preferred=candidates.filter(e=>path.basename(e.archive).toLowerCase()===path.basename(provenance).toLowerCase());
   if(preferred.length||name.toLowerCase().endsWith('.dff'))candidates=preferred;
   // A standalone player mesh can reference the unique shared player dictionary in gta3.img.
  }
  else candidates=candidates.filter(e=>path.basename(e.archive).toLowerCase()!=='player.img');
  if(provenance==='SAMP.ide'&&candidates.some(e=>path.basename(e.archive).toLowerCase()==='samp.img'))candidates=candidates.filter(e=>path.basename(e.archive).toLowerCase()==='samp.img');
  else if(provenance!=='SAMP.ide')candidates=candidates.filter(e=>path.basename(e.archive).toLowerCase()!=='samp.img');
  if(candidates.length>1)throw Error('Ambiguous IMG sources '+name+': '+candidates.map(e=>e.archive).join(', '));
  const entry=candidates[0];let buffer,source;
  if(entry){buffer=Buffer.alloc(entry.length);if(fs.readSync(entry.fd,buffer,0,buffer.length,entry.offset)!==buffer.length)throw Error('Truncated entry '+name);source={archive:entry.archive,entry:name.toLowerCase(),offset:entry.offset,bytes:entry.length,sha256:digest(buffer)};}
  else {if(!looseCandidates.length)throw Error('Missing IMG or loose entry '+name);if(looseCandidates.length!==1)throw Error('Ambiguous loose source '+name+': '+looseCandidates.join(', '));buffer=fs.readFileSync(looseCandidates[0]);source={file:looseCandidates[0],entry:name.toLowerCase(),offset:0,bytes:buffer.length,sha256:digest(buffer)};}
  const binaryDir=path.join(sources,digest(buffer));fs.mkdirSync(binaryDir,{recursive:true});
  const target=path.join(binaryDir,name.toLowerCase());if(!fs.existsSync(target)||digest(fs.readFileSync(target))!==digest(buffer))fs.writeFileSync(target,buffer);return {buffer,path:target,source};
 }
 for(const id of ids){try{const m=models.get(id);if(!m)throw Error('Unknown model '+id);const dff=extract((m.dff||m.name)+'.dff',m.source),txd=extract(m.txd+'.txd',m.source);const a={id:'sa:model:'+id,model:m.name,dff:dff.path,sourceFiles:[txd.path],sources:[dff.source,txd.source],textures:{}};const dir=path.join(sources,digest(txd.buffer),'textures');fs.mkdirSync(dir,{recursive:true});const b=txd.buffer;const parsed=new TXDReader().parse(b.buffer.slice(b.byteOffset,b.byteOffset+b.byteLength));for(const t of parsed.textures){if(!t.imageData)continue;if(path.basename(t.name)!==t.name)throw Error('Unsafe texture name');const shared=cachedTexture(t);if(shared){a.textures[t.name.toLowerCase()]=shared;continue;}const png=new PNG({width:t.width,height:t.height});png.data=Buffer.from(t.imageData);const target=path.join(dir,t.name+'.png');fs.writeFileSync(target,PNG.sync.write(png));a.textures[t.name.toLowerCase()]=target;}manifest.assets.push(a);}catch(error){manifest.failures.push({id,error:String(error)});}}
 fs.writeFileSync(path.join(out,'input.json'),JSON.stringify(manifest,null,2)+'\n');console.log(JSON.stringify({assets:manifest.assets.length,failures:manifest.failures,input:path.join(out,'input.json')}));if(manifest.failures.length)process.exitCode=1;
}finally{for(const fd of handles)fs.closeSync(fd);}
