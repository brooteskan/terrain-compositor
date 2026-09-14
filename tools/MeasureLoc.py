#!/usr/bin/env python3
"""Count physical lines, including blanks/comments, without checkout/filter effects."""

import argparse
from collections import Counter
import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def git(*args):
    return subprocess.check_output(
        ["git", "-c", f"safe.directory={ROOT.as_posix()}", "-C", str(ROOT), *args]
    )


def category(path):
    if path.endswith(".md") or path.startswith("docs/"):
        return "documentation"
    if "/EnginePatches/" in path or "/EngineOverrides/" in path:
        return "engine patches/overrides"
    if "/Code/Tests/" in path:
        return "tests"
    if "/Code/Include/" in path:
        return "public headers"
    if "/Code/Source/" in path and Path(path).suffix in {".cpp", ".h", ".inl"}:
        return "production C++"
    if "/Assets/" in path:
        return "shaders/assets"
    if path in {"LICENSE", "NOTICE"}:
        return "license/notice"
    return "build/tooling"


def measure(revision, worktree):
    commit = git("rev-parse", f"{revision}^{{commit}}").decode().strip()
    if worktree:
        paths = git("ls-files", "-z", "--cached", "--others", "--exclude-standard")
    else:
        # Gitlinks identify dependency commits, not first-party blobs. Trying to
        # read one with `git show commit:path` fails on recursive checkouts.
        entries = git("ls-tree", "-rz", commit).decode().split("\0")
        paths = "\0".join(entry.split("\t", 1)[1] for entry in entries if entry and entry.split()[1] == "blob").encode()
    files = []
    binary = []
    for path in sorted(set(paths.decode().strip("\0").split("\0"))):
        if not path or (worktree and not (ROOT / path).is_file()):
            continue
        data = (ROOT / path).read_bytes() if worktree else git("show", f"{commit}:{path}")
        if b"\0" in data:
            binary.append(path)
            continue
        lines = data.count(b"\n") + int(bool(data) and not data.endswith(b"\n"))
        files.append({"path": path, "category": category(path), "lines": lines})
    totals = Counter()
    for file in files:
        totals[file["category"]] += file["lines"]
    return {
        "revision": commit,
        "scope": "worktree (tracked + unignored untracked)" if worktree else "committed blobs",
        "count": "physical lines including blanks/comments; final unterminated line counts",
        "categories": dict(sorted(totals.items())),
        "total_text": sum(totals.values()),
        "maintained_text": sum(totals.values()) - totals["license/notice"],
        "first_party_cpp": totals["production C++"] + totals["public headers"],
        "binary_files_excluded": binary,
        "files": files,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", default="HEAD")
    parser.add_argument("--worktree", action="store_true")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    if args.worktree and args.revision != "HEAD":
        parser.error("--worktree cannot be combined with a historical revision")
    report = measure(args.revision, args.worktree)
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(f"{report['revision']} | {report['scope']}")
        print(report["count"])
        for name, lines in report["categories"].items():
            print(f"{name:28} {lines:7,d}")
        for name in ("total_text", "maintained_text", "first_party_cpp"):
            print(f"{name:28} {report[name]:7,d}")
        print(f"Binary files excluded: {len(report['binary_files_excluded'])}")


if __name__ == "__main__":
    main()
