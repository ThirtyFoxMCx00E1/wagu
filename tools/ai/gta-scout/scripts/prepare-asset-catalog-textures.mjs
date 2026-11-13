#!/usr/bin/env node
/** Decode explicitly audited texture occurrences to immutable native RGBA PNGs. */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {TXDReader} from '../shared/TXDReader.js';
import {PNG} from 'pngjs';
const [auditArg,keysArg,outArg]=process.argv.slice(2);
if(!outArg)throw Error('Usage: node prepare-asset-catalog-textures.mjs TEXTURES_AUDIT KEYS OUTPUT');
const auditPath=path.resolve(auditArg),auditBytes=fs.readFileSync(auditPath),rows=JSON.parse(auditBytes),byKey=new Map(rows.map(r=>[r.key,r]));
const keys=JSON.parse(fs.readFileSync(keysArg)),out=path.resolve(outArg);fs.mkdirSync(out,{recursive:true});
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const cache=new Map(),overrides={},records=[],failures=[];const started=Date.now();
if(new Set(keys).size!==keys.length)throw Error('Repeated texture key');
for(const key of keys){try{
 const row=byKey.get(key);if(!row||row.status!=='resolved'||row.sources.length!==1)throw Error('No unambiguous audited source');
 const src=row.sources[0].source,sourcePath=src.path||src.file||src.archive,identity=JSON.stringify(src);
 if(!cache.has(identity)){
  const fd=fs.openSync(sourcePath,'r'),buffer=Buffer.alloc(src.bytes);
  try{if(fs.readSync(fd,buffer,0,buffer.length,src.offset)!==buffer.length||sha(buffer)!==src.sha256)throw Error('Audited source bytes changed');}finally{fs.closeSync(fd);}
  cache.set(identity,new TXDReader().parse(buffer.buffer.slice(buffer.byteOffset,buffer.byteOffset+buffer.byteLength)));
 }
 const textures=cache.get(identity).textures.filter(t=>t.name===row.name);if(textures.length!==1)throw Error('Texture occurrence ambiguous');
 const texture=textures[0];if(!texture.imageData)throw Error('Decoder returned no pixels');
 const pixelSha=sha(Buffer.concat([Buffer.from(`${texture.width}x${texture.height}:`),Buffer.from(texture.imageData)]));
 if(pixelSha!==row.pixelSha256)throw Error('Decoded pixel identity changed');
 const png=new PNG({width:texture.width,height:texture.height});png.data=Buffer.from(texture.imageData);
 const bytes=PNG.sync.write(png),pngPath=path.join(out,pixelSha+'.png');
 const cached=fs.existsSync(pngPath)&&sha(fs.readFileSync(pngPath))===sha(bytes);
 if(!cached)fs.writeFileSync(pngPath,bytes);
 overrides[key]=[pngPath];records.push({key,pixelSha256:pixelSha,pngPath,pngSha256:sha(bytes),width:texture.width,height:texture.height,source:src,cached});
}catch(error){failures.push({key,error:String(error)});}}
const report={sourceAudit:{path:auditPath,sha256:sha(auditBytes)},records,failures,seconds:(Date.now()-started)/1000,decodedDictionaries:cache.size,annotation_status:'Native image evidence prepared only; visual inspection still required.'};
fs.writeFileSync(path.join(out,'overrides.json'),JSON.stringify(overrides,null,2)+'\n');
fs.writeFileSync(path.join(out,'manifest.json'),JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify({ready:records.length,failures,seconds:report.seconds,decodedDictionaries:cache.size}));
if(failures.length)process.exitCode=1;
