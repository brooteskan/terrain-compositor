import hashlib,json,os,time,traceback
from pathlib import Path
import azlmbr.atom as atom
import azlmbr.bus as bus
import azlmbr.debug as debug
import azlmbr.editor as editor
import azlmbr.entity as entity
import azlmbr.terrain_canvas as canvas
import azlmbr.legacy.general as general
import azlmbr.paths as paths
ROOT=Path(paths.projectroot);OUT=Path(os.environ.get('TG_INCREMENTAL_CLIPMAP_REPORT_ROOT',str(ROOT/'user/automation/incremental-material-clipmaps')));OUT.mkdir(parents=True,exist_ok=True)
LEVEL=ROOT/'Levels/VueLiveLink/VueLiveLink.prefab';HASH=hashlib.sha256(LEVEL.read_bytes()).hexdigest()
R={'passed':False,'errors':[],'captures':[],'samples':[]};CVARS={};CAM=None;SIZE=None;POLICY=None
trace=debug.TraceMessageBusHandler();trace.connect(None)
def error(a):
    if a[0]=='AtomViewportDisplayIconSystemComponent' and 'FlyCameraInput.svg' in str(a[4]):return False
    R['errors'].append([str(a[0]),str(a[4])]);return False
trace.add_callback('OnPreError',error)
def save(): (OUT/'results.json').write_text(json.dumps(R,indent=2))
def cv(n,v):
    if n not in CVARS:CVARS[n]=general.get_cvar(n)
    general.run_console('%s %s'%(n,v))
def stat(n):return int(general.get_cvar('r_terrainGraphClipmap'+n))
def settle(s):
    end=time.monotonic()+s
    while time.monotonic()<end:general.idle_wait_frames(1)
def capture(n):
    o=atom.FrameCaptureRequestBus(bus.Broadcast,'CaptureScreenshot',str(OUT/(n+'.png')));assert o.IsSuccess()
    done={};h=atom.FrameCaptureNotificationBusHandler();h.connect(o.GetValue())
    h.add_callback('OnFrameCaptureFinished',lambda a:done.update(ok=a[0]==atom.FrameCaptureResult_Success))
    try:
        end=time.monotonic()+30
        while not done:assert time.monotonic()<end;general.idle_wait_frames(1)
        assert done['ok'];R['captures'].append(n);save()
    finally:h.disconnect()
try:
    general.idle_enable(True)
    for n,v in [('ed_keepEditorActive',1),('ed_backgroundUpdatePeriod',-1),('vsync_interval',0),('sys_MaxFPS',-1),('r_terrainGraphClipmapDiagnostics',True)]:cv(n,v)
    assert general.open_level(str(LEVEL));settle(5)
    types=editor.EditorComponentAPIBus(bus.Broadcast,'FindComponentTypeIdsByEntityType',['Terrain Material'],entity.EntityType().Game)
    owners=[o for o in entity.SearchBus(bus.Broadcast,'SearchEntities',entity.SearchFilter()) if editor.EditorComponentAPIBus(bus.Broadcast,'HasComponentOfType',o,types[0])]
    R['material_status']=[canvas.TerrainTintMaterialRequestBus(bus.Event,'GetStatus',o) for o in owners];save()
    p,r=general.get_current_view_position(),general.get_current_view_rotation();CAM=([p.x,p.y,p.z],[r.x,r.y,r.z])
    v=general.get_viewport_size();SIZE=(int(v.x),int(v.y));POLICY=general.get_viewport_expansion_policy()
    general.set_viewport_expansion_policy('FixedSize');general.set_viewport_size(1280,720);general.update_viewport()
    cfg=json.loads((Path(__file__).resolve().parent/'VueSceneBenchmark.json').read_text());general.set_current_view_position(*cfg['position']);general.set_current_view_rotation(*cfg['rotation'])
    cv('r_displayInfo',0);cv('r_terrainGraphClipmaps',True);cv('r_terrainGraphClipmapIncremental',False);settle(4);capture('full')
    cv('r_terrainGraphClipmapBudgetTexels',32768);cv('r_terrainGraphClipmapBudgetPasses',2);cv('r_terrainGraphClipmapIncremental',True)
    for frame in range(230):
        general.idle_wait_frames(1)
        sample={'frame':frame,'texels':stat('Texels'),'pending':stat('PendingTexels'),'ready':general.get_cvar('r_terrainGraphClipmapReady')}
        R['samples'].append(sample);assert sample['texels']<=32768
        if frame>20 and not sample['pending'] and sample['ready']=='true':break
    assert R['samples'][-1]['ready']=='true' and not R['samples'][-1]['pending'],R['samples'][-1]
    capture('incremental');cv('r_terrainGraphClipmapDebug',True);capture('coverage');cv('r_terrainGraphClipmapDebug',False)
    for i in range(40):
        general.set_current_view_position(cfg['position'][0]+i*.25,cfg['position'][1]+i*.125,cfg['position'][2]);general.idle_wait_frames(1)
        R['samples'].append({'move':i,'texels':stat('Texels'),'pending':stat('PendingTexels')});assert stat('Texels')<=32768
    capture('moving-pending');cv('r_terrainGraphClipmaps',False);settle(1);capture('moving-direct')
    cv('r_terrainGraphClipmaps',True);cv('r_terrainGraphClipmapBudgetTexels',1024)
    general.set_current_view_position(cfg['position'][0]+512,cfg['position'][1]+256,cfg['position'][2]+50)
    for i in range(12):general.idle_wait_frames(1);assert stat('Texels')<=1024
    capture('teleport-pending');cv('r_terrainGraphClipmaps',False);settle(1);capture('teleport-direct')
    R['passed']=not R['errors']
except Exception:R['exception']=traceback.format_exc()
finally:
    if CAM:general.set_current_view_position(*CAM[0]);general.set_current_view_rotation(*CAM[1])
    if SIZE:general.set_viewport_size(*SIZE)
    if POLICY:general.set_viewport_expansion_policy(POLICY)
    for n,v in CVARS.items():general.run_console('%s %s'%(n,v))
    R['scene_unchanged']=hashlib.sha256(LEVEL.read_bytes()).hexdigest()==HASH;R['passed']=R['passed'] and R['scene_unchanged'];save()
    trace.disconnect();general.idle_enable(False);general.exit_no_prompt()
