from pathlib import Path
import json,hashlib,shutil,sys
P=Path('D:/diversion/TG/TGProject');R=P/'Gems/terrain-compositor';B=R/'integrations/tg';W=Path('D:/brooteskan/work');O=W/'texture-sampling';N=O/'normal-policy';N.mkdir(exist_ok=True)
result=json.loads((P/'user/TerrainSceneProfile/20261025-texture-sampling-clean/results.json').read_text());assert result['passed'] and all(result['restoration'].values());env=json.loads((P/'user/texture-sampling-clean-environment.json').read_text());assert env['restored'];(O/'clean-environment.json').write_text(json.dumps(env,indent=2)+'\n')
rels=['Assets/VueLiveLink/VueTerrainMesh.azsli','Assets/VueLiveLink/VueTerrainOverlap.azsli','Assets/VueLiveLink/VueTerrainOverlap.materialtype','Gem/Source/Editor/VueLiveLink/terrain_overlap.py']
for rel in rels:
 dst=O/'experiment'/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(B/'project'/rel,dst);shutil.copyfile(O/'before/canonical'/rel,B/'project'/rel)
p=B/'project'/rels[1];s=p.read_text();needle='option bool o_terrainContributorTiles = true;';assert needle in s;s=s.replace(needle,needle+'\n// Fine normal textures are an explicit quality choice; original fallback ignores it.\noption bool o_terrainDetailNormalMaps = false;',1);needle='s.normalMap >= 0, s.normalStrength';assert s.count(needle)==2;s=s.replace(needle,'(s.normalMap >= 0 && (!o_terrainContributorTiles || o_terrainDetailNormalMaps)), s.normalStrength');p.write_text(s)
group=json.loads(Path('D:/O3DE-SDK-TG/Gems/Atom/Feature/Common/Assets/Materials/Types/MaterialInputs/NormalPropertyGroup.json').read_text());group.pop('functors');(B/'project/Assets/VueLiveLink/VueTerrainNormalPropertyGroup.json').write_text(json.dumps(group,indent=2)+'\n');shutil.copyfile(W/'VueTerrainNormalPolicy.lua',B/'project/Assets/VueLiveLink/VueTerrainNormalPolicy.lua')
p=B/'project'/rels[3];s=p.read_text();needle="    groups = data['propertyLayout']['propertyGroups']";assert needle in s;s=s.replace(needle,needle+'''
    # The compositor owns normal-texture policy so the original fallback can
    # restore authored maps without changing or republishing their bindings.
    for group in groups:
        if str(group.get('$import', '')).endswith('/NormalPropertyGroup.json'):
            group['$import'] = 'VueTerrainNormalPropertyGroup.json'
    data.setdefault('functors', []).append({'type': 'Lua', 'args': {
        'file': 'Assets/VueLiveLink/VueTerrainNormalPolicy.lua'}})
''',1)
needle="        prop('key', 'UInt', 0, 'm_vueContributorKey')]})";assert needle in s;s=s.replace(needle,"""        {'name': 'detailNormals', 'displayName': 'Detail Normal Maps',
         'description': 'Adds fine bumps from normal textures. Off keeps the geometric normal field. The original shader fallback retains authored normal settings.',
         'type': 'Bool', 'defaultValue': False,
         'connection': {'type': 'ShaderOption', 'name': 'o_terrainDetailNormalMaps'}},
"""+needle,1);p.write_text(s)
# Accept only this experiment's current consumer hashes during the transition.
manifest=json.loads((O/'before/manifest.json').read_text())
for e in manifest['files']:
 if e['path'] in rels:
  h=hashlib.sha256((O/'before/consumer'/e['path']).read_bytes().replace(b'\r\n',b'\n')).hexdigest()
  if h not in e['accepted_sha256']:e['accepted_sha256'].append(h)
temporary=json.loads(json.dumps(manifest))
for e in temporary['files']:
 if e['path'] in rels:
  h=hashlib.sha256((P/e['path']).read_bytes().replace(b'\r\n',b'\n')).hexdigest()
  if h not in e['accepted_sha256']:e['accepted_sha256'].append(h)
new=['Assets/VueLiveLink/VueTerrainNormalPropertyGroup.json','Assets/VueLiveLink/VueTerrainNormalPolicy.lua']
for rel in new:
 assert not (P/rel).exists();e={'path':rel,'source':'project/'+rel,'binary':False,'accepted_sha256':[],'accepted_json_sha256':[]};temporary['files'].append(e);manifest['files'].append(dict(e))
(B/'manifest.json').write_text(json.dumps(temporary,indent=2)+'\n');sys.path.insert(0,str(B));from SyncProject import sync;print(sync(P));(B/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');sys.path.insert(0,str(P/'Gem/Source/Editor/VueLiveLink'));import terrain_overlap;terrain_overlap.install_material_layout(P);shutil.copyfile(P/rels[2],B/'project'/rels[2])
for rel in rels+new:shutil.copyfile(B/'project'/rel,N/Path(rel).name)
mapping={};audit=json.loads((O/'audit.json').read_text())
for mode in (0,1,2):
 mapping[mode]={}
 for raw in audit['materials']:
  src=Path(raw);mat=json.loads((P/src).read_text());props=mat['propertyValues'];props.pop('contributorSelection.enabled',None);props.pop('contributorSelection.detailNormals',None);props.pop('contributorSelection.key',None)
  if mode==0:props['contributorSelection.enabled']=False
  if mode==2:props['contributorSelection.detailNormals']=True
  dst=Path('Assets/VueLiveLink/normal_policy_test')/str(mode)/(src.parent.name+'.material');(P/dst).parent.mkdir(parents=True,exist_ok=True);(P/dst).write_text(json.dumps(mat,indent=2)+'\n');mapping[mode][src.as_posix().lower()]=dst.as_posix()
s=(W/'profile_default_shader.py').read_text();start=s.index('  cases=[');end=s.index('  output=',start);s=s[:start]+"  cases=[case('complete',0),case('previous-fast',2),case('default',1),case('previous-fast-repeat',2),case('default-repeat',1),case('complete-final',0)]\n"+s[end:];start=s.index('dict(MODE_MATERIALS=');end=s.index(',SHADER_SHA256=',start);s=s[:start]+'dict(MODE_MATERIALS='+repr(mapping)+s[end:];s=s.replace('20261024-default-shader','20261025-normal-quality-policy').replace('overlap-pages-isolated-environment.json','normal-policy-environment.json');compile(s,'normalpolicy','exec');(W/'profile_normal_policy.py').write_text(s);print('Installed controllable normal quality policy with original fallback and authored bindings preserved')
