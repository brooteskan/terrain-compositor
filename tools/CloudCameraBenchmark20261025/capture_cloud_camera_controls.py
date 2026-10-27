import json
from pathlib import Path
import azlmbr.bus as bus
import azlmbr.render as render
import azlmbr.editor as editor
import azlmbr.legacy.general as general
import editor_link as api
owner=general.find_editor_entity('clouds');comp=api._component(owner,'Cloud Scene Settings')
paths=editor.EditorComponentAPIBus(bus.Broadcast,'BuildComponentPropertyList',comp)
result={'lighting':json.loads(render.CloudSceneRequestBus(bus.Event,'GetLighting',owner)), 'properties':{str(p):str(api._get(comp,str(p))) for p in paths if '|' in str(p)},'measurements':json.loads(render.CloudSceneRequestBus(bus.Event,'GetMeasurements',owner))}
Path('D:/diversion/TG/TGProject/user/cloud-terrain-effective-controls.json').write_text(json.dumps(result,indent=2))
print('CLOUD_CAMERA_CONTROLS_CAPTURED')
