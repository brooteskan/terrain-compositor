import os as _os
from pathlib import Path as _Path
PROJECT = _Path(_os.environ['TG_PROJECT_ROOT']).resolve() if 'TG_PROJECT_ROOT' in _os.environ else next(
    p for p in _Path(__file__).resolve().parents if (p/'project.json').is_file())
"""Byte-level contract for the current signed RGBA16F terrain normal writer.

These are offline serialization tests, not Image Builder or GPU validation.
An intentional format migration must update both the writer and this contract.
"""
import math
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str((PROJECT/'Gem') / "Source/Editor/VueLiveLink"))
import terrain_normals


HEADER_BYTES = 148
PIXEL_BYTES = 8


def sample_rows(nx, ny):
    return [[terrain_normals.normal_pixel((x - 2, y - 2, 3))
             for x in range(nx)] for y in range(ny)]


class TerrainNormalDDSContractTests(unittest.TestCase):
    def test_signed_oblique_normal_has_known_little_endian_bytes(self):
        # (-3, 4, 0) normalizes to (-0.6, 0.8, 0), not UNORM encoding.
        actual = terrain_normals.normal_pixel((-3, 4, 0))
        self.assertEqual(actual, bytes.fromhex("cdb8663a0000003c"))
        self.assertEqual(len(actual), PIXEL_BYTES)
        self.assertEqual(terrain_normals.normal_pixel((-1, 0, 0)),
                         bytes.fromhex("00bc00000000003c"))

    def test_normalization_preserves_sign_and_positive_scale_invariance(self):
        original = (-2, 3, -6)
        packed = terrain_normals.normal_pixel(original)
        self.assertEqual(packed, terrain_normals.normal_pixel(tuple(v * 8 for v in original)))
        values = struct.unpack("<4e", packed)
        for actual, source in zip(values[:3], original):
            self.assertAlmostEqual(actual, source / 7, delta=0.0005)
        self.assertAlmostEqual(math.sqrt(sum(v * v for v in values[:3])), 1, delta=0.0005)
        self.assertEqual(values[3], 1.0)

    def test_degenerate_and_nonfinite_normals_are_rejected(self):
        invalid = ((0, 0, 0), (1e-12, 0, 0), (math.inf, 0, 1),
                   (-math.inf, 1, 0), (math.nan, 0, 1))
        for normal in invalid:
            with self.subTest(normal=normal):
                with self.assertRaisesRegex(ValueError, "Degenerate terrain normal"):
                    terrain_normals.normal_pixel(normal)

    def test_header_declares_rgba16f_pitch_and_single_2d_resource(self):
        content, lattice = terrain_normals.texture(sample_rows(3, 3), 3, 3)
        self.assertEqual(lattice, [3, 3])
        self.assertEqual(content[:4], b"DDS ")
        self.assertEqual(struct.unpack_from("<7I", content, 4),
                         (124, 0x100F, 4, 4, 4 * PIXEL_BYTES, 0, 0))
        self.assertEqual(content[32:76], bytes(44))
        self.assertEqual(struct.unpack_from("<II4s5I", content, 76),
                         (32, 4, b"DX10", 0, 0, 0, 0, 0))
        self.assertEqual(struct.unpack_from("<5I", content, 108), (0x1000, 0, 0, 0, 0))
        # DXGI_FORMAT_R16G16B16A16_FLOAT, TEXTURE2D, one array element.
        self.assertEqual(struct.unpack_from("<5I", content, 128), (10, 3, 0, 1, 0))
        self.assertEqual(len(content), HEADER_BYTES + 4 * 4 * PIXEL_BYTES)

    def test_padding_repeats_last_column_and_last_row_without_changing_bytes(self):
        rows = sample_rows(3, 3)
        before = [list(row) for row in rows]
        content, lattice = terrain_normals.texture(rows, 3, 3)
        self.assertEqual(lattice, [3, 3])
        self.assertEqual(rows, before)
        for y in range(4):
            for x in range(4):
                offset = HEADER_BYTES + (y * 4 + x) * PIXEL_BYTES
                self.assertEqual(content[offset:offset + PIXEL_BYTES],
                                 rows[min(y, 2)][min(x, 2)], (x, y))

    def test_rectangular_lattice_keeps_independent_padded_dimensions(self):
        rows = sample_rows(3, 2)
        content, lattice = terrain_normals.texture(rows, 3, 2)
        self.assertEqual(lattice, [3, 2])
        self.assertEqual(struct.unpack_from("<3I", content, 12), (2, 4, 4 * PIXEL_BYTES))
        self.assertEqual(len(content), HEADER_BYTES + 4 * 2 * PIXEL_BYTES)
        second_row = HEADER_BYTES + 4 * PIXEL_BYTES
        self.assertEqual(content[second_row:second_row + PIXEL_BYTES], rows[1][0])
        self.assertEqual(content[-PIXEL_BYTES:], rows[1][-1])


if __name__ == "__main__":
    unittest.main()
