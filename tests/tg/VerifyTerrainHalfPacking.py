"""Exercise the production C++ binary16 packer against Python's IEEE oracle."""
import argparse,ctypes,json,math,random,struct,subprocess
from pathlib import Path

def verify(root,compiler_env,out):
    out.mkdir(parents=True,exist_ok=True)
    header=root/'integrations/o3de/Code/Include/TerrainCompositor/TerrainHeightPrecision.h'
    cpp=out/'packing.cpp';dll=out/'packing.dll'
    cpp.write_text('#include "'+header.as_posix()+'"\nextern "C" __declspec(dllexport) unsigned short pack(float value) { return TerrainCompositor::PackTerrainHeight16(value); }\n')
    batch=out/'build.cmd'
    batch.write_text('@echo off\ncall "'+str(compiler_env)+'" -arch=x64 -host_arch=x64 >nul\ncl /nologo /LD /O2 /EHsc /Fe:"'+str(dll)+'" /Fo:"'+str(out/'packing.obj')+'" "'+str(cpp)+'"\n')
    result=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=out,capture_output=True,text=True)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    lib=ctypes.CDLL(str(dll));pack=lib.pack;pack.argtypes=[ctypes.c_float];pack.restype=ctypes.c_uint16
    count=0
    for bits in range(65536):
        value=struct.unpack('<e',struct.pack('<H',bits))[0]
        result=pack(value)
        if math.isnan(value):assert result&0x7fff>0x7c00
        else:assert result==bits,(bits,result)
        count+=1
    rng=random.Random(20261024)
    for _ in range(100000):
        bits=rng.getrandbits(32);value=struct.unpack('<f',struct.pack('<I',bits))[0]
        if math.isnan(value):continue
        try:expected=struct.unpack('<H',struct.pack('<e',value))[0]
        except OverflowError:expected=0xfc00 if value<0 else 0x7c00
        assert pack(value)==expected,(hex(bits),value,pack(value),expected)
        count+=1
    report={'passed':True,'cases':count,'half_patterns':65536,'float_patterns':100000,'production_header':str(header)}
    (out/'packing.json').write_text(json.dumps(report,indent=2)+'\n')
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--compiler-env',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();print(json.dumps(verify(a.root,a.compiler_env,a.output),indent=2))
