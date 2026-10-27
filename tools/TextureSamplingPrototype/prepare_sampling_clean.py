from pathlib import Path
import json,shutil,hashlib
from PIL import Image
P=Path('D:/diversion/TG/TGProject');W=Path('D:/brooteskan/work');O=W/'texture-sampling';r=json.loads((P/'user/TerrainSceneProfile/20261024-texture-sampling/results.json').read_text());assert r['passed'] and all(r['restoration'].values())
e=json.loads((P/'user/texture-sampling-environment.json').read_text());assert e['restored'];(O/'first-environment.json').write_text(json.dumps(e,indent=2)+'\n')
target=Path('Assets/VueLiveLink/texture_sampling_fields/constant_detail_normal.png');Image.new('RGB',(4,4),(128,128,255)).save(P/target)
settings=(P/'Assets/VueLiveLink/texture_sampling_fields/xy/2ad83d8_31a7c446_normals.dds.assetinfo').read_text().replace('TerrainSampling_RG16F','Normals');(P/target).with_suffix('.png.assetinfo').write_text(settings)
mapping=json.loads((O/'mapping.json').read_text());mapping['7']={}
for raw in mapping['1']:
 mat=json.loads((P/mapping['1'][raw]).read_text())
 for k in ('normal.textureMap','overlapSource0.normalMap','overlapSource1.normalMap'):mat['propertyValues'][k]=target.as_posix()
 src=Path(raw);dst=Path('Assets/VueLiveLink/texture_sampling_test/7')/(src.parent.name+'.material');(P/dst).parent.mkdir(parents=True,exist_ok=True);(P/dst).write_text(json.dumps(mat,indent=2)+'\n');mapping['7'][raw]=dst.as_posix()
s=(W/'profile_texture_sampling.py').read_text();start=s.index('  cases=[');end=s.index('  output=',start)
s=s[:start]+"  cases=[case('complete',0),case('default',1),case('tiny-normal-maps',7),case('no-normal-maps',4),case('default-middle',1),case('compact-xy',2),case('compact-octa',3),case('mip-probe',5),case('no-normal-maps-repeat',4),case('tiny-normal-maps-repeat',7),case('default-final',1),case('complete-final',0)]\n"+s[end:]
start=s.index('MODE_MATERIALS=');end=s.index(',SHADER_SHA256=',start);s=s[:start]+'MODE_MATERIALS='+repr({int(k):v for k,v in mapping.items()})+s[end:];s=s.replace('20261024-texture-sampling','20261025-texture-sampling-clean').replace('texture-sampling-environment.json','texture-sampling-clean-environment.json');compile(s,'samplingclean','exec');(W/'profile_sampling_clean.py').write_text(s);(O/'mapping.json').write_text(json.dumps(mapping,indent=2)+'\n');print('Prepared clean comparison without ground binding changes, including a 4x4 constant-map bandwidth control')
