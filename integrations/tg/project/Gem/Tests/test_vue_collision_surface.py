"""Geometry fixtures for the inspector's background collision surface worker."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT/'Gem/Source/Editor/VueLiveLink'))
spec = importlib.util.spec_from_file_location('prepare_vue_composition', PROJECT/'Editor/Scripts/prepare_vue_composition.py')
worker = importlib.util.module_from_spec(spec)
try:
    spec.loader.exec_module(worker)
    AVAILABLE = True
except ImportError:
    AVAILABLE = False


@unittest.skipUnless(AVAILABLE, 'Requires the Vue worker Python with numpy and Pillow')
class CollisionSurfaceWorkerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.original = worker.PROJECT, worker.OUTPUT
        worker.PROJECT, worker.OUTPUT = self.root, self.root/'prepared'
        (self.root/'Assets').mkdir()

    def tearDown(self):
        worker.PROJECT, worker.OUTPUT = self.original
        self.temporary.cleanup()

    def source(self, name, height, translation=None):
        raw = ('Vertices: *12 { a: 0,0,H,16,0,H,0,16,H,16,16,H }\n'
               'PolygonVertexIndex: *6 { a: 0,1,-3,1,3,-3 }').replace('H', str(height))
        path = self.root/'Assets'/(name+'.fbx')
        path.write_text(raw)
        return dict(name=name, entity=name, source='Assets/'+name+'.fbx',
            sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
            matrix=[[1,0,0],[0,1,0],[0,0,1]], translation=translation or [0,0,0])

    def prepare(self, rows):
        with contextlib.redirect_stdout(io.StringIO()): worker.prepare(32, dict(sources=rows))
        return json.loads((worker.OUTPUT/'VueTerrainSurface.json').read_text())

    def test_flat_zero_surface_is_finite_and_has_unsigned_16_bit_samples(self):
        info = self.prepare([self.source('flat', 0)])
        with worker.Image.open(worker.OUTPUT/'VueTerrainSurface.tif') as image:
            pixels = worker.np.asarray(image)
        self.assertEqual(pixels.dtype, worker.np.uint16)
        self.assertEqual(int(pixels.max()), 0)
        self.assertEqual(info['heightScale'], 1)

    def test_world_translation_and_topmost_overlap_are_preserved(self):
        info = self.prepare([self.source('low', 2), self.source('high', 3, [8,0,4])])
        self.assertAlmostEqual(info['heightScale'], 7)
        self.assertLess(info['lower'][0], 0)
        self.assertGreater(info['upper'][0], 24)
        with worker.Image.open(worker.OUTPUT/'VueTerrainSurface.tif') as image:
            pixels = worker.np.asarray(image)
        center_row = pixels[len(pixels)//2]
        self.assertGreater(int(center_row.max()), 65000)
        self.assertEqual(int(center_row[0]), 0)
        self.assertEqual(int(center_row[-1]), 0)
        self.assertEqual(len(info['sources']), 2)

    def test_changed_geometry_is_rejected_before_publishing_files(self):
        source = self.source('changed', 2)
        (self.root/source['source']).write_text('changed during refresh')
        with self.assertRaisesRegex(ValueError, 'changed during refresh'): self.prepare([source])
        self.assertFalse(worker.OUTPUT.exists())

    def test_source_outside_assets_is_rejected(self):
        source = self.source('outside', 2)
        source['source'] = '../outside.fbx'
        with self.assertRaisesRegex(ValueError, 'inside project Assets'): self.prepare([source])
        self.assertFalse(worker.OUTPUT.exists())


if __name__ == '__main__': unittest.main()
