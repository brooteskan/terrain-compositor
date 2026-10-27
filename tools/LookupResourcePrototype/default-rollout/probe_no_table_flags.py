from pathlib import Path
import subprocess,csv,json
P=Path('D:/diversion/TG/TGProject');O=Path('D:/brooteskan/work/lookup-cause');F=O/'offline-no-table';D=P/'build/windows/bin/profile/Builders/DirectXShaderCompiler/dxc.exe';R=Path('D:/brooteskan/work/rga-2.15/rga.exe');rows=[]
for flags in [['-O1'],['-O2'],['-Gfa'],['-Gfp']]:
 label='flags'+''.join(flags);path=F/(label+'.hlsl');path.write_bytes((O/'offline-candidate/fallback.hlsl').read_bytes())
 args=[D,'-E','PixelShader','-T','ps_6_2','-Zpr','-enable-16bit-types',*flags,'-Fo',path.with_suffix('.dxil'),'-Fc',path.with_suffix('.txt'),path];r=subprocess.run(list(map(str,args)),capture_output=True,text=True)
 if r.returncode:print(label,r.stderr,flush=True);continue
 args=[R,'-s','dx12','-c','gfx90c','--ps-blob',path.with_suffix('.dxil'),'--vs-blob',O/'offline/vertex.dxil','--ps-model','ps_6_2','--vs-model','vs_6_2','--autogen-dir',O/'rga-auto','--isa',F/(label+'-isa.txt'),'-a',F/(label+'-stats.txt')];r=subprocess.run(list(map(str,args)),capture_output=True,text=True);(F/(label+'.log')).write_text(r.stdout+r.stderr)
 stats=F/('gfx90c_'+label+'-stats_pixel.txt');assert stats.exists(),r.stdout+r.stderr;rows.append({'case':label,**next(csv.DictReader(stats.open()))});print(rows[-1],flush=True)
(F/'flags-summary.json').write_text(json.dumps(rows,indent=2)+'\n')
