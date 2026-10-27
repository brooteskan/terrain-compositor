from pathlib import Path
import json,hashlib,shutil,sys,re
import numpy as np
P=Path('D:/diversion/TG/TGProject');R=P/'Gems/terrain-compositor';B=R/'integrations/tg';W=Path('D:/brooteskan/work');O=W/'lookup-cause';small=O/'small32'
data=np.load(small/'tiles.npz');labels=data['cells'].ravel();words=[sum(int(labels[i+j])<<(2*j) for j in range(16)) for i in range(0,len(labels),16)]
decoded=np.array([(words[i>>4]>>((i&15)*2))&3 for i in range(len(labels))]);assert np.array_equal(decoded,labels)
s=(small/'VueTerrainContributorTiles.azsli').read_text()
for name in ('VueContributorEndpoint','VueContributorBlend'):s=re.sub(r'static const uint '+name+r'\[[^\n]+\n','',s)
needle='uint VueContributorTile(float2 world)'
s=s.replace(needle,'static const uint VueContributorPacked[64] = { '+','.join(str(x)+'u' for x in words)+' };\n'+needle)
start=s.index('    uint bit = ');end=s.index('\n}',start)
s=s[:start]+'''    uint packed = VueContributorPacked[index >> 4u];
    return (packed >> ((index & 15u) * 2u)) & 3u;'''+s[end:]
(small/'VueTerrainContributorTiles.packed.azsli').write_text(s)
pub=json.loads((small/'publication.json').read_text());pub['representation']='packed 2-bit labels';pub['decoded_cells_verified']=len(labels);pub['logical_bytes']=len(words)*4;(small/'publication.json').write_text(json.dumps(pub,indent=2)+'\n')
include='Assets/VueLiveLink/VueTerrainContributorTiles.azsli';shutil.copyfile(small/'VueTerrainContributorTiles.packed.azsli',B/'project'/include)
m=json.loads((B/'manifest.json').read_text())
for e in m['files']:
 if e['path']==include:
  h=hashlib.sha256((P/include).read_bytes().replace(b'\r\n',b'\n')).hexdigest()
  if h not in e['accepted_sha256']:e['accepted_sha256'].append(h)
(B/'manifest.json').write_text(json.dumps(m,indent=2)+'\n');sys.path.insert(0,str(B));from SyncProject import sync
print(sync(P))
status=json.loads((P/'user/VueLiveLink/terrain_overlap_status.json').read_text());mapping={}
for mode in (0,3,4):
 mapping[mode]={}
 for raw in status['materials'].values():
  src=Path(raw).with_suffix('.material');material=json.loads((P/src).read_text());material['propertyValues'].update({'contributorSelection.key':pub['key'],'contributorSelection.mode':mode})
  target=Path('Assets/VueLiveLink/lookup_small')/str(mode)/(src.parent.name+'.material');(P/target).parent.mkdir(parents=True,exist_ok=True);(P/target).write_text(json.dumps(material,indent=2)+'\n')
  mapping[mode][str(src).replace('\\','/').lower()]=str(target).replace('\\','/')
s=(W/'profile_lookup_cause.py').read_text();start=s.index('  cases=[');end=s.index('  output=',start)
s=s[:start]+"  cases=[case('complete',0),case('small-lookup',4),case('selection',3),case('complete-repeat',0),case('selection-repeat',3),case('small-lookup-repeat',4),case('complete-final',0)]\n"+s[end:]
start=s.index('dict(MODE_MATERIALS=');end=s.index(',SHADER_SHA256=',start);s=s[:start]+'dict(MODE_MATERIALS='+repr(mapping)+s[end:]
s=s.replace("root=W/'contributor-tiles'","root=W/'lookup-cause/small32'").replace('20261024-lookup-cause','20261024-lookup-small')
compile(s,'lookup-small','exec');(W/'profile_lookup_small.py').write_text(s);print('Packed conservative 256-byte table installed; profile prepared')
