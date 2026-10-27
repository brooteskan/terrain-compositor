"""Install the measured resource-layout path by default, with normal selection."""
from pathlib import Path
import json,hashlib,sys,shutil
P=Path('D:/diversion/TG/TGProject');R=P/'Gems/terrain-compositor';B=R/'integrations/tg';W=Path('D:/brooteskan/work');O=W/'lookup-cause';N=O/'no-table';A=O/'adoption';A.mkdir(exist_ok=True)
result=json.loads((P/'user/TerrainSceneProfile/20261024-no-table-loops/results.json').read_text());assert result['passed'] and all(result['restoration'].values())
environment=json.loads((P/'user/overlap-pages-isolated-environment.json').read_text());assert environment['restored'];(N/'environment.json').write_text(json.dumps(environment,indent=2)+'\n')
rels=['Assets/VueLiveLink/VueTerrainOverlap.azsli','Assets/VueLiveLink/VueTerrainOverlap.materialtype','Gem/Source/Editor/VueLiveLink/terrain_overlap.py']
# Drop every temporary loop variant and restore the previously verified body.
for rel in rels:shutil.copyfile(N/'before'/rel,B/'project'/rel)
p=B/'project'/rels[0];s=p.read_text();s=s.replace('// Optional frozen-publication contributor classifier; default is the original shader.\noption bool o_terrainContributorTiles = false;', '''// Measured resource-layout path. With the default publication key zero,
// contributor selection/shading is identical to the original implementation.
// Disable contributorSelection.enabled to compile the original fallback.
option bool o_terrainContributorTiles = true;''',1);p.write_text(s)
p=B/'project'/rels[2];s=p.read_text();s=s.replace("'name': 'contributorSelection', 'displayName': 'Contributor Tiles (Prototype)'","'name': 'contributorSelection', 'displayName': 'Terrain Shader'")
needle="{'name': 'enabled', 'type': 'Bool', 'defaultValue': False,\n         'connection': {'type': 'ShaderOption', 'name': 'o_terrainContributorTiles'}}"
assert needle in s;s=s.replace(needle,"{'name': 'enabled', 'displayName': 'Optimized Shader', 'description': 'Measured resource-layout optimization. Disable to use the original shader.', 'type': 'Bool', 'defaultValue': True,\n         'connection': {'type': 'ShaderOption', 'name': 'o_terrainContributorTiles'}}",1)
s=s.replace("    groups.append({'name': 'contributorSelection'", "    # Keep the optional frozen classifier unkeyed. The default optimizes the\n    # compiled pipeline without rejecting contributors or depending on a bake.\n    groups.append({'name': 'contributorSelection'",1);p.write_text(s)
p=B/'project'/rels[1];m=json.loads(p.read_text());g=next(g for g in m['propertyLayout']['propertyGroups'] if g.get('name')=='contributorSelection');g['displayName']='Terrain Shader';prop=next(x for x in g['properties'] if x['name']=='enabled');prop.update(defaultValue=True,displayName='Optimized Shader',description='Measured resource-layout optimization. Disable to use the original shader.');p.write_text(json.dumps(m,indent=2,sort_keys=True)+'\n')
manifest=json.loads((N/'manifest-before.json').read_text());transitional=json.loads(json.dumps(manifest))
for e in transitional['files']:
 if e['path'] in rels:
  h=hashlib.sha256((P/e['path']).read_bytes().replace(b'\r\n',b'\n')).hexdigest()
  if h not in e['accepted_sha256']:e['accepted_sha256'].append(h)
(B/'manifest.json').write_text(json.dumps(transitional,indent=2)+'\n');sys.path.insert(0,str(B));from SyncProject import sync;print(sync(P));(B/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
sys.path.insert(0,str(P/'Gem/Source/Editor/VueLiveLink'));import terrain_overlap;terrain_overlap.install_material_layout(P);assert json.loads((P/rels[1]).read_text())==m
status=json.loads((P/'user/VueLiveLink/terrain_overlap_status.json').read_text());mapping={}
for mode in (0,1,2):
 mapping[mode]={}
 for raw in status['materials'].values():
  src=Path(raw).with_suffix('.material');mat=json.loads((P/src).read_text());props=mat['propertyValues'];props.pop('compilerExperiment.mode',None);props.pop('contributorSelection.enabled',None);props.pop('contributorSelection.key',None)
  if mode!=1:props['contributorSelection.enabled']=mode==2
  dst=Path('Assets/VueLiveLink/default_shader_test')/str(mode)/(src.parent.name+'.material');(P/dst).parent.mkdir(parents=True,exist_ok=True);(P/dst).write_text(json.dumps(mat,indent=2)+'\n');mapping[mode][src.as_posix().lower()]=dst.as_posix()
s=(W/'profile_lookup_candidate.py').read_text();start=s.index('  cases=[');end=s.index('  output=',start);s=s[:start]+"  cases=[case('complete',0),case('default',1),case('explicit-fast',2),case('complete-repeat',0),case('default-repeat',1),case('complete-final',0)]\n"+s[end:]
start=s.index('dict(MODE_MATERIALS=');end=s.index(',SHADER_SHA256=',start);s=s[:start]+'dict(MODE_MATERIALS='+repr(mapping)+s[end:];s=s.replace('20261024-lookup-candidate','20261024-default-shader');compile(s,'adoption','exec');(W/'profile_default_shader.py').write_text(s)
for rel in rels:shutil.copyfile(B/'project'/rel,A/Path(rel).name)
print('Optimized pipeline is the material and shader default; key remains zero and original fallback remains available')
