import os as _os
from pathlib import Path as _Path
PROJECT = _Path(_os.environ['TG_PROJECT_ROOT']).resolve() if 'TG_PROJECT_ROOT' in _os.environ else next(
    p for p in _Path(__file__).resolve().parents if (p/'project.json').is_file())
"""Regular/adaptive exported terrain normal fields without a running Editor."""
import ast
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import tempfile
import time
from types import SimpleNamespace
from unittest.mock import Mock, patch
import unittest

sys.path.insert(0, str((PROJECT/'Gem') / "Source/Editor/VueLiveLink"))
import terrain_normals
import terrain_mesh


def raw_field(uvs, faces):
    normals = [(u, v, 1) for u, v in uvs]
    indices, polygons = [], []
    for face in faces:
        indices.extend(face)
        polygons.extend([*face[:-1], -face[-1]-1])
    def array(name, values):
        return "%s: *%d { a: %s }" % (name, len(values), ",".join(map(str, values)))
    return "\n".join((array("UV", [v for p in uvs for v in p]),
        array("Normals", [v for p in normals for v in p]), array("NormalsIndex", indices),
        array("UVIndex", indices), array("PolygonVertexIndex", polygons)))


class TerrainNormalsTests(unittest.TestCase):
    def test_adaptive_triangles_preserve_normal_transform_and_flipped_v(self):
        uvs = [(0, 0), (1, 0), (1, 1), (0, 1), (.31, .43)]
        faces = [(0, 1, 4), (1, 2, 4), (2, 3, 4), (3, 0, 4)]
        transform = [[0, -3, 0], [2, 0, 0], [0, 0, 4]]
        content, size = terrain_normals.build(raw_field(uvs, faces), transform)
        self.assertEqual(size, [512, 512])
        self.assertEqual(len(content), 148+512*512*8)
        for x, y in ((0, 0), (511, 511), (256, 256), (137, 223)):
            u, v = x/511, 1-y/511
            expected = [v/3, -u/2, .25]
            length = math.sqrt(sum(n*n for n in expected))
            actual = struct.unpack_from("<4e", content, 148+(y*512+x)*8)
            for value, n in zip(actual[:3], expected):
                self.assertAlmostEqual(value, n/length, delta=0.001)
            self.assertEqual(actual[3], 1)

    def test_small_outer_texel_inset_is_extended_but_interior_holes_are_rejected(self):
        uvs = [(0, 0), (.998, 0), (.998, .998), (0, .998), (.31, .43)]
        faces = [(0, 1, 4), (1, 2, 4), (2, 3, 4), (3, 0, 4)]
        transform = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
        self.assertIsNotNone(terrain_normals.build(raw_field(uvs, faces), transform))
        self.assertIsNone(terrain_normals.build(raw_field(uvs, faces[:-1]), transform))

    def test_regular_lattice_retains_its_original_resolution(self):
        uvs = [(0, 0), (1, 0), (1, 1), (0, 1)]
        content, size = terrain_normals.build(raw_field(uvs, [(0, 1, 2), (0, 2, 3)]),
                                            [[1, 0, 0], [0, 1, 0], [0, 0, 1]])
        self.assertEqual(size, [2, 2])
        self.assertEqual(len(content), 148+2*2*8)


class TerrainBakeFieldTests(unittest.TestCase):
    """Exercise the component bake coordinator and its real asset preparation."""

    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.project = Path(folder.name)
        self.link_id, self.uid = "a"*32, "123:456"
        self.active = terrain_mesh.asset_directory(self.project, self.link_id, self.uid, "a")
        self.active.mkdir(parents=True)
        (self.active / "terrain.fbx").write_bytes(b"previous working terrain")
        (self.active / "terrain.material").write_bytes(b"previous working material")
        self.previous = {p.name: p.read_bytes() for p in self.active.iterdir()}
        self.data = {"uid": self.uid, "position": [2.0, 3.0, 4.0],
                     "transformation": [[1, 0, 0], [0, 1, 0], [0, 0, 1]],
                     "activeBank": "a", "terrainMaterialVersion": 3,
                     "modelProduct": "previous.azmodel", "bakeStatus": "Baked mesh"}
        path = Path(terrain_mesh.__file__).with_name("editor_link.py")
        tree = ast.parse(path.read_text(encoding="utf-8"))
        cls = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == "LinkSession")
        names = ("_begin_terrain_bake", "_tick_terrain_bake")
        methods = [n for n in cls.body if isinstance(n, ast.FunctionDef) and n.name in names]
        self.lookup = Mock(return_value=("terrain", "source component", self.data))
        env = dict(PROJECT=self.project, Path=Path, time=time, terrain_entity_by_uid=self.lookup)
        self.env = env
        exec(compile(ast.Module(body=methods, type_ignores=[]), str(path), "exec"), env)
        session_type = type("LinkSession", (), {name: env[name] for name in names})
        self.session = session_type()
        self.session.link_id = self.link_id
        self.session._terrain_status = Mock()
        self.session._status = Mock()

    def export(self, adaptive):
        uvs = [(0, 0), (1, 0), (1, 1), (0, 1)]
        faces = [(0, 1, 2), (0, 2, 3)]
        if adaptive:
            uvs = [(0, 0), (.998, 0), (.998, .998), (0, .998), (.31, .43)]
            faces = [(0, 1, 4), (1, 2, 4), (2, 3, 4), (3, 0, 4)]
        transformation = [[0, -3, 0], [2, 0, 0], [0, 0, 4]]
        self.session.message = {"terrains": [{"uid": self.uid, "position": [2.0, 3.0, 4.0],
                                            "transformation": transformation}]}
        source = self.project / "user/VueLiveLink/inbox/terrain/request/terrain.fbx"
        source.parent.mkdir(parents=True)
        maps = source.parent / "Maps"
        maps.mkdir()
        (maps / "Color.png").write_bytes(b"baked color fixture")
        raw = (raw_field(uvs, faces) + '\n\tModel: 1, "Model::Terrain", "Mesh" {\n\t}\n'
               '\tMaterial: 2, "Material::Baked", "" {\n'
               '\t\tProperties70: {\n'
               '\t\t\tP: "DiffuseColor", "Color", "", "A",0.2,0.3,0.4\n\t\t}\n\t}\n'
               '\tTexture: 3, "Texture::Color", "" {\n'
               '\t\tRelativeFilename: "%s"\n\t}\n'
               'C: "OP",3,2,"DiffuseColor"\n' % (maps / "Color.png").as_posix())
        source.write_text(raw, encoding="utf-8", newline="\n")
        return {"uid": self.uid, "requestId": "request", "fbx": str(source),
                "fbxSha256": hashlib.sha256(source.read_bytes()).hexdigest()}

    def test_component_bake_generates_adaptive_field_in_replacement_bank(self):
        export = self.export(adaptive=True)
        self.session._begin_terrain_bake(export)
        prepared = self.session.terrain_task["prepared"]
        destination = terrain_mesh.asset_directory(self.project, self.link_id, self.uid, "b")
        self.assertEqual(Path(prepared["normalSource"]), destination / "terrain_normals.dds")
        self.assertTrue(prepared["normalProduct"].endswith("/b/terrain_normals.dds.streamingimage"))
        content = Path(prepared["normalSource"]).read_bytes()
        self.assertEqual(len(content), 148+512*512*8)
        # Model-space rotation/scale and flipped V reach the actual published file.
        normal = struct.unpack_from("<4e", content, 148)
        covered_v = math.floor(.998*511)/511
        self.assertAlmostEqual(normal[0]/normal[2], covered_v*4/3, delta=0.001)
        self.assertAlmostEqual(normal[1], 0, delta=0.001)
        material = json.loads(Path(prepared["materialSource"]).read_text())
        props = material["propertyValues"]
        self.assertEqual(props["normalSmoothing.gridSize"], [512, 512])
        self.assertEqual(props["normalSmoothing.field"], "terrain_normals.dds")
        self.assertEqual(props["normalSmoothing.strength"], 1)
        self.assertEqual(props["baseColor.color"], [.2, .3, .4, 1])
        self.assertEqual(props["baseColor.textureMap"], "Maps/Color.png")
        self.assertEqual((destination / "terrain_normals.dds.assetinfo").read_text(),
                         terrain_normals.TEXTURE_SETTINGS)
        self.assertEqual((destination / "Maps/Color.png").read_bytes(), b"baked color fixture")
        self.assertNotIn(str(Path(export["fbx"]).parent), Path(prepared["source"]).read_text())
        self.assertEqual(self.previous, {p.name: p.read_bytes() for p in self.active.iterdir()})
        self.assertEqual(self.data["modelProduct"], "previous.azmodel")
        self.session._terrain_status.assert_called_once_with(self.uid, "Processing mesh", "Waiting for Asset Processor")

    def test_repeated_bake_preparation_does_not_rewrite_identical_assets(self):
        export = self.export(adaptive=False)
        self.session._begin_terrain_bake(export)
        destination = Path(self.session.terrain_task["prepared"]["source"]).parent
        before = {p.relative_to(destination): (p.read_bytes(), p.stat().st_mtime_ns)
                  for p in destination.rglob("*") if p.is_file()}
        self.session._begin_terrain_bake(export)
        after = {p.relative_to(destination): (p.read_bytes(), p.stat().st_mtime_ns)
                 for p in destination.rglob("*") if p.is_file()}
        self.assertEqual(before, after)

    def test_bake_does_not_bind_model_before_normal_field_is_processed(self):
        self.session._begin_terrain_bake(self.export(adaptive=False))
        task = self.session.terrain_task
        self.session.phase_started = time.monotonic()
        self.session._product = Mock(return_value="catalogued asset")
        for key in ("modelProduct", "materialProduct"):
            cache = self.project / "Cache/pc" / task["prepared"][key]
            cache.parent.mkdir(parents=True, exist_ok=True)
            cache.write_bytes(b"processed fixture")
        self.lookup.reset_mock()
        self.session._tick_terrain_bake()
        self.lookup.assert_not_called()  # No binding or source commit before the DDS product exists.
        self.assertIs(self.session.terrain_task, task)
        self.assertEqual(self.data["modelProduct"], "previous.azmodel")

    def test_processed_field_commit_rebases_response_and_refreshes_overlap(self):
        # Execute the actual commit coordinator with inert application APIs.
        # Assert the product containing the field is bound before publication.
        self.session._begin_terrain_bake(self.export(adaptive=False))
        task = self.session.terrain_task
        self.session.phase_started = time.monotonic()
        self.session._product = Mock(side_effect=lambda path: path)
        for key in ("modelProduct", "materialProduct", "normalProduct"):
            cache = self.project / "Cache/pc" / task["prepared"][key]
            cache.parent.mkdir(parents=True, exist_ok=True)
            cache.write_bytes(b"processed fixture")
        owner = SimpleNamespace(Equal=lambda other: other == "terrain")
        self.lookup.return_value = (owner, "source", self.data)
        bindings = {("mesh", "Controller|Configuration|Model Asset"): "previous.azmodel",
                    ("material", "Default Material|Material Asset"): "previous.azmaterial"}
        def store(owner, component, updated, *args):
            self.data.clear(); self.data.update(updated)
        self.env.update(_ensure_named_component=lambda owner, name: (name.lower(), False),
            _get=lambda component, path: bindings[component, path],
            _set=lambda component, path, value: bindings.update({(component, path): value}),
            _relative_baked_row=lambda row, prior: row, to_o3de_row=lambda row: row,
            _sync_terrain_transform=Mock(), _set_vue_snapshot=store,
            editor=SimpleNamespace(VueTerrainSourceRequestBus=None), general=SimpleNamespace(save_level=Mock()))
        def publish():
            self.assertIsNone(self.session.terrain_task)
            self.assertEqual(self.data["normalProduct"], task["prepared"]["normalProduct"])
            self.session._status.assert_called_with("applied", "Vue terrain " + self.uid + " mesh bound")
            self.assertEqual(bindings["material", "Default Material|Material Asset"], task["prepared"]["materialProduct"])
        response, overlap = SimpleNamespace(rebase=Mock()), SimpleNamespace(invalidate=Mock(side_effect=publish))
        stubs = {"material_response_editor": response, "terrain_overlap_editor": overlap,
            "sunlight_editor": SimpleNamespace(document=Mock(return_value={}), lights=Mock()),
            "ambient_editor": SimpleNamespace(external_state=Mock())}
        with patch.dict(sys.modules, stubs):
            self.session._tick_terrain_bake()
        response.rebase.assert_called_once_with(self.uid)
        overlap.invalidate.assert_called_once()
        self.assertEqual(self.data["bakeStatus"], "Baked mesh")
        self.assertEqual(self.data["activeBank"], "b")


if __name__ == "__main__":
    unittest.main()
