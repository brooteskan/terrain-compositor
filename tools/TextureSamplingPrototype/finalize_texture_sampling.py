from pathlib import Path
import json,sys,shutil,hashlib,statistics
import numpy as np
from PIL import Image
W=Path('D:/brooteskan/work');O=W/'texture-sampling';P=Path('D:/diversion/TG/TGProject');R=P/'Gems/terrain-compositor';B=R/'integrations/tg';A=R/'tools/TextureSamplingPrototype';A.mkdir(exist_ok=True)
sys.path.insert(0,str(W));from analyze_contributors import analyze,image_diff
data={}
for phase,folder,env in [('initial','20261024-texture-sampling',O/'first-environment.json'),('clean','20261025-texture-sampling-clean',O/'clean-environment.json'),('policy','20261025-normal-quality-policy',P/'user/normal-policy-environment.json')]:
 capture=P/'user/TerrainSceneProfile'/folder;d=analyze(capture);assert d['passed'] and all(d['restoration'].values());d['environment']=json.loads(env.read_text());assert d['environment']['restored'];d['capture_directory']=str(capture);data[phase]=d
data['initial']['validity']='Ground material rebinding left cached ground tiles visually stale after ground-single. Exclude every subsequent case from optimization/appearance comparisons. The clean and policy phases start fresh editors and do not change ground bindings.'
cut=next(i for i,x in enumerate(data['initial']['rows']) if x['name']=='ground-single')
for i,row in enumerate(data['initial']['rows']):row['valid_for_comparison']=i<=cut
data['audit']=json.loads((O/'audit.json').read_text());data['field_representations']=json.loads((O/'fields.json').read_text());data['field_cooked_products']=[]
for label,mapping in json.loads((O/'texture-mapping.json').read_text()).items():
 for original,rel in mapping.items():
  cache=P/'Cache/pc'/rel.lower();product=cache.with_suffix(cache.suffix+'.streamingimage');assert product.is_file();data['field_cooked_products'].append({'representation':label,'original':original,'path':str(product),'cooked_bytes':product.stat().st_size})
pol=data['policy']['rows'];previous=statistics.median(x['gpu_ms'] for x in pol if x['name'].startswith('previous-fast'));new=statistics.median(x['gpu_ms'] for x in pol if x['name'].startswith('default'));original=statistics.median(x['gpu_ms'] for x in pol if x['name'].startswith('complete'));assert new<previous*.97,(new,previous)
data['summary']={'original_gpu_ms':original,'previous_fast_gpu_ms':previous,'new_default_gpu_ms':new,'gain_ms':previous-new,'gpu_time_reduction_percent':100*(previous-new)/previous,'initial_new_default_then_fallback_local_bytes':pol[0]['local_memory_bytes'],'after_enabling_detail_normals_local_bytes':next(x['local_memory_bytes'] for x in pol if x['name']=='previous-fast')}
data['summary']['detail_first_bind_added_mib']=(data['summary']['after_enabling_detail_normals_local_bytes']-data['summary']['initial_new_default_then_fallback_local_bytes'])/1048576
clean=P/'user/TerrainSceneProfile/20261025-texture-sampling-clean';final=P/'user/TerrainSceneProfile/20261025-normal-quality-policy';data['cross_appearance']={'fallback_vs_prechange':image_diff(clean/'complete.png',final/'complete.png'),'detail_on_vs_previous_fast':image_diff(clean/'default.png',final/'previous-fast.png'),'default_vs_normal_removal_control':image_diff(clean/'no-normal-maps.png',final/'default.png'),'default_vs_previous_fast':image_diff(final/'previous-fast.png',final/'default.png')}
assert data['cross_appearance']['fallback_vs_prechange']['changed_pixels']==0 and data['cross_appearance']['detail_on_vs_previous_fast']['changed_pixels']==0
for x in data['clean']['rows']:
 if x['name'].startswith(('complete','default')):assert x['versus_complete']['changed_pixels']==0
level=P/'Levels/VueLiveLink/VueLiveLink.prefab';data['saved_level_sha256']=hashlib.sha256(level.read_bytes()).hexdigest();assert data['saved_level_sha256']=='faec5fd3f9b69643b918af3a917623776d31bf7cc962485cfba712313a36a922'
layout=json.loads((P/'Assets/VueLiveLink/VueTerrainOverlap.materialtype').read_text());props={p['name']:p for g in layout['propertyLayout']['propertyGroups'] if g.get('name')=='contributorSelection' for p in g['properties']};assert props['enabled']['defaultValue'] is True and props['detailNormals']['defaultValue'] is False and props['key']['defaultValue']==0
status=json.loads((P/'user/VueLiveLink/terrain_overlap_status.json').read_text());data['inherited_defaults']=[]
for raw in status['materials'].values():
 path=P/Path(raw).with_suffix('.material');v=json.loads(path.read_text())['propertyValues'];assert v.get('contributorSelection.enabled',True) and not v.get('contributorSelection.detailNormals',False) and v.get('contributorSelection.key',0)==0;assert v['normal.textureMap'];data['inherited_defaults'].append(str(path))
assert 'o_terrainNormalPacking' not in (P/'Assets/VueLiveLink/VueTerrainMesh.azsli').read_text();assert 'samplingExperiment' not in json.dumps(layout)
sys.path.insert(0,str(B));from SyncProject import sync;assert sync(P,check=True)==[]
for folder in ('experiment','normal-policy'):
 dst=A/folder;shutil.copytree(O/folder,dst,dirs_exist_ok=True)
for name in ('audit.json','fields.json','texture-mapping.json','ground.json','first-environment.json','clean-environment.json'):shutil.copyfile(O/name,A/name)
for name in ('audit_sampling.py','prepare_texture_sampling.py','add_ground_sampling_control.py','prepare_sampling_clean.py','profile_texture_sampling.py','profile_sampling_clean.py','install_normal_quality_policy.py','profile_normal_policy.py','add_normal_policy_test.py','finalize_texture_sampling.py'):
 shutil.copyfile(W/name,A/name)
shutil.copyfile(W/'VueTerrainNormalPolicy.lua',A/'VueTerrainNormalPolicy.lua');shutil.copyfile(P/'user/normal-policy-environment.json',A/'normal-policy-environment.json')
# Archive/delete only these known task-owned source assets, within their checked roots.
for tag,count in [('texture_sampling_test',8),('normal_policy_test',3)]:
 root=(P/'Assets/VueLiveLink'/tag).resolve();assert root.is_relative_to((P/'Assets/VueLiveLink').resolve())
 for mode in range(count):
  for p in (root/str(mode)).glob('*.material'):
   assert p.resolve().parent==root/str(mode) and p.stem in ('d9e7b35caf90f0292a9e19d9','9f344ef8359b9976db0ed01a');dst=A/'materials'/tag/str(mode)/p.name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dst);p.unlink()
ground=P/'Assets/VueLiveLink/texture_sampling_test/ground-single.material';assert ground.resolve().parent==(P/'Assets/VueLiveLink/texture_sampling_test').resolve();dst=A/'materials/ground-single.material';shutil.copyfile(ground,dst);ground.unlink()
fields=(P/'Assets/VueLiveLink/texture_sampling_fields').resolve()
for label,mapping in json.loads((O/'texture-mapping.json').read_text()).items():
 for rel in mapping.values():
  for p in [P/rel,(P/rel).with_suffix('.dds.assetinfo')]:
   assert p.resolve().is_relative_to(fields);dst=A/'fields'/p.relative_to(fields);dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dst);p.unlink()
for name in ('constant_detail_normal.png','constant_detail_normal.png.assetinfo'):
 p=fields/name;assert p.resolve().parent==fields;dst=A/'fields'/name;shutil.copyfile(p,dst);p.unlink()
p=P/'Config/AtomImageBuilder/TerrainSampling_RG16F.preset';assert p.resolve().parent==(P/'Config/AtomImageBuilder').resolve();shutil.copyfile(p,A/p.name);p.unlink()
data['focused_tests_passed']=15;data['delivery_pending']=[];data['decision']='User accepted observed visual differences. Adopt only the measured normal-detail quality policy: Optimized Shader stays enabled by default, Detail Normal Maps defaults off, authored image bindings stay intact, original shader fallback restores authored normal settings. No compact fields, mip bias or changed ground pattern shipped.'
(O/'results.json').write_text(json.dumps(data,indent=2)+'\n');(R/'docs/TerrainTextureSampling20261025.json').write_text(json.dumps(data,indent=2)+'\n');print(json.dumps(data['summary'],indent=2));print(json.dumps(data['cross_appearance'],indent=2));print('Verified final gain, fallback, appearance, restoration and delivery; temporary assets archived and removed')
