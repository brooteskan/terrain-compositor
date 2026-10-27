from pathlib import Path
import json,statistics,sys
import numpy as np
from PIL import Image

F=Path('D:/diversion/TG/TGProject/user/TerrainSceneProfile/20261025-cloud-light-steps')

def pixels(a,b,roi=None):
 x=np.asarray(Image.open(a).convert('RGB'),dtype=np.int16);y=np.asarray(Image.open(b).convert('RGB'),dtype=np.int16)
 assert x.shape==y.shape
 h,w=x.shape[:2];mask=np.ones((h,w),dtype=bool)
 mask[:150,w-650:]=False;mask[h-100:,:100]=False
 if roi:
  other=np.zeros_like(mask);l,t,r,bottom=roi;other[t:bottom,l:r]=True;mask &= other
 d=np.abs(x-y)[mask];peak=d.max(axis=1)
 return {'pixels':int(len(d)),'changed_pixels':int((peak>0).sum()),'pixels_over_1':int((peak>1).sum()),'pixels_over_4':int((peak>4).sum()),'pixels_over_8':int((peak>8).sum()),'max_rgb_difference':int(d.max()),'mean_absolute_rgb':float(d.mean()),'rmse_rgb':float(np.sqrt(np.mean(d.astype(float)**2))),'p99_max_rgb':float(np.percentile(peak,99))}

def analyze():
 r=json.loads((F/'results.json').read_text());rows=[]
 for c in r['cases']:
  path=F/(c['name']+'-frames.json')
  if not path.exists():continue
  raw=path.read_text();events=json.loads(raw.replace('[,','[',1) if raw.startswith('[,') else raw)
  values=[e['dur']/1000 for e in events if e.get('name')=='Frame Gpu Time' and e.get('ph')=='X']
  row={'name':c['name'],'light_steps':c['configuration']['light_steps'],'gpu_ms':statistics.median(values),'gpu_samples':len(values),'local_memory_bytes':c['video_memory']['local']['CurrentUsage'],'lighting':c['lighting']}
  samples=c.get('enabled_pipeline_samples',[])
  if samples:
   row.update(cloud_ms=statistics.median(x['gpuCloudNs']/1e6 for x in samples),density_bytes=samples[-1]['densityBytes'],pool_resident_bytes=samples[-1]['gpuPoolResidentBytes'],pool_used_bytes=samples[-1]['gpuPoolUsedBytes'],cloud_counts=sorted(set(x['cloudCount'] for x in samples)))
  if (F/'light-16.png').exists() and (F/(c['name']+'.png')).exists():
   row['appearance']=pixels(F/'light-16.png',F/(c['name']+'.png'))
   row['cloud_roi_appearance']=pixels(F/'light-16.png',F/(c['name']+'.png'),(100,120,760,610))
  rows.append(row)
 return {'passed':r['passed'],'restoration':r.get('restoration'),'active_case':r.get('active_case'),'exception':r.get('exception'),'rows':rows}

if __name__=='__main__':
 a=analyze()
 if '--compact' in sys.argv:
  print(json.dumps({'passed':a['passed'],'active':a['active_case'],'exception':a['exception'],'rows':[{k:x.get(k) for k in ['name','gpu_ms','cloud_ms','local_memory_bytes','appearance']} for x in a['rows']]},indent=2))
 else:print(json.dumps(a,indent=2))
