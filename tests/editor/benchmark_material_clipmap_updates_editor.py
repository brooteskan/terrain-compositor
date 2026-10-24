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
ROOT=Path(paths.projectroot);OUT=Path(os.environ.get('TG_CLIPMAP_BENCHMARK_REPORT_ROOT',str(ROOT/'user/automation/material-clipmap-benchmark')));OUT.mkdir(parents=True,exist_ok=True)
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
    for n,v in [('ed_keepEditorActive',1),('ed_backgroundUpdatePeriod',-1),('vsync_interval',0),('sys_MaxFPS',-1),
                ('r_terrainGraphClipmapDiagnostics',True),('r_terrainGraphClipmaps',True),
                ('r_terrainGraphClipmapBudgetTexels',32768),('r_terrainGraphClipmapBudgetPasses',2),('r_displayInfo',0)]:cv(n,v)
    assert general.open_level(str(LEVEL));settle(5)
    p,r=general.get_current_view_position(),general.get_current_view_rotation();CAM=([p.x,p.y,p.z],[r.x,r.y,r.z])
    v=general.get_viewport_size();SIZE=(int(v.x),int(v.y));POLICY=general.get_viewport_expansion_policy()
    general.set_viewport_expansion_policy('FixedSize');general.set_viewport_size(1280,720);general.update_viewport()
    cfg=json.loads((Path(__file__).resolve().parent/'VueSceneBenchmark.json').read_text());general.set_current_view_rotation(*cfg['rotation'])
    R['cases']=[]
    for case,incremental in [('full-a',False),('strips-a',True),('strips-b',True),('full-b',False)]:
        general.set_current_view_position(576.0,864.0,cfg['position'][2])
        cv('r_terrainGraphClipmapIncremental',incremental)
        settle(4)
        deadline=time.monotonic()+40
        while general.get_cvar('r_terrainGraphClipmapReady')!='true' or (incremental and stat('PendingTexels')):
            assert time.monotonic()<deadline,'Cache warmup timeout';general.idle_wait_frames(1)
        settle(2)
        capture(case+'-start')
        entry={'name':case,'incremental':incremental,'frames':[]};R['cases'].append(entry);R['active_case']=case;save()
        for frame in range(360):
            before=time.perf_counter()
            general.set_current_view_position(576.0+frame*.25,864.0+frame*.125,cfg['position'][2])
            general.idle_wait_frames(1)
            row={'index':frame,'wall_ms':(time.perf_counter()-before)*1000,
                 'frame_id':stat('FrameId'),'native_frame_ms':stat('FrameUs')/1000,
                 'texels':stat('Texels'),'pending':stat('PendingTexels'),'gpu_ns':stat('GpuNs'),'gpu_samples':stat('GpuSamples')}
            assert not incremental or row['texels']<=32768, row
            entry['frames'].append(row)
        capture(case+'-end');save()
    R['passed']=not R['errors']

except Exception:R['exception']=traceback.format_exc()
finally:
    if CAM:general.set_current_view_position(*CAM[0]);general.set_current_view_rotation(*CAM[1])
    if SIZE:general.set_viewport_size(*SIZE)
    if POLICY:general.set_viewport_expansion_policy(POLICY)
    for n,v in CVARS.items():general.run_console('%s %s'%(n,v))
    R['scene_unchanged']=hashlib.sha256(LEVEL.read_bytes()).hexdigest()==HASH;R['passed']=R['passed'] and R['scene_unchanged'];save()
    trace.disconnect();general.idle_enable(False);general.exit_no_prompt()
