from pathlib import Path
import json,hashlib,shutil,sys,re
import numpy as np
P=Path('D:/diversion/TG/TGProject');R=P/'Gems/terrain-compositor';B=R/'integrations/tg';W=Path('D:/brooteskan/work');O=W/'lookup-cause';M=O/'medium64'
labels=np.load(M/'tiles.npz')['cells'].ravel();words=[sum(int(labels[i+j])<<(2*j) for j in range(16)) for i in range(0,len(labels),16)]
assert all(((words[i>>4]>>((i&15)*2))&3)==int(labels[i]) for i in range(len(labels)))
s=(M/'VueTerrainContributorTiles.azsli').read_text()
for name in ('VueContributorEndpoint','VueContributorBlend'):s=re.sub(r'static const uint '+name+r'\[[^\n]+\n','',s)
needle='uint VueContributorTile(float2 world)';s=s.replace(needle,f'static const uint VueContributorPacked[{len(words)}] = {{ '+','.join(str(x)+'u' for x in words)+' };\n'+needle)
start=s.index('    uint bit = ');end=s.index('\n}',start);s=s[:start]+'''    uint packed = VueContributorPacked[index >> 4u];
    return (packed >> ((index & 15u) * 2u)) & 3u;'''+s[end:]
(M/'VueTerrainContributorTiles.packed.azsli').write_text(s)
pub=json.loads((M/'publication.json').read_text());pub.update(representation='packed 2-bit labels',decoded_cells_verified=len(labels),logical_bytes=len(words)*4);(M/'publication.json').write_text(json.dumps(pub,indent=2)+'\n')
include='Assets/VueLiveLink/VueTerrainContributorTiles.azsli';shutil.copyfile(M/'VueTerrainContributorTiles.packed.azsli',B/'project'/include)
mesh='Assets/VueLiveLink/VueTerrainMesh.azsli';overlap='Assets/VueLiveLink/VueTerrainOverlap.azsli'
p=B/'project'/mesh;s=p.read_text();needle='''    if (!VueTryTerrainOverlap(IN, SV, geoData, params, uvDxDy, surface))
        surface = EvaluateSurface_StandardPBR(IN, SV, geoData, params, uvDxDy, true);'''
assert needle in s
s=s.replace(needle,'''    if (o_terrainContributorControl == 5)
    {
        [branch] if (!VueTryTerrainOverlap(IN, SV, geoData, params, uvDxDy, surface))
            surface = EvaluateSurface_StandardPBR(IN, SV, geoData, params, uvDxDy, true);
    }
    else
    {
'''+needle+'''
    }''',1);p.write_text(s)
p=B/'project'/overlap;s=p.read_text();start=s.index('bool VueTryTerrainOverlap16(');end=s.index('\nbool VueTryTerrainOverlap(VsOutput',start)
clone=s[start:end].replace('VueTryTerrainOverlap16(','VueTryTerrainOverlap16Recompute(').replace('    VueOverlapQuery queries[4];\n','')
clone=clone.replace('queries[i] = VueQueryOverlap16','VueOverlapQuery query = VueQueryOverlap16').replace('queries[i].height','query.height').replace('queries[i].coverage','query.coverage')
needle='VueOverlapSource s = VueGetOverlapSource(j, params); VueOverlapQuery q = queries[j];';assert needle in clone
clone=clone.replace(needle,'''VueOverlapSource s = VueGetOverlapSource(j, params);
        VueOverlapQuery q = (VueOverlapQuery)0;
        q.uv = float2(dot(s.uvRow0, float3(geoData.positionWS.xy, 1)), dot(s.uvRow1, float3(geoData.positionWS.xy, 1)));''');assert 'queries[' not in clone
s=s[:end]+'\n// Recompute cheap UV arithmetic only for retained material contributors.\n'+clone+s[end:]
needle='    if (o_terrainHeightFp16) return VueTryTerrainOverlap16('
assert needle in s;s=s.replace(needle,'''    if (o_terrainContributorControl == 6 && o_terrainHeightFp16)
        return VueTryTerrainOverlap16Recompute(IN, SV, geoData, params, uvDxDy, worldDerivatives, surface);
'''+needle,1);p.write_text(s)
m=json.loads((B/'manifest.json').read_text())
for e in m['files']:
 if e['path'] in (include,mesh,overlap):
  h=hashlib.sha256((P/e['path']).read_bytes().replace(b'\r\n',b'\n')).hexdigest()
  if h not in e['accepted_sha256']:e['accepted_sha256'].append(h)
(B/'manifest.json').write_text(json.dumps(m,indent=2)+'\n');sys.path.insert(0,str(B));from SyncProject import sync
print(sync(P))
status=json.loads((P/'user/VueLiveLink/terrain_overlap_status.json').read_text());mapping={}
for mode in (0,3,4,5,6):
 mapping[mode]={}
 for raw in status['materials'].values():
  src=Path(raw).with_suffix('.material');material=json.loads((P/src).read_text());material['propertyValues'].update({'contributorSelection.key':0 if mode in (5,6) else pub['key'],'contributorSelection.mode':mode})
  target=Path('Assets/VueLiveLink/lookup_alternatives')/str(mode)/(src.parent.name+'.material');(P/target).parent.mkdir(parents=True,exist_ok=True);(P/target).write_text(json.dumps(material,indent=2)+'\n');mapping[mode][str(src).replace('\\','/').lower()]=str(target).replace('\\','/')
s=(W/'profile_lookup_small.py').read_text();start=s.index('  cases=[');end=s.index('  output=',start)
s=s[:start]+"  cases=[case('complete',0),case('branch-hint',5),case('recompute',6),case('medium-lookup',4),case('selection',3),case('complete-repeat',0),case('selection-repeat',3),case('recompute-repeat',6),case('branch-hint-repeat',5),case('medium-lookup-repeat',4),case('complete-final',0)]\n"+s[end:]
start=s.index('dict(MODE_MATERIALS=');end=s.index(',SHADER_SHA256=',start);s=s[:start]+'dict(MODE_MATERIALS='+repr(mapping)+s[end:]
s=s.replace("root=W/'lookup-cause/small32'","root=W/'lookup-cause/medium64'").replace('20261024-lookup-small','20261024-lookup-alternatives');compile(s,'alternatives','exec');(W/'profile_lookup_alternatives.py').write_text(s)
for rel in (mesh,overlap):shutil.copyfile(B/'project'/rel,M/Path(rel).name)
print('1 KiB classifier, branch hint and no-query-array controls prepared')
