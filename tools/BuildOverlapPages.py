"""Experimental edit-time, bounded retained-overlap material cache.

Offline reference baker: requires numpy and Pillow. Uses source PNGs rather than
cooked BC textures; this is deliberately an appearance/performance experiment,
not a production-equivalent material baker. Never changes authored materials.
"""
import argparse, hashlib, json, struct, time
from pathlib import Path
import numpy as np
from PIL import Image

VERSION = 1
GRID = 4

def unit(x):
    return x / np.maximum(np.linalg.norm(x, axis=-1, keepdims=True), 1e-12)

def sample(a, uv, centers=False, grid=None, wrap=False):
    h,w = a.shape[:2]
    p = uv * (np.array(grid or [w,h])-1) if centers else uv*np.array([w,h])-.5
    lo=np.floor(p).astype(int); f=p-lo
    def fetch(dx,dy):
        x=lo[...,0]+dx; y=lo[...,1]+dy
        return a[y%h,x%w] if wrap else a[np.clip(y,0,h-1),np.clip(x,0,w-1)]
    return ((fetch(0,0)*(1-f[...,0,None])+fetch(1,0)*f[...,0,None])*(1-f[...,1,None])+
            (fetch(0,1)*(1-f[...,0,None])+fetch(1,1)*f[...,0,None])*f[...,1,None]).astype(np.float32)

def read(path):
    if path.suffix.lower()=='.dds':
        b=path.read_bytes(); h,w=struct.unpack_from('<II',b,12); fmt=struct.unpack_from('<I',b,128)[0]
        dtype,ch={10:('<f2',4),54:('<f2',1),41:('<f4',1)}[fmt]
        return np.frombuffer(b,dtype=dtype,offset=148).reshape(h,w,ch).astype(np.float32)
    return np.asarray(Image.open(path).convert('RGB'),np.float32)/255

def dds(path,a):
    h,w=a.shape[:2]
    header=struct.pack('<7I',124,0x100F,h,w,w*8,0,0)+bytes(44)
    header+=struct.pack('<II4s5I',32,4,b'DX10',0,0,0,0,0)+struct.pack('<5I',0x1000,0,0,0,0)
    path.write_bytes(b'DDS '+header+struct.pack('<5I',10,3,0,1,0)+a.astype('<f2').tobytes())

def build(project, size=1024):
    start=time.perf_counter(); project=Path(project)
    status=json.loads((project/'user/VueLiveLink/terrain_overlap_status.json').read_text())
    mats=[project/Path(x).with_suffix('.material') for x in status['materials'].values()]
    props=json.loads(mats[0].read_text())['propertyValues']
    assert props['terrainOverlap.count']==2 and props['terrainOverlap.heightFp16'], 'Prototype supports the saved FP16 pair only'
    for mat in mats:
        other=json.loads(mat.read_text())['propertyValues']
        assert {k:v for k,v in other.items() if k.startswith(('overlapSource','terrainOverlap'))}=={k:v for k,v in props.items() if k.startswith(('overlapSource','terrainOverlap'))}
    sources=[{k.split('.',1)[1]:v for k,v in props.items() if k.startswith(f'overlapSource{i}.')} for i in range(2)]
    deps={}; textures=[]; bounds=[]
    for s in sources:
        assert not s['specularMap'] and not s['occlusionMap'], 'Only scalar specular/AO supported'
        tex={}
        for k in ('heightMap','normalField','colorMap','normalMap'):
            if not s[k]: continue
            path=project/s[k]; deps[s[k]]=hashlib.sha256(path.read_bytes()).hexdigest(); tex[k]=read(path)
            if k=='colorMap':
                a=tex[k];tex[k]=np.where(a<=.04045,a/12.92,((a+.055)/1.055)**2.4)
        textures.append(tex)
        rows=np.array([s['uvRow0'],s['uvRow1']]); inv=np.linalg.inv(rows[:,:2])
        corners=(np.array([[0,0],[0,1],[1,0],[1,1]])-rows[:,2])@inv.T
        bounds.append([corners.min(0),corners.max(0)])
    low=np.max(np.array(bounds)[:,0],0); high=np.min(np.array(bounds)[:,1],0)
    assert np.all(high>low) and size in (256,512,1024,2048)
    pitch=size//GRID; core=pitch-2; extent=high-low; step=extent/(GRID*core)
    signature={'version':VERSION,'baker_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'props':props,'dependencies':deps,'size':size,'low':low.tolist(),'high':high.tolist()}
    key=hashlib.sha256(json.dumps(signature,sort_keys=True).encode()).hexdigest()[:24]
    out=project/'Assets/VueLiveLink/overlap_pages'/key;out.mkdir(parents=True,exist_ok=True)
    # Each page has a one-texel gutter. No mip chain: runtime rejects footprints
    # larger than the gutter and falls back when cache resolution is inadequate.
    axis=np.concatenate([np.arange(core+2)-.5+i*core for i in range(GRID)])
    x,y=np.meshgrid(low[0]+axis*step[0],low[1]+axis*step[1]); world=np.stack([x,y,np.ones_like(x)],-1).astype(np.float32)
    queries=[]
    half=lambda x:np.asarray(x,dtype=np.float16).astype(np.float32)
    def ramp(x):
        t=half(np.clip(half(x)/half(max(props['terrainOverlap.width'],1e-6)),0,1))
        return half(half(t*t)*half(half(3)-half(half(2)*t)))
    cutoff=half(props.get('groundContact.height',0)+max(props.get('groundContact.tolerance',.001),0))
    for s,tex in zip(sources,textures):
        uv=world@np.array([s['uvRow0'],s['uvRow1']],np.float32).T
        height=half(half(sample(tex['heightMap'],uv,True)[...,0])*half(s['heightScaleOffset'][0])+half(s['heightScaleOffset'][1]))
        distance=np.minimum(uv,1-uv)/np.maximum(np.linalg.norm(np.array([s['uvRow0'][:2],s['uvRow1'][:2]]),axis=1),1e-8)
        cover=ramp(half(distance.min(-1)))
        cover=np.where((height>cutoff)&np.all((uv>=0)&(uv<=1),-1),cover,0)
        queries.append((uv,height,cover))
    scores=[half(queries[i][2]*half(1-half(queries[1-i][2]*ramp(half(queries[1-i][1]-queries[i][1]))))) for i in range(2)]
    total=half(scores[0]+scores[1]); weights=[half(s/np.maximum(total,1e-20)) for s in scores]
    valid=(weights[0]>0)&(weights[1]>0)&np.isfinite(total)
    # Dilate invalid regions by two texels to keep bilinear filtering from
    # inventing blend coverage or losing arbitrarily small valid contributors.
    safe=valid.copy()
    for dy in range(-2,3):
        for dx in range(-2,3):safe &= np.roll(np.roll(valid,dy,0),dx,1)
    safe[:2]=False;safe[-2:]=False;safe[:,:2]=False;safe[:,-2:]=False
    color=np.zeros((*x.shape,3),np.float32); normal=color.copy(); vertex=color.copy()
    factors=np.zeros((*x.shape,4),np.float32)
    aces=np.array([[.6130974,.3395231,.0473795],[.0701937,.9163539,.0134524],[.0206156,.1095698,.8698146]],np.float32)
    for s,tex,q,w in zip(sources,textures,queries,weights):
        uv=q[0]; n=half(sample(tex['normalField'],uv,True,s['gridSize'])[...,:3])
        n=half(unit(n@np.array([s['normalRow0'],s['normalRow1'],s['normalRow2']],np.float32).T))
        t=np.broadcast_to(np.array(s['tangent'],np.float32),n.shape);t=half(unit(t-n*np.sum(t*n,-1,keepdims=True))); b=-half(unit(np.cross(n,t)))
        shading=n
        if 'normalMap' in tex:
            # Image Builder swaps R/G and SDK GetTangentSpaceNormal swaps back.
            xy=half(sample(tex['normalMap'],uv,wrap=True)[...,:2]*2-1)
            z=np.maximum(np.sqrt(np.maximum(1-np.sum(xy*xy,-1,keepdims=True),0)),.01)
            xy=half(xy*half(s['normalStrength']))
            shading=half(unit(t*xy[...,0,None]+b*xy[...,1,None]+n*z))
        c=np.array(s['color'][:3],np.float32)
        sampled=(sample(tex['colorMap'],uv,wrap=True)@aces.T) if 'colorMap' in tex else np.broadcast_to(c,n.shape)
        c=c*((1-s['colorFactor'])+sampled*s['colorFactor']) if 'colorMap' in tex else c*s['colorFactor']
        color=half(color+half(w[...,None]*half(c)));normal=half(normal+half(w[...,None]*shading));vertex=half(vertex+half(w[...,None]*n))
        factors=half(factors+half(w[...,None]*half([s['specular'],s['roughness'],s['metallic'],s['occlusion']])))
    channels=[np.concatenate([color,factors[...,:1]],-1),np.concatenate([unit(normal),factors[...,1:2]],-1),np.concatenate([unit(vertex),factors[...,2:3]],-1),np.stack([factors[...,3],safe,np.zeros_like(x),np.zeros_like(x)],-1)]
    for i,a in enumerate(channels):
        dds(out/f'page{i}.dds',a)
        # Reuse the project's known no-mip, linear half-float image preset.
        import sys
        sys.path.insert(0,str(project/'Gem/Source/Editor/VueLiveLink'))
        from terrain_normals import TEXTURE_SETTINGS
        (out/f'page{i}.dds.assetinfo').write_text(TEXTURE_SETTINGS)
    pageprops={'overlapPages.enabled':False,'overlapPages.low':low.tolist(),'overlapPages.extent':extent.tolist(),'overlapPages.size':float(size)}
    for i in range(4):pageprops[f'overlapPages.channel{i}']=(out/f'page{i}.dds').relative_to(project).as_posix()
    material_paths={}
    for mat in mats:
        data=json.loads(mat.read_text()); data['propertyValues'].update(pageprops)
        target=out/(mat.parent.name+'.material');target.write_text(json.dumps(data,indent=2)+'\n');material_paths[str(mat.relative_to(project))]=str(target.relative_to(project))
    report={'prototype':True,'baker':'offline CPU source-image approximation','signature':signature,'key':key,'materials':material_paths,'properties':pageprops,'pages':16,'atlas_size':size,'payload_bytes':size*size*32,'source_files_bytes':sum((project/p).stat().st_size for p in deps),'valid_texel_percent':float(safe.mean()*100),'world_texel_size':step.tolist(),'bake_wall_seconds':time.perf_counter()-start}
    (out/'publication.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='signature'},indent=2));return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--project',type=Path,required=True);p.add_argument('--size',type=int,default=1024);a=p.parse_args();build(a.project,a.size)
