import os as _os
from pathlib import Path as _Path
PROJECT = _Path(_os.environ['TG_PROJECT_ROOT']).resolve() if 'TG_PROJECT_ROOT' in _os.environ else next(
    p for p in _Path(__file__).resolve().parents if (p/'project.json').is_file())
"""Offline checks of signed RGBA16F terrain-normal DDS generation.

These tests do not establish Image Builder support, GPU format or performance.
"""

import importlib.util
import math
from pathlib import Path
import struct
import sys
import unittest
import xml.etree.ElementTree as ET


def load_normals():
    source = (PROJECT/'Gem') / "Source" / "Editor" / "VueLiveLink"

    def load(name, path):
        spec = importlib.util.spec_from_file_location(name, path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    previous = sys.modules.get("terrain_transform")
    sys.modules["terrain_transform"] = load("terrain_transform", source / "terrain_transform.py")
    try:
        return load("_terrain_normals_half_float_test", source / "terrain_normals.py")
    finally:
        if previous is None:
            sys.modules.pop("terrain_transform", None)
        else:
            sys.modules["terrain_transform"] = previous


NORMALS = load_normals()
IDENTITY = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]


def unit(values):
    length = math.sqrt(sum(value * value for value in values))
    return tuple(value / length for value in values)


def fbx_grid(normals):
    flattened = ",".join(str(value) for normal in normals for value in normal)
    return ("Normals: *12 { a: " + flattened + " }\n"
            "UV: *8 { a: 0,0,1,0,0,1,1,1 }\n"
            "NormalsIndex: *4 { a: 0,1,2,3 }\n"
            "UVIndex: *4 { a: 0,1,2,3 }\n")


class TerrainNormalHalfFloatTests(unittest.TestCase):
    def test_signed_pixel_and_alpha(self):
        pixel = NORMALS.normal_pixel((-1, 2, -3))
        self.assertEqual(len(pixel), 8)
        decoded = struct.unpack("<4e", pixel)
        for actual, expected in zip(decoded[:3], unit((-1, 2, -3))):
            self.assertAlmostEqual(actual, expected, delta=0.0005)
        self.assertLess(decoded[0], 0)
        self.assertLess(decoded[2], 0)
        self.assertEqual(decoded[3], 1.0)

    def test_rejects_degenerate_and_nonfinite_normals(self):
        for normal in ((0, 0, 0), (float("nan"), 0, 1),
                       (float("inf"), 0, 1), (1e308, 1e308, 1e308)):
            with self.subTest(normal=normal):
                with self.assertRaises(ValueError):
                    NORMALS.normal_pixel(normal)

    def test_header_pitch_format_size_and_horizontal_padding(self):
        pixels = [NORMALS.normal_pixel(normal) for normal in
                  ((1, 0, 1), (0, 1, 1), (-1, 0, 1),
                   (0, -1, 1), (1, 1, 1), (-1, -1, 1))]
        rows = [pixels[:3], pixels[3:]]
        dds, grid = NORMALS.texture(rows, 3, 2)
        self.assertEqual(grid, [3, 2])
        self.assertEqual(dds[:4], b"DDS ")
        self.assertEqual(struct.unpack_from("<7I", dds, 4),
                         (124, 0x100F, 2, 4, 32, 0, 0))
        self.assertEqual(struct.unpack_from("<II4s", dds, 76), (32, 4, b"DX10"))
        self.assertEqual(struct.unpack_from("<5I", dds, 128), (10, 3, 0, 1, 0))
        self.assertEqual(len(dds), 148 + 4 * 2 * 8)
        self.assertEqual(dds[148:180], b"".join(rows[0] + [rows[0][-1]]))
        self.assertEqual(dds[180:], b"".join(rows[1] + [rows[1][-1]]))
        self.assertEqual(len(dds) - 148, (4 * 2 * 16) // 2)

    def test_vertical_padding_repeats_final_row(self):
        rows = [[NORMALS.normal_pixel((x, y, 1)) for x in (-1, 1)]
                for y in (-1, 0, 1)]
        dds, grid = NORMALS.texture(rows, 2, 3)
        self.assertEqual(grid, [2, 3])
        self.assertEqual(struct.unpack_from("<III", dds, 12), (4, 2, 16))
        self.assertEqual(len(dds), 148 + 2 * 4 * 8)
        self.assertEqual(dds[-16:], b"".join(rows[-1]))
        self.assertEqual(dds[-32:-16], dds[-16:])

    def test_import_settings_request_half_float_without_mips(self):
        root = ET.fromstring(NORMALS.TEXTURE_SETTINGS)
        values = {node.attrib["field"]: node.attrib.get("value")
                  for node in root.iter("Class") if "field" in node.attrib}
        self.assertEqual(values["Preset"], "LUT_RGBA16F")
        self.assertEqual(values["SizeReduceLevel"], "0")
        self.assertEqual(values["EngineReduce"], "false")
        self.assertEqual(values["EnableMipmap"], "false")

    def test_regular_grid_retains_baked_axes_and_flipped_v(self):
        normals = [(0, 0, 1), (1, 0, 1), (0, 1, 1), (-1, -1, 1)]
        dds, grid = NORMALS.build(fbx_grid(normals), IDENTITY)
        self.assertEqual(grid, [2, 2])
        self.assertEqual(len(dds), 148 + 4 * 8)
        for pixel_index, source_index in enumerate((2, 3, 0, 1)):
            x, y, z = normals[source_index]
            expected = unit((-x, -y, z))
            actual = struct.unpack_from("<4e", dds, 148 + pixel_index * 8)
            for component, target in zip(actual[:3], expected):
                self.assertAlmostEqual(component, target, delta=0.0005)
            self.assertEqual(actual[3], 1.0)

    def test_adaptive_raster_uses_same_half_float_encoding(self):
        output = NORMALS.adaptive_field(
            [(0, 0, 1)], [(0, 0), (1, 0), (1, 1), (0, 1)],
            [0, 0, 0, 0], [0, 1, 2, 3], [0, 1, 2, -4], IDENTITY, size=4)
        self.assertIsNotNone(output)
        dds, grid = output
        self.assertEqual(grid, [4, 4])
        self.assertEqual(len(dds), 148 + 16 * 8)
        for offset in range(148, len(dds), 8):
            self.assertEqual(struct.unpack_from("<4e", dds, offset), (0, 0, 1, 1))

    def test_unsupported_inputs_still_fail_explicitly(self):
        self.assertIsNone(NORMALS.build("", IDENTITY))
        with self.assertRaises(ValueError):
            NORMALS.build("Normals: *2 { a: 1,2 }", IDENTITY)
        raw = fbx_grid([(0, 0, 1)] * 4)
        with self.assertRaises(ValueError):
            NORMALS.build(raw.replace("0,0,1,0,0,1,1,1", "nan,0,1,0,0,1,1,1"), IDENTITY)
        with self.assertRaises(ValueError):
            NORMALS.build(raw, [[0, 0, 0], [0, 1, 0], [0, 0, 1]])
        self.assertIsNone(NORMALS.adaptive_field(
            [(0, 0, 1)], [(0, 0), (1, 0), (1, 1)],
            [0, 0, 0], [0, 1, 2], [0, 1, -3], IDENTITY, size=1))

    def test_bounded_direction_quantization_error(self):
        worst_degrees = 0.0
        for index in range(2048):
            z = 1 - 2 * (index + 0.5) / 2048
            radius = math.sqrt(1 - z * z)
            angle = index * math.pi * (3 - math.sqrt(5))
            original = (radius * math.cos(angle), radius * math.sin(angle), z)
            decoded = struct.unpack("<4e", NORMALS.normal_pixel(original))
            restored = unit(decoded[:3])
            cosine = max(-1.0, min(1.0, sum(a * b for a, b in zip(original, restored))))
            worst_degrees = max(worst_degrees, math.degrees(math.acos(cosine)))
        self.assertLess(worst_degrees, 0.05)


if __name__ == "__main__":
    unittest.main()
