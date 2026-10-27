from pathlib import Path
import json,re,subprocess,hashlib,sys
P=Path('D:/diversion/TG/TGProject');O=Path('D:/brooteskan/work/lookup-cause');C=P/'Cache/pc/assets/vuelivelink';stem=C/'vueterrainoverlap_mainpipeline_forwardpass_standardlighting_dx12'
source=stem.with_suffix('.hlsl').read_text();assert 'GetSpecializationConstant_o_terrainContributorControl' in source
options=json.loads(stem.with_suffix('.options.json').read_text())['ShaderOptions']
folder=O/(sys.argv[1] if len(sys.argv)>1 else 'offline');folder.mkdir(exist_ok=True)
dxc=P/'build/windows/bin/profile/Builders/DirectXShaderCompiler/dxc.exe'
# Keep unknown live renderer/material options volatile; fix only captured overlap
# properties and the orthogonal experimental mode. This is an offline probe,
# not a dump of the driver's actual live pipeline machine code.
fixed={'o_terrainHeightFp16':True,'o_terrainOverlapFixedHeightExtent':True,'o_terrainOverlapSourceCount':2,'o_terrainOverlapPages':False}
rows=[]
modes=tuple(map(int,sys.argv[2].split(','))) if len(sys.argv)>2 else ((0,3,4) if len(sys.argv)>1 else (0,4,5,6))
for mode in modes:
 text=source
 for name,value in dict(fixed,o_terrainContributorControl=mode).items():
  kind='bool' if isinstance(value,bool) else 'int';v=str(value).lower()
  text,n=re.subn(kind+r' GetSpecializationConstant_'+name+r'\(\)\s*\{[^}]*\}',f'{kind} GetSpecializationConstant_{name}() {{ return {v}; }}',text);assert n==1,name
 path=folder/f'mode{mode}.hlsl';path.write_text(text)
 cmd=[dxc,'-E','PixelShader','-T','ps_6_2','-Zpr','-enable-16bit-types','-Fo',path.with_suffix('.dxil'),'-Fc',path.with_suffix('.txt'),path]
 result=subprocess.run([str(x) for x in cmd],capture_output=True,text=True);assert not result.returncode,result.stdout+result.stderr
 a=path.with_suffix('.txt').read_text()
 row={'mode':mode,'dxil_bytes':path.with_suffix('.dxil').stat().st_size,'allocas':re.findall(r'^.*alloca.*$',a,re.M),'samples':len(re.findall(r'call[^\n]*@dx.op.sample',a)),'derivatives':len(re.findall(r'call[^\n]*@dx.op.unary.f32\(i32 (83|84|85|86)',a)),'global_arrays':re.findall(r'^@[^\n]*\[(?:512|1024) x i32\][^\n]*',a),'dxil_sha256':hashlib.sha256(path.with_suffix('.dxil').read_bytes()).hexdigest()}
 rows.append(row)
(folder/'summary.json').write_text(json.dumps(rows,indent=2)+'\n');print(json.dumps([{k:v for k,v in r.items() if k not in ('allocas','global_arrays')}|{'alloca_count':len(r['allocas']),'global_array_count':len(r['global_arrays'])} for r in rows],indent=2))
