from pathlib import Path
import json,re,subprocess,csv
P=Path('D:/diversion/TG/TGProject');W=Path('D:/brooteskan/work');O=W/'live-data';F=O/'native';F.mkdir(exist_ok=True);old=W/'lookup-cause'
source=(P/'Cache/pc/assets/vuelivelink/vueterrainoverlap_mainpipeline_forwardpass_standardlighting_dx12.hlsl').read_text();assert 'GetSpecializationConstant_o_terrainSamplingLifetime' in source
D=P/'build/windows/bin/profile/Builders/DirectXShaderCompiler/dxc.exe';R=W/'rga-2.15/rga.exe';rows=[]
for label,mode,enabled in [('original',0,False),('default',0,True),('color-first',1,True),('normal-last',2,True),('late-base',3,True)]:
 s=source
 for name,value in dict(o_terrainHeightFp16=True,o_terrainOverlapFixedHeightExtent=True,o_terrainOverlapSourceCount=2,o_terrainOverlapPages=False,o_terrainContributorTiles=enabled,o_terrainSamplingLifetime=mode).items():
  kind='bool' if isinstance(value,bool) else 'int';s,n=re.subn(kind+r' GetSpecializationConstant_'+name+r'\(\)\s*\{[^}]*\}',f'{kind} GetSpecializationConstant_{name}() {{ return {str(value).lower()}; }}',s);assert n==1,name
 path=F/(label+'.hlsl');path.write_text(s)
 args=[D,'-E','PixelShader','-T','ps_6_2','-Zpr','-enable-16bit-types','-Fo',path.with_suffix('.dxil'),'-Fc',path.with_suffix('.txt'),path];r=subprocess.run(list(map(str,args)),capture_output=True,text=True);assert not r.returncode,r.stderr[-3000:]
 args=[R,'-s','dx12','-c','gfx90c','--ps-blob',path.with_suffix('.dxil'),'--vs-blob',old/'offline/vertex.dxil','--ps-model','ps_6_2','--vs-model','vs_6_2','--autogen-dir',old/'rga-auto','--isa',F/(label+'-isa.txt'),'-a',F/(label+'-stats.txt')];r=subprocess.run(list(map(str,args)),capture_output=True,text=True);(F/(label+'.log')).write_text(r.stdout+r.stderr)
 stats=F/('gfx90c_'+label+'-stats_pixel.txt');assert stats.exists(),r.stdout+r.stderr
 dxil=path.with_suffix('.txt').read_text();calls=[line for line in dxil.splitlines() if ' call ' in line and '@dx.op.sample' in line]
 row={'case':label,'sample_calls':len(calls),**next(csv.DictReader(stats.open()))};rows.append(row);print(row,flush=True)
 (F/'summary.json').write_text(json.dumps(rows,indent=2)+'\n')
