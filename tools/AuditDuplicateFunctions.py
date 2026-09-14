#!/usr/bin/env python3
"""Conservative lexical function audit; near matches require human semantic review.

Scan first-party C++/headers/shaders and complete bodies in maintained patch hunks.
Comments and whitespace are ignored. Identifiers/literals are preserved for exact
matches; --near also compares consistent identifier renamings. This is not a C++
parser or proof of semantic uniqueness (see docs/ProductionFunctionAudit.md).
"""

import argparse
from collections import defaultdict
from dataclasses import dataclass
from difflib import SequenceMatcher
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
EXTENSIONS = {'.cpp', '.h', '.hpp', '.inl', '.azsl', '.azsli', '.hlsl', '.hlsli', '.patch'}
TOKEN = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|R"([^ ()\\\t\r\n]*)\([\s\S]*?\)\1"|'
                   r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_]\w*|'
                   r'\d+(?:\.\d*)?(?:[eE][+-]?\d+)?[\w]*|::|->|&&|\|\||==|!=|<=|>=|<<|>>|\+\+|--|\S')
KEYWORDS = set('if else for while do switch case default return break continue const constexpr static inline '
               'void bool float double int unsigned signed long short char auto size_t true false nullptr '
               'class struct template typename public private protected virtual override final noexcept '
               'sizeof alignof decltype new delete this using namespace throw try catch'.split())


def git(*args):
    return subprocess.check_output(['git', '-c', f'safe.directory={ROOT.as_posix()}', '-c',
        f'core.excludesFile={ROOT.as_posix()}/.gitignore', '-C', str(ROOT), *args])


def in_scope(path):
    return (path.startswith('integrations/o3de/') and '/Code/Tests/' not in path and
            Path(path).suffix in EXTENSIONS)


def fragments(path, text):
    if not path.endswith('.patch'):
        yield text, 0
        return
    # Do not join noncontiguous hunks or deleted lines into invented functions.
    lines, first = [], 0
    for number, line in enumerate(text.splitlines(), 1):
        if line.startswith('@@') or line.startswith('diff --git'):
            if lines:
                yield '\n'.join(lines), first
            lines, first = [], number
        elif line.startswith(('+', ' ')) and not line.startswith('+++'):
            lines.append(line[1:])
        elif lines:
            lines.append('') # Keep patch line locations stable for deleted lines.
    if lines:
        yield '\n'.join(lines), first


@dataclass
class Function:
    path: str
    line: int
    name: str
    body: tuple

    def location(self):
        return f'{self.path}:{self.line} {self.name}'


def functions(path, text):
    for fragment, offset in fragments(path, text):
        matches = [m for m in TOKEN.finditer(fragment) if not m.group().startswith(('//', '/*'))]
        values = [m.group() for m in matches]
        pairs, stack = {}, []
        for i, value in enumerate(values):
            if value in ('(', '{', '['):
                stack.append(i)
            elif value in (')', '}', ']'):
                if stack and values[stack[-1]] == {')': '(', '}': '{', ']': '['}[value]:
                    opening = stack.pop()
                    pairs[opening] = i
                    pairs[i] = opening
                else:
                    stack.clear() # Incomplete patch hunk.
        for start, value in enumerate(values):
            if value != '{' or start not in pairs:
                continue
            j = start - 1
            while j >= 0 and values[j] in ('const', 'override', 'final', 'noexcept', 'mutable', '&', '&&'):
                j -= 1
            # Lambdas are maintained functions too, including parameterless captures.
            capture = j if j >= 0 and values[j] == ']' else None
            if j >= 0 and values[j] == ')' and j in pairs and pairs[j] > 0 and values[pairs[j] - 1] == ']':
                capture = pairs[j] - 1
            if capture is not None and capture in pairs:
                # Array initialization is not a callable. Captures follow an assignment,
                # argument delimiter, return, or another expression operator.
                before = pairs[capture] - 1
                if before < 0 or values[before] in ('=', '(', ',', 'return', '{', ':'):
                    line = offset + fragment.count('\n', 0, matches[start].start()) + 1
                    yield Function(path, line, '<lambda>', tuple(values[start + 1:pairs[start]]))
                    continue
            if j < 0 or values[j] != ')' or j not in pairs:
                continue
            opening = pairs[j]
            if opening < 1:
                continue
            name = values[opening - 1]
            if name in ('if', 'for', 'while', 'switch', 'catch', 'AZ_RTTI', 'AZ_COMPONENT') or name == ']':
                continue
            if not re.fullmatch(r'[A-Za-z_]\w*', name):
                if opening >= 2 and values[opening - 2] == 'operator':
                    name = 'operator' + name
                else:
                    continue
            k = opening - 2
            while k >= 1 and values[k] == '::':
                name = values[k - 1] + '::' + name
                k -= 2
            yield Function(path, offset + fragment.count('\n', 0, matches[start].start()) + 1,
                           name, tuple(values[start + 1:pairs[start]]))


def renamed(body):
    names = {}
    return tuple(names.setdefault(t, f'id{len(names)}') if re.fullmatch(r'[A-Za-z_]\w*', t)
                 and t not in KEYWORDS else t for t in body)


def digest(body):
    return hashlib.sha256(' '.join(body).encode()).hexdigest()[:16]


def audit(items, near=False):
    groups = defaultdict(list)
    for f in items:
        if f.body: # Empty overrides carry no maintained implementation.
            groups[f.body].append(f)
    exact = [{'hash': digest(body), 'tokens': len(body), 'locations': [f.location() for f in group]}
             for body, group in groups.items() if len(group) > 1]
    similar = []
    if near:
        candidates = [(f, renamed(f.body)) for f in items if len(f.body) >= 30]
        for i, (a, ab) in enumerate(candidates):
            for b, bb in candidates[i+1:]:
                if a.body == b.body or min(len(ab), len(bb)) / max(len(ab), len(bb)) < .72:
                    continue
                matcher = SequenceMatcher(None, ab, bb, autojunk=False)
                if matcher.quick_ratio() < .82:
                    continue
                ratio = matcher.ratio()
                if ratio >= .82:
                    similar.append({'similarity': round(ratio, 3), 'locations': [a.location(), b.location()]})
    return exact, sorted(similar, key=lambda pair: -pair['similarity'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--revision', help='audit committed blobs instead of the working tree')
    parser.add_argument('--near', action='store_true')
    parser.add_argument('--json', type=Path)
    parser.add_argument('--check', action='store_true', help='fail on exact groups without a matching reviewed disposition')
    args = parser.parse_args()
    paths = (git('ls-tree', '-rz', '--name-only', args.revision) if args.revision else
             git('ls-files', '-z', '--cached', '--others', '--exclude-standard')).decode().split('\0')
    items, files = [], []
    for path in sorted(set(filter(in_scope, paths))):
        text = git('show', f'{args.revision}:{path}').decode() if args.revision else (ROOT / path).read_text()
        files.append(path)
        items.extend(functions(path, text))
    exact, near = audit(items, args.near)
    manifest = ROOT / 'tools/ProductionFunctionAuditExceptions.json'
    reviewed = json.loads(manifest.read_text()) if manifest.exists() else {}
    for group in exact:
        exception = reviewed.get(group['hash'], {})
        # A new caller must be reviewed too; names allow source lines to move.
        locations = [re.sub(r':\d+ ', ' ', location) for location in group['locations']]
        group['disposition'] = exception.get('reason') if sorted(locations) == sorted(exception.get('locations', [])) else None
    report = {'scope': args.revision or 'working tree', 'files': files,
              'functions': [f.location() for f in items], 'exact': exact, 'near': near}
    if args.json:
        args.json.write_text(json.dumps(report, indent=2) + '\n')
    unresolved = [g for g in exact if not g['disposition']]
    print(f'{len(files)} production inputs; {len(items)} recognized bodies; '
          f'{len(exact)} exact groups ({len(unresolved)} unreviewed); {len(near)} near pairs.')
    for group in unresolved:
        print(group['hash'], f"({group['tokens']} tokens)")
        for location in group['locations']:
            print(' ', location)
    return bool(args.check and unresolved)


if __name__ == '__main__':
    raise SystemExit(main())
