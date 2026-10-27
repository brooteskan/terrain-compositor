from pathlib import Path
import json,statistics
O=Path('D:/brooteskan/work/lookup-cause');P=Path('D:/diversion/TG/TGProject/user/TerrainSceneProfile');output={}
for phase,prefix in [('small','20261024-lookup-small'),('alternatives','20261024-lookup-alternatives')]:
 samples=[json.loads(x) for x in (O/(phase+'-clocks.jsonl')).read_text().splitlines()]
 r=json.loads((P/prefix/'results.json').read_text());rows=[]
 for case in r['cases']:
  f=P/prefix/(case['name']+'-frames.json')
  if not f.exists():continue
  s=f.read_text();ev=json.loads(s.replace('[,','[',1) if s.startswith('[,') else s)
  duration=sum(e['dur']/1e6 for e in ev if e.get('ph')=='X' and e.get('name')=='Engine Cpu Time')
  end=f.stat().st_mtime;start=end-duration
  selected=[x['adapters'][0]['od5'] for x in samples if start+1<x['time']<end-1 and x['case']==case['name']]
  if not selected:continue
  row={'case':case['name'],'window_basis':'Frame file mtime minus summed Engine Cpu Time; exclude 1 second at each end','samples':len(selected)}
  for key in ('engine_clock','memory_clock','activity'):
   v=[x[key] for x in selected];row[key]={'min':min(v),'median':statistics.median(v),'max':max(v)}
  rows.append(row)
 output[phase]=rows
(O/'clock-summary.json').write_text(json.dumps(output,indent=2)+'\n');print(json.dumps(output,indent=2))
