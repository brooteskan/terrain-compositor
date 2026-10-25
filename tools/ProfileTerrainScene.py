"""Profile the saved editor view with reversible terrain-only ablations; never save."""
import hashlib,json,os,shutil,sys,time,traceback
from pathlib import Path
import azlmbr.atom as atom
import azlmbr.bus as bus
import azlmbr.debug as debug
import azlmbr.editor as editor
import azlmbr.entity as entity
import azlmbr.legacy.general as general
import azlmbr.math as azmath
import azlmbr.paths as paths
import azlmbr.render as render

P=Path(paths.projectroot);CONFIG=json.loads(Path(os.environ['TC_SCENE_PROFILE_CONFIG']).read_text())
OUT=Path(CONFIG['output'])
if not OUT.is_absolute():OUT=P/OUT
OUT.mkdir(parents=True,exist_ok=False)
LEVEL=P/'Levels/VueLiveLink/VueLiveLink.prefab';HASH=hashlib.sha256(LEVEL.read_bytes()).hexdigest()
R={'passed':False,'cases':[],'restoration':{},'original_level_sha256':HASH};CVARS={};MESHES=[];AREA=None;AREA_DISABLED=False;CAMERA=None;VIEWPORT=None;VIS=None;BINDINGS=[];MONITOR=False;SOURCE=None;MATERIAL_OVERRIDES=[];PRECISION_COMPONENT=None;ORIGINAL_PRECISION=None
def save():(OUT/'results.json').write_text(json.dumps(R,indent=2)+'\n')
def cv(n,v):
 if n not in CVARS:CVARS[n]=general.get_cvar(n)
 general.run_console('%s %s'%(n,str(v).lower() if isinstance(v,bool) else v))
def settle(seconds):
 end=time.monotonic()+seconds
 while time.monotonic()<end:general.idle_wait_frames(1)
def wait(predicate,seconds=90):
 end=time.monotonic()+seconds
 while not predicate():
  assert time.monotonic()<end,'Capture timeout';general.idle_wait_frames(1)
def camera():
 p=general.get_current_view_position();r=general.get_current_view_rotation();return [[p.x,p.y,p.z],[r.x,r.y,r.z]]
def visibility():
 return {str(o):bool(editor.EditorEntityInfoRequestBus(bus.Event,'IsVisible',o)) for o in entity.SearchBus(bus.Broadcast,'SearchEntities',entity.SearchFilter())}
def capture(kind,name):
 events={'timestamps':('CapturePassTimestamp','OnCaptureQueryTimestampFinished'),
  'statistics':('CapturePassPipelineStatistics','OnCaptureQueryPipelineStatisticsFinished'),
  'cpu_frame':('CaptureCpuFrameTime','OnCaptureCpuFrameTimeFinished'),
  'metadata':('CaptureBenchmarkMetadata','OnCaptureBenchmarkMetadataFinished')}
 request,event=events[kind];done={};h=atom.ProfilingCaptureNotificationBusHandler();h.connect()
 h.add_callback(event,lambda a:done.update(success=bool(a[0]),info=str(a[1])))
 try:
  path=str(OUT/(name+'.json'));args=('Terrain scene',path) if kind=='metadata' else (path,)
  assert atom.ProfilingCaptureRequestBus(bus.Broadcast,request,*args),request
  wait(lambda:bool(done));assert done['success'],done
 finally:h.disconnect()
def shot(name):
 o=atom.FrameCaptureRequestBus(bus.Broadcast,'CaptureScreenshot',str(OUT/(name+'.png')));assert o.IsSuccess(),str(o.GetError())
 done={};h=atom.FrameCaptureNotificationBusHandler();h.connect(o.GetValue())
 h.add_callback('OnFrameCaptureFinished',lambda a:done.update(success=a[0]==atom.FrameCaptureResult_Success))
 try:wait(lambda:bool(done));assert done['success']
 finally:h.disconnect()
def metrics(name):
 folder=P/'user';before=set(folder.glob('Performance_*.json'));n=CONFIG.get('frames',120)
 for key,value in [('r_metricsDataLogType','all'),('r_metricsWaitTimePerCaptureBatch',0),('r_metricsFrameCountPerCaptureBatch',n),('r_metricsMeasureGpuTime',True),('r_metricsQuitUponCompletion',False)]:cv(key,value)
 cv('r_metricsNumberOfCaptureBatches',1)
 wall=[];last=time.perf_counter()
 for _ in range(n+12):
  general.idle_wait_frames(1);now=time.perf_counter();wall.append((now-last)*1000);last=now
 def ready():
  files=set(folder.glob('Performance_*.json'))-before
  if len(files)!=1:return False
  try:
   raw=next(iter(files)).read_text();json.loads(raw.replace('[,','[',1) if raw.startswith('[,') else raw);return True
  except (ValueError,OSError):return False
 wait(ready)
 source=(set(folder.glob('Performance_*.json'))-before).pop();dest=name+'-frames.json';shutil.copyfile(source,OUT/dest)
 cv('r_metricsMeasureGpuTime',False);cv('r_metricsNumberOfCaptureBatches',0)
 return dest,wall
def counters():
 result={}
 for n in ('Updates','Ready','Texels','PendingTexels','GpuNs','GpuSamples','FrameUs','FrameId'):
  try:result[n]=general.get_cvar('r_terrainGraphClipmap'+n)
  except Exception as ex:result[n]=str(ex)
 return result
def reset():
 global AREA_DISABLED
 if 'overlap' in globals():overlap.stop()
 if PRECISION_COMPONENT and ORIGINAL_PRECISION is not None:
  value=editor.EditorComponentAPIBus(bus.Broadcast,'GetComponentProperty',PRECISION_COMPONENT,'Configuration|Height Precision').GetValue()
  if value!=ORIGINAL_PRECISION:
   assert editor.EditorComponentAPIBus(bus.Broadcast,'SetComponentProperty',PRECISION_COMPONENT,'Configuration|Height Precision',ORIGINAL_PRECISION).IsSuccess()
 for owner,slot,key in MATERIAL_OVERRIDES:render.MaterialComponentRequestBus(bus.Event,'ClearPropertyValue',owner,slot,key)
 MATERIAL_OVERRIDES.clear()
 for target,original in BINDINGS:
  if overlap.raw_path(response.binding(target))!=overlap.raw_path(original):
   response.link()._set(target['component'],target['property'],original)
 for name,owner,visible in MESHES:editor.EditorEntityAPIBus(bus.Event,'SetVisibilityState',owner,visible)
 if AREA_DISABLED:
  assert editor.EditorComponentAPIBus(bus.Broadcast,'EnableComponents',[AREA]);AREA_DISABLED=False
 for n in R.get('case_cvars',[]):general.run_console('%s %s'%(n,CVARS[n]))
 R['case_cvars']=[]
def cpu_trace(name):
 profiler=debug.g_ProfilerSystem
 if not profiler.IsValid():R['cpu_trace_unavailable']=True;return
 path=OUT/(name+'-cpu-trace.json');active=False
 try:
  active=bool(profiler.StartCapture(str(path)));assert active
  settle(CONFIG.get('cpu_seconds',3));assert profiler.EndCapture();active=False;profiler.SetActive(False)
  wait(lambda:path.is_file() and path.stat().st_size>0)
 finally:
  if active:profiler.EndCapture()
  profiler.SetActive(False)

save()
try:
 general.idle_enable(True)
 # Keep background focus from changing pacing during capture. No resolution,
 # camera, material, precision, or LOD changes are made for the baseline.
 cv('ed_keepEditorActive',1);cv('ed_backgroundUpdatePeriod',-1)
 if not CONFIG.get('use_current_level',False):assert general.open_level_no_prompt('VueLiveLink')
 R['using_current_level']=CONFIG.get('use_current_level',False)
 settle(CONFIG.get('warmup_seconds',15))
 CAMERA=camera();size=general.get_viewport_size();VIEWPORT=[int(size.x),int(size.y)];VIS=visibility()
 R['camera']=CAMERA;R['viewport']=VIEWPORT
 R['original_pacing']={n:general.get_cvar(n) for n in ('sys_MaxFPS','vsync_interval')}
 sys.path.insert(0,str(P/'Gem/Source/Editor/VueLiveLink'))
 import terrain_overlap_editor as overlap
 import material_response_editor as response
 MONITOR=overlap._monitor is not None
 if MONITOR:
  wait(lambda:overlap._monitor.pending is None)
 R['overlap_status']=overlap._monitor.last_status if MONITOR else None
 for target in response.targets():
  if target['ground']:continue
  owner=target['owner'];MESHES.append((target['name'],owner,bool(editor.EditorEntityInfoRequestBus(bus.Event,'IsVisible',owner))))
  BINDINGS.append((target,response.binding(target)))
 R['meshes']=[{'name':n,'visible':v,'entity':str(o)} for n,o,v in MESHES]
 # Freeze automatic material republishing while render visibility is isolated.
 overlap.stop()
 owner=general.find_editor_entity('Terrain Region')
 types=editor.EditorComponentAPIBus(bus.Broadcast,'FindComponentTypeIdsByEntityType',['Terrain Layer Spawner'],entity.EntityType().Game)
 outcome=editor.EditorComponentAPIBus(bus.Broadcast,'GetComponentOfType',owner,types[0]);assert outcome.IsSuccess();AREA=outcome.GetValue()
 comp=editor.EditorComponentAPIBus(bus.Broadcast,'GetComponentOfType',general.find_editor_entity('Terrain Compositor'),azmath.Uuid_CreateString('{43588E85-B704-461F-AC94-8C9E1D916A9A}')).GetValue()
 precision=editor.EditorComponentAPIBus(bus.Broadcast,'GetComponentProperty',comp,'Configuration|Height Precision');assert precision.IsSuccess();R['height_precision']=precision.GetValue()
 PRECISION_COMPONENT=comp;ORIGINAL_PRECISION=R['height_precision']
 capture('metadata','metadata');save()
 cv('r_terrainGraphClipmapDiagnostics',CONFIG.get('cache_diagnostics',False))
 for case in CONFIG['cases']:
  reset();name=case['name'];R['active_case']=name;save();print('TERRAIN_SCENE_PROFILE',name,flush=True)
  for n,v in case.get('cvars',{}).items():cv(n,v);R['case_cvars'].append(n)
  if 'height_precision' in case:
   bits=case['height_precision'];assert bits in (16,32)
   assert editor.EditorComponentAPIBus(bus.Broadcast,'SetComponentProperty',comp,'Configuration|Height Precision',bits).IsSuccess()
   width=(R.get('overlap_status') or {}).get('width',10)
   expected=overlap.model.fingerprint(P,[row[1] for row in overlap.snapshot()],width,bits)
   overlap.start()
   wait(lambda:overlap._monitor.last_status and overlap._monitor.last_status.get('phase')=='ready' and
        overlap._monitor.last_status.get('revision')==expected and overlap._monitor.pending is None,240)
   publication=dict(overlap._monitor.last_status,heightPrecision=bits,height_formats=[])
   for product in publication['materials'].values():
    values=json.loads((P/product).with_suffix('.material').read_text())['propertyValues']
    assert values['terrainOverlap.count']==len(MESHES) and values['terrainOverlap.heightFp16']==(bits==16)
    for index in range(len(MESHES)):
     data=(P/values['overlapSource%d.heightMap'%index]).read_bytes()
     dxgi=int.from_bytes(data[128:132],'little');assert dxgi==(54 if bits==16 else 41)
     publication['height_formats'].append(dxgi)
   R.setdefault('precision_publications',{})[name]=publication
   overlap.stop()
  hidden=case.get('hide_meshes',[])
  for mesh_name,owner,visible in MESHES:
   if hidden=='all' or mesh_name in hidden:editor.EditorEntityAPIBus(bus.Event,'SetVisibilityState',owner,False)
  if case.get('base_material'):
   for target,original in BINDINGS:
    raw=overlap.raw_path(original);parent=overlap.model.original_material(P,raw);assert raw!=parent,(target['name'],raw)
    response.link()._set(target['component'],target['property'],response.product(parent))
   wait(lambda:all(render.MaterialComponentRequestBus(bus.Event,'IsMaterialAssetReady',t['owner'],render.MaterialAssignmentId()) for t,_ in BINDINGS))
  if case.get('material_overrides'):
   for target,original in BINDINGS:
    evidence=response.assignment_evidence(target)
    assert all(not value['properties'] for value in evidence.values()),'Existing material overrides need explicit preservation'
    slot=render.MaterialAssignmentId()
    for key,value in case['material_overrides'].items():
     MATERIAL_OVERRIDES.append((target['owner'],slot,key))
     render.MaterialComponentRequestBus(bus.Event,'SetPropertyValue',target['owner'],slot,key,value)
     actual=render.MaterialComponentRequestBus(bus.Event,'GetPropertyValue',target['owner'],slot,key)
     assert actual==value,(key,actual,value)
  if case.get('terrain_area') is False:
   assert editor.EditorComponentAPIBus(bus.Broadcast,'DisableComponents',[AREA]);AREA_DISABLED=True
  settle(CONFIG.get('settle_seconds',5))
  assert camera()==CAMERA,'Camera changed'
  size=general.get_viewport_size();assert [int(size.x),int(size.y)]==VIEWPORT,'Viewport changed'
  entry={'name':name,'configuration':case,'cache_before':counters()};R['cases'].append(entry);save()
  if case.get('screenshot',False):shot(name)
  entry['frame_metrics'],entry['python_wall_ms']=metrics(name)
  for index in range(CONFIG.get('pass_samples',3)):capture('timestamps',name+'-passes-%02d'%index)
  if case.get('pipeline_statistics',True):capture('statistics',name+'-statistics')
  capture('cpu_frame',name+'-cpu-frame')
  entry['cache_after']=counters()
  if case.get('cpu_trace',False):cpu_trace(name)
  assert hashlib.sha256(LEVEL.read_bytes()).hexdigest()==HASH
  save()
 R['passed']=True
except Exception:R['exception']=traceback.format_exc();print(R['exception'],flush=True)
finally:
 try:
  reset();settle(3)
  for target,value in BINDINGS:
   assert overlap.raw_path(response.binding(target))==overlap.raw_path(value),'Material binding changed'
  for name,owner,visible in MESHES:assert bool(editor.EditorEntityInfoRequestBus(bus.Event,'IsVisible',owner))==visible
  if MONITOR:overlap.start()
  if CAMERA:R['restoration']['camera']=camera()==CAMERA
  if VIS:R['restoration']['entity_visibility']=visibility()==VIS
  R['restoration']['mesh_visibility']=True;R['restoration']['material_bindings']=True
  if PRECISION_COMPONENT:R['restoration']['height_precision']=editor.EditorComponentAPIBus(bus.Broadcast,'GetComponentProperty',PRECISION_COMPONENT,'Configuration|Height Precision').GetValue()==ORIGINAL_PRECISION
 except Exception:R['restore_exception']=traceback.format_exc();R['passed']=False
 for n,v in CVARS.items():
  try:general.run_console('%s %s'%(n,v))
  except Exception:R.setdefault('cvar_restore_errors',[]).append(n);R['passed']=False
 R['restoration']['level_file']=hashlib.sha256(LEVEL.read_bytes()).hexdigest()==HASH
 R['passed']=R['passed'] and all(R['restoration'].values());R.pop('active_case',None);save()
 general.idle_enable(False)
 from PySide6 import QtCore
 if CONFIG.get('exit_editor',True):QtCore.QTimer.singleShot(0,general.exit_no_prompt)
 else:print('TERRAIN_SCENE_PROFILE_FINISHED',R['passed'],str(OUT),flush=True)
