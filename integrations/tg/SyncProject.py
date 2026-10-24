"""Synchronize gem-owned TG consumer files without overwriting unknown edits."""
import argparse
import hashlib
import json
from pathlib import Path


def digest(data, binary=False):
    return hashlib.sha256(data if binary else data.replace(b'\r\n', b'\n')).hexdigest()


def json_digest(data):
    return digest(json.dumps(json.loads(data),sort_keys=True,separators=(',',':')).encode())


def sync(project, bundle=None, check=False):
    project = Path(project).resolve()
    bundle = Path(bundle or Path(__file__).resolve().parent).resolve()
    manifest = json.loads((bundle/'manifest.json').read_text(encoding='utf-8'))
    pending = []
    conflicts = []
    # Preflight every file before writing any of them. Never silently discard
    # consumer edits; changes to managed files belong in the submodule source.
    for entry in manifest['files']:
        source = (bundle/entry['source']).resolve()
        target = (project/entry['path']).resolve()
        if not source.is_relative_to(bundle) or not target.is_relative_to(project):
            raise ValueError('Sync path escapes its root: '+entry['path'])
        data = source.read_bytes()
        expected = digest(data, entry['binary'])
        current = digest(target.read_bytes(), entry['binary']) if target.exists() else None
        if current == expected:
            continue
        accepted=current in entry['accepted_sha256']
        if current is not None and not accepted and entry.get('accepted_json_sha256'):
            try:
                accepted=json_digest(target.read_bytes()) in entry['accepted_json_sha256']
            except (ValueError,UnicodeError):
                pass
        if current is not None and not accepted:
            conflicts.append(entry['path'])
        else:
            pending.append((target, data))
    if conflicts:
        raise RuntimeError('TG has local edits to gem-managed files; reconcile them in '
                           'terrain-compositor/integrations/tg before syncing: '+', '.join(conflicts))
    if not check:
        for target, data in pending:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    return [str(path.relative_to(project)) for path, _ in pending]


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', type=Path, required=True)
    parser.add_argument('--check', action='store_true', help='Report pending updates without writing')
    args = parser.parse_args()
    changed = sync(args.project, check=args.check)
    print(json.dumps({'project':str(args.project), 'pending' if args.check else 'updated':changed}, indent=2))
    if args.check and changed:
        raise SystemExit(1)
