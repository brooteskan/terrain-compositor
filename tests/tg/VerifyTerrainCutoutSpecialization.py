"""Inspect production cutout specialization: absent features must emit no loads."""
import argparse,json,re,subprocess
from pathlib import Path


def verify(project,engine,out):
    out.mkdir(parents=True,exist_ok=True)
    stem=project/'Cache/pc/assets/vuelivelink/vuesimpleterrain_forward_dx12'
    source=stem.with_suffix('.hlsl').read_text(encoding='utf-8')
    options={o['name']:o for o in json.loads(stem.with_suffix('.options.json').read_text(encoding='utf-8'))['ShaderOptions']}
    names=('o_terrainHasMeshCutouts','o_terrainHasHeightGaps')
    assert all(options[n]['specializationId']>=0 for n in names)
    probe='''
float4 CutoutAuditPS(float3 world:TEXCOORD0):SV_Target0 {
    return float4(TGMeshCutoutDiscardsTerrain(world),0,0,1);
}
'''
    compiler=engine/'bin/Windows/profile/Default/Builders/DirectXShaderCompiler/dxc.exe'
    report={'source':str(stem.with_suffix('.hlsl')),'cases':[]}
    for cutouts,gaps in ((False,False),(True,False),(False,True),(True,True)):
        text=source
        for name,value in zip(names,(cutouts,gaps)):
            text,n=re.subn(r'bool GetSpecializationConstant_'+name+r'\(\)\s*\{[^}]*\}',
                           f'bool GetSpecializationConstant_{name}() {{ return {str(value).lower()}; }}',text)
            assert n==1,(name,n)
        path=out/f'cutouts_{int(cutouts)}_gaps_{int(gaps)}.hlsl';path.write_text(text+probe,encoding='utf-8')
        assembly=path.with_suffix('.txt')
        result=subprocess.run([str(compiler),'-E','CutoutAuditPS','-T','ps_6_2','-Zpr','-enable-16bit-types',
                               '-Fo',str(path.with_suffix('.dxil')),'-Fc',str(assembly),str(path)],capture_output=True,text=True)
        assert result.returncode==0,result.stdout+result.stderr
        dxil=assembly.read_text(encoding='utf-8')
        loads=len(re.findall(r'call[^\n]+@dx\.op\.(?:bufferLoad|rawBufferLoad|cbufferLoad)',dxil))
        if cutouts or gaps:assert loads>0
        else:assert loads==0,loads
        report['cases'].append(dict(cutouts=cutouts,gaps=gaps,resource_loads=loads,compiled=True))
    (out/'specialization.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project',type=Path,required=True);parser.add_argument('--engine',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True);a=parser.parse_args()
    print(json.dumps(verify(a.project,a.engine,a.output),indent=2))
