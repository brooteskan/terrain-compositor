import importlib.util
from pathlib import Path
import sys
import unittest

SPEC = importlib.util.spec_from_file_location('duplicate_audit', Path(__file__).resolve().parents[1] / 'AuditDuplicateFunctions.py')
audit = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = audit
SPEC.loader.exec_module(audit)


class DuplicateAuditTests(unittest.TestCase):
    def parse(self, source, path='integrations/o3de/Code/Source/example.cpp'):
        return list(audit.functions(path, source))

    def test_exact_ignores_comments_whitespace_and_function_name(self):
        exact, _ = audit.audit(self.parse('int One(int x) { x += 1; return x; }\n'
                                         'int Two(int x) { /* } misleading */ x+=1; return x; }'))
        self.assertEqual(len(exact), 1)
        self.assertEqual(len(exact[0]['locations']), 2)

    def test_literals_remain_significant_and_braces_in_strings_are_safe(self):
        items = self.parse('void A() { Log("}"); Done(); } void B() { Log("{"); Done(); }')
        self.assertEqual(len(items), 2)
        self.assertEqual(audit.audit(items)[0], [])

    def test_nested_blocks_are_not_functions(self):
        items = self.parse('bool A(int x) const noexcept { if (x) { while (x) { --x; } } return true; }')
        self.assertEqual([f.name for f in items], ['A'])

    def test_lambdas_and_operator_bodies_are_included(self):
        items = self.parse('auto a = [this](int x) mutable { Work(x); Done(); }; '
                           'auto b = [this] { Work(x); Done(); }; '
                           'bool operator==(const X& other) const { return id == other.id; }')
        self.assertEqual(len(items), 3)
        self.assertEqual(len(audit.audit(items)[0]), 1)

    def test_patch_hunks_never_create_a_body_across_missing_context(self):
        patch = '--- a/a.cpp\n+++ b/a.cpp\n@@ -1,2 +1,2 @@\n int A() {\n+ Do();\n@@ -40,2 +40,2 @@\n+ Done();\n }\n'
        self.assertEqual(self.parse(patch, 'integrations/o3de/EnginePatches/a.cpp.patch'), [])
        complete = '--- a/a.cpp\n+++ b/a.cpp\n@@ -1,2 +1,2 @@\n-int Old() { return 1; }\n+int New() { return 2; }\n'
        self.assertEqual([f.name for f in self.parse(complete, 'a.patch')], ['New'])

    def test_scope_excludes_tests_dependencies_and_generated_outputs(self):
        self.assertTrue(audit.in_scope('integrations/o3de/Assets/Shaders/Terrain/TerrainCommon.azsli'))
        self.assertTrue(audit.in_scope('integrations/o3de/EngineOverrides/TerrainRaycast/example.h'))
        for path in ['external/a.cpp', 'tests/coverage/a.cpp', 'build/a.cpp', 'integrations/o3de/Code/Tests/a.cpp']:
            self.assertFalse(audit.in_scope(path))

    def test_near_normalizes_identifiers_without_losing_repeated_names(self):
        self.assertEqual(audit.renamed(('left', '+', 'left', '*', 'right')), audit.renamed(('a', '+', 'a', '*', 'b')))
        self.assertNotEqual(audit.renamed(('left', '+', 'left')), audit.renamed(('a', '+', 'b')))


if __name__ == '__main__':
    unittest.main()
