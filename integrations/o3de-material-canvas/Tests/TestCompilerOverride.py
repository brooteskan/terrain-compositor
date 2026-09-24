"""Exercise the actual CMake patcher, including version and patch-drift failures."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ENGINE = Path(sys.argv.pop(1))
CMAKE = sys.argv.pop(1)
ROOT = Path(__file__).resolve().parents[1]
SOURCE = ENGINE / 'Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/MaterialGraphCompiler.cpp'


class CompilerOverrideTests(unittest.TestCase):
    def run_patcher(self, source, mutate_patch=False):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'EnginePatches').mkdir()
            shutil.copyfile(ROOT / 'EngineOverrides.cmake', root / 'EngineOverrides.cmake')
            patch = (ROOT / 'EnginePatches/MaterialGraphCompiler.cpp.patch').read_text()
            if mutate_patch:
                patch = patch.replace('graphParameters.SortProperties();', 'graphParameters.SortProperties(); // deliberately changed')
            (root / 'EnginePatches/MaterialGraphCompiler.cpp.patch').write_text(patch, newline='\n')
            (root / 'original.cpp').write_bytes(source)
            result = subprocess.run([CMAKE, '-DTC_CANVAS_COMPILER_SOURCE=' + str(root / 'original.cpp'),
                '-DTC_CANVAS_COMPILER_OUTPUT=' + str(root / 'output/compiler.cpp'), '-P', str(root / 'EngineOverrides.cmake')],
                capture_output=True, text=True)
            output = root / 'output/compiler.cpp'
            return result.returncode, result.stdout + result.stderr, output.read_bytes() if output.exists() else None

    def test_pinned_revision_and_crlf_normalize_identically(self):
        lf = SOURCE.read_text().replace('\r\n', '\n')
        code, log, expected = self.run_patcher(lf.encode())
        self.assertEqual(code, 0, log)
        code, log, actual = self.run_patcher(lf.replace('\n', '\r\n').encode())
        self.assertEqual(code, 0, log)
        self.assertEqual(expected, actual)

    def test_engine_drift_is_refused(self):
        code, log, output = self.run_patcher(SOURCE.read_bytes() + b'\n// upstream changed\n')
        self.assertNotEqual(code, 0)
        self.assertIn('pinned MaterialGraphCompiler input changed', log)
        self.assertIsNone(output)

    def test_patch_output_drift_is_refused(self):
        code, log, output = self.run_patcher(SOURCE.read_bytes(), mutate_patch=True)
        self.assertNotEqual(code, 0)
        self.assertIn('compiler override output changed', log)
        self.assertIsNone(output)


if __name__ == '__main__':
    unittest.main()
