"""Vue retained-terrain adapter. Pure preparation; no Editor imports.

Publishes owned child materials and a bounded height query texture per FBX. The
height query only selects appearance: it never displaces or clips geometry.
"""
import hashlib
import json
import math
import os
import re
import struct
from pathlib import Path
from terrain_normals import TEXTURE_SETTINGS as NORMAL_TEXTURE_SETTINGS

# Height queries consume only red. Preserve the same scalar float values while
# avoiding three unused channels; normal fields retain their signed RGBA format.
TEXTURE_SETTINGS = NORMAL_TEXTURE_SETTINGS.replace("LUT_RGBA16F", "LUT_R32F")
HALF_TEXTURE_SETTINGS = NORMAL_TEXTURE_SETTINGS.replace("LUT_RGBA16F", "TerrainOverlap_R16F")

MAX_SOURCES = 4
FIELD_SIZE = 256
VERSION = 8
HEIGHT_VERSION = 4
OWNED_DESCRIPTION = 'Owned Vue terrain overlap adapter; original appearance is its parent.'

FIELDS = {
    'heightMap': ('Image', None), 'colorMap': ('Image', None), 'normalMap': ('Image', None),
    'normalField': ('Image', None), 'specularMap': ('Image', None), 'occlusionMap': ('Image', None),
    'uvRow0': ('Vector3', [0, 0, 0]), 'uvRow1': ('Vector3', [0, 0, 0]),
    'heightScaleOffset': ('Vector2', [1, 0]), 'gridSize': ('Vector2', [2, 2]),
    'normalRow0': ('Vector3', [1, 0, 0]), 'normalRow1': ('Vector3', [0, 1, 0]),
    'normalRow2': ('Vector3', [0, 0, 1]), 'tangent': ('Vector3', [1, 0, 0]),
    'color': ('Color', [1, 1, 1, 1]), 'colorFactor': ('Float', 1),
    'roughness': ('Float', .5), 'metallic': ('Float', 0), 'specular': ('Float', .5),
    'normalStrength': ('Float', 1), 'occlusion': ('Float', 1), 'smooth': ('Float', 1),
}


def install_material_layout(project):
    """Regenerate the adapter property declarations, preserving existing groups."""
    folder = Path(project) / 'Assets/VueLiveLink'
    path = folder / 'VueTerrainOverlap.materialtype'
    data = json.loads((folder / 'VueTerrainMesh.materialtype').read_text())
    data['materialShaderDefines'] = 'VueTerrainOverlap_Defines.azsli'
    groups = data['propertyLayout']['propertyGroups']
    # The native source loader requires decimal Color channels, including
    # defaults inherited from the original Vue material type.
    for group in groups:
        for existing in group.get('properties', []):
            if existing.get('type') == 'Color' and isinstance(existing.get('defaultValue'), list):
                existing['defaultValue'] = [float(v) for v in existing['defaultValue']]
    groups[:] = [g for g in groups if not g.get('name', '').startswith(('terrainOverlap', 'overlapSource'))]
    def prop(name, kind, default, member):
        p = dict(name=name, type=kind, connection={'type': 'ShaderInput', 'name': member})
        if default is not None:
            if kind in ('Color', 'Vector2', 'Vector3'):
                default = [float(v) for v in default]
            elif kind == 'Float':
                default = float(default)
            p['defaultValue'] = default
        if name == 'count': p.update(min=0, max=4)
        if name == 'width': p.update(min=0.0)
        return p
    groups.append({'name': 'terrainOverlap', 'displayName': 'Terrain Overlap', 'properties': [
        prop('width', 'Float', 0, 'm_vueOverlapWidth'),
        {'name': 'count', 'type': 'UInt', 'defaultValue': 0, 'min': 0, 'max': 4,
         'connection': {'type': 'ShaderOption', 'name': 'o_terrainOverlapSourceCount'}},
        {'name': 'heightFp16', 'displayName': '16-bit Height Arithmetic', 'type': 'Bool', 'defaultValue': False,
         'connection': {'type': 'ShaderOption', 'name': 'o_terrainHeightFp16'}},
        {'name': 'fixedHeightExtent', 'type': 'Bool', 'defaultValue': False,
         'connection': {'type': 'ShaderOption', 'name': 'o_terrainOverlapFixedHeightExtent'}}]})
    # Experimental bounded cache is opt-in on immutable prototype children.
    # Normal edit-driven publications contain no page bindings and use fallback.
    groups[:] = [g for g in groups if g.get('name') != 'overlapPages']
    groups.append({'name': 'overlapPages', 'displayName': 'Overlap Pages (Prototype)', 'properties': [
        {'name': 'enabled', 'type': 'Bool', 'defaultValue': False,
         'connection': {'type': 'ShaderOption', 'name': 'o_terrainOverlapPages'}},
        prop('debug', 'Bool', False, 'm_vuePagesDebug'),
        prop('low', 'Vector2', [0, 0], 'm_vuePagesLow'),
        prop('extent', 'Vector2', [1, 1], 'm_vuePagesExtent'),
        prop('size', 'Float', 1024, 'm_vuePagesSize'),
        *[prop('channel'+str(i), 'Image', None, 'm_vuePagesChannel'+str(i)) for i in range(4)]]})
    # Keep the optional frozen classifier unkeyed. The default optimizes the
    # compiled pipeline without rejecting contributors or depending on a bake.
    groups.append({'name': 'contributorSelection', 'displayName': 'Terrain Shader', 'properties': [
        {'name': 'enabled', 'displayName': 'Optimized Shader', 'description': 'Measured resource-layout optimization. Disable to use the original shader.', 'type': 'Bool', 'defaultValue': True,
         'connection': {'type': 'ShaderOption', 'name': 'o_terrainContributorTiles'}},
        prop('key', 'UInt', 0, 'm_vueContributorKey')]})
    groups.append({'name':'samplingExperiment','displayName':'Texture Sampling (Experiment)','properties':[{'name':'normalPacking','type':'UInt','defaultValue':0,'min':0,'max':2,'connection':{'type':'ShaderOption','name':'o_terrainNormalPacking'}},{'name':'mipProbe','type':'Bool','defaultValue':False,'connection':{'type':'ShaderOption','name':'o_terrainTextureMipProbe'}}]})
    shader = [f'// Generated by terrain_overlap.install_material_layout.\n#pragma once\nstatic const uint VueOverlapHeightFieldSize = {FIELD_SIZE}u;\nstruct VueOverlapSource\n{{']
    types = dict(Image='int', Vector3='float3', Vector2='float2', Color='float4', Float='float')
    shader += [f'    {types[kind]} {name};' for name, (kind, _) in FIELDS.items()]
    shader += ['};', 'VueOverlapSource VueGetOverlapSource(uint index, const MaterialParameters params)', '{',
               '    VueOverlapSource result = (VueOverlapSource)0;']
    for i in range(MAX_SOURCES):
        groups.append({'name': f'overlapSource{i}', 'displayName': f'Overlap Source {i+1}', 'properties':
                       [prop(n, t, d, f'm_vueOverlap{i}_{n}') for n, (t, d) in FIELDS.items()]})
        shader.append(f'    if (index == {i}) {{')
        shader += [f'        result.{n} = params.m_vueOverlap{i}_{n};' for n in FIELDS]
        shader.append('    }')
    shader += ['    return result;', '}']
    write_json(path, data)
    write_changed(folder / 'VueTerrainOverlapInputs.azsli', '\n'.join(shader) + '\n')


def write_changed(path, data):
    path = Path(path)
    if isinstance(data, str):
        data = data.encode('utf-8')
    if path.is_file() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + '.writing')
    temp.write_bytes(data)
    os.replace(temp, path)


def write_json(path, data):
    write_changed(path, json.dumps(data, indent=2, sort_keys=True) + '\n')


def material_properties(project, path, seen=None):
    project = Path(project).resolve()
    path = Path(path).resolve()
    if not path.is_relative_to(project / 'Assets'):
        raise ValueError('Overlap material must belong to project Assets')
    seen = set() if seen is None else seen
    if path in seen or len(seen) > 16:
        raise ValueError('Cyclic material parents')
    seen.add(path)
    data = json.loads(path.read_text(encoding='utf-8-sig'))
    result = {}
    if data.get('parentMaterial'):
        parent = data['parentMaterial']
        result.update(material_properties(project, project / parent if parent.lower().startswith('assets/') else path.parent / parent, seen))
    for key, value in data.get('propertyValues', {}).items():
        if isinstance(value, str) and Path(value).suffix.lower() in ('.png', '.dds', '.tif', '.tiff', '.jpg', '.jpeg'):
            texture = project / value if value.lower().startswith('assets/') else path.parent / value
            value = texture.resolve().relative_to(project).as_posix()
        result[key] = value
    return result


def original_material(project, product_path):
    """Unwrap only our owned child, so response ownership checks see their base."""
    project = Path(project).resolve()
    path = str(product_path).replace('\\', '/')
    for _ in range(8):
        if not path.lower().startswith('assets/vuelivelink/overlap/'):
            return path
        source = (project / path).with_suffix('.material').resolve()
        if not source.is_relative_to(project / 'Assets/VueLiveLink/overlap') or not source.is_file():
            return path
        data = json.loads(source.read_text())
        if data.get('description') != OWNED_DESCRIPTION:
            return path
        sidecar = source.with_suffix('.overlap.json')
        if not sidecar.is_file():
            # Older publications may have lost their sidecars. Capture the exact
            # retained appearance instead of guessing an imported parent or
            # silently reverting the user's lighting/material settings.
            properties = {k: v for k, v in data.get('propertyValues', {}).items()
                          if not k.startswith(('overlapSource', 'terrainOverlap'))}
            captured = {'materialType': 'Assets/VueLiveLink/VueTerrainMesh.materialtype',
                        'materialTypeVersion': data.get('materialTypeVersion', 2),
                        'description': 'Recovered retained terrain appearance', 'propertyValues': properties}
            identity = hashlib.sha256(json.dumps(captured, sort_keys=True).encode()).hexdigest()
            original = project / 'Assets/VueLiveLink/response/recovered' / (identity + '.material')
            write_json(original, captured)
            write_json(sidecar, {'originalMaterial': original.relative_to(project).as_posix(),
                                 'version': VERSION, 'recoveredAppearance': True})
        metadata = json.loads(sidecar.read_text())
        path = str(Path(metadata['originalMaterial']).with_suffix('.azmaterial')).replace('\\', '/').lower()
    raise ValueError('Cyclic owned overlap material chain')


def response_defaults(project):
    """Read the actual project declarations used by the terrain shaders."""
    root = Path(project).resolve()
    path = root/'Assets/VueLiveLink/VueTerrainMesh.materialtype'
    if not path.is_file():
        return {}, set()
    files, values = {path}, {}
    groups = json.loads(path.read_text(encoding='utf-8-sig')).get('propertyLayout', {}).get('propertyGroups', [])
    for group in groups:
        if '$import' in group:
            imported = (path.parent/group['$import']).resolve()
            if not imported.is_file():
                continue
            if not imported.is_relative_to(root/'Assets'):
                raise ValueError('Terrain property declaration outside project Assets')
            files.add(imported)
            group = json.loads(imported.read_text(encoding='utf-8-sig'))
        name = group.get('name')
        if name not in ('vueResponse', 'groundBlend', 'groundContact'):
            continue
        for prop in group.get('properties', []):
            if 'defaultValue' in prop or prop.get('type') == 'Image':
                values[name+'.'+prop['name']] = (prop.get('defaultValue', ''), prop.get('type'))
    return values, files


def response_signature(project, props):
    """Explicit shader defaults and inherited defaults have the same meaning."""
    defaults, _ = response_defaults(project)
    values = {key: default for key, (default, _) in defaults.items()}
    values.update({k: v for k, v in props.items() if k.startswith(('vueResponse.', 'groundBlend.', 'groundContact.'))})
    for key, (_, kind) in defaults.items():
        value = values[key]
        if kind == 'Color' and isinstance(value, list) and len(value) == 3:
            values[key] = value + [1.0]
        elif kind == 'Image' and value is None:
            values[key] = ''
    return values


def dependency_state(project, source):
    """Cheap monitor key: stat parents, referenced images, and baked geometry."""
    project = Path(project).resolve()
    files, visited = {Path(source['fbx']).resolve()}, set()
    _, declarations = response_defaults(project)
    files.update(declarations)
    def visit(path):
        path = path.resolve()
        if path in visited or len(visited) > 16:
            raise ValueError('Cyclic material dependency')
        visited.add(path); files.add(path)
        data = json.loads(path.read_text(encoding='utf-8-sig'))
        for value in [data.get('parentMaterial', ''), *data.get('propertyValues', {}).values()]:
            if not isinstance(value, str) or not value:
                continue
            if Path(value).suffix.lower() not in ('.material', '.png', '.dds', '.tif', '.tiff', '.jpg', '.jpeg'):
                continue
            p = (project / value if value.lower().startswith('assets/') else path.parent / value).resolve()
            if not p.is_relative_to(project / 'Assets'):
                raise ValueError('Material dependency outside project Assets')
            if p.suffix == '.material':
                visit(p)
            else:
                files.add(p)
                if p.with_suffix(p.suffix + '.assetinfo').exists(): files.add(p.with_suffix(p.suffix + '.assetinfo'))
    visit(Path(source['material']))
    return [(p.as_posix(), p.stat().st_size, p.stat().st_mtime_ns) for p in sorted(files)]


def fingerprint(project, sources, width, height_precision=32):
    if height_precision not in (16, 32):
        raise ValueError('Height Precision must be 16 or 32')
    return hashlib.sha256(json.dumps({'version': VERSION, 'width': width, 'heightPrecision': height_precision, 'heightFieldSize': FIELD_SIZE,
        'sources': sorted(sources, key=lambda s: s['key']),
        'dependencies': [dependency_state(project, s) for s in sorted(sources, key=lambda s: s['key'])]},
        sort_keys=True).encode()).hexdigest()


def instance_mapping(world_rows, translation, scale, baked_matrix):
    """SceneAPI's baked model has an extra half turn around Z."""
    rows = [[world_rows[r][c] * scale[c] for c in range(3)] for r in range(3)]
    matrix = [[sum(rows[r][k] * (-1 if k < 2 else 1) * baked_matrix[k][c] for k in range(3))
               for c in range(3)] for r in range(3)]
    # Orthogonal entity basis with positive scales; inverse transpose for normals.
    lengths = [sum(rows[r][c] ** 2 for r in range(3)) for c in range(3)]
    if min(lengths) <= 1e-12:
        raise ValueError('Singular terrain instance transform')
    normals = [[rows[r][c] / lengths[c] for c in range(3)] for r in range(3)]
    return {'matrix': matrix, 'translation': translation, 'normalRows': normals}


def height_field(raw, size=FIELD_SIZE, height_precision=32):
    """Rasterize FBX polygon fans into original UV0, with SceneAPI's V flip.

    Require affine UV-to-XY, one height per XY, and complete coverage for regular
    quads or adaptive triangles. Vue can omit the final UV texel (e.g. 511/512);
    retain that UV scale and extend only the small outer border, without filling
    interior holes. Returned bounds describe the full 0..1 UV domain.
    """
    if size < 2:
        raise ValueError('Terrain height query must have at least two samples per axis')
    def array(name, cast=float):
        match = re.search(r'\b' + name + r':\s*\*\d+\s*\{\s*a:\s*([^}]+)', raw)
        if not match:
            raise ValueError('Missing FBX ' + name)
        return [cast(x) for x in match[1].split(',')]
    v, uv = array('Vertices'), array('UV')
    vertices = list(zip(v[::3], v[1::3], v[2::3]))
    coords = list(zip(uv[::2], uv[1::2]))
    indices, ui = array('PolygonVertexIndex', int), array('UVIndex', int)
    if (not vertices or not coords or len(v) % 3 or len(uv) % 2 or
            len(indices) != len(ui) or not all(math.isfinite(x) for x in v + uv)):
        raise ValueError('Invalid FBX vertex/UV data')
    vertex_lo = [min(p[a] for p in vertices) for a in (0, 1)]
    vertex_span = [max(p[a] for p in vertices) - vertex_lo[a] for a in (0, 1)]
    uv_lo = [min(p[a] for p in coords) for a in (0, 1)]
    uv_hi = [max(p[a] for p in coords) for a in (0, 1)]
    if min(vertex_span) <= 0 or any(uv_hi[a] <= uv_lo[a] for a in (0, 1)):
        raise ValueError('Degenerate terrain footprint')
    if (any(q < -1e-5 for q in uv_lo) or any(q > 1.00001 for q in uv_hi) or
            any(abs(uv_lo[a]) + abs(uv_hi[a]-1) > .02 for a in (0, 1))):
        raise ValueError('Vue overlap requires nearly full rectangular terrain UV0')
    span = [vertex_span[a] / (uv_hi[a]-uv_lo[a]) for a in (0, 1)]
    lo = [vertex_lo[a] - uv_lo[a]*span[a] for a in (0, 1)]
    domain_lo = [max(0, uv_lo[0])*(size-1), max(0, 1-uv_hi[1])*(size-1)]
    domain_hi = [min(1, uv_hi[0])*(size-1), min(1, 1-uv_lo[1])*(size-1)]
    pixels = [None] * (size * size)
    polygon = []
    def triangle(a, b, c):
        ax, ay, az = a; bx, by, bz = b; cx, cy, cz = c
        det = (bx-ax)*(cy-ay)-(by-ay)*(cx-ax)
        if abs(det) < 1e-12:
            return
        points = ((ax, ay), (bx, by), (cx, cy))
        lower = [min(p[a] for p in points) for a in (0, 1)]
        upper = [max(p[a] for p in points) for a in (0, 1)]
        start = [0 if lower[a] <= domain_lo[a]+1e-5 else max(0, math.ceil(lower[a]-1e-5))
                 for a in (0, 1)]
        end = [size-1 if upper[a] >= domain_hi[a]-1e-5 else min(size-1, math.floor(upper[a]+1e-5))
               for a in (0, 1)]
        for y in range(start[1], end[1]+1):
            sample_y = min(domain_hi[1], max(domain_lo[1], y))
            for x in range(start[0], end[0]+1):
                sample_x = min(domain_hi[0], max(domain_lo[0], x))
                bweight = ((sample_x-ax)*(cy-ay)-(sample_y-ay)*(cx-ax))/det
                cweight = ((bx-ax)*(sample_y-ay)-(by-ay)*(sample_x-ax))/det
                if min(bweight, cweight, 1-bweight-cweight) < -1e-5:
                    continue
                z = az + bweight*(bz-az) + cweight*(cz-az)
                at = y*size+x
                if pixels[at] is not None and abs(pixels[at]-z) > 1e-3:
                    raise ValueError('Multiple heights at one terrain UV')
                pixels[at] = z
    for index, uv_index in zip(indices, ui):
        vertex_index = index if index >= 0 else -index-1
        if not 0 <= vertex_index < len(vertices) or not 0 <= uv_index < len(coords):
            raise ValueError('Invalid FBX vertex/UV index')
        p = vertices[vertex_index]
        u, original_v = coords[uv_index]
        if max(abs((p[a]-lo[a])/span[a]-q) for a, q in enumerate((u, original_v))) > 1e-4:
            raise ValueError('Vue overlap requires affine baked terrain UV0')
        polygon.append((u*(size-1), (1-original_v)*(size-1), p[2]))
        if index < 0:
            if len(polygon) not in (3, 4):
                raise ValueError('Vue overlap supports triangular/quad heightfield polygons')
            for i in range(1, len(polygon)-1):
                triangle(polygon[0], polygon[i], polygon[i+1])
            polygon = []
    if polygon or any(z is None for z in pixels):
        raise ValueError('Incomplete terrain height query coverage')
    if height_precision not in (16, 32):
        raise ValueError('Height Precision must be 16 or 32')
    if any(not math.isfinite(z) or (height_precision == 16 and abs(z) > 65504) for z in pixels):
        raise ValueError('Terrain height is outside the selected finite float range')
    stride, dxgi, packing = (2, 54, '<e') if height_precision == 16 else (4, 41, '<f')
    header = struct.pack('<7I', 124, 0x100F, size, size, size*stride, 0, 0) + bytes(44)
    header += struct.pack('<II4s5I', 32, 4, b'DX10', 0, 0, 0, 0, 0)
    header += struct.pack('<5I', 0x1000, 0, 0, 0, 0) + struct.pack('<5I', dxgi, 3, 0, 1, 0)
    return b'DDS ' + header + b''.join(struct.pack(packing, z) for z in pixels), lo, span


def mapping(matrix, translation, lo, span):
    """Raw Vue vertex -> world matrix (FBX bake and entity transform combined)."""
    if not all(math.isfinite(v) for row in matrix for v in row) or not all(math.isfinite(v) for v in translation):
        raise ValueError('Non-finite terrain transform')
    if any(abs(matrix[a][2]) > 1e-6 for a in (0, 1)) or any(abs(matrix[2][a]) > 1e-6 for a in (0, 1)):
        raise ValueError('Tilted retained terrain is not an XY heightfield')
    a, b = matrix[0][:2]; c, d = matrix[1][:2]
    det = a*d-b*c
    if det < 1e-9 or matrix[2][2] <= 0:
        raise ValueError('Invalid terrain transform')
    x, y = translation[:2]
    return {
        'uvRow0': [d/det/span[0], -b/det/span[0], (-d*x+b*y)/det/span[0]-lo[0]/span[0]],
        'uvRow1': [c/det/span[1], -a/det/span[1], 1+(lo[1]+(-c*x+a*y)/det)/span[1]],
        'heightScaleOffset': [matrix[2][2], translation[2]],
        'tangent': [a, c, 0],
    }


def prepare(project, sources, width=10.0, height_precision=32):
    """Sources: key, material, fbx, matrix, translation, normalRows. No mutation of imported materials."""
    project = Path(project).resolve()
    if not math.isfinite(width) or width < 0 or not 2 <= len(sources) <= MAX_SOURCES:
        raise ValueError('Vue overlap needs 2–4 baked sources and a finite nonnegative width')
    sources = sorted(sources, key=lambda s: s['key'])
    if len({s['key'] for s in sources}) != len(sources):
        raise ValueError('Duplicate terrain source identity')
    revision = fingerprint(project, sources, width, height_precision)
    prepared = []
    response = None
    for source in sources:
        source_document = json.loads(Path(source['material']).read_text(encoding='utf-8-sig'))
        if source_document.get('materialType') and Path(source_document['materialType']).name.lower() != 'vueterrainmesh.materialtype':
            raise ValueError('Vue overlap requires the original VueTerrainMesh material type')
        props = material_properties(project, source['material'])
        if props.get('opacity.mode', 'Opaque') != 'Opaque' or props.get('emissive.enable', False) or props.get('clearCoat.enable', False):
            raise ValueError('Vue overlap supports opaque baked PBR without emissive/clear coat')
        if props.get('baseColor.textureBlendMode', 'Multiply') != 'Multiply' or any(k.startswith('uv.') for k in props):
            raise ValueError('Use baked UV0 and Multiply color blending for Vue overlap')
        if any(props.get(k) for k in ('roughness.textureMap', 'metallic.textureMap', 'normal.flipX', 'normal.flipY')):
            raise ValueError('Vue overlap currently requires scalar roughness/metallic and unflipped baked normals')
        label = source.get('name', source['key'])
        if not props.get('normalSmoothing.field'):
            raise ValueError(label + ': baked normal field is missing; use Bake terrain on this terrain')
        if props.get('normalSmoothing.strength', 1) != 1:
            raise ValueError(label + ': Vue overlap requires normal smoothing strength 1')
        if any(k.lower().endswith('texturemapuv') and v != 'Tiled' for k, v in props.items()):
            raise ValueError('Vue overlap requires baked UV0 for all PBR channels')
        signature = response_signature(project, props)
        # Lighting response remains the current Vue lighting implementation. Do
        # not silently mix unlike non-PBR lighting models at an intersection.
        if response is not None and response != signature:
            raise ValueError('Participating terrains need matching Vue lighting response and ground-contact settings')
        response = signature
        raw = Path(source['fbx']).read_text()
        digest = hashlib.sha256(raw.encode()).hexdigest()
        folder = project / 'Assets/VueLiveLink/overlap' / hashlib.sha256(source['key'].encode()).hexdigest()[:24]
        suffix = '_16f' if height_precision == 16 else ''
        field = folder / ('v%d_%s_s%d%s.dds' % (HEIGHT_VERSION, digest[:24], FIELD_SIZE, suffix))
        meta = field.with_suffix('.json')
        if field.is_file() and meta.is_file():
            info = json.loads(meta.read_text()); lo, span = info['lo'], info['span']
        else:
            content, lo, span = height_field(raw, FIELD_SIZE, height_precision=height_precision)
            write_changed(field.with_suffix('.dds.assetinfo'), HALF_TEXTURE_SETTINGS if height_precision == 16 else TEXTURE_SETTINGS)
            write_changed(field, content)
            write_json(meta, {'lo': lo, 'span': span, 'sourceSha256': digest, 'size': FIELD_SIZE, 'version': HEIGHT_VERSION})
        values = mapping(source['matrix'], source['translation'], lo, span)
        values.update(heightMap=field.relative_to(project).as_posix(), gridSize=props.get('normalSmoothing.gridSize', [2, 2]),
                      color=[float(v) for v in props.get('baseColor.color', [1, 1, 1, 1])], colorFactor=props.get('baseColor.factor', 1),
                      roughness=props.get('roughness.factor', .5), metallic=props.get('metallic.factor', 0),
                      specular=props.get('specularF0.factor', .5), normalStrength=props.get('normal.factor', 1),
                      occlusion=props.get('occlusion.diffuseFactor', 1), smooth=props.get('normalSmoothing.strength', 1))
        for dest, group, prop in [('colorMap','baseColor','textureMap'), ('normalMap','normal','textureMap'),
                                 ('normalField','normalSmoothing','field'), ('specularMap','specularF0','textureMap'),
                                 ('occlusionMap','occlusion','diffuseTextureMap')]:
            values[dest] = props.get(group+'.'+prop, '') if props.get(group+'.useTexture', True) else ''
        for i, row in enumerate(source['normalRows']):
            values['normalRow'+str(i)] = row
        prepared.append((source, folder, values))
    shared = {'terrainOverlap.width': width, 'terrainOverlap.count': len(prepared),
              'terrainOverlap.heightFp16': height_precision == 16, 'terrainOverlap.fixedHeightExtent': True}
    for i, (_, _, values) in enumerate(prepared):
        shared.update({f'overlapSource{i}.{key}': value for key, value in values.items()})
    outputs = {}
    for source, folder, _ in prepared:
        child = folder / (revision + '.material')
        # Material inheritance cannot cross material types. Capture the resolved
        # original properties in this immutable adapter and keep reversible
        # ownership metadata alongside it, without changing the original asset.
        write_json(child.with_suffix('.overlap.json'), {'originalMaterial': Path(source['material']).resolve().relative_to(project).as_posix(),
                   'revision': revision, 'version': VERSION})
        write_json(child, {'materialType': 'Assets/VueLiveLink/VueTerrainOverlap.materialtype', 'materialTypeVersion': 2,
                          'description': OWNED_DESCRIPTION,
                          'propertyValues': dict(material_properties(project, source['material']), **shared)})
        outputs[source['key']] = child
    return outputs
