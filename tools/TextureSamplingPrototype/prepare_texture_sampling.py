from pathlib import Path
import json,hashlib,shutil,struct,sys
import numpy as np
P=Path('D:/diversion/TG/TGProject');R=P/'Gems/terrain-compositor';B=R/'integrations/tg';W=Path('D:/brooteskan/work');O=W/'texture-sampling';audit=json.loads((O/'audit.json').read_text());assert not (O/'before').exists()
rels=['Assets/VueLiveLink/VueTerrainMesh.azsli','Assets/VueLiveLink/VueTerrainOverlap.azsli','Assets/VueLiveLink/VueTerrainOverlap.materialtype','Gem/Source/Editor/VueLiveLink/terrain_overlap.py'];hashes={}
for rel in rels:
 for tag,root in [('canonical',B/'project'),('consumer',P)]:
  target=O/'before'/tag/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/rel,target);hashes[tag+'/'+rel]=hashlib.sha256(target.read_bytes()).hexdigest()
shutil.copyfile(B/'manifest.json',O/'before/manifest.json');(O/'before/hashes.json').write_text(json.dumps(hashes,indent=2)+'\n')
preset=P/'Config/AtomImageBuilder/TerrainSampling_RG16F.preset';assert not preset.exists();settings={'Type':'JsonSerialization','Version':1,'ClassName':'MultiplatformPresetSettings','ClassData':{'DefaultPreset':{'UUID':'{D3A482BF-0E52-4F9A-8C27-37B01C4E975A}','Name':'TerrainSampling_RG16F','SourceColor':'Linear','DestColor':'Linear','PixelFormat':'R16G16F','SuppressEngineReduce':True},'PlatformsPresets':{}}};preset.write_text(json.dumps(settings,indent=2)+'\n')
sys.path.insert(0,str(P/'Gem/Source/Editor/VueLiveLink'));import terrain_normals
texture_mapping={'xy':{},'octa':{}};field_reports=[]
sys.path.insert(0,str(W));from BuildOverlapPages import sample,unit
rng=np.random.default_rng(4762)
for r in audit['textures']:
 if r.get('dxgi')!=10:continue
 rel=r['path'];b=(P/rel).read_bytes();assert hashlib.sha256(b).hexdigest()==r['sha256'];h,w=r['height'],r['width'];n=np.frombuffer(b,dtype='<f2',offset=148).reshape(h,w,4)[...,:3].astype(np.float32);assert n[...,2].min()>=0
 u=unit(n);octa=u[...,:2]/np.abs(u).sum(axis=-1,keepdims=True);entry={'original':rel,'size':[w,h],'original_payload_bytes':w*h*8,'representations':{}}
 uv=rng.random((100000,2),dtype=np.float32);ref=unit(sample(n,uv,centers=True))
 for label,values in [('xy',n[...,:2]),('octa',octa)]:
  target=Path('Assets/VueLiveLink/texture_sampling_fields')/label/(Path(rel).parts[-3]+'_normals.dds');path=P/target;path.parent.mkdir(parents=True,exist_ok=True);header=struct.pack('<7I',124,0x100F,h,w,w*4,0,0)+bytes(44)+struct.pack('<II4s5I',32,4,b'DX10',0,0,0,0,0)+struct.pack('<5I',0x1000,0,0,0,0)+struct.pack('<5I',34,3,0,1,0)
  path.write_bytes(b'DDS '+header+values.astype('<f2').tobytes());path.with_suffix('.dds.assetinfo').write_text(terrain_normals.TEXTURE_SETTINGS.replace('LUT_RGBA16F','TerrainSampling_RG16F'))
  xy=sample(values.astype('<f2').astype(np.float32),uv,centers=True).astype('<f2').astype(np.float32)
  if label=='xy':out=np.concatenate([xy,np.sqrt(np.maximum(1-(xy*xy).sum(axis=-1,keepdims=True),0))],axis=-1)
  else:
   out=np.concatenate([xy,1-np.abs(xy).sum(axis=-1,keepdims=True)],axis=-1);fold=out[...,2]<0;out[fold,:2]=(1-np.abs(out[fold,1::-1]))*np.where(out[fold,:2]>=0,1,-1)
  out=unit(out.astype('<f2').astype(np.float32));angle=np.degrees(np.arctan2(np.linalg.norm(np.cross(ref,out),axis=-1),np.sum(ref*out,axis=-1)))
  texture_mapping[label][rel]=target.as_posix();entry['representations'][label]={'path':target.as_posix(),'payload_bytes':w*h*4,'random_samples':len(uv),'mean_angular_error_degrees':float(angle.mean()),'p99_angular_error_degrees':float(np.quantile(angle,.99)),'max_angular_error_degrees':float(angle.max())}
 field_reports.append(entry)
(O/'fields.json').write_text(json.dumps(field_reports,indent=2)+'\n');(O/'texture-mapping.json').write_text(json.dumps(texture_mapping,indent=2)+'\n')
p=B/'project'/rels[0];s=p.read_text();needle='#pragma once';s=s.replace(needle,needle+'\n// Reversible texture-bandwidth experiments. Zero/false retains the faster default.\n[[range(0, 2)]]\noption int o_terrainNormalPacking = 0;\noption bool o_terrainTextureMipProbe = false;\n',1)
needle='#include "VueGroundSampling.azsli"';assert needle in s
s=s.replace(needle,'''// Decode only experimental child-material field bindings.
half3 VueDecodeNormalField(half3 raw)
{
    if (o_terrainNormalPacking == 1)
    {
        float2 xy = float2(raw.xy);
        return half3(raw.xy, half(sqrt(saturate(1.0 - dot(xy, xy)))));
    }
    if (o_terrainNormalPacking == 2)
    {
        float3 n = float3(raw.xy, 1.0 - abs(float(raw.x)) - abs(float(raw.y)));
        if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        return half3(n);
    }
    return raw;
}
'''+needle,1)
needle='    float4 uvDxDy = float4(ddx(geoData.uvs[0]), ddy(geoData.uvs[0]));';assert needle in s;s=s.replace(needle,needle+'\n    if (o_terrainTextureMipProbe) uvDxDy *= 2.0;',1)
needle='return half3(GetMaterialTexture(params.m_vueNormalField).SampleLevel(GetMaterialTextureSampler(), texel, 0).xyz);';assert needle in s;s=s.replace(needle,'return VueDecodeNormalField(half3(GetMaterialTexture(params.m_vueNormalField).SampleLevel(GetMaterialTextureSampler(), texel, 0).xyz));',1);p.write_text(s)
p=B/'project'/rels[1];s=p.read_text();needle='half3 n = half3(GetMaterialTexture(s.normalField).SampleLevel(GetMaterialTextureSampler(), texel, 0).xyz);';assert needle in s;s=s.replace(needle,'half3 n = VueDecodeNormalField(half3(GetMaterialTexture(s.normalField).SampleLevel(GetMaterialTextureSampler(), texel, 0).xyz));',1)
needle='''    return float4(dot(source.uvRow0.xy, dx), dot(source.uvRow1.xy, dx),
                  dot(source.uvRow0.xy, dy), dot(source.uvRow1.xy, dy));''';assert needle in s;s=s.replace(needle,'''    float4 gradients = float4(dot(source.uvRow0.xy, dx), dot(source.uvRow1.xy, dx),
                  dot(source.uvRow0.xy, dy), dot(source.uvRow1.xy, dy));
    return o_terrainTextureMipProbe ? gradients * 2.0 : gradients;''',1);p.write_text(s)
p=B/'project'/rels[3];s=p.read_text();needle="    shader = [f'// Generated by terrain_overlap.install_material_layout.";assert needle in s
s=s.replace(needle,"    groups.append({'name':'samplingExperiment','displayName':'Texture Sampling (Experiment)','properties':[{'name':'normalPacking','type':'UInt','defaultValue':0,'min':0,'max':2,'connection':{'type':'ShaderOption','name':'o_terrainNormalPacking'}},{'name':'mipProbe','type':'Bool','defaultValue':False,'connection':{'type':'ShaderOption','name':'o_terrainTextureMipProbe'}}]})\n"+needle,1);p.write_text(s)
m=json.loads((B/'manifest.json').read_text())
for e in m['files']:
 if e['path'] in rels:
  val=hashlib.sha256((P/e['path']).read_bytes().replace(b'\r\n',b'\n')).hexdigest()
  if val not in e['accepted_sha256']:e['accepted_sha256'].append(val)
(B/'manifest.json').write_text(json.dumps(m,indent=2)+'\n');sys.path.insert(0,str(B));from SyncProject import sync;print(sync(P));import terrain_overlap;terrain_overlap.install_material_layout(P);shutil.copyfile(P/rels[2],B/'project'/rels[2])
mapping={}
for mode in range(6):
 mapping[mode]={}
 for raw,v in audit['materials'].items():
  src=Path(raw);mat=json.loads((P/src).read_text());props=mat['propertyValues'];props.pop('contributorSelection.enabled',None);props.pop('contributorSelection.key',None);props.update({'samplingExperiment.normalPacking':mode-1 if mode in (2,3) else 0,'samplingExperiment.mipProbe':mode==5})
  if mode==0:props['contributorSelection.enabled']=False
  if mode in (2,3):
   remap=texture_mapping['xy' if mode==2 else 'octa']
   for k,val in list(props.items()):
    if isinstance(val,str) and val in remap:props[k]=remap[val]
  if mode==4:
   for k in ('normal.textureMap','overlapSource0.normalMap','overlapSource1.normalMap'):props[k]=''
  dst=Path('Assets/VueLiveLink/texture_sampling_test')/str(mode)/(src.parent.name+'.material');(P/dst).parent.mkdir(parents=True,exist_ok=True);(P/dst).write_text(json.dumps(mat,indent=2)+'\n');mapping[mode][src.as_posix().lower()]=dst.as_posix()
s=(W/'profile_default_shader.py').read_text();start=s.index('  cases=[');end=s.index('  output=',start)
s=s[:start]+"  cases=[case('complete',0),case('default',1),case('compact-xy',2),case('compact-octa',3),case('default-middle',1),case('no-normal-maps',4),case('mip-probe',5),case('default-repeat',1),case('mip-probe-repeat',5),case('no-normal-maps-repeat',4),case('compact-octa-repeat',3),case('compact-xy-repeat',2),case('default-final',1),case('complete-final',0)]\n"+s[end:]
start=s.index('dict(MODE_MATERIALS=');end=s.index(',SHADER_SHA256=',start);s=s[:start]+'dict(MODE_MATERIALS='+repr(mapping)+s[end:];s=s.replace('20261024-default-shader','20261024-texture-sampling').replace('overlap-pages-isolated-environment.json','texture-sampling-environment.json');compile(s,'sampling','exec');(W/'profile_texture_sampling.py').write_text(s)
(O/'mapping.json').write_text(json.dumps(mapping,indent=2)+'\n');print(json.dumps(field_reports,indent=2));print('Prepared compact fields and explicitly appearance-changing diagnostic controls; default/fallback retained')
