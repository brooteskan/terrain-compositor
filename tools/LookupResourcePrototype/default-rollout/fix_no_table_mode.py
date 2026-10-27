from pathlib import Path
import json,sys,shutil
P=Path('D:/diversion/TG/TGProject');B=P/'Gems/terrain-compositor/integrations/tg'
for root in (P,B/'project'):
 p=root/'Gem/Source/Editor/VueLiveLink/terrain_overlap.py';s=p.read_text().replace("'type':'Enum','enumValues':['Original','ShadeLoop','AllLoop','QueryLoop'],'defaultValue':'Original'","'type':'UInt','defaultValue':0,'min':0,'max':3");p.write_text(s)
sys.path.insert(0,str(P/'Gem/Source/Editor/VueLiveLink'));import terrain_overlap;terrain_overlap.install_material_layout(P);shutil.copyfile(P/'Assets/VueLiveLink/VueTerrainOverlap.materialtype',B/'project/Assets/VueLiveLink/VueTerrainOverlap.materialtype')
for p in (P/'Assets/VueLiveLink/no_table_test').glob('*/*.material'):
 m=json.loads(p.read_text());mode=int(p.parent.name);m['propertyValues']['compilerExperiment.mode']=0 if mode<2 else mode-1;p.write_text(json.dumps(m,indent=2)+'\n')
