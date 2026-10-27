"""Editor --runpython harness. Requires TC_OVERLAP_PAGE_REFERENCE (prior results),
TC_OVERLAP_PAGE_PUBLICATION (publication.json), TC_OVERLAP_PAGE_OUTPUT (new folder)."""
import sys,os,json,time,traceback,hashlib
from pathlib import Path
import azlmbr.paths as paths
P=Path(paths.projectroot);W=Path(__file__).resolve().parent;sys.path.insert(0,str(W))
import azlmbr.legacy.general as general
import azlmbr.editor as editor
import azlmbr.bus as bus
from PySide6 import QtCore

def run():
 disabled=[];aerial=[];profile_saved=None;environment=[]
 try:
  general.idle_enable(True)
  assert general.open_level_no_prompt('VueLiveLink')
  general.idle_wait_frames(20)
  previous=json.loads(Path(os.environ['TC_OVERLAP_PAGE_REFERENCE']).read_text())
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
  for label in ('Sky Atmosphere','HDRI Sky','Finite Cloud Volume','Fog','Volumetric Fog'):
   types=editor.EditorComponentAPIBus(bus.Broadcast,'FindComponentTypeIdsByEntityType',[label],entity.EntityType().Game)
   count=0
   if len(types)==1:
    for owner in owners:
     found=editor.EditorComponentAPIBus(bus.Broadcast,'GetComponentOfType',owner,types[0])
     if not found.IsSuccess():continue
     comp=found.GetValue();enabled=bool(editor.EditorComponentAPIBus(bus.Broadcast,'IsComponentEnabled',comp))
     if enabled:
      disabled.append(comp);assert editor.EditorComponentAPIBus(bus.Broadcast,'DisableComponents',[comp]);count+=1
   environment.append({'type':label,'disabled':count})
  types=editor.EditorComponentAPIBus(bus.Broadcast,'FindComponentTypeIdsByEntityType',['Vue Atmosphere'],entity.EntityType().Game)
  for owner in owners:
   if len(types)!=1:break
   found=editor.EditorComponentAPIBus(bus.Broadcast,'GetComponentOfType',owner,types[0])
   if found.IsSuccess():
    old=bool(editor.VueAtmosphereRequestBus(bus.Event,'GetAerialVisible',owner));aerial.append((owner,old))
    editor.VueAtmosphereRequestBus(bus.Event,'SetAerialVisible',owner,False)
  comp=api._component(general.find_editor_entity('clouds'),'Cloud Scene Settings')
  path=next(str(x) for x in editor.EditorComponentAPIBus(bus.Broadcast,'BuildComponentPropertyList',comp) if str(x).endswith('Measure transport cost'))
  profile_saved=(comp,path,api._get(comp,path));api._set(comp,path,True)
  def native_samples():
   rows=[]
   for i in range(60):
    general.idle_wait_frames(1)
    rows.append(json.loads(render.CloudSceneRequestBus(bus.Event,'GetMeasurements',general.find_editor_entity('clouds'))))
   return rows
  root=Path(os.environ['TC_OVERLAP_PAGE_PUBLICATION']).resolve().parent
  pub=json.loads((root/'publication.json').read_text())
  for rel,h in pub['signature']['dependencies'].items():assert hashlib.sha256((P/rel).read_bytes()).hexdigest()==h,'Stale page input: '+rel
  cases=[{'name':'direct','screenshot':True},
         {'name':'pages','page_materials':True,'screenshot':True,'material_overrides':{'overlapPages.enabled':True}},
         {'name':'direct-repeat','screenshot':True},
         {'name':'pages-repeat','page_materials':True,'screenshot':True,'material_overrides':{'overlapPages.enabled':True}},
         {'name':'coverage','page_materials':True,'screenshot':True,'material_overrides':{'overlapPages.enabled':True,'overlapPages.debug':True}},
         {'name':'fallback-only','page_materials':True,'screenshot':True,'material_overrides':{'overlapPages.enabled':False}}]
  output=Path(os.environ['TC_OVERLAP_PAGE_OUTPUT'])
  cfg={'output':str(output),'use_current_level':True,'exit_editor':False,'frames':240,'pass_samples':3,'warmup_seconds':20,'settle_seconds':6,'cache_diagnostics':False,'cases':cases}
  path=output.parent/(output.name+'-config.json');path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(cfg));os.environ['TC_SCENE_PROFILE_CONFIG']=str(path)
  source=(P/'Gems/terrain-compositor/tools/ProfileTerrainScene.py').read_text()
  needle="  if case.get('material_overrides'):"
  insert='''  if case.get('page_materials'):
   for target,original in BINDINGS:
    raw=overlap.raw_path(original)
    normalized=raw.replace('\\\\','/').lower().replace('.azmaterial','.material')
    mapping={k.replace('\\\\','/').lower():v for k,v in PAGE_PUBLICATION['materials'].items()}
    assert normalized in mapping, ('Unknown original binding',raw)
    product_path=mapping[normalized].replace('\\\\','/').replace('.material','.azmaterial').lower()
    response.link()._set(target['component'],target['property'],response.product(product_path))
   wait(lambda:all(render.MaterialComponentRequestBus(bus.Event,'IsMaterialAssetReady',t['owner'],render.MaterialAssignmentId()) for t,_ in BINDINGS),240)
'''
  assert needle in source;source=source.replace(needle,insert+needle)
  source=source.replace("entry={'name':name,'configuration':case,'cache_before':counters()}","entry={'name':name,'configuration':case,'cache_before':counters(),'video_memory':PROCESS_MEMORY()}")
  source=source.replace("R['camera']=CAMERA;R['viewport']=VIEWPORT", "R['camera']=CAMERA;R['viewport']=VIEWPORT;R['page_publication']=PAGE_PUBLICATION;R['environment_disabled']=ENVIRONMENT_DISABLED")
  source=source.replace("entry['cache_after']=counters()","entry['cache_after']=counters();entry['enabled_pipeline_samples']=NATIVE_SAMPLES()")
  from OverlapPageMemory import memory
  exec(compile(source,'ProfileTerrainScene.py','exec'),dict(PAGE_PUBLICATION=pub,PROCESS_MEMORY=memory,ENVIRONMENT_DISABLED=environment,NATIVE_SAMPLES=native_samples,__name__='__main__'))
 except Exception:
  (P/'user/overlap-pages-editor-error.txt').write_text(traceback.format_exc());print(traceback.format_exc(),flush=True)
 finally:
  general.idle_enable(True)
  restored=True
  for comp in reversed(disabled):
   restored=bool(editor.EditorComponentAPIBus(bus.Broadcast,'EnableComponents',[comp])) and restored
  for owner,old in aerial:
   editor.VueAtmosphereRequestBus(bus.Event,'SetAerialVisible',owner,old)
   restored=(bool(editor.VueAtmosphereRequestBus(bus.Event,'GetAerialVisible',owner))==old) and restored
  if profile_saved:
   comp,path,value=profile_saved;api._set(comp,path,value);restored=(api._get(comp,path)==value) and restored
  keep=general.get_cvar("ed_keepEditorActive");general.run_console("ed_keepEditorActive 1")
  try:general.idle_wait_frames(60)
  finally:general.run_console("ed_keepEditorActive "+str(keep))
  for comp in disabled:restored=bool(editor.EditorComponentAPIBus(bus.Broadcast,'IsComponentEnabled',comp)) and restored
  (P/'user/overlap-pages-isolated-environment.json').write_text(json.dumps({'disabled':environment,'aerial_controls':len(aerial),'restored':restored},indent=2))
  general.idle_enable(False)

QtCore.QTimer.singleShot(15000,run)
