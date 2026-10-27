"""Read-only AMD ADL clock sampling. No settings or clock-control APIs used."""
import ctypes as c,json,time,sys
from pathlib import Path
class PM(c.Structure):
 _fields_=[(n,c.c_int) for n in ('size','engine_clock','memory_clock','voltage','activity','performance_level','bus_speed','bus_lanes','max_bus_lanes','reserved')]
class ODN(c.Structure):
 _fields_=[(n,c.c_int) for n in ('core_clock','memory_clock','dcef_clock','gfx_clock','uvd_clock','vce_clock','activity','core_level','memory_level','dcef_level','gfx_level','uvd_level','vce_level','bus_speed','bus_lanes','max_bus_lanes','voltage','voltage_ci')]
lib=c.WinDLL('C:/Windows/System32/atiadlxx.dll');crt=c.CDLL('msvcrt');crt.malloc.argtypes=[c.c_size_t];crt.malloc.restype=c.c_void_p
malloc=c.WINFUNCTYPE(c.c_void_p,c.c_int)(lambda n:crt.malloc(n));context=c.c_void_p()
lib.ADL2_Main_Control_Create.argtypes=[type(malloc),c.c_int,c.POINTER(c.c_void_p)];lib.ADL2_Main_Control_Destroy.argtypes=[c.c_void_p]
status=lib.ADL2_Main_Control_Create(malloc,1,c.byref(context));assert status==0,status
def current():
 count=c.c_int();r=lib.ADL2_Adapter_NumberOfAdapters_Get(context,c.byref(count));assert r==0,r
 rows=[]
 for i in range(min(count.value,1)):
  supported=c.c_int();enabled=c.c_int();version=c.c_int();cap=lib.ADL2_Overdrive_Caps(context,i,c.byref(supported),c.byref(enabled),c.byref(version))
  p=PM();p.size=c.sizeof(p);pstatus=lib.ADL2_Overdrive5_CurrentActivity_Get(context,i,c.byref(p))
  n=ODN();nstatus=lib.ADL2_OverdriveN_PerformanceStatus_Get(context,i,c.byref(n))
  rows.append({'adapter':i,'cap_status':cap,'supported':supported.value,'enabled':enabled.value,'version':version.value,'od5_status':pstatus,'od5':{name:getattr(p,name) for name,_ in p._fields_},'odn_status':nstatus,'odn':{name:getattr(n,name) for name,_ in n._fields_}})
 return rows
try:
 if len(sys.argv)==1:print(json.dumps(current(),indent=2))
 else:
  out=Path(sys.argv[1]);profile=Path(sys.argv[2]);end=time.monotonic()+float(sys.argv[3]);out.parent.mkdir(exist_ok=True)
  with out.open('w') as f:
   while time.monotonic()<end:
    try:case=json.loads(profile.read_text()).get('active_case','restored')
    except Exception:case='starting'
    row={'time':time.time(),'case':case,'adapters':current()};f.write(json.dumps(row)+'\n');f.flush();time.sleep(1)
finally:lib.ADL2_Main_Control_Destroy(context)
