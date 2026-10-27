import json,statistics,sys
from pathlib import Path
import numpy as np
from PIL import Image
def image_diff(a,b):
 x=np.asarray(Image.open(a).convert('RGB'),dtype=np.int16);y=np.asarray(Image.open(b).convert('RGB'),dtype=np.int16)
 assert x.shape==y.shape
 mask=np.ones(x.shape[:2],bool);mask[:150,620:1203]=False;mask[455:466,998:1062]=False
 v=np.abs(x-y)[mask]
 return dict(changed_pixels=int(np.any(v,axis=1).sum()),max_rgb_difference=int(v.max()),pixels_over_one=int(np.any(v>1,axis=1).sum()),compared_pixels=len(v))
def analyze(p):
 p=Path(p);r=json.loads((p/'results.json').read_text());rows=[]
 for c in r['cases']:
  frame=p/(c['name']+'-frames.json')
  if not frame.exists():continue
  s=frame.read_text();events=json.loads(s.replace('[,','[',1) if s.startswith('[,') else s)
  row={'name':c['name']}
  for n,key in [('Frame Gpu Time','gpu_ms'),('Engine Cpu Time','cpu_ms')]:
   v=[e['dur']/1000 for e in events if e.get('name')==n and e.get('ph')=='X']
   if v:row[key]=statistics.median(v)
  row['local_memory_bytes']=c.get('video_memory',{}).get('local',{}).get('CurrentUsage')
  native={x['pipelineTimestamp']:x['gpuPipelineNs']/1e6 for x in c.get('enabled_pipeline_samples',[]) if x.get('pipelineTimestamp') and x.get('gpuPipelineNs')}
  if native:row['enabled_pipeline_ms']=statistics.median(native.values())
  if (p/'complete.png').exists() and (p/(c['name']+'.png')).exists():row['versus_complete']=image_diff(p/'complete.png',p/(c['name']+'.png'))
  rows.append(row)
 result={'passed':r['passed'],'restoration':r.get('restoration'),'rows':rows}
 if (p/'discard-before.png').exists():result['discard_equivalence']=image_diff(p/'discard-after.png',p/'discard-before.png')
 return result
if __name__=='__main__':
 for p in sys.argv[1:]:print(json.dumps({'path':p,**analyze(p)},indent=2))
