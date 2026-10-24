"""Compile all count/precision combinations from the cooked production pixel shader.

The SDK patches DX12 specialization constants at PSO creation. This probe fixes
only the two overlap constants in the emitted HLSL to check every combination
offline; it does not replace production assets or measure GPU machine code.
"""
import argparse,json,re,subprocess
from pathlib import Path

def verify(project,engine,out):
    out.mkdir(parents=True,exist_ok=True)
    stem=project/'Cache/pc/assets/vuelivelink/vueterrainoverlap_mainpipeline_forwardpass_standardlighting_dx12'
    source=stem.with_suffix('.hlsl').read_text()
    options={o['name']:o for o in json.loads(stem.with_suffix('.options.json').read_text())['ShaderOptions']}
    names=('o_terrainOverlapSourceCount','o_terrainHeightFp16')
    ids={name:options[name]['specializationId'] for name in names}
    assert all(value>=0 for value in ids.values()),ids
    compiler=engine/'bin/Windows/profile/Default/Builders/DirectXShaderCompiler/dxc.exe'
    report={'production_hlsl':str(stem.with_suffix('.hlsl')),'native_specialization_ids':ids,'cases':[]}
    for count in (2,3,4):
        for bits in (16,32):
            text=source
            for name,kind,value in ((names[0],'int',str(count)),(names[1],'bool',str(bits==16).lower())):
                pattern=kind+r' GetSpecializationConstant_'+name+r'\(\)\s*\{[^}]*\}'
                text,n=re.subn(pattern,f'{kind} GetSpecializationConstant_{name}() {{ return {value}; }}',text)
                assert n==1,(name,n)
            path=out/f'overlap_{count}_{bits}.hlsl';path.write_text(text,encoding='utf-8')
            dxil=path.with_suffix('.dxil');assembly=path.with_suffix('.txt')
            command=[compiler,'-E','PixelShader','-T','ps_6_2','-Zpr','-enable-16bit-types','-Fo',dxil,'-Fc',assembly,path]
            result=subprocess.run([str(a) for a in command],capture_output=True,text=True)
            if result.returncode:raise RuntimeError(result.stdout+result.stderr)
            report['cases'].append({'count':count,'precision':bits,'compiled':True,'dxil_bytes':dxil.stat().st_size})
    (out/'specialization.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return report

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project',type=Path,required=True);parser.add_argument('--engine',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True);a=parser.parse_args()
    print(json.dumps(verify(a.project,a.engine,a.output),indent=2))
