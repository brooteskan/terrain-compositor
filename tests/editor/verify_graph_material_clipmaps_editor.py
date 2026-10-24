"""Run in a dedicated Editor. Restore every source edit and never save the level."""
import hashlib,json,os,sqlite3,time,traceback
from pathlib import Path
import azlmbr.asset as asset
import azlmbr.atom as atom
import azlmbr.bus as bus
import azlmbr.debug as debug
import azlmbr.editor as editor
import azlmbr.entity as entity
import azlmbr.legacy.general as general
import azlmbr.math as math
import azlmbr.paths as paths
import azlmbr.terrain_canvas as canvas
from PySide6 import QtCore

ROOT=Path(paths.projectroot)
OUT=Path(os.environ.get('TG_MATERIAL_CLIPMAP_REPORT_ROOT',str(ROOT/'user/MaterialClipmapValidation')))
OUT.mkdir(parents=True,exist_ok=True)
EXAMPLES=Path(__file__).resolve().parents[2]/'integrations/o3de-material-canvas/Assets/MaterialCanvas/Terrain/Examples'
LEVEL=ROOT/'Levels/TerrainMeshHeightGapAcceptance/TerrainMeshHeightGapAcceptance.prefab'
HASH=hashlib.sha256(LEVEL.read_bytes()).hexdigest()
RESULT={'passed':False,'cases':[],'captures':[],'errors':[],'cleanup_errors':[]}
CVARS={};ORIGINALS={};SELECTOR=None;OLD_MATERIAL=None;CREATED=False;OWNER=None
CAMERA=None;SIZE=None;POLICY=None
trace=debug.TraceMessageBusHandler();trace.connect(None)
def trace_error(args):
    if args[0]=='AtomViewportDisplayIconSystemComponent' and 'Viewport/FlyCameraInput.svg' in str(args[4]):
        RESULT.setdefault('environment_errors',[]).append(str(args[4]));return False
    RESULT['errors'].append({'window':str(args[0]),'message':str(args[4])})
    return False
trace.add_callback('OnPreError',trace_error)
def save(): (OUT/'results.json').write_text(json.dumps(RESULT,indent=2))
def wait(predicate,seconds=60):
    end=time.monotonic()+seconds
    while not predicate():
        assert time.monotonic()<end,'Timed out waiting for editor state'
        general.idle_wait_frames(1)
def settle(seconds):
    end=time.monotonic()+seconds
    while time.monotonic()<end:general.idle_wait_frames(1)
def cvar(name,value):
    if name not in CVARS:CVARS[name]=general.get_cvar(name)
    general.run_console('%s %s'%(name,value))
def ready():
    value=general.get_cvar('r_terrainGraphClipmapReady')
    RESULT['cache_state']={name:general.get_cvar(name) for name in
        ('r_terrainGraphClipmapReady','r_terrainGraphClipmapUpdates','r_terrainGraphClipmaps')}
    return str(value).lower() in ('true','1','1.0')
def updates():
    return int(general.get_cvar('r_terrainGraphClipmapUpdates'))
def capture(name):
    outcome=atom.FrameCaptureRequestBus(bus.Broadcast,'CaptureScreenshot',str(OUT/(name+'.png')))
    assert outcome.IsSuccess(),outcome.GetError()
    done={};handler=atom.FrameCaptureNotificationBusHandler();handler.connect(outcome.GetValue())
    handler.add_callback('OnFrameCaptureFinished',lambda args:done.update(success=args[0]==atom.FrameCaptureResult_Success))
    try:wait(lambda:bool(done));assert done['success']
    finally:handler.disconnect()
    RESULT['captures'].append(name);save()
def change(value):
    outcome=editor.EditorComponentAPIBus(bus.Broadcast,'SetComponentProperty',SELECTOR,'Material',value)
    assert outcome.IsSuccess(),outcome.GetError()
    wait(lambda:canvas.TerrainTintMaterialRequestBus(bus.Event,'GetStatus',OWNER) in
         ('Graph terrain material active','Default graph terrain material'))
def jobs(path):
    with sqlite3.connect('file:'+str(ROOT/'Cache/assetdb.sqlite')+'?mode=ro',uri=True) as db:
        return db.execute('SELECT j.LastLogTime,j.Status FROM Jobs j JOIN Sources s ON j.SourcePK=s.SourceID '
                          'WHERE lower(s.SourceName) LIKE ? ORDER BY j.LastLogTime DESC LIMIT 1',
                          ('%/'+path.name.lower(),)).fetchone()
def publish(path,data,job_path=None):
    if path.read_bytes()==data:return
    ORIGINALS.setdefault(path,path.read_bytes())
    watched=job_path or path;before=jobs(watched)
    forward=EXAMPLES/'textured_terrain_Forward.shader' if path.suffix=='.azsli' else None
    before_forward=jobs(forward) if forward else None
    path.write_bytes(data)
    wait(lambda:jobs(watched)!=before and jobs(watched) is not None and jobs(watched)[1] in (4,5),180)
    assert jobs(watched)[1]==4,(watched,jobs(watched))
    if forward:
        wait(lambda:jobs(forward)!=before_forward and jobs(forward) is not None and jobs(forward)[1] in (4,5),180)
        assert jobs(forward)[1]==4,(forward,jobs(forward))
    settle(4);wait(ready)
def stable_reuse(name):
    wait(ready);settle(3);before=updates();general.idle_wait_frames(60);after=updates()
    RESULT.setdefault('reuse',[]).append({'case':name,'before':before,'after':after})
    assert before==after,('Static material cache refreshed while idle',before,after)
    save()

try:
    general.idle_enable(True)
    for name,value in [('ed_keepEditorActive',1),('ed_backgroundUpdatePeriod',-1),('vsync_interval',0),('sys_MaxFPS',-1)]:cvar(name,value)
    # Retain the original full-layer reference contracts; incremental behavior has a separate test.
    cvar('r_terrainGraphClipmapIncremental',False)
    cvar('r_terrainGraphClipmapDiagnostics',True)
    assert general.open_level(str(LEVEL));settle(6)
    p,r=general.get_current_view_position(),general.get_current_view_rotation();CAMERA=([p.x,p.y,p.z],[r.x,r.y,r.z])
    size=general.get_viewport_size();SIZE=(int(size.x),int(size.y));POLICY=general.get_viewport_expansion_policy()
    general.set_viewport_expansion_policy('FixedSize');general.set_viewport_size(1280,720);general.update_viewport()
    types=editor.EditorComponentAPIBus(bus.Broadcast,'FindComponentTypeIdsByEntityType',['Terrain Material'],entity.EntityType().Game)
    owners=[candidate for candidate in entity.SearchBus(bus.Broadcast,'SearchEntities',entity.SearchFilter())
            if editor.EditorComponentAPIBus(bus.Broadcast,'HasComponentOfType',candidate,types[0])]
    assert len(owners)<=1
    if owners:
        OWNER=owners[0]
        SELECTOR=editor.EditorComponentAPIBus(bus.Broadcast,'GetComponentOfType',OWNER,types[0]).GetValue()
        OLD_MATERIAL=editor.EditorComponentAPIBus(bus.Broadcast,'GetComponentProperty',SELECTOR,'Material').GetValue()
    else:
        OWNER=editor.ToolsApplicationRequestBus(bus.Broadcast,'CreateNewEntity',entity.EntityId());CREATED=True
        SELECTOR=editor.EditorComponentAPIBus(bus.Broadcast,'AddComponentsOfType',OWNER,types).GetValue()[0]
    materials={}
    cvar('r_terrainGraphClipmapDebug',False)
    for name in ('default_terrain','procedural_terrain','textured_terrain'):
        material=asset.AssetCatalogRequestBus(bus.Broadcast,'GetAssetIdByPath',
            'materialcanvas/terrain/examples/'+name+'.azmaterial',math.Uuid(),False)
        assert material.is_valid(),name
        materials[name]=material;change(material)
        for distance,position in [('near',(16,16,32)),('far',(16,-180,180))]:
            label=name+'-'+distance
            general.set_current_view_position(*map(float,position));general.set_current_view_rotation(-35.0,0.0,0.0)
            cvar('r_terrainGraphClipmaps',False);settle(3);capture(label+'-direct')
            cvar('r_terrainGraphClipmaps',True);settle(3);wait(ready);capture(label+'-cached')
            before=updates();general.idle_wait_frames(15);after=updates()
            if after<=before:
                # A material hot reload creates a fresh cache while AP is settling.
                RESULT.setdefault('counter_resets',[]).append({'case':label,'before':before,'after':after})
                settle(2);wait(ready);before=updates();general.idle_wait_frames(15);after=updates()
            assert after>before,('Dynamic-safe graph must update each frame',before,after)
            cvar('r_terrainGraphClipmapDebug',True);capture(label+'-coverage');cvar('r_terrainGraphClipmapDebug',False)
            RESULT['cases'].append({'name':label,'ready':ready(),'updates_before':before,'updates_after':after});save()
    for _ in range(3):
        for material in materials.values():change(material)
    settle(3);wait(ready);RESULT['rapid_switch']='passed'
    change(asset.AssetId());settle(3);wait(ready);capture('cleared-selection');RESULT['clear_selection']='passed'
    change(materials['textured_terrain']);settle(3);wait(ready)
    path=EXAMPLES/'textured_terrain.material';original=path.read_bytes()
    data=json.loads(original);data['propertyValues'].update({'surface.color':[0.8,0.45,0.2,1.0],'terrain.clipmapTemporalReuse':True})
    publish(path,(json.dumps(data,indent=4)+'\n').encode());capture('material-edit');stable_reuse('material-edit')
    before=updates();general.set_current_view_position(16.25,-179.75,180.0);settle(2);wait(ready)
    RESULT['small_motion_reuse']={'before':before,'after':updates()};assert updates()==before
    capture('small-motion-cached');cvar('r_terrainGraphClipmaps',False);settle(1);capture('small-motion-direct')
    cvar('r_terrainGraphClipmaps',True);settle(1);wait(ready)
    before=updates();general.set_current_view_position(18.1,-179.75,180.0);settle(3);wait(ready)
    RESULT['single_layer_scroll']={'before':before,'after':updates()};assert 0<updates()-before<5
    # Move the camera, then verify the cache refreshes and settles again.
    before=updates();general.set_current_view_position(24.0,-176.0,180.0);settle(4);wait(ready)
    RESULT['camera_refresh']={'before':before,'after':updates()};assert updates()>before
    stable_reuse('after-camera-move');capture('camera-move')
    shader=EXAMPLES/'textured_terrain_Tint.azsli';original_shader=shader.read_bytes()
    edited=original_shader.replace(b'result.baseColor = inBaseColor;',b'result.baseColor = inBaseColor * 0.5;')
    assert edited!=original_shader
    publish(shader,edited,EXAMPLES/'textured_terrain_Clipmap.shader');capture('shader-edit');stable_reuse('shader-edit')
    cvar('r_terrainGraphClipmaps',False);settle(1);capture('shader-edit-direct');cvar('r_terrainGraphClipmaps',True);settle(1)
    publish(shader,original_shader,EXAMPLES/'textured_terrain_Clipmap.shader');capture('shader-restored')
    publish(path,original);capture('material-restored')
    general.enter_game_mode();wait(lambda:general.is_in_game_mode());settle(5);wait(ready);capture('game-mode')
    general.exit_game_mode();settle(4);wait(ready);RESULT['game_mode']='passed'
    RESULT['passed']=True
except Exception:
    RESULT['exception']=traceback.format_exc()
    cvar('r_terrainGraphClipmapDebug',True);settle(1);capture('failure-coverage')
    save()
finally:
    for path,data in reversed(list(ORIGINALS.items())):
        try:publish(path,data,EXAMPLES/'textured_terrain_Clipmap.shader' if path.suffix=='.azsli' else None)
        except Exception:RESULT['cleanup_errors'].append(str(path)+': '+traceback.format_exc())
    try:
        if general.is_in_game_mode():general.exit_game_mode();settle(2)
        if CREATED and OWNER:editor.ToolsApplicationRequestBus(bus.Broadcast,'DeleteEntityById',OWNER)
        elif SELECTOR and OLD_MATERIAL is not None:change(OLD_MATERIAL)
        if CAMERA:
            general.set_current_view_position(*CAMERA[0]);general.set_current_view_rotation(*CAMERA[1])
        if SIZE:general.set_viewport_size(*SIZE)
        if POLICY:general.set_viewport_expansion_policy(POLICY)
        for name,value in CVARS.items():general.run_console('%s %s'%(name,value))
    except Exception:RESULT['cleanup_errors'].append(traceback.format_exc())
    RESULT['scene_hash_unchanged']=hashlib.sha256(LEVEL.read_bytes()).hexdigest()==HASH
    RESULT['sources_restored']=all(path.read_bytes()==data for path,data in ORIGINALS.items())
    RESULT['passed']=RESULT['passed'] and RESULT['scene_hash_unchanged'] and RESULT['sources_restored'] and not RESULT['errors'] and not RESULT['cleanup_errors']
    save();trace.disconnect();general.idle_enable(False);general.exit_no_prompt()
