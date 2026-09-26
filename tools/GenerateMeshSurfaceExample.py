"""Run with Blender --background --python <this file> -- <output-directory>."""
import bpy
import math
import sys
from pathlib import Path

output = Path(sys.argv[sys.argv.index('--') + 1])
output.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
n = 17
vertices = [(x * 8 - 64, y * 8 - 64, 12 + 9 * math.sin(x / 16 * math.pi) * math.sin(y / 16 * math.pi))
            for y in range(n) for x in range(n)]
faces, slots = [], []
for y in range(n - 1):
    for x in range(n - 1):
        a = y * n + x
        faces.extend([(a, a + 1, a + n + 1), (a, a + n + 1, a + n)])
        slots.extend([0 if x < 5 else 1 if x < 11 else 2] * 2)
mesh = bpy.data.meshes.new('TerrainSurfaceLabels')
mesh.from_pydata(vertices, [], faces)
mesh.update()
obj = bpy.data.objects.new('TerrainSurfaceLabels', mesh)
bpy.context.collection.objects.link(obj)
for name, color in [('Rock', (.42, .48, .58, 1)), ('Soil', (.40, .16, .055, 1)), ('Road', (.045, .05, .055, 1))]:
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = color
    mesh.materials.append(mat)
for polygon, slot in zip(mesh.polygons, slots): polygon.material_index = slot
obj.select_set(True)
bpy.context.view_layer.objects.active = obj
bpy.ops.wm.save_as_mainfile(filepath=str(output / 'TerrainSurfaceLabels.blend'))
bpy.ops.export_scene.fbx(filepath=str(output / 'TerrainSurfaceLabels.fbx'), use_selection=True,
    object_types={'MESH'}, add_leaf_bones=False, bake_anim=False, axis_forward='-Y', axis_up='Z')
