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
 cloud_saved=[];vis_saved=[];disabled=[];aerial=[];quality_saved=None;profile_saved=None;environment=[];original_keep=general.get_cvar("ed_keepEditorActive")
 try:
  general.idle_enable(True)
  general.run_console("ed_keepEditorActive 1")
  assert general.get_current_level_name(),'No level open'
  reference=json.loads((P/'user/TerrainSceneProfile/20261025-cloud-terrain-camera/results.json').read_text())
  pos=general.get_current_view_position();rot=general.get_current_view_rotation();size=general.get_viewport_size()
  current=[[pos.x,pos.y,pos.z],[rot.x,rot.y,rot.z]]
  assert current==reference['camera'],('Camera changed',current)
  assert [int(size.x),int(size.y)]==reference['viewport'],'Viewport changed'
  mt=json.loads((P/'Assets/VueLiveLink/VueTerrainOverlap.materialtype').read_text())
  group=next(g for g in mt['propertyLayout']['propertyGroups'] if g.get('name')=='contributorSelection')
  defaults={v['name']:v.get('defaultValue') for v in group['properties']}
  assert defaults=={'enabled':True,'detailNormals':False,'key':0},defaults
  import azlmbr.entity as entity
  import azlmbr.render as render
  sys.path.insert(0,str(P/'Gem/Source/Editor/VueLiveLink'))
  import editor_link as api
  cloud_controls=[]
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
      cloud_controls.append({'entity':str(owner),'properties':{str(x):str(api._get(comp,str(x))) for x in editor.EditorComponentAPIBus(bus.Broadcast,'BuildComponentPropertyList',comp)},'status':render.CloudVolumeRequestBus(bus.Event,'GetStatus',owner)})
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
  quality_path=next(str(x) for x in editor.EditorComponentAPIBus(bus.Broadcast,'BuildComponentPropertyList',comp) if str(x).endswith('Reference and quality|Light samples'))
  quality_saved=(comp,quality_path,api._get(comp,quality_path))
  assert quality_saved[2]==16,quality_saved[2]
  cloud_owner=general.find_editor_entity('clouds')
  original_lighting=json.loads(render.CloudSceneRequestBus(bus.Event,'GetLighting',cloud_owner))
  assert original_lighting['viewSteps']==192 and original_lighting['lightSteps']==16,original_lighting
  def lighting():return json.loads(render.CloudSceneRequestBus(bus.Event,'GetLighting',cloud_owner))
  def select_steps(count):
   api._set(quality_saved[0],quality_saved[1],count)
   general.idle_wait_frames(4)
   actual=lighting();assert actual['lightSteps']==count,actual
   for key,value in original_lighting.items():
    if key!='lightSteps':assert actual[key]==value,(key,actual[key],value)
   assert api._get(quality_saved[0],quality_saved[1])==count
   return actual
  def native_samples():
   rows=[]
   for i in range(60):
    general.idle_wait_frames(1)
    rows.append(json.loads(render.CloudSceneRequestBus(bus.Event,'GetMeasurements',general.find_editor_entity('clouds'))))
   return rows
  general.idle_wait_frames(30)
  checks=native_samples();assert checks[-1].get('cloudCount',0)>0,checks[-1]
  (P/'user/cloud-light-steps-enabled-verification.json').write_text(json.dumps(checks[-1],indent=2))
  (P/'user/cloud-light-steps-volume-controls.json').write_text(json.dumps(cloud_controls,indent=2))
  def case(n,suffix=''):return {'name':'light-%02d'%n+suffix,'screenshot':True,'light_steps':n}
  cases=[case(16),case(8),case(4),case(2),case(2,'-repeat'),case(4,'-repeat'),case(8,'-repeat'),case(16,'-final')]
  output=P/'user/TerrainSceneProfile/20261025-cloud-light-steps'
  cfg={'output':str(output),'use_current_level':True,'exit_editor':False,'frames':240,'pass_samples':1,'warmup_seconds':12,'settle_seconds':6,'cache_diagnostics':False,'cases':cases}
  path=output.parent/(output.name+'-config.json');path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(cfg));os.environ['TC_SCENE_PROFILE_CONFIG']=str(path)
  source=(P/'Gems/terrain-compositor/tools/ProfileTerrainScene.py').read_text()
  needle="  if case.get('material_overrides'):"
  assert needle in source;source=source.replace(needle,"  if 'light_steps' in case:SELECT_STEPS(case['light_steps'])\n"+needle)
  source=source.replace("entry={'name':name,'configuration':case,'cache_before':counters()}","entry={'name':name,'configuration':case,'cache_before':counters(),'video_memory':PROCESS_MEMORY(),'lighting':LIGHTING()}")
  source=source.replace("R['camera']=CAMERA;R['viewport']=VIEWPORT", "R['camera']=CAMERA;R['viewport']=VIEWPORT;R['shader_sha256']=SHADER_SHA256;R['environment_enabled']=ENVIRONMENT_ENABLED;R['original_lighting']=ORIGINAL_LIGHTING;R['terrain_defaults']=DEFAULTS")
  source=source.replace("entry['cache_after']=counters()","entry['cache_after']=counters();entry['enabled_pipeline_samples']=NATIVE_SAMPLES()")
  from OverlapPageMemory import memory
  exec(compile(source,'ProfileTerrainScene.py','exec'),dict(SELECT_STEPS=select_steps,LIGHTING=lighting,SHADER_SHA256=hashlib.sha256((P/'Assets/VueLiveLink/VueTerrainOverlap.azsli').read_bytes()).hexdigest(),PROCESS_MEMORY=memory,ENVIRONMENT_ENABLED=environment,ORIGINAL_LIGHTING=original_lighting,DEFAULTS=defaults,NATIVE_SAMPLES=native_samples,__name__='__main__'))
 except Exception:
  (P/'user/cloud-light-steps-error.txt').write_text(traceback.format_exc());print(traceback.format_exc(),flush=True)
 finally:
  general.idle_enable(True)
  restored=True
  if quality_saved:
   comp,path,value=quality_saved;api._set(comp,path,value);general.idle_wait_frames(4)
   restored=(api._get(comp,path)==value and lighting()==original_lighting) and restored
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
  (P/'user/cloud-light-steps-environment.json').write_text(json.dumps({'enabled':environment,'aerial_controls':len(aerial),'restored':restored,'light_steps':quality_saved[2] if quality_saved else None,'lighting_after':lighting() if quality_saved else None},indent=2))
  general.run_console("ed_keepEditorActive "+str(original_keep))
  general.idle_enable(False)

QtCore.QTimer.singleShot(0,run)
