"""Inspect native DXIL for bounded material math and world-space culling.

The probes are appended to actual cooked production HLSL, so SDK aliases and
compiler configuration cannot silently turn the audited helpers into FP32.
Conversions at stage boundaries are allowed; arithmetic widths are checked.
"""
import argparse,json,re,subprocess
from pathlib import Path


def verify(project,engine,out):
    out.mkdir(parents=True,exist_ok=True)
    retained=project/'Cache/pc/assets/vuelivelink/vueterrainoverlap_mainpipeline_forwardpass_standardlighting_dx12.hlsl'
    native=project/'Cache/pc/materials/terrain/default_terrain_forward_dx12.hlsl'
    assert native.is_file(),native
    probes=[('material','half',retained,'''float4 PrecisionAuditPS(float4 input:TEXCOORD0):SV_Target0 {
        half4 a=half4(input);
        half3 n=TC_GroundNormal16(a.xyz,a.xy,a.w);
        half4 color=TC_BlendGroundColors16(a,a.wzyx,a.yzwx,a.xyz);
        half3 aces=TC_GroundColorToAces16(color.xyz);
        half gain=VueDirectScale(a.x,a.y)+VueAmbientScale(a.z,a.w);
        half lobe=VueDiffuseLobe(a.x,a.y,a.z);
        half3 result=VueDiffuseLambert16(aces,n,a.xyz,half3(gain,gain,gain));
        return float4(result,lobe);
    }'''),('culling','float',retained,'''float4 PrecisionAuditPS(float4 input:TEXCOORD0):SV_Target0 {
        VsOutput_VueTerrain output=(VsOutput_VueTerrain)0;
        MaterialParameters p=(MaterialParameters)0;
        output.worldPosition=float3(0,0,input.x);
        p.m_vueGroundHeight=input.y;p.m_vueGroundTolerance=input.z;
        VueCullGround(output,p);
        return float4(output.groundCullDistance,0,0,1);
    }'''),('cache-factors','half',native,'''float4 PrecisionAuditPS(float4 input:TEXCOORD0):SV_Target0 {
        half4 a=half4(input);
        half2 factors=TC_GraphCacheBlendFactors16(a.xy,a.zw,a.wx,a.yz,a.xy);
        TerrainSurfaceChannels surface=(TerrainSurfaceChannels)0;
        surface.baseColor=a.xyz;surface.normal=a.yzw;surface.roughness=a.x;surface.metalness=a.y;
        surface.specularFactor=factors.x;surface.ambientOcclusion=factors.y;
        TerrainSurfaceContext context=(TerrainSurfaceContext)0;context.geometricNormal=input.xyz;
        surface=TC_ValidateSurface(surface,context);
        return float4(surface.normal*surface.specularFactor,surface.ambientOcclusion);
    }''')]
    compiler=engine/'bin/Windows/profile/Default/Builders/DirectXShaderCompiler/dxc.exe';report={}
    for name,expected,production,probe in probes:
        source=production.read_text(encoding='utf-8');path=out/(name+'.hlsl');path.write_text(source+'\n'+probe,encoding='utf-8')
        assembly=path.with_suffix('.txt')
        args=[compiler,'-E','PrecisionAuditPS','-T','ps_6_2','-Zpr','-enable-16bit-types','-Fo',path.with_suffix('.dxil'),'-Fc',assembly,path]
        result=subprocess.run([str(x) for x in args],capture_output=True,text=True)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        code=assembly.read_text();arithmetic=re.findall(r'\b(fadd|fsub|fmul|fdiv)\s+(?:fast\s+)?(half|float)\b',code)
        native_requirement='Use native low precision' in code
        if expected=='half':assert native_requirement,(name,'native FP16 requirement missing')
        counts={kind:sum(kind==width for _,width in arithmetic) for kind in ('half','float')}
        assert counts[expected]>0 and counts['float' if expected=='half' else 'half']==0,(name,counts)
        intrinsics=re.findall(r'@dx\.op\.(?:unary|binary|tertiary|dot[234])\.f(16|32)\(',code)
        intrinsic_counts={kind:sum(width==bits for width in intrinsics) for kind,bits in (('half','16'),('float','32'))}
        assert intrinsic_counts['float' if expected=='half' else 'half']==0,(name,intrinsic_counts)
        report[name]={'production_hlsl':str(production),'arithmetic':counts,'math_intrinsics':intrinsic_counts,'expected':expected,'native_16bit_enabled':True,'requires_native_low_precision':native_requirement,'passed':True}
    (out/'material-precision.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return report


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--project',type=Path,required=True);p.add_argument('--engine',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();print(json.dumps(verify(a.project,a.engine,a.output),indent=2))
