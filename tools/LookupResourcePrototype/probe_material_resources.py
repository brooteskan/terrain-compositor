from pathlib import Path
import json,re,subprocess,csv
P=Path('D:/diversion/TG/TGProject');O=Path('D:/brooteskan/work/lookup-cause');F=O/'offline-material';F.mkdir(exist_ok=True)
source=(O/'offline/mode0.hlsl').read_text();opts=json.loads((P/'Cache/pc/assets/vuelivelink/vueterrainoverlap_mainpipeline_forwardpass_standardlighting_dx12.options.json').read_text())['ShaderOptions']
D=P/'build/windows/bin/profile/Builders/DirectXShaderCompiler/dxc.exe';R=Path('D:/brooteskan/work/rga-2.15/rga.exe')
values={o['name']:(o['defaultValue'] or ('false' if o['type']=='?bool' else o['values'][0])) for o in opts}
values.update(o_applySpecularAA='true',o_baseColor_useTexture='true',o_normal_useTexture='true',o_terrainHeightFp16='true',o_terrainOverlapFixedHeightExtent='true',o_terrainOverlapSourceCount='2',o_terrainOverlapPages='false')
rows=[]
for mode in (0,4):
 s=source
 for name,val in dict(values,o_terrainContributorControl=str(mode)).items():
  pattern=r'([:\w]+) GetSpecializationConstant_'+name+r'\(\)\s*\{[^}]*\}'
  found=re.search(pattern,s)
  # Captured overlap constants are already fixed by the earlier probe.
  if not found:continue
  kind=found[1];s,n=re.subn(pattern,f'{kind} GetSpecializationConstant_{name}() {{ return ({kind})({val}); }}',s);assert n==1,name
 path=F/f'mode{mode}.hlsl';path.write_text(s)
 args=[D,'-E','PixelShader','-T','ps_6_2','-Zpr','-enable-16bit-types','-Fo',path.with_suffix('.dxil'),'-Fc',path.with_suffix('.txt'),path]
 r=subprocess.run(list(map(str,args)),capture_output=True,text=True);assert not r.returncode,r.stderr
 if mode==0:
  r=subprocess.run(list(map(str,[D,'-E','VertexShader','-T','vs_6_2','-Zpr','-enable-16bit-types','-Fo',F/'vertex.dxil',path])),capture_output=True,text=True);assert not r.returncode,r.stderr
 args=[R,'-s','dx12','-c','gfx90c','--ps-blob',path.with_suffix('.dxil'),'--vs-blob',F/'vertex.dxil','--ps-model','ps_6_2','--vs-model','vs_6_2','--autogen-dir',O/'rga-auto','--isa',F/f'mode{mode}-isa.txt','-a',F/f'mode{mode}-stats.txt']
 r=subprocess.run(list(map(str,args)),capture_output=True,text=True);(F/f'mode{mode}.log').write_text(r.stdout+r.stderr)
 stats=F/f'gfx90c_mode{mode}-stats_pixel.txt';assert stats.exists(),r.stdout+r.stderr
 rows.append({'mode':mode,**next(csv.DictReader(stats.open()))})
(F/'specialization-inputs.json').write_text(json.dumps({'note':'Representative fully specialized material; renderer defaults, not captured live driver PSO.','values':values,'rows':rows},indent=2)+'\n');print(json.dumps(rows,indent=2))
