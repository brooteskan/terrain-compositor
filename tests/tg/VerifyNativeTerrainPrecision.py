"""Compile production overlap resolvers and inspect DXIL arithmetic widths."""
import argparse,json,re,subprocess
from pathlib import Path

def run(args):
    result=subprocess.run([str(a) for a in args],capture_output=True,text=True)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)

def verify(root,engine,out):
    out.mkdir(parents=True,exist_ok=True)
    builders=engine/'bin/Windows/profile/Default/Builders'
    report={}
    for bits,kind in [(16,'half'),(32,'float')]:
        suffix='16' if bits==16 else ''
        production=root/f'integrations/o3de/Assets/ShaderLib/TerrainCompositor/TerrainOverlapWeights{suffix}.azsli'
        source=production.read_text()+f'''
float4 MainPS(float4 input : TEXCOORD0) : SV_Target0
{{
    {kind} rawHeight = {kind}(input.x);
    {kind} height = rawHeight * {kind}(input.y) + {kind}(input.z);
    {kind}4 weights = TC_OverlapWeights{suffix}({kind}4(height,input.y,input.z,input.w),
        {kind}4(input.w,input.z,input.y,input.x),4,{kind}(input.y),true);
    return float4(weights);
}}
'''
        azsl=out/f'production_{bits}.azsl';azsl.write_text(source)
        hlsl=azsl.with_suffix('.hlsl');dxil=azsl.with_suffix('.dxil');assembly=azsl.with_suffix('.txt')
        run([builders/'AZSLc/azslc.exe',azsl,'-o',hlsl])
        run([builders/'DirectXShaderCompiler/dxc.exe','-T','ps_6_2','-E','MainPS',
             '-enable-16bit-types','-Fo',dxil,'-Fc',assembly,hlsl])
        text=assembly.read_text()
        arithmetic=re.findall(r'\b(fadd|fsub|fmul|fdiv)\s+(?:fast\s+)?(half|float)\b',text)
        counts={name:sum(width==name for _,width in arithmetic) for name in ('half','float')}
        if not counts[kind] or counts['float' if kind=='half' else 'half']:
            raise AssertionError((bits,counts))
        report[str(bits)]={'arithmetic':counts,'native_fp16':bits==16,'production':str(production),'assembly':str(assembly)}
    (out/'native-precision.json').write_text(json.dumps(report,indent=2)+'\n')
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[2]);p.add_argument('--engine',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();print(json.dumps(verify(a.root,a.engine,a.output),indent=2))
