import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

GEM=Path(__file__).resolve().parents[2]
BUNDLE=GEM/'integrations/tg'
spec=importlib.util.spec_from_file_location('tg_sync',BUNDLE/'SyncProject.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)


class ProjectSyncTests(unittest.TestCase):
    def test_fresh_project_installs_every_managed_file_and_is_idempotent(self):
        with tempfile.TemporaryDirectory() as folder:
            project=Path(folder)
            changed=module.sync(project)
            manifest=json.loads((BUNDLE/'manifest.json').read_text())
            self.assertEqual(len(changed),len(manifest['files']))
            for entry in manifest['files']:
                self.assertEqual((project/entry['path']).read_bytes(),(BUNDLE/entry['source']).read_bytes())
            self.assertEqual(module.sync(project),[])

    def test_unknown_edit_is_preserved_and_preflight_prevents_partial_updates(self):
        with tempfile.TemporaryDirectory() as folder:
            project=Path(folder)
            manifest=json.loads((BUNDLE/'manifest.json').read_text())
            conflict=project/manifest['files'][-1]['path']
            conflict.parent.mkdir(parents=True);conflict.write_text('user edit')
            with self.assertRaisesRegex(RuntimeError,'local edits'):
                module.sync(project)
            self.assertEqual(conflict.read_text(),'user edit')
            self.assertFalse((project/manifest['files'][0]['path']).exists())

    def test_previous_source_revision_updates_and_check_is_read_only(self):
        with tempfile.TemporaryDirectory() as folder:
            project=Path(folder)/'consumer';bundle=Path(folder)/'bundle'
            project.mkdir();bundle.mkdir()
            (project/'source.py').write_bytes(b'old\r\n')
            (bundle/'source.py').write_bytes(b'new\n')
            (bundle/'manifest.json').write_text(json.dumps({'files':[{'path':'source.py','source':'source.py',
                'binary':False,'accepted_sha256':[module.digest(b'old\n')]}]}))
            self.assertEqual(module.sync(project,bundle,check=True),['source.py'])
            self.assertEqual((project/'source.py').read_bytes(),b'old\r\n')
            module.sync(project,bundle)
            self.assertEqual((project/'source.py').read_bytes(),b'new\n')

    def test_paths_cannot_escape_consumer_root(self):
        with tempfile.TemporaryDirectory() as folder:
            bundle=Path(folder)/'bundle';bundle.mkdir()
            (bundle/'manifest.json').write_text(json.dumps({'files':[{'path':'../outside','source':'manifest.json'}]}))
            with self.assertRaisesRegex(ValueError,'escapes'):
                module.sync(Path(folder)/'consumer',bundle)


if __name__=='__main__':unittest.main()
