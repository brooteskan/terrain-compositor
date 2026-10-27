from pathlib import Path
import subprocess,re,json,csv
P=Path('D:/diversion/TG/TGProject');O=Path('D:/brooteskan/work/lookup-cause');F=O/'offline-no-table';F.mkdir(exist_ok=True)
source=(O/'offline-candidate/fallback.hlsl').read_text();start=source.index('bool VueTryTerrainOverlap16(');end=source.index('bool VueTryTerrainOverlap(',start)
D=P/'build/windows/bin/profile/Builders/DirectXShaderCompiler/dxc.exe';R=Path('D:/brooteskan/work/rga-2.15/rga.exe');rows=[]
for label,indices in [('shade-loop',[2]),('all-loop',[0,1,2]),('query-loop',[0])]:
 block=source[start:end];n=[0]
 def repl(m):
  i=n[0];n[0]+=1
  return '[ loop ]' if i in indices else m[0]
 block=re.sub(r'\[ unroll \]',repl,block);assert n[0]==3
 s=source[:start]+block+source[end:];path=F/(label+'.hlsl');path.write_text(s)
 args=[D,'-E','PixelShader','-T','ps_6_2','-Zpr','-enable-16bit-types','-Fo',path.with_suffix('.dxil'),'-Fc',path.with_suffix('.txt'),path];r=subprocess.run(list(map(str,args)),capture_output=True,text=True)
 if r.returncode:print(label,r.stderr);continue
 args=[R,'-s','dx12','-c','gfx90c','--ps-blob',path.with_suffix('.dxil'),'--vs-blob',O/'offline/vertex.dxil','--ps-model','ps_6_2','--vs-model','vs_6_2','--autogen-dir',O/'rga-auto','--isa',F/(label+'-isa.txt'),'-a',F/(label+'-stats.txt')];r=subprocess.run(list(map(str,args)),capture_output=True,text=True);(F/(label+'.log')).write_text(r.stdout+r.stderr)
 stats=F/('gfx90c_'+label+'-stats_pixel.txt');assert stats.exists(),r.stdout+r.stderr;rows.append({'case':label,**next(csv.DictReader(stats.open()))});print(rows[-1],flush=True)
(F/'summary.json').write_text(json.dumps(rows,indent=2)+'\n')
