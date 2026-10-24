"""Compile production height kernels, retaining addressing in FP32."""
import argparse,json,re,subprocess
from pathlib import Path

def verify(root,engine,out):
    out.mkdir(parents=True,exist_ok=True)
    builders=engine/'bin/Windows/profile/Default/Builders'
    def compile_case(name,source,kind):
        azsl=out/(name+'.azsl');azsl.write_text(source,encoding='utf-8')
        hlsl=azsl.with_suffix('.hlsl');dxil=azsl.with_suffix('.dxil');asm=azsl.with_suffix('.txt')
        for args in ([builders/'AZSLc/azslc.exe',azsl,'-o',hlsl],
                     [builders/'DirectXShaderCompiler/dxc.exe','-T','ps_6_2','-E','MainPS','-enable-16bit-types','-Fo',dxil,'-Fc',asm,hlsl]):
            p=subprocess.run([str(x) for x in args],capture_output=True,text=True)
            if p.returncode:raise RuntimeError(name+'\n'+p.stdout+p.stderr)
        ops=re.findall(r'\b(fadd|fsub|fmul|fdiv)\s+(?:fast\s+)?(half|float)\b',asm.read_text())
        counts={w:sum(t==w for _,t in ops) for w in ('half','float')}
        assert counts[kind],(name,counts)
        if kind=='float':assert counts['half']==0,(name,counts)
        return {'arithmetic':counts,'assembly':str(asm)}
    shader=root/'integrations/o3de/Assets/ShaderLib/TerrainCompositor/TerrainOverlap.azsli'
    text=shader.read_text();text=text[text.index('struct TerrainOverlapSource'):text.index('// Material channels are blended')]
    # Resource semantics only; the actual production functions are unchanged.
    source='ShaderResourceGroupSemantic TestSem { FrequencyId = 0; };\nShaderResourceGroup Data : TestSem { StructuredBuffer<uint> words; };\n'
    for name in ('TerrainOverlapWeights.azsli','TerrainOverlapWeights16.azsli'):
        source+=(shader.parent/name).read_text()+'\n'
    source+=text.replace('TC_OVERLAP_WORDS', 'Data::words')
    report={}
    for bits in (32,16):
        suffix='16' if bits==16 else ''
        main=f'''\nfloat4 MainPS(float4 input : TEXCOORD0) : SV_Target0
{{
    TerrainOverlapSource sampled = TC_OverlapSource{suffix}((uint)input.w, input.xy, input.z);
    return float4(sampled.normal * sampled.coverage, sampled.worldPosition.z);
}}
'''
        report['stamp_'+str(bits)]=compile_case('stamp_'+str(bits),source+main,'half' if bits==16 else 'float')
    # Extract exact vertex height function, substituting only SRG interfaces for
    # a self-contained harness. GPU production assets are also cooked separately.
    common=(root/'integrations/o3de/Assets/Shaders/Terrain/TerrainCommon.azsli').read_text()
    vertex=common[common.index('struct VertexInput'):common.index('float2 GetWorldXYPosition')]
    body=common[common.index('float2 GetWorldXYPosition'):]
    body=body.replace('SceneSrg::TerrainWorldData','WorldData').replace('ObjectSrg::PatchData','PatchData')
    body=body.replace('ObjectSrg::m_patchData','patchData').replace('TerrainSrg::CalculateClodDistance','CalculateClodDistance')
    body=body.replace('TerrainSrg::m_meshData.m_rcpClodDistance','Data::rcpClodDistance').replace('ViewSrg::m_viewProjectionMatrix','Data::viewProjection')
    prefix='''struct WorldData { float m_zMin; float m_zExtents; };
struct PatchData { float2 m_xyTranslation; float m_xyScale; uint m_lodLevel; float m_rcpLodLevel; uint m_heightFp16; };
ShaderResourceGroupSemantic TestSem { FrequencyId = 0; };
ShaderResourceGroup Data : TestSem { float rcpClodDistance; float4x4 viewProjection; };
float CalculateClodDistance(uint lod, float3 p) { return 128.0 * float(1u << lod) - length(p); }
float3 DecodeTerrainNormal(float2 n) { return normalize(float3(n, sqrt(saturate(1-dot(n,n))))); }
static const bool o_useTerrainClod = true;
'''
    for bits in (32,16):
        main=f'''\nfloat4 MainPS(float4 input : TEXCOORD0) : SV_Target0
{{
    VertexInput v=(VertexInput)0; v.m_zPosition=input.x; v.m_lodZPosition=input.y; v.m_xyPosition=input.zw;
    PatchData p=(PatchData)0; p.m_xyScale=64; p.m_rcpLodLevel=1; p.m_heightFp16={1 if bits==16 else 0};
    WorldData w; w.m_zMin=-100; w.m_zExtents=1024;
    float3 position, normal; float4 clip;
    CalculateTerrainPosition(w,p,v,position,clip,normal);
    return float4(position,normal.z);
}}
'''
        report['geometry_'+str(bits)]=compile_case('geometry_'+str(bits),prefix+vertex+body+main,'half' if bits==16 else 'float')
    (out/'all-height-precision.json').write_text(json.dumps(report,indent=2)+'\n')
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--engine',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();print(json.dumps(verify(a.root,a.engine,a.output),indent=2))
