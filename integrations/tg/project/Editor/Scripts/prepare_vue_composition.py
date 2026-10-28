"""Rasterize captured or saved Vue meshes into the compositor's heightfield.

Run with Python + numpy + Pillow, then run register_vue_composition.py in the
Editor, or use the compositor inspector's refresh button. Original mesh
and material assets remain the visible surfaces.
"""
import hashlib
import json
import math
from pathlib import Path
import re
import numpy as np
from PIL import Image

PROJECT = Path(__file__).resolve().parents[2]
OUTPUT = PROJECT / 'Assets/VueLiveLink/composition'
TEXTURE_SETTINGS = '''<ObjectStream version="3">
  <Class name="TextureSettings" version="2" type="{980132FF-C450-425D-8AE0-BD96A8486177}">
    <Class name="Name" field="Preset" value="GSI16" type="{3D2B920C-9EFD-40D5-AAE0-DF131C3D4931}"/>
    <Class name="unsigned int" field="SizeReduceLevel" value="0" type="{43DA906B-7DEF-4CA8-9790-854106D3F983}"/>
    <Class name="bool" field="EngineReduce" value="false" type="{A0CA880C-AFE4-43CB-926C-59AC48496112}"/>
    <Class name="bool" field="EnableMipmap" value="false" type="{A0CA880C-AFE4-43CB-926C-59AC48496112}"/>
  </Class>
</ObjectStream>
'''


def clip_volume(info):
    """Closed, hidden render mask; collision/query contribution stays enabled."""
    width, depth = np.array(info['upper'])-info['lower']
    height = info['heightScale']+10
    vertices = [(x*width/2, y*depth/2, z*height/2) for z in [-1, 1] for y in [-1, 1] for x in [-1, 1]]
    triangles = [(0,2,1),(1,2,3),(4,5,6),(5,7,6),(0,1,4),(1,5,4),
                 (2,6,3),(3,6,7),(0,4,2),(2,4,6),(1,3,5),(3,7,5)]
    coords = ','.join(str(v) for p in vertices for v in p)
    indices = ','.join(str(v) for a,b,c in triangles for v in [a,b,-c-1])
    content = '''; FBX 7.4.0 project file
FBXHeaderExtension: { FBXHeaderVersion: 1003
 FBXVersion: 7400
}
GlobalSettings: { Version: 1000
 Properties70: {
  P: "UpAxis", "int", "Integer", "",2
  P: "UpAxisSign", "int", "Integer", "",1
  P: "FrontAxis", "int", "Integer", "",1
  P: "FrontAxisSign", "int", "Integer", "",-1
  P: "CoordAxis", "int", "Integer", "",0
  P: "CoordAxisSign", "int", "Integer", "",1
  P: "UnitScaleFactor", "double", "Number", "",100
 }
}
Documents: { Count: 1
 Document: 10, "Scene", "Scene" { RootNode: 0 }
}
Definitions: { Version: 100
 Count: 2
 ObjectType: "Geometry" { Count: 1 }
 ObjectType: "Model" { Count: 1 }
}
Objects: {
 Geometry: 100, "Geometry::VueCompositionClip", "Mesh" {
  Vertices: *24 { a: COORDS }
  PolygonVertexIndex: *36 { a: INDICES }
  GeometryVersion: 124
 }
 Model: 200, "Model::VueCompositionClip", "Mesh" {
  Version: 232
  Properties70: {
   P: "Lcl Translation", "Lcl Translation", "", "A",0,0,0
   P: "Lcl Rotation", "Lcl Rotation", "", "A",0,0,0
   P: "Lcl Scaling", "Lcl Scaling", "", "A",1,1,1
  }
  Shading: T
  Culling: "CullingOff"
 }
}
Connections: {
 C: "OO",100,200
 C: "OO",200,0
}
'''.replace('COORDS', coords).replace('INDICES', indices)
    (OUTPUT/'VueCompositionClip.fbx').write_text(content)
    (OUTPUT/'VueCompositionClip.fbx.assetinfo').write_text(json.dumps({'values': [{
        '$type':'{07B356B7-3635-40B5-878A-FAC4EFD5AD86} MeshGroup', 'name':'VueCompositionClip',
        'id':'{9A5CE3CF-816C-4EDB-BF61-753301B21181}',
        'nodeSelectionList':{'selectedNodes':['RootNode.VueCompositionClip'], 'unselectedNodes':[]},
        'rules':{'rules':[]}}]}, indent=2))


def transform(entity, entities):
    comp = next(c for c in entity['Components'].values() if 'TransformComponent' in c['$type'])
    data = comp.get('Transform Data', {})
    x, y, z = np.radians(data.get('Rotate', [0, 0, 0]))
    cx, cy, cz, sx, sy, sz = math.cos(x), math.cos(y), math.cos(z), math.sin(x), math.sin(y), math.sin(z)
    local = np.eye(4)
    local[:3, :3] = np.array([[cz*cy, cz*sy*sx-sz*cx, cz*sy*cx+sz*sx],
                              [sz*cy, sz*sy*sx+cz*cx, sz*sy*cx-cz*sx],
                              [-sy, cy*sx, cy*cx]]) * data.get('Scale', 1)
    local[:3, 3] = data.get('Translate', [0, 0, 0])
    parent = entities.get(comp.get('Parent Entity'))
    return transform(parent, entities) @ local if parent else local


def mesh(raw):
    def array(name, dtype):
        body = re.search(name + r':\s*\*\d+\s*{\s*a:\s*([^}]+)', raw).group(1)
        return np.fromstring(body, sep=',', dtype=dtype)
    vertices = array('Vertices', float).reshape(-1, 3)
    faces, polygon = [], []
    for index in array('PolygonVertexIndex', np.int64):
        polygon.append(int(index) if index >= 0 else -int(index)-1)
        if index < 0:
            faces.extend((polygon[0], polygon[i], polygon[i+1]) for i in range(1, len(polygon)-1))
            polygon = []
    if polygon or not np.isfinite(vertices).all():
        raise ValueError('Invalid terrain FBX')
    return vertices, faces


def rasterize(vertices, faces, lower, upper, size, heights):
    points = vertices.copy()
    points[:, 0] = (points[:, 0]-lower[0])/(upper[0]-lower[0])*(size-1)
    points[:, 1] = (upper[1]-points[:, 1])/(upper[1]-lower[1])*(size-1)
    for face in faces:
        a, b, c = points[list(face)]
        lo = np.maximum(np.ceil(np.minimum(np.minimum(a[:2], b[:2]), c[:2])).astype(int), 0)
        hi = np.minimum(np.floor(np.maximum(np.maximum(a[:2], b[:2]), c[:2])).astype(int), size-1)
        if np.any(lo > hi): continue
        determinant = (b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
        if abs(determinant) < 1e-12: continue
        yy, xx = np.ogrid[lo[1]:hi[1]+1, lo[0]:hi[0]+1]
        u = ((b[1]-c[1])*(xx-c[0])+(c[0]-b[0])*(yy-c[1]))/determinant
        v = ((c[1]-a[1])*(xx-c[0])+(a[0]-c[0])*(yy-c[1]))/determinant
        valid = (u >= -1e-8) & (v >= -1e-8) & (u+v <= 1+1e-8)
        tile = heights[lo[1]:hi[1]+1, lo[0]:hi[0]+1]
        np.maximum(tile, np.where(valid, u*a[2]+v*b[2]+(1-u-v)*c[2], 0), out=tile)


def prepare(size=4096, manifest=None):
    rows = manifest['sources'] if manifest else []
    if manifest is None:
        level = json.loads((PROJECT/'Levels/VueLiveLink/VueLiveLink.prefab').read_text())
        entities = {e['Id']: e for e in [level['ContainerEntity'], *level['Entities'].values()]}
        for entity in entities.values():
            source = next((c for c in entity['Components'].values() if c['$type'] == 'EditorVueTerrainSourceComponent'), None)
            if not source: continue
            metadata = json.loads(source['VueSnapshot'])
            model = metadata.get('modelProduct')
            if not model: continue
            world = transform(entity, entities)
            scale = next((c.get('NonUniformScale', [1, 1, 1]) for c in entity['Components'].values() if c['$type'] == 'EditorNonUniformScaleComponent'), [1, 1, 1])
            matrix = world[:3, :3] @ np.diag(scale) @ np.diag([-1, -1, 1]) @ np.array(metadata['bakedSourceTransformation'])
            rows.append(dict(name=entity['Name'], entity=entity['Id'],
                             source=(Path(model).parent/'terrain.fbx').as_posix(),
                             matrix=matrix.tolist(), translation=world[:3, 3].tolist()))
    sources, meshes = [], []
    for row in rows:
        path = (PROJECT/row['source']).resolve()
        if not path.is_relative_to(PROJECT/'Assets'):
            raise ValueError('Terrain mesh must be inside project Assets')
        raw = path.read_bytes()
        digest = hashlib.sha256(raw).hexdigest()
        if row.get('sha256', digest) != digest:
            raise ValueError(row['name']+': terrain changed during refresh')
        vertices, faces = mesh(raw.decode())
        vertices = vertices @ np.array(row['matrix']).T + row['translation']
        if not np.isfinite(vertices).all(): raise ValueError('Non-finite terrain placement')
        meshes.append((vertices, faces))
        sources.append(dict(row, sha256=digest))
    if not meshes: raise ValueError('No baked Vue terrain sources')
    # Two blank border texels make the zero-height base meet the surrounding ground.
    lower = np.min([v.min(axis=0) for v, _ in meshes], axis=0)[:2]
    upper = np.max([v.max(axis=0) for v, _ in meshes], axis=0)[:2]
    if np.any(upper <= lower): raise ValueError('Terrain has no XY footprint')
    padding = (upper-lower)/(size-5)*2
    lower -= padding; upper += padding
    heights = np.zeros((size, size), dtype=np.float32)
    for vertices, faces in meshes: rasterize(vertices, faces, lower, upper, size, heights)
    maximum = max(1.0, float(heights.max()))
    OUTPUT.mkdir(parents=True, exist_ok=True)
    Image.fromarray(np.rint(heights/maximum*65535).astype(np.uint16)).save(OUTPUT/'VueTerrainSurface.tif')
    (OUTPUT/'VueTerrainSurface.tif.assetinfo').write_text(TEXTURE_SETTINGS)
    info = dict(version=1, size=size, lower=lower.tolist(), upper=upper.tolist(), heightScale=maximum,
                spacing=((upper-lower)/(size-1)).tolist(), sources=sources)
    # Reference samples away from triangle edges for Editor/runtime verification.
    samples = []
    for vertices, faces in meshes:
        for face in faces[::max(1, len(faces)//12)]:
            p = vertices[list(face)].mean(axis=0)
            ix, iy = (p[0]-lower[0])/(upper[0]-lower[0])*(size-1), (upper[1]-p[1])/(upper[1]-lower[1])*(size-1)
            x, y = int(ix), int(iy); fx, fy = ix-x, iy-y
            h = heights[y, x]*(1-fx)*(1-fy)+heights[y, x+1]*fx*(1-fy)+heights[y+1, x]*(1-fx)*fy+heights[y+1, x+1]*fx*fy
            if h > 2: samples.append([float(p[0]), float(p[1]), float(h)])
    info['samples'] = samples
    (OUTPUT/'VueTerrainSurface.json').write_text(json.dumps(info, indent=2)+'\n')
    clip_volume(info)
    print(json.dumps(info, indent=2))


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--size', type=int, default=4096)
    args = parser.parse_args()
    if args.size < 16 or args.size > 8192 or args.size & (args.size-1):
        parser.error('size must be a power of two from 16 to 8192')
    if args.output: OUTPUT = args.output.resolve()
    prepare(args.size, json.loads(args.manifest.read_text()) if args.manifest else None)
