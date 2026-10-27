from pathlib import Path
import json,re,subprocess,csv
P=Path('D:/diversion/TG/TGProject');O=Path('D:/brooteskan/work/lookup-cause');F=O/'offline-candidate';F.mkdir(exist_ok=True)
src=P/'Cache/pc/assets/vuelivelink/vueterrainoverlap_mainpipeline_forwardpass_standardlighting_dx12.hlsl';source=src.read_text();assert 'GetSpecializationConstant_o_terrainContributorTiles' in source
D=P/'build/windows/bin/profile/Builders/DirectXShaderCompiler/dxc.exe';R=Path('D:/brooteskan/work/rga-2.15/rga.exe');rows=[]
for enabled in (False,True):
 label='candidate' if enabled else 'fallback';s=source
 for name,value in dict(o_terrainHeightFp16=True,o_terrainOverlapFixedHeightExtent=True,o_terrainOverlapSourceCount=2,o_terrainOverlapPages=False,o_terrainContributorTiles=enabled).items():
  kind='bool' if isinstance(value,bool) else 'int';s,n=re.subn(kind+r' GetSpecializationConstant_'+name+r'\(\)\s*\{[^}]*\}',f'{kind} GetSpecializationConstant_{name}() {{ return {str(value).lower()}; }}',s);assert n==1,name
 path=F/(label+'.hlsl');path.write_text(s)
 args=[D,'-E','PixelShader','-T','ps_6_2','-Zpr','-enable-16bit-types','-Fo',path.with_suffix('.dxil'),'-Fc',path.with_suffix('.txt'),path];r=subprocess.run(list(map(str,args)),capture_output=True,text=True);assert not r.returncode,r.stderr
 args=[R,'-s','dx12','-c','gfx90c','--ps-blob',path.with_suffix('.dxil'),'--vs-blob',O/'offline/vertex.dxil','--ps-model','ps_6_2','--vs-model','vs_6_2','--autogen-dir',O/'rga-auto','--isa',F/(label+'-isa.txt'),'-a',F/(label+'-stats.txt')];r=subprocess.run(list(map(str,args)),capture_output=True,text=True);(F/(label+'.log')).write_text(r.stdout+r.stderr)
 stats=F/('gfx90c_'+label+'-stats_pixel.txt');assert stats.exists(),r.stdout+r.stderr;rows.append({'case':label,**next(csv.DictReader(stats.open()))})
(F/'summary.json').write_text(json.dumps(rows,indent=2)+'\n');print(json.dumps(rows,indent=2))
