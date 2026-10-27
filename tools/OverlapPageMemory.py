"""Read this process's DXGI video-memory usage, not global GPU usage."""
import ctypes as c
import uuid

def memory():
    try:
        guid=lambda s:(c.c_ubyte*16).from_buffer_copy(uuid.UUID(s).bytes_le)
        def method(obj,index,result,*args):
            vt=c.cast(obj,c.POINTER(c.POINTER(c.c_void_p))).contents
            return c.WINFUNCTYPE(result,c.c_void_p,*args)(vt[index])
        factory=c.c_void_p();adapter=c.c_void_p();a3=c.c_void_p()
        hr=c.WinDLL('dxgi').CreateDXGIFactory1(c.byref(guid('770aae78-f26f-4dba-a829-253c83d1b387')),c.byref(factory))
        if hr<0:raise RuntimeError(hr)
        try:
            hr=method(factory,12,c.c_long,c.c_uint,c.POINTER(c.c_void_p))(factory,0,c.byref(adapter))
            if hr<0:raise RuntimeError(hr)
            hr=method(adapter,0,c.c_long,c.c_void_p,c.POINTER(c.c_void_p))(adapter,c.byref(guid('645967a4-1392-4310-a798-8053ce3e93fd')),c.byref(a3))
            if hr<0:raise RuntimeError(hr)
            class Info(c.Structure):
                _fields_=[('Budget',c.c_uint64),('CurrentUsage',c.c_uint64),('AvailableForReservation',c.c_uint64),('CurrentReservation',c.c_uint64)]
            out={}
            for segment in (0,1):
                info=Info();hr=method(a3,14,c.c_long,c.c_uint,c.c_uint,c.POINTER(Info))(a3,0,segment,c.byref(info))
                if hr<0:raise RuntimeError(hr)
                out['local' if segment==0 else 'nonlocal']={k:int(getattr(info,k)) for k,_ in info._fields_}
            return out
        finally:
            for obj in (a3,adapter,factory):
                if obj:method(obj,2,c.c_ulong)(obj)
    except Exception as ex:return {'unavailable':str(ex)}
