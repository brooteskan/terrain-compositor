from pathlib import Path
import json,struct,sys,hashlib
import numpy as np
from PIL import Image
P=Path('D:/diversion/TG/TGProject');W=Path('D:/brooteskan/work');O=W/'texture-sampling';O.mkdir(exist_ok=True)
status=json.loads((P/'user/VueLiveLink/terrain_overlap_status.json').read_text());textures={};materials={}
for raw in status['materials'].values():
 path=P/Path(raw).with_suffix('.material');v=json.loads(path.read_text())['propertyValues'];materials[str(path.relative_to(P))]=v
 for name,value in v.items():
  if isinstance(value,str) and value and (P/value).is_file():textures.setdefault(value,[]).append(name)
textures.setdefault('Assets/VueLiveLink/ground_color.png',[]).append('ground and terrain contact')
rows=[]
for rel,uses in textures.items():
 path=P/rel;r={'path':rel,'uses':uses,'source_bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
 if path.suffix.lower()=='.dds':
  b=path.read_bytes();h,w=struct.unpack_from('<II',b,12);fmt=struct.unpack_from('<I',b,128)[0];ch={10:4,54:1,41:1}[fmt];r.update(width=w,height=h,dxgi=fmt,payload_bytes=len(b)-148)
  if ch==4:
   n=np.frombuffer(b,dtype='<f2',offset=148).reshape(h,w,4).astype(np.float32);r.update(min_xyz=n[...,:3].min(axis=(0,1)).tolist(),max_xyz=n[...,:3].max(axis=(0,1)).tolist(),alpha_unique=np.unique(n[...,3]).tolist());r['compact_xy_valid_hemisphere']=bool(np.all(n[...,2]>=0))
 else:
  im=Image.open(path);r.update(width=im.width,height=im.height,mode=im.mode)
 cache=P/'Cache/pc'/rel.lower();products=list(cache.parent.glob(cache.name+'.*'));r['cooked_products']=[{'path':str(p.relative_to(P)),'bytes':p.stat().st_size} for p in products if p.suffix in ('.streamingimage','.imagemipchain')];r['cooked_total_bytes']=sum(x['bytes'] for x in r['cooked_products']);rows.append(r)
report={'materials':materials,'textures':rows,'absent_channels':{k:[x for x in v if x.endswith(('specularMap','occlusionMap')) and not v[x]] for k,v in materials.items()}}
(O/'audit.json').write_text(json.dumps(report,indent=2)+'\n')
for r in rows:print({k:r[k] for k in ('path','width','height','dxgi','mode','payload_bytes','min_xyz','max_xyz','cooked_total_bytes') if k in r})
