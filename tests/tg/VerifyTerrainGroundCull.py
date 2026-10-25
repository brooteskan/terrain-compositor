"""Compare production ground culling against pixel discard on a hardware GPU.

Run from a Visual Studio x64 developer command prompt. The small D3D11 raster
fixture checks 112 coverage cases at 1x/2x MSAA, including perspective, coplanar
triangles, and a large world height. Full O3DE shader cooking/editor validation
remains separate. No software rasterizer fallback is used.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess


def verify(root, output):
    output.mkdir(parents=True, exist_ok=True)
    production = root / 'integrations/tg/project/Assets/VueLiveLink/VueTerrainMesh.azsli'
    text = production.read_text(encoding='utf-8')
    helper = re.search(r'void VueCullGround\([^}]+\}', text).group()
    source = '''cbuffer Inputs:register(b0) {float groundHeight;float groundTolerance;float2 padding;};
struct MaterialParameters {float m_vueGroundHeight;float m_vueGroundTolerance;};
struct VsInput {float4 position:POSITION;float3 worldPosition:TEXCOORD0;};
struct VsOutput {
    precise sample float4 position:SV_Position;
    sample float3 worldPosition:TEXCOORD0;
    float groundCullDistance:SV_CullDistance0;
};
''' + helper + '''
VsOutput ReferenceVS(VsInput input) {
    VsOutput o;o.position=input.position;o.worldPosition=input.worldPosition;
    o.groundCullDistance=1;return o;
}
VsOutput OptimizedVS(VsInput input) {
    VsOutput o=ReferenceVS(input);MaterialParameters p;
    p.m_vueGroundHeight=groundHeight;p.m_vueGroundTolerance=groundTolerance;
    VueCullGround(o,p);return o;
}
float4 ReferencePS(VsOutput input):SV_Target {
    if(input.worldPosition.z<=groundHeight+max(groundTolerance,0.0))discard;
    return 1;
}
float4 OptimizedPS(VsOutput input):SV_Target {return ReferencePS(input);}
'''
    shader = output / 'ground_cull.hlsl'
    shader.write_text(source, encoding='utf-8')
    compiler = shutil.which('cl')
    if not compiler:
        raise RuntimeError('Run from a Visual Studio x64 developer command prompt (cl is required).')
    executable = output / 'TerrainGroundCullGpu.exe'
    command = [compiler, '/nologo', '/EHsc', '/O2', '/std:c++17',
               str(root / 'tests/tg/TerrainGroundCullGpu.cpp'), '/Fe:' + str(executable),
               '/Fo:' + str(output / 'TerrainGroundCullGpu.obj'),
               '/link', 'd3d11.lib', 'd3dcompiler.lib']
    build = subprocess.run(command, capture_output=True, text=True)
    (output / 'build.log').write_text(build.stdout + build.stderr, encoding='utf-8')
    if build.returncode:
        raise RuntimeError('GPU fixture build failed; see ' + str(output / 'build.log'))
    run = subprocess.run([str(executable), str(shader)], capture_output=True, text=True)
    (output / 'gpu.log').write_text(run.stdout + run.stderr, encoding='utf-8')
    report = json.loads(run.stdout.strip().splitlines()[-1])
    report.update(passed=run.returncode == 0, production=str(production),
                  production_sha256=hashlib.sha256(production.read_bytes()).hexdigest(),
                  scope='isolated production cull helper; native hardware rasterization')
    (output / 'coverage.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    assert report['passed'], report
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(verify(args.root.resolve(), args.output.resolve()), indent=2))
