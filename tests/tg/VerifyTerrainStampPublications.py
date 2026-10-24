from pathlib import Path
import xml.etree.ElementTree as ET,subprocess,json,os,argparse
parser=argparse.ArgumentParser(description='Compile and run production image/mesh height publication checks without the missing SDK test framework.')
for arg in ('root','project','engine','compiler-env','output'):parser.add_argument('--'+arg,type=Path,required=True)
a=parser.parse_args();P=a.project;R=a.root;O=R/'integrations/o3de';out=a.output;out.mkdir(parents=True,exist_ok=True)
candidates=list((P/'build/windows/External').glob('*/TerrainCompositor.Static.vcxproj'))
assert len(candidates)==1,candidates
project=candidates[0]
tree=ET.parse(project);ns={'m':'http://schemas.microsoft.com/developer/msbuild/2003'}
incs=max((e.text for e in tree.findall('.//m:AdditionalIncludeDirectories',ns)),key=len).split(';')
source=(O/'Code/Tests/TerrainHeightPrecisionTests.cpp').read_text().replace('#include <AzTest/AzTest.h>','''#include <cstdio>
#include <cstdlib>
#define TEST(s,n) void s##_##n()
#define EXPECT_EQ(a,b) do { if ((a)!=(b)) { std::fprintf(stderr,"Mismatch at line %d\\n",__LINE__); std::abort(); } } while(0)
#define ASSERT_TRUE(a) do { if (!(a)) { std::fprintf(stderr,"Failure at line %d\\n",__LINE__); std::abort(); } } while(0)
''')
source+='''
int main()
{
 TerrainCompositor::TerrainHeightPrecision_HalfPackingHasIeeeEndpointsSubnormalsAndEvenRounding();
 TerrainCompositor::TerrainHeightPrecision_ImageAndMeshSamplesPackInSelectedImmutablePublication();
 std::puts("Actual image/mesh publication checks passed in 16-bit and 32-bit modes.");
}
'''
(out/'tests.cpp').write_text(source)
args=['/nologo','/std:c++20','/Zc:preprocessor','/Zc:__cplusplus','/MD','/EHsc','/O2','/DNDEBUG','/DNOMINMAX','/DWIN32','/D_WIN64','/DAZ_PROFILE_BUILD','/DAZ_ENABLE_TRACING','/DAZ_ENABLE_DEBUG_TOOLS']
args+=['/I"'+i+'"' for i in incs if i and not i.startswith('%')]
args += ['/external:W0',tree.find('.//m:ClCompile/m:AdditionalOptions',ns).text.replace('%(AdditionalOptions)','')]
args+=['"'+str(out/'tests.cpp')+'"','"'+str(O/'Code/Source/TerrainOverlap.cpp')+'"','/Fe:"'+str(out/'verify.exe')+'"','/link','"'+str(a.engine/'lib/Windows/profile/Default/AzCore.lib')+'"']
(out/'args.rsp').write_text('\n'.join(args))
(out/'build.cmd').write_text('@echo off\ncall "'+str(a.compiler_env)+'" -arch=x64 -host_arch=x64 >nul\ncl @"'+str(out/'args.rsp')+'"\n')
r=subprocess.run(['cmd','/d','/c',str(out/'build.cmd')],cwd=out,capture_output=True,text=True);(out/'build.log').write_text(r.stdout+r.stderr)
if r.returncode:raise RuntimeError(r.stdout+r.stderr)
env=os.environ.copy();env['PATH']=str(P/'build/windows/bin/profile')+';'+env['PATH']
r=subprocess.run([str(out/'verify.exe')],cwd=out,env=env,capture_output=True,text=True);print(r.stdout,r.stderr);assert r.returncode==0,r.returncode
(out/'result.json').write_text(json.dumps({'passed':True,'source':str(O/'Code/Source/TerrainOverlap.cpp'),'test_source':str(O/'Code/Tests/TerrainHeightPrecisionTests.cpp'),'kinds':['heightmap','mesh'],'precisions':[16,32]},indent=2))
