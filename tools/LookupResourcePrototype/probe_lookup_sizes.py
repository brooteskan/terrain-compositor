from pathlib import Path
import re,json,subprocess,csv
O=Path('D:/brooteskan/work/lookup-cause');F=O/'offline';P=Path('D:/diversion/TG/TGProject');D=P/'build/windows/bin/profile/Builders/DirectXShaderCompiler/dxc.exe';R=Path('D:/brooteskan/work/rga-2.15/rga.exe')
source=(F/'mode4.hlsl').read_text();rows=[]
for words in (8,32,128):
 s=source
 for name in ('VueContributorEndpoint','VueContributorBlend'):
  pattern=r'static const uint '+name+r'\s*\[\s*512\s*\]\s*=\s*\{([^}]+)\}'
  match=re.search(pattern,s);assert match,name
  values=match[1].split(',')[:words]
  s=s[:match.start()]+f'static const uint {name}[{words}] = {{'+','.join(values)+'}'+s[match.end():]
 s=re.sub(r'index\s*>>\s*5u',f'(index >> 5u) & {words-1}u',s)
 assert f'& {words-1}u' in s
 path=F/f'words{words}.hlsl';path.write_text(s)
 cmd=[D,'-E','PixelShader','-T','ps_6_2','-Zpr','-enable-16bit-types','-Fo',path.with_suffix('.dxil'),'-Fc',path.with_suffix('.txt'),path]
 r=subprocess.run(list(map(str,cmd)),capture_output=True,text=True);assert not r.returncode,r.stderr
 cmd=[R,'-s','dx12','-c','gfx90c','--ps-blob',path.with_suffix('.dxil'),'--vs-blob',F/'vertex.dxil','--ps-model','ps_6_2','--vs-model','vs_6_2','--autogen-dir',O/'rga-auto','--isa',F/f'words{words}-isa.txt','-a',F/f'words{words}-stats.txt']
 r=subprocess.run(list(map(str,cmd)),capture_output=True,text=True);(F/f'words{words}-rga.log').write_text(r.stdout+r.stderr)
 out=F/f'gfx90c_words{words}-stats_pixel.txt';assert out.exists(),r.stdout+r.stderr
 row=next(csv.DictReader(out.open()));rows.append({'words_per_bitset':words,'table_bytes':words*8,**row})
print(json.dumps(rows,indent=2));(F/'size-probe.json').write_text(json.dumps(rows,indent=2)+'\n')
