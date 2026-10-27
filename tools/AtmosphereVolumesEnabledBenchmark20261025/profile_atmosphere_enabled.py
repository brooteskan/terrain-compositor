"""Editor --runpython harness. Requires TC_OVERLAP_PAGE_REFERENCE (prior results),
TC_OVERLAP_PAGE_PUBLICATION (publication.json), TC_OVERLAP_PAGE_OUTPUT (new folder)."""
import sys,os,json,time,traceback,hashlib
from pathlib import Path
import azlmbr.paths as paths
P=Path(paths.projectroot);W=Path('D:/brooteskan/work');sys.path.insert(0,str(P/'Gems/terrain-compositor/tools'))
import azlmbr.legacy.general as general
import azlmbr.editor as editor
import azlmbr.bus as bus
from PySide6 import QtCore

def run():
 cloud_saved=[];vis_saved=[];disabled=[];aerial=[];profile_saved=None;environment=[];original_keep=general.get_cvar("ed_keepEditorActive")
 try:
  general.idle_enable(True)
  general.run_console("ed_keepEditorActive 1")
  assert general.open_level_no_prompt('VueLiveLink')
  general.idle_wait_frames(20)
  previous=json.loads((P/'user/TerrainSceneProfile/20261024-overlap-queries-final/results.json').read_text())
  general.set_current_view_position(*previous['camera'][0]);general.set_current_view_rotation(*previous['camera'][1])
  general.set_viewport_expansion_policy('FixedSize');general.set_viewport_size(*previous['viewport']);general.update_viewport()
  for name in ('Terrain6','Terrain7'):
   target=general.find_editor_entity(name);assert target.IsValid();editor.EditorEntityAPIBus(bus.Event,'SetVisibilityState',target,True)
  general.idle_wait_frames(30)
  import azlmbr.entity as entity
  import azlmbr.render as render
  sys.path.insert(0,str(P/'Gem/Source/Editor/VueLiveLink'))
  import editor_link as api
  owners=entity.SearchBus(bus.Broadcast,'SearchEntities',entity.SearchFilter())
  for label in ('Sky Atmosphere','HDRI Sky','Finite Cloud Volume'):
   types=editor.EditorComponentAPIBus(bus.Broadcast,'FindComponentTypeIdsByEntityType',[label],entity.EntityType().Game)
   count=0
   if len(types)==1:
    for owner in owners:
     found=editor.EditorComponentAPIBus(bus.Broadcast,'GetComponentOfType',owner,types[0])
     if not found.IsSuccess():continue
     comp=found.GetValue();enabled=bool(editor.EditorComponentAPIBus(bus.Broadcast,'IsComponentEnabled',comp))
     disabled.append((comp,enabled))
     if not enabled:assert editor.EditorComponentAPIBus(bus.Broadcast,'EnableComponents',[comp])
     assert editor.EditorComponentAPIBus(bus.Broadcast,'IsComponentEnabled',comp)
     if label=='Finite Cloud Volume':
      prop=next(str(x) for x in editor.EditorComponentAPIBus(bus.Broadcast,'BuildComponentPropertyList',comp) if str(x).endswith('|Enabled'))
      old=api._get(comp,prop);cloud_saved.append((comp,prop,old));api._set(comp,prop,True)
      current=owner
      while current.IsValid():
       visible=bool(editor.EditorEntityInfoRequestBus(bus.Event,'IsVisible',current))
       if not visible:
        vis_saved.append((current,visible));editor.EditorEntityAPIBus(bus.Event,'SetVisibilityState',current,True)
       current=editor.EditorEntityInfoRequestBus(bus.Event,'GetParent',current)
     count+=1
   environment.append({'type':label,'enabled':count})
  types=editor.EditorComponentAPIBus(bus.Broadcast,'FindComponentTypeIdsByEntityType',['Vue Atmosphere'],entity.EntityType().Game)
  for owner in owners:
   if len(types)!=1:break
   found=editor.EditorComponentAPIBus(bus.Broadcast,'GetComponentOfType',owner,types[0])
   if found.IsSuccess():
    old=bool(editor.VueAtmosphereRequestBus(bus.Event,'GetAerialVisible',owner));aerial.append((owner,old))
    editor.VueAtmosphereRequestBus(bus.Event,'SetAerialVisible',owner,True)
  comp=api._component(general.find_editor_entity('clouds'),'Cloud Scene Settings')
  path=next(str(x) for x in editor.EditorComponentAPIBus(bus.Broadcast,'BuildComponentPropertyList',comp) if str(x).endswith('Measure transport cost'))
  profile_saved=(comp,path,api._get(comp,path));api._set(comp,path,True)
  def native_samples():
   rows=[]
   for i in range(60):
    general.idle_wait_frames(1)
    rows.append(json.loads(render.CloudSceneRequestBus(bus.Event,'GetMeasurements',general.find_editor_entity('clouds'))))
   return rows
  general.idle_wait_frames(240)
  checks=native_samples();assert checks[-1].get('cloudCount',0)>0,checks[-1]
  (P/'user/cloud-enabled-verification.json').write_text(json.dumps(checks[-1],indent=2))
  root=W/'lookup-cause/medium64'
  pub=json.loads((root/'publication.json').read_text())
  for rel,h in pub['signature']['dependencies'].items():assert hashlib.sha256((P/rel).read_bytes()).hexdigest()==h,'Stale page input: '+rel
  key=pub['key']
  def case(name,mode):return {'name':name,'screenshot':True,'mode':mode}
  cases=[case('complete',0),case('previous-fast',2),case('default',1),case('previous-fast-repeat',2),case('default-repeat',1),case('complete-final',0)]
  output=P/'user/TerrainSceneProfile/20261025-atmosphere-volumes-enabled'
  cfg={'output':str(output),'use_current_level':True,'exit_editor':False,'frames':240,'pass_samples':1,'warmup_seconds':12,'settle_seconds':6,'cache_diagnostics':False,'cases':cases}
  path=output.parent/(output.name+'-config.json');path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(cfg));os.environ['TC_SCENE_PROFILE_CONFIG']=str(path)
  source=(P/'Gems/terrain-compositor/tools/ProfileTerrainScene.py').read_text()
  needle="  if case.get('material_overrides'):"
  insert='''  if case.get('mode') is not None:
   mapping=MODE_MATERIALS[case['mode']]
   for target,original in BINDINGS:
    raw=overlap.raw_path(original).replace(chr(92),'/').lower().replace('.azmaterial','.material')
    assert raw in mapping, raw
    product=mapping[raw].lower().replace('.material','.azmaterial')
    response.link()._set(target['component'],target['property'],response.product(product))
   wait(lambda:all(render.MaterialComponentRequestBus(bus.Event,'IsMaterialAssetReady',t['owner'],render.MaterialAssignmentId()) for t,_ in BINDINGS),240)
'''
  assert needle in source;source=source.replace(needle,insert+needle)
  source=source.replace("entry={'name':name,'configuration':case,'cache_before':counters()}","entry={'name':name,'configuration':case,'cache_before':counters(),'video_memory':PROCESS_MEMORY()}")
  source=source.replace("R['camera']=CAMERA;R['viewport']=VIEWPORT", "R['camera']=CAMERA;R['viewport']=VIEWPORT;R['tile_publication']=PAGE_PUBLICATION;R['shader_sha256']=SHADER_SHA256;R['environment_enabled']=ENVIRONMENT_DISABLED")
  source=source.replace("entry['cache_after']=counters()","entry['cache_after']=counters();entry['enabled_pipeline_samples']=NATIVE_SAMPLES()")
  from OverlapPageMemory import memory
  exec(compile(source,'ProfileTerrainScene.py','exec'),dict(MODE_MATERIALS={0: {'assets/vuelivelink/overlap/d9e7b35caf90f0292a9e19d9/13d046a1907c1ddef8a6ce2438e433a7af1396d73b7006df19a4376676016fcd.material': 'Assets/VueLiveLink/atmosphere_shader_test/0/d9e7b35caf90f0292a9e19d9.material', 'assets/vuelivelink/overlap/9f344ef8359b9976db0ed01a/13d046a1907c1ddef8a6ce2438e433a7af1396d73b7006df19a4376676016fcd.material': 'Assets/VueLiveLink/atmosphere_shader_test/0/9f344ef8359b9976db0ed01a.material'}, 1: {'assets/vuelivelink/overlap/d9e7b35caf90f0292a9e19d9/13d046a1907c1ddef8a6ce2438e433a7af1396d73b7006df19a4376676016fcd.material': 'Assets/VueLiveLink/atmosphere_shader_test/1/d9e7b35caf90f0292a9e19d9.material', 'assets/vuelivelink/overlap/9f344ef8359b9976db0ed01a/13d046a1907c1ddef8a6ce2438e433a7af1396d73b7006df19a4376676016fcd.material': 'Assets/VueLiveLink/atmosphere_shader_test/1/9f344ef8359b9976db0ed01a.material'}, 2: {'assets/vuelivelink/overlap/d9e7b35caf90f0292a9e19d9/13d046a1907c1ddef8a6ce2438e433a7af1396d73b7006df19a4376676016fcd.material': 'Assets/VueLiveLink/atmosphere_shader_test/2/d9e7b35caf90f0292a9e19d9.material', 'assets/vuelivelink/overlap/9f344ef8359b9976db0ed01a/13d046a1907c1ddef8a6ce2438e433a7af1396d73b7006df19a4376676016fcd.material': 'Assets/VueLiveLink/atmosphere_shader_test/2/9f344ef8359b9976db0ed01a.material'}},SHADER_SHA256=hashlib.sha256((P/'Assets/VueLiveLink/VueTerrainOverlap.azsli').read_bytes()).hexdigest(),PAGE_PUBLICATION=pub,PROCESS_MEMORY=memory,ENVIRONMENT_DISABLED=environment,NATIVE_SAMPLES=native_samples,__name__='__main__'))
 except Exception:
  (P/'user/overlap-pages-editor-error.txt').write_text(traceback.format_exc());print(traceback.format_exc(),flush=True)
 finally:
  general.idle_enable(True)
  restored=True
  for comp,path,old in cloud_saved:
   api._set(comp,path,old);restored=(api._get(comp,path)==old) and restored
  for owner,old in reversed(vis_saved):editor.EditorEntityAPIBus(bus.Event,'SetVisibilityState',owner,old)
  for comp,old in reversed(disabled):
   action='EnableComponents' if old else 'DisableComponents'
   restored=bool(editor.EditorComponentAPIBus(bus.Broadcast,action,[comp])) and restored
  for owner,old in aerial:
   editor.VueAtmosphereRequestBus(bus.Event,'SetAerialVisible',owner,old)
   restored=(bool(editor.VueAtmosphereRequestBus(bus.Event,'GetAerialVisible',owner))==old) and restored
  if profile_saved:
   comp,path,value=profile_saved;api._set(comp,path,value);restored=(api._get(comp,path)==value) and restored
  keep=general.get_cvar("ed_keepEditorActive");general.run_console("ed_keepEditorActive 1")
  try:general.idle_wait_frames(60)
  finally:general.run_console("ed_keepEditorActive "+str(keep))
  for comp,old in disabled:restored=(bool(editor.EditorComponentAPIBus(bus.Broadcast,'IsComponentEnabled',comp))==old) and restored
  (P/'user/atmosphere-volumes-enabled-environment.json').write_text(json.dumps({'enabled':environment,'aerial_controls':len(aerial),'restored':restored},indent=2))
  general.run_console("ed_keepEditorActive "+str(original_keep))
  general.idle_enable(False)

QtCore.QTimer.singleShot(15000,run)
