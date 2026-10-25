"""Compile actual cooked endpoint helpers and check their arithmetic widths."""
import argparse,json,re,subprocess
from pathlib import Path

def verify(project,engine,out):
    out.mkdir(parents=True,exist_ok=True)
    cooked=project/'Cache/pc/assets/vuelivelink/vueterrainoverlap_mainpipeline_forwardpass_standardlighting_dx12.hlsl'
    source=cooked.read_text(encoding='utf-8')
    report={'production_hlsl':str(cooked),'cases':[]}
    compiler=engine/'bin/Windows/profile/Default/Builders/DirectXShaderCompiler/dxc.exe'
    for bits,kind in ((16,'half'),(32,'float')):
        assert f'bool VueOverlapPairEndpoint{bits}(' in source
        probe=f'''\nfloat4 EndpointPrecisionPS(float4 values:TEXCOORD0,float width:TEXCOORD1):SV_Target0 {{
    return float4(VueOverlapPairEndpoint{bits}({kind}2(values.xy),{kind}2(values.zw),{kind}(width)),0,0,1);
}}\n'''
        path=out/f'endpoint_{bits}.hlsl';path.write_text(source+probe,encoding='utf-8')
        assembly=path.with_suffix('.txt')
        result=subprocess.run([str(compiler),'-E','EndpointPrecisionPS','-T','ps_6_2','-enable-16bit-types','-Fc',str(assembly),'-Fo',str(path.with_suffix('.dxil')),str(path)],capture_output=True,text=True)
        assert result.returncode==0,result.stdout+result.stderr
        arithmetic=re.findall(r'\b(fadd|fsub|fmul|fdiv)\s+(?:fast\s+)?(half|float)\b',assembly.read_text(encoding='utf-8'))
        counts={name:sum(width==name for _,width in arithmetic) for name in ('half','float')}
        assert counts[kind]>0 and counts['float' if kind=='half' else 'half']==0,counts
        report['cases'].append({'precision':bits,'compiled':True,'arithmetic':counts})
    (out/'endpoint-precision.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--project',type=Path,required=True);p.add_argument('--engine',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();print(json.dumps(verify(a.project,a.engine,a.output),indent=2))
