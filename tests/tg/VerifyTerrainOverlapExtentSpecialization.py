"""Compile production count/precision variants and audit dimension specialization."""
import argparse,json,re,subprocess,hashlib
from pathlib import Path

def verify(project,engine,out):
    out.mkdir(parents=True,exist_ok=True)
    stem=project/'Cache/pc/assets/vuelivelink/vueterrainoverlap_mainpipeline_forwardpass_standardlighting_dx12'
    source=stem.with_suffix('.hlsl').read_text(encoding='utf-8')
    options={o['name']:o for o in json.loads(stem.with_suffix('.options.json').read_text(encoding='utf-8'))['ShaderOptions']}
    names=('o_terrainOverlapSourceCount','o_terrainHeightFp16','o_terrainOverlapFixedHeightExtent')
    assert all(options[n]['specializationId']>=0 for n in names)
    assert 'VueOverlapHeightFieldSize' in source
    compiler=engine/'bin/Windows/profile/Default/Builders/DirectXShaderCompiler/dxc.exe'
    report={'production_hlsl':str(stem.with_suffix('.hlsl')),'source_sha256':hashlib.sha256(source.encode()).hexdigest(),'cases':[]}
    for count,bits,fixed in [(n,b,True) for n in (2,3,4) for b in (16,32)]+[(2,16,False)]:
        text=source
        for name,kind,value in ((names[0],'int',str(count)),(names[1],'bool',str(bits==16).lower()),(names[2],'bool',str(fixed).lower())):
            text,n=re.subn(kind+r' GetSpecializationConstant_'+name+r'\(\)\s*\{[^}]*\}',f'{kind} GetSpecializationConstant_{name}() {{ return {value}; }}',text)
            assert n==1,(name,n)
        path=out/f'overlap_{count}_{bits}_{int(fixed)}.hlsl';path.write_text(text,encoding='utf-8')
        assembly=path.with_suffix('.txt');dxil=path.with_suffix('.dxil')
        result=subprocess.run([str(compiler),'-E','PixelShader','-T','ps_6_2','-Zpr','-enable-16bit-types','-Fo',str(dxil),'-Fc',str(assembly),str(path)],capture_output=True,text=True)
        assert result.returncode==0,result.stdout+result.stderr
        dimensions=len(re.findall(r'call[^\n]+@dx\.op\.getDimensions',assembly.read_text(encoding='utf-8')))
        report['cases'].append({'count':count,'precision':bits,'fixed_extent':fixed,'compiled':True,'dimension_queries':dimensions,'dxil_bytes':dxil.stat().st_size})
    a=next(x for x in report['cases'] if x['count']==2 and x['precision']==16 and x['fixed_extent'])
    b=report['cases'][-1]
    report['dimension_queries_removed']=b['dimension_queries']-a['dimension_queries']
    assert report['dimension_queries_removed']>=2,report
    (out/'extent-specialization.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--project',type=Path,required=True);p.add_argument('--engine',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();print(json.dumps(verify(a.project,a.engine,a.output),indent=2))
