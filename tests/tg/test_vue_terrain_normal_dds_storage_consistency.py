import os as _os
from pathlib import Path as _Path
PROJECT = _Path(_os.environ['TG_PROJECT_ROOT']).resolve() if 'TG_PROJECT_ROOT' in _os.environ else next(
    p for p in _Path(__file__).resolve().parents if (p/'project.json').is_file())
"""Coupled DDS declaration/pitch/payload checks for terrain normals.

Offline serialization evidence only. Synthetic half-float fixtures validate the
oracle, not a staged writer, Image Builder compatibility or GPU performance.
"""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str((PROJECT/'Gem') / "Source/Editor/VueLiveLink"))
import terrain_normals


HEADER_BYTES = 148
# DXGI_FORMAT_R32G32B32A32_FLOAT and R16G16B16A16_FLOAT.
FORMATS = {2: ("<4f", 16), 10: ("<4e", 8)}
NORMALS = ((-3, 4, 0), (0, -1, 0), (1, 0, 0), (0, 0, -2))
EXPECTED = ((-0.6, 0.8, 0.0, 1.0), (0.0, -1.0, 0.0, 1.0),
            (1.0, 0.0, 0.0, 1.0), (0.0, 0.0, -1.0, 1.0))


def checked_layout(content):
    """Reject unsupported or inconsistent single-resource, unmipped DDS data."""
    if len(content) < HEADER_BYTES or content[:4] != b"DDS ":
        raise ValueError("Missing DDS header")
    if struct.unpack_from("<I", content, 4)[0] != 124:
        raise ValueError("Invalid DDS header size")
    if struct.unpack_from("<II4s", content, 76) != (32, 4, b"DX10"):
        raise ValueError("Expected DX10 pixel format")
    flags, height, width, pitch, depth, mipmaps = struct.unpack_from("<6I", content, 8)
    if flags != 0x100F or min(width, height) < 1 or depth != 0 or mipmaps != 0:
        raise ValueError("Unsupported DDS dimensions or flags")
    dxgi, dimension, misc, array_size, misc2 = struct.unpack_from("<5I", content, 128)
    if dxgi not in FORMATS or (dimension, misc, array_size, misc2) != (3, 0, 1, 0):
        raise ValueError("Unsupported DX10 resource")
    packing, pixel_bytes = FORMATS[dxgi]
    if pitch != width * pixel_bytes:
        raise ValueError("Pitch disagrees with declared pixel format")
    if len(content) != HEADER_BYTES + width * height * pixel_bytes:
        raise ValueError("Payload disagrees with declared dimensions and format")
    return dxgi, packing, pixel_bytes, width, height


def writer_sample():
    pixels = [terrain_normals.normal_pixel(n) for n in NORMALS]
    content, lattice = terrain_normals.texture([pixels[:2], pixels[2:]], 2, 2)
    return content, lattice, pixels


class TerrainNormalDDSStorageConsistencyTests(unittest.TestCase):
    def test_writer_two_by_two_declares_exact_signed_payload(self):
        content, lattice, pixels = writer_sample()
        dxgi, packing, pixel_bytes, width, height = checked_layout(content)
        self.assertEqual(lattice, [2, 2])
        self.assertEqual((width, height), (2, 2))
        self.assertEqual([len(p) for p in pixels], [pixel_bytes] * 4)
        expected_payload = b"".join(struct.pack(packing, *v) for v in EXPECTED)
        self.assertEqual(content[HEADER_BYTES:], expected_payload)
        self.assertEqual(len(content), {2: 212, 10: 180}[dxgi])
        decoded = list(struct.iter_unpack(packing, content[HEADER_BYTES:]))
        tolerance = 0.0003 if dxgi == 10 else 0.0000001
        for actual, expected in zip(decoded, EXPECTED):
            for value, target in zip(actual, expected):
                self.assertAlmostEqual(value, target, delta=tolerance)

    def test_oracle_accepts_consistent_float32_and_float16_fixtures(self):
        original, _, _ = writer_sample()
        for dxgi, (packing, pixel_bytes) in FORMATS.items():
            with self.subTest(dxgi=dxgi):
                header = bytearray(original[:HEADER_BYTES])
                struct.pack_into("<I", header, 20, 2 * pixel_bytes)
                struct.pack_into("<I", header, 128, dxgi)
                fixture = bytes(header) + b"".join(struct.pack(packing, *v) for v in EXPECTED)
                self.assertEqual(checked_layout(fixture),
                                 (dxgi, packing, pixel_bytes, 2, 2))
                self.assertEqual(len(fixture), HEADER_BYTES + 4 * pixel_bytes)

    def test_oracle_rejects_partial_format_migrations_and_corruption(self):
        original, _, _ = writer_sample()
        dxgi, _, pixel_bytes, _, _ = checked_layout(original)
        cases = {"truncated_header": original[:147],
                 "truncated_payload": original[:-1],
                 "extra_payload": original + b"\x00"}
        for name, offset, value in (
                ("format_only", 128, 10 if dxgi == 2 else 2),
                ("pitch_only", 20, 2 * pixel_bytes + 8),
                ("unsupported_format", 128, 0),
                ("wrong_dimension", 132, 4),
                ("array_resource", 140, 2)):
            changed = bytearray(original)
            struct.pack_into("<I", changed, offset, value)
            cases[name] = bytes(changed)
        changed = bytearray(original)
        struct.pack_into("<I", changed, 20, 4 * pixel_bytes)
        cases["pitch_and_format_without_payload"] = bytes(changed[:128]) + struct.pack(
            "<I", 10 if dxgi == 2 else 2) + bytes(changed[132:])
        for name, fixture in cases.items():
            with self.subTest(case=name):
                with self.assertRaises(ValueError):
                    checked_layout(fixture)


if __name__ == "__main__":
    unittest.main()
