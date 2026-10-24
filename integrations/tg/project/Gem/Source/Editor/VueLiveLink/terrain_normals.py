"""Build a normal field for Vue's regular or adaptive terrain mesh.

Uses only the standard library so this can run in the Editor's Python runtime.
The field is in baked model space, independently of material UV transforms.
"""

import bisect
from array import array
import math
import re
import struct

from terrain_transform import rotation_and_scale


# Keep signed components: UNORM would discard negative model-space normals.
PIXEL_FORMAT = "<4e"
BYTES_PER_PIXEL = struct.calcsize(PIXEL_FORMAT)
DDS_DXGI_FORMAT = 10  # DXGI_FORMAT_R16G16B16A16_FLOAT.


TEXTURE_SETTINGS = '''<ObjectStream version="3">
  <Class name="TextureSettings" version="2" type="{980132FF-C450-425D-8AE0-BD96A8486177}">
    <Class name="Name" field="Preset" value="LUT_RGBA16F" type="{3D2B920C-9EFD-40D5-AAE0-DF131C3D4931}"/>
    <Class name="unsigned int" field="SizeReduceLevel" value="0" type="{43DA906B-7DEF-4CA8-9790-854106D3F983}"/>
    <Class name="bool" field="EngineReduce" value="false" type="{A0CA880C-AFE4-43CB-926C-59AC48496112}"/>
    <Class name="bool" field="EnableMipmap" value="false" type="{A0CA880C-AFE4-43CB-926C-59AC48496112}"/>
  </Class>
</ObjectStream>
'''


# Height lookup textures retain full-precision world-space heights.
HEIGHT_TEXTURE_SETTINGS = TEXTURE_SETTINGS.replace("LUT_RGBA16F", "LUT_RGBA32F")

def baked_vector(n, axes, scales):
    # FBX bakes Vue's scale/rotation and an extra 180-degree Z yaw.
    return [sum(axes[col][axis] * n[col] / scales[col] for col in range(3)) * sign
            for axis, sign in enumerate((-1.0, -1.0, 1.0))]


def normal_pixel(n):
    length = math.sqrt(sum(value * value for value in n))
    if length < 1e-8 or not math.isfinite(length):
        raise ValueError("Degenerate terrain normal")
    return struct.pack(PIXEL_FORMAT, *(value / length for value in n), 1.0)


def texture(rows, nx, ny):
    # Power-of-two padding avoids Image Builder resampling the control lattice.
    width, height = (1 << (size - 1).bit_length() for size in (nx, ny))
    pixels = bytearray()
    for y in range(height):
        row = rows[min(y, ny - 1)]
        pixels.extend(b"".join(row + [row[-1]] * (width - nx)))
    header = struct.pack("<7I", 124, 0x100F, height, width, width * BYTES_PER_PIXEL, 0, 0) + bytes(44)
    header += struct.pack("<II4s5I", 32, 4, b"DX10", 0, 0, 0, 0, 0)
    header += struct.pack("<5I", 0x1000, 0, 0, 0, 0)
    header += struct.pack("<5I", DDS_DXGI_FORMAT, 3, 0, 1, 0)
    return b"DDS " + header + pixels, [nx, ny]


def adaptive_field(normals, uvs, normal_indices, uv_indices, polygons, transformation, size=512):
    """Rasterize the exported adaptive triangles in UV0, retaining their normals.

    Only a continuous, nearly full rectangular terrain domain is accepted.
    Interior holes and overlapping UV charts with incompatible normals remain
    unsupported. A small outer texel inset is extended to the field border.
    """
    if len(polygons) != len(uv_indices) or size < 2:
        return None
    lo = [min(uv[i] for uv in uvs) for i in range(2)]
    hi = [max(uv[i] for uv in uvs) for i in range(2)]
    if (any(v < -1e-5 for v in lo) or any(v > 1.00001 for v in hi) or
            any(abs(lo[i]) + abs(hi[i]-1) > .02 for i in range(2))):
        return None
    axes, scales = rotation_and_scale(transformation, "adaptive normal field")
    transformed = [baked_vector(n, axes, scales) for n in normals]
    values, filled = array('f', [0.0]) * (size*size*3), bytearray(size*size)
    domain_lo = [max(0, math.ceil(v*(size-1)-1e-5)) for v in lo]
    domain_hi = [min(size-1, math.floor(v*(size-1)+1e-5)) for v in hi]

    def raster(corners):
        points = [[v*(size-1) for v in uvs[uv_indices[c]]] for c in corners]
        ns = [transformed[normal_indices[c]] for c in corners]
        (ax, ay), (bx, by), (cx, cy) = points
        area = (bx-ax)*(cy-ay)-(by-ay)*(cx-ax)
        if abs(area) < 1e-10:
            return True
        xlo, ylo = [max(0, math.ceil(min(p[i] for p in points)-1e-5)) for i in range(2)]
        xhi, yhi = [min(size-1, math.floor(max(p[i] for p in points)+1e-5)) for i in range(2)]
        for y in range(ylo, yhi+1):
            for x in range(xlo, xhi+1):
                wa = ((bx-x)*(cy-y)-(by-y)*(cx-x))/area
                wb = ((cx-x)*(ay-y)-(cy-y)*(ax-x))/area
                wc = 1-wa-wb
                if min(wa, wb, wc) < -1e-5:
                    continue
                n = [wa*ns[0][i]+wb*ns[1][i]+wc*ns[2][i] for i in range(3)]
                pixel = y*size+x
                offset = pixel*3
                if filled[pixel]:
                    prior = values[offset:offset+3]
                    lengths = math.sqrt(sum(v*v for v in prior)*sum(v*v for v in n))
                    if lengths < 1e-12 or sum(a*b for a,b in zip(prior,n))/lengths < .9999:
                        return False
                values[offset:offset+3] = array('f', n)
                filled[pixel] = 1
        return True

    face = []
    for index, end in enumerate(polygons):
        face.append(index)
        if end < 0:
            if len(face) < 3:
                return None
            for i in range(1, len(face)-1):
                if not raster((face[0], face[i], face[i+1])):
                    return None
            face = []
    if face or any(not filled[y*size+x] for y in range(domain_lo[1], domain_hi[1]+1)
                   for x in range(domain_lo[0], domain_hi[0]+1)):
        return None
    rows = []
    for y in range(size):
        row = []
        # LUT_RGBA16F preserves rows; match SceneAPI's flipped V as in the grid path.
        source_y = min(domain_hi[1], max(domain_lo[1], size-1-y))
        for x in range(size):
            source_x = min(domain_hi[0], max(domain_lo[0], x))
            offset = (source_y*size+source_x)*3
            row.append(normal_pixel(values[offset:offset+3]))
        rows.append(row)
    return texture(rows, size, size)


def build(raw, transformation):
    """Return signed half-float DDS bytes and grid size, or None for unsupported UV coverage."""
    def array(name, width=1, integer=False):
        match = re.search(r'\b' + name + r':\s*\*\d+\s*\{\s*a:\s*([^}]+)', raw)
        if not match:
            return []
        values = [int(value) if integer else float(value) for value in match[1].split(",")]
        if len(values) % width:
            raise ValueError("Malformed terrain " + name)
        return [tuple(values[i:i + width]) for i in range(0, len(values), width)] if width > 1 else values

    normals = array("Normals", 3)
    uvs = array("UV", 2)
    normal_indices = array("NormalsIndex", integer=True)
    uv_indices = array("UVIndex", integer=True)
    if not normals or not uvs or not normal_indices or len(normal_indices) != len(uv_indices):
        return None
    if not all(math.isfinite(value) for uv in uvs for value in uv):
        raise ValueError("Non-finite terrain UVs")

    def cluster(axis):
        groups = []
        for value in sorted({uv[axis] for uv in uvs}):
            if not groups or value - groups[-1][0] > 1e-5:
                groups.append([])
            groups[-1].append(value)
        return ([sum(group) / len(group) for group in groups],
                {value: i for i, group in enumerate(groups) for value in group})

    us, u_indices = cluster(0)
    vs, v_indices = cluster(1)
    lattice = {}
    for ni, ui in zip(normal_indices, uv_indices):
        normal, uv = normals[ni], uvs[ui]
        if not all(math.isfinite(v) for v in (*normal, *uv)):
            raise ValueError("Non-finite terrain normals or UVs")
        # Vue's regular rows differ by a few float ULPs between columns.
        key = (u_indices[uv[0]], v_indices[uv[1]])
        previous = lattice.get(key)
        if previous is not None and sum((a - b) ** 2 for a, b in zip(previous, normal)) > 1e-6:
            return None  # Preserve intentionally split normals instead of welding a seam.
        lattice[key] = normal
    nx, ny = len(us), len(vs)
    if (nx < 2 or ny < 2 or nx * ny != len(lattice) or max(nx, ny) > 2048 or
            abs(us[0]) + abs(vs[0]) + abs(us[-1] - 1) + abs(vs[-1] - 1) > 1e-4):
        return adaptive_field(normals, uvs, normal_indices, uv_indices,
                              array("PolygonVertexIndex", integer=True), transformation)
    axes, scales = rotation_and_scale(transformation, "normal field")

    def interval(coords, value):
        index = max(0, min(len(coords) - 2, bisect.bisect_right(coords, value) - 1))
        return index, (value - coords[index]) / (coords[index + 1] - coords[index])

    rows = []
    for y in range(ny):
        # SceneAPI flips imported FBX V. The processed texture must do the same.
        iy, fy = interval(vs, 1.0 - y / (ny - 1))
        row = []
        for x in range(nx):
            ix, fx = interval(us, x / (nx - 1))
            corners = [lattice[(ix + dx, iy + dy)] for dy in (0, 1) for dx in (0, 1)]
            weights = ((1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy)
            n = [sum(w * c[axis] for w, c in zip(weights, corners)) for axis in range(3)]
            row.append(normal_pixel(baked_vector(n, axes, scales)))
        rows.append(row)

    return texture(rows, nx, ny)
