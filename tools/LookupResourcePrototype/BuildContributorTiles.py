"""Conservative whole-tile contributor bounds for a frozen FP16 terrain pair.

No material samples or representative-point classification. Bounds cover all
bilinear height patches intersecting the transformed tile's UV rectangle.
The generated two bitsets classify guaranteed endpoint and guaranteed blend
tiles; all uncertain tiles retain the exact production selector.
"""
import argparse, hashlib, json, math, struct, time
from pathlib import Path
import numpy as np

def read_height(path):
    data=Path(path).read_bytes();h,w=struct.unpack_from('<II',data,12)
    fmt=struct.unpack_from('<I',data,128)[0]
    assert fmt in (54,41) and len(data)==148+w*h*(2 if fmt==54 else 4)
    a=np.frombuffer(data,offset=148,dtype='<f2' if fmt==54 else '<f4').reshape(h,w).astype(np.float64)
    assert np.isfinite(a).all()
    return a

def bilinear(a,uv):
    h,w=a.shape;pos=np.clip(uv,0,1)*[w-1,h-1];lo=np.floor(pos).astype(int);f=pos-lo
    hi=np.minimum(lo+1,[w-1,h-1])
    return ((a[lo[...,1],lo[...,0]]*(1-f[...,0])+a[lo[...,1],hi[...,0]]*f[...,0])*(1-f[...,1])+
            (a[hi[...,1],lo[...,0]]*(1-f[...,0])+a[hi[...,1],hi[...,0]]*f[...,0])*f[...,1])

def tile_source_bounds(source,a,low,high,halo):
    rows=np.array([source['uvRow0'],source['uvRow1']],np.float64)
    corners=np.array([[low[0]-halo,low[1]-halo,1],[low[0]-halo,high[1]+halo,1],
                      [high[0]+halo,low[1]-halo,1],[high[0]+halo,high[1]+halo,1]])
    uvs=corners@rows.T;umin=uvs.min(0)-1e-5;umax=uvs.max(0)+1e-5
    if np.any(umax<0) or np.any(umin>1):return {'missing':True}
    h,w=a.shape;l=np.clip(umin,0,1)*[w-1,h-1];r=np.clip(umax,0,1)*[w-1,h-1]
    # Piecewise bilinear extrema on an axis-aligned rectangle occur at its
    # corners and grid-line intersections. This bounds the UV parallelogram too.
    xs=np.unique(np.r_[l[0],np.arange(math.ceil(l[0]),math.floor(r[0])+1),r[0]])
    ys=np.unique(np.r_[l[1],np.arange(math.ceil(l[1]),math.floor(r[1])+1),r[1]])
    xx,yy=np.meshgrid(xs,ys);samples=bilinear(a,np.stack([xx/(w-1),yy/(h-1)],-1))
    support=a[max(0,math.floor(l[1])):min(h,math.ceil(r[1])+2),max(0,math.floor(l[0])):min(w,math.ceil(r[0])+2)]
    # Cover sampler interpolation phase quantization plus half conversion,
    # multiplication/addition/FMA rounding. Reject overflow rather than classify.
    raw_error=(float(support.max())-float(support.min()))/64+max(1,abs(float(samples.min())),abs(float(samples.max())))/512
    scale=float(np.float16(source['heightScaleOffset'][0]));offset=float(np.float16(source['heightScaleOffset'][1]))
    vals=np.array([samples.min()-raw_error,samples.max()+raw_error])*scale+offset
    error=max(1,abs(float(vals.min())),abs(float(vals.max())))/128+.02
    distance=np.minimum(umin,1-umax)/np.maximum(np.linalg.norm(rows[:,:2],axis=1),1e-8)
    return {'missing':False,'lo':float(vals.min()-error),'hi':float(vals.max()+error),'edge_distance':float(distance.min())}

def classify(bounds,width,cutoff):
    # 0 uncertain, 1 guaranteed no blend, 2 guaranteed blend.
    if any(b['missing'] for b in bounds):return 1
    if any(b['hi']<cutoff-.01 for b in bounds):return 1
    full=[b['edge_distance']>width*1.1+.01 and b['lo']>cutoff+.01 for b in bounds]
    if any(full[i] and bounds[i]['lo']-bounds[1-i]['hi']>width*1.1+.1 for i in (0,1)):return 1
    if all(full) and max(bounds[0]['hi']-bounds[1]['lo'],bounds[1]['hi']-bounds[0]['lo'])<width*.25:return 2
    return 0

def pair_blends(props,arrays,world):
    half=lambda x:np.asarray(x,np.float16).astype(np.float32)
    width=half(props['terrainOverlap.width']);heights=[];coverage=[]
    cutoff=half(np.float32(props.get('groundContact.height',0))+np.float32(max(props.get('groundContact.tolerance',.001),0)))
    def ramp(v):
        t=half(np.clip(half(v)/max(float(width),2**-14),0,1));return half(half(t*t)*half(half(3)-half(half(2)*t)))
    for i,a in enumerate(arrays):
        s={k.split('.',1)[1]:v for k,v in props.items() if k.startswith(f'overlapSource{i}.')}
        rows=np.array([s['uvRow0'],s['uvRow1']],np.float32)
        uv=np.c_[world.astype(np.float32),np.ones(len(world),np.float32)]@rows.T
        hh=half(half(half(bilinear(a,uv))*half(s['heightScaleOffset'][0]))+half(s['heightScaleOffset'][1]))
        d=np.minimum(uv,1-uv)/np.maximum(np.linalg.norm(rows[:,:2],axis=1),1e-8)
        cc=np.where(np.all((uv>=0)&(uv<=1),-1)&(hh>cutoff),ramp(half(d.min(-1))),0)
        heights.append(hh);coverage.append(cc)
    score=[half(coverage[i]*half(1-half(coverage[1-i]*ramp(half(heights[1-i]-heights[i]))))) for i in (0,1)]
    total=half(score[0]+score[1]);return (score[0]>0)&(score[1]>0)&(total>2**-14)

def build(project,out,grid=128):
    start=time.perf_counter();project=Path(project);out=Path(out);out.mkdir(parents=True,exist_ok=True)
    status=json.loads((project/'user/VueLiveLink/terrain_overlap_status.json').read_text())
    mats=[project/Path(x).with_suffix('.material') for x in status['materials'].values()]
    props=json.loads(mats[0].read_text())['propertyValues']
    assert props['terrainOverlap.count']==2 and props['terrainOverlap.heightFp16'] and props['terrainOverlap.width']>=1
    assert grid in (32,64,128)
    sources=[{k.split('.',1)[1]:v for k,v in props.items() if k.startswith(f'overlapSource{i}.')} for i in (0,1)]
    arrays=[read_height(project/s['heightMap']) for s in sources];deps={s['heightMap']:hashlib.sha256((project/s['heightMap']).read_bytes()).hexdigest() for s in sources}
    rects=[]
    for s in sources:
        rows=np.array([s['uvRow0'],s['uvRow1']]);rect=(np.array([[0,0],[0,1],[1,0],[1,1]])-rows[:,2])@np.linalg.inv(rows[:,:2]).T
        rects.append([rect.min(0),rect.max(0)])
    halo=.02+4e-6*max(abs(np.array(rects).ravel()))
    low=np.max(np.array(rects)[:,0],0)-halo;high=np.min(np.array(rects)[:,1],0)+halo;step=(high-low)/grid
    width=props['terrainOverlap.width'];cutoff=float(np.float16(np.float32(props.get('groundContact.height',0))+np.float32(max(props.get('groundContact.tolerance',.001),0))))
    cells=np.zeros((grid,grid),np.uint8)
    for y in range(grid):
        for x in range(grid):
            lo=low+[x,y]*step;hi=lo+step
            cells[y,x]=classify([tile_source_bounds(s,a,lo,hi,halo) for s,a in zip(sources,arrays)],width,cutoff)
    signature={'version':1,'props':props,'dependencies':deps,'grid':grid,'baker_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    key=int(hashlib.sha256(json.dumps(signature,sort_keys=True).encode()).hexdigest()[:7],16) or 1
    words=[]
    for label in (1,2):
        bits=(cells.ravel()==label);words.append([sum(int(bits[j+k])<<k for k in range(32)) for j in range(0,len(bits),32)])
    f=lambda v:format(float(np.float32(v)),'.9g')+('' if '.' in format(float(np.float32(v)),'.9g') or 'e' in format(float(np.float32(v)),'.9g') else '.0')
    code=f'''// Generated conservative contributor tiles. Frozen publication; no texture resource.
#pragma once
static const uint VueContributorKey = {key}u;
static const float2 VueContributorLow = float2({f(low[0])}, {f(low[1])});
static const float2 VueContributorHigh = float2({f(high[0])}, {f(high[1])});
static const float2 VueContributorScale = float2({f(grid/(high[0]-low[0]))}, {f(grid/(high[1]-low[1]))});
static const uint VueContributorEndpoint[{len(words[0])}] = {{ {','.join(str(v)+'u' for v in words[0])} }};
static const uint VueContributorBlend[{len(words[1])}] = {{ {','.join(str(v)+'u' for v in words[1])} }};
uint VueContributorTile(float2 world)
{{
    if (!all(isfinite(world))) return 0;
    if (any(world < VueContributorLow) || any(world > VueContributorHigh)) return 1;
    uint2 cell = min(uint2(max((world - VueContributorLow) * VueContributorScale, 0.0)), {grid-1}u);
    uint index = cell.y * {grid}u + cell.x;
    uint bit = 1u << (index & 31u);
    if ((VueContributorEndpoint[index >> 5u] & bit) != 0u) return 1;
    if ((VueContributorBlend[index >> 5u] & bit) != 0u) return 2;
    return 0;
}}
'''
    (out/'VueTerrainContributorTiles.azsli').write_text(code)
    np.savez(out/'tiles.npz',cells=cells,low=low,high=high)
    rng=np.random.default_rng(71205);world=rng.uniform(low,high,(300000,2));uv=(world-low)/(high-low);xy=np.minimum((uv*grid).astype(int),grid-1);labels=cells[xy[:,1],xy[:,0]]
    blend=pair_blends(props,arrays,world)
    errors=int(((labels==1)&blend).sum()+((labels==2)&~blend).sum());assert errors==0,errors
    report={'key':key,'signature':signature,'grid':grid,'logical_bytes':len(words[0])*8,'cells':{str(i):int((cells==i).sum()) for i in (0,1,2)},'random_validation_points':len(world),'classification_errors':errors,'low':low.tolist(),'high':high.tolist(),'halo_world':halo,'build_wall_seconds':time.perf_counter()-start}
    (out/'publication.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='signature'},indent=2));return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--project',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--grid',type=int,default=128);a=p.parse_args();build(a.project,a.output,a.grid)
