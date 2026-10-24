"""Exercise the real inspector setting; never change visibility/camera/LOD or save."""
import hashlib,json,os,struct,sys,time,traceback
from pathlib import Path
import azlmbr.bus as bus
import azlmbr.editor as editor
import azlmbr.legacy.general as general
import azlmbr.paths as paths
import azlmbr.render as render
import azlmbr.math as azmath

P=Path(paths.projectroot);OUT=P/'user/TerrainPrecisionInspector';OUT.mkdir(parents=True,exist_ok=True)
LEVEL=P/'Levels/VueLiveLink/VueLiveLink.prefab';BEFORE=hashlib.sha256(LEVEL.read_bytes()).hexdigest()
sys.path.insert(0,str(P/'Gem/Source/Editor/VueLiveLink'))
import editor_link as link
import material_response_editor as response
import terrain_overlap_editor as overlap
R={'passed':False,'cases':[]};originals=[];updater=None;component=None;prior=32

def save():(OUT/'results.json').write_text(json.dumps(R,indent=2)+'\n')
def wait(predicate,seconds=240):
    end=time.monotonic()+seconds
    while not predicate():
        assert time.monotonic()<end,str(updater.last_status if updater else 'timeout')
        general.idle_wait_frames(1)
def visibility():
    data=json.loads(LEVEL.read_text())
    return {e['Name']:bool(editor.EditorEntityInfoRequestBus(bus.Event,'IsVisible',general.find_editor_entity(e['Name'])))
            for e in data['Entities'].values()}
def camera():
    p=general.get_current_view_position();r=general.get_current_view_rotation()
    return [[p.x,p.y,p.z],[r.x,r.y,r.z]]

save()
try:
    general.idle_enable(True)
    assert hasattr(editor,'TerrainHeightPrecisionNotificationBusHandler'),'Updated compositor module missing'
    assert general.open_level_no_prompt('VueLiveLink')
    general.idle_wait_frames(30)
    overlap.stop()
    found=editor.EditorComponentAPIBus(bus.Broadcast,'GetComponentOfType',general.find_editor_entity('Terrain Compositor'),
        azmath.Uuid_CreateString('{43588E85-B704-461F-AC94-8C9E1D916A9A}'))
    assert found.IsSuccess(),str(found.GetError())
    component=found.GetValue()
    prior=int(link._get(component,'Configuration|Height Precision'))
    for target in response.targets():
        if not target['ground']:originals.append((target,response.binding(target)))
    vis=visibility();view=camera()
    updater=overlap.start()
    for bits in (32,16,32):
        previous=updater.key
        link._set(component,'Configuration|Height Precision',bits)
        wait(lambda:updater.last_status and updater.last_status.get('phase')=='ready' and updater.pending is None
             and (previous is None or updater.key!=previous))
        wait(lambda:all(render.MaterialComponentRequestBus(bus.Event,'IsMaterialAssetReady',target['owner'],render.MaterialAssignmentId())
                        for target in response.targets() if not target['ground']))
        general.idle_wait_frames(3)
        rendered=int(editor.TerrainHeightPrecisionRequestBus(bus.Event,'GetRenderedHeightPrecision',general.find_editor_entity('Terrain Compositor')))
        stride=int(editor.TerrainHeightPrecisionRequestBus(bus.Event,'GetHeightVertexStride',general.find_editor_entity('Terrain Compositor')))
        assert rendered==bits,(bits,rendered)
        assert stride==(4 if bits==16 else 8),(bits,stride)
        entry={'bits':bits,'rendered_height_precision':rendered,'vertex_stride':stride,'materials':{},'height_formats':[]}
        for target in response.targets():
            if target['ground']:continue
            path=overlap.raw_path(response.binding(target));source=(P/path).with_suffix('.material')
            props=json.loads(source.read_text())['propertyValues']
            assert props['terrainOverlap.heightFp16']==(bits==16)
            entry['materials'][target['name']]=path
            for key,value in props.items():
                if key.startswith('overlapSource') and key.endswith('.heightMap') and value:
                    dxgi=struct.unpack_from('<I',(P/value).read_bytes(),128)[0]
                    assert dxgi==(10 if bits==16 else 2),(value,dxgi)
                    entry['height_formats'].append(dxgi)
        assert visibility()==vis,'Scene visibility changed'
        assert camera()==view,'Scene camera changed'
        entry['visibility_unchanged']=True;entry['camera_unchanged']=True
        R['cases'].append(entry);save()
    # Property path is the same one used by the inspector; all changes remain in memory.
    R['scene_unchanged']=hashlib.sha256(LEVEL.read_bytes()).hexdigest()==BEFORE
    assert R['scene_unchanged'];R['passed']=True
except Exception:R['exception']=traceback.format_exc()
finally:
    overlap.stop()
    if component:
        try:link._set(component,'Configuration|Height Precision',prior)
        except Exception:R['restore_exception']=traceback.format_exc()
    for target,value in originals:
        try:link._set(target['component'],target['property'],value)
        except Exception:R['restore_exception']=traceback.format_exc()
    R['scene_unchanged']=hashlib.sha256(LEVEL.read_bytes()).hexdigest()==BEFORE
    R['passed']=R['passed'] and R['scene_unchanged'] and 'restore_exception' not in R
    save();general.idle_enable(False)
    general.exit_no_prompt()
