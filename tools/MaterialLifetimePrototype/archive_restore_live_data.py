from pathlib import Path
import json,hashlib,shutil,sys,statistics
W=Path('D:/brooteskan/work');O=W/'live-data';P=Path('D:/diversion/TG/TGProject');R=P/'Gems/terrain-compositor';B=R/'integrations/tg';A=R/'tools/MaterialLifetimePrototype';A.mkdir(exist_ok=True)
sys.path.insert(0,str(W));from analyze_contributors import analyze,image_diff
capture=P/'user/TerrainSceneProfile/20261024-live-data';data=analyze(capture);assert data['passed'] and all(data['restoration'].values()),data
env=json.loads((P/'user/live-data-environment.json').read_text());assert env['restored'];data['environment']=env;data['capture_directory']=str(capture)
rows=data['rows'];groups={}
for label,prefix in [('Original fallback','complete'),('Faster default','default'),('Color before normal frame','color-first'),('Normal frame last','normal-last'),('Normal frame last and late surface','late-base')]:
 rr=[x for x in rows if x['name'].startswith(prefix)];groups[prefix]={'label':label,'gpu_ms':statistics.median(x['gpu_ms'] for x in rr),'gpu_case_median_min':min(x['gpu_ms'] for x in rr),'gpu_case_median_max':max(x['gpu_ms'] for x in rr),'cpu_ms':statistics.median(x['cpu_ms'] for x in rr),'max_changed_pixels':max(x['versus_complete']['changed_pixels'] for x in rr),'resident_mib':sorted(set(x['local_memory_bytes']/1048576 for x in rr))}
default=groups['default']['gpu_ms']
for g in groups.values():g['gpu_change_vs_default_percent']=100*(g['gpu_ms']-default)/default
data['groups']=groups;data['native_probes']=json.loads((O/'native/summary.json').read_text());data['native_probe_limitation']='Controlled offline source probes, not a live driver PSO dump; unrelated renderer/material options remain volatile.'
data['appearance_vs_previous_default']=image_diff(P/'user/TerrainSceneProfile/20261024-default-shader/default.png',capture/'default.png')
level=P/'Levels/VueLiveLink/VueLiveLink.prefab';data['saved_level_sha256']=hashlib.sha256(level.read_bytes()).hexdigest();assert data['saved_level_sha256']=='faec5fd3f9b69643b918af3a917623776d31bf7cc962485cfba712313a36a922'
rels=['Assets/VueLiveLink/VueTerrainOverlap.azsli','Assets/VueLiveLink/VueTerrainOverlap.materialtype','Gem/Source/Editor/VueLiveLink/terrain_overlap.py']
for rel in rels:
 target=A/'experiment'/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(B/'project'/rel,target)
 for tag,root in [('canonical',B/'project'),('consumer',P)]:shutil.copyfile(O/'before'/tag/rel,root/rel)
shutil.copyfile(O/'before/manifest.json',B/'manifest.json')
hashes=json.loads((O/'before/hashes.json').read_text());data['source_restoration']={}
for tag,root in [('canonical',B/'project'),('consumer',P)]:
 for rel in rels:
  key=tag+'/'+rel;data['source_restoration'][key]=hashlib.sha256((root/rel).read_bytes()).hexdigest()==hashes[key];assert data['source_restoration'][key]
layout=json.loads((P/rels[1]).read_text());props={p['name']:p for g in layout['propertyLayout']['propertyGroups'] if g.get('name')=='contributorSelection' for p in g['properties']};assert props['enabled']['defaultValue'] is True and props['key']['defaultValue']==0
sys.path.insert(0,str(B));from SyncProject import sync;assert sync(P,check=True)==[]
for name in ('prepare_live_data.py','analyze_live_data_native.py','profile_live_data.py','archive_restore_live_data.py'):
 shutil.copyfile(W/name,A/name)
for p in O.glob('*.azsli'):shutil.copyfile(p,A/p.name)
target=A/'native';target.mkdir(exist_ok=True)
for p in (O/'native').iterdir():
 if p.name=='summary.json' or p.name.endswith('-stats_pixel.txt'):shutil.copyfile(p,target/p.name)
shutil.copyfile(P/'user/live-data-environment.json',A/'environment.json')
root=(P/'Assets/VueLiveLink/live_data_test').resolve();assert root.is_relative_to((P/'Assets/VueLiveLink').resolve())
for mode in range(5):
 for p in (root/str(mode)).glob('*.material'):
  assert p.resolve().parent==root/str(mode) and p.stem in ('d9e7b35caf90f0292a9e19d9','9f344ef8359b9976db0ed01a')
  target=A/'materials'/str(mode)/p.name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,target);p.unlink()
data['default_retained']='All three changed source files and manifest restored byte-for-byte to the pre-experiment faster-default version. Original fallback remains available.'
(O/'results.json').write_text(json.dumps(data,indent=2)+'\n');(R/'docs/TerrainMaterialLifetimes20261024.json').write_text(json.dumps(data,indent=2)+'\n')
print(json.dumps(groups,indent=2));print('Archived experiment, restored faster default and original fallback byte-for-byte, and removed only known temporary benchmark materials')
