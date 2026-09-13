"""Summarize optional per-sector CPU diagnostics; never treat worker totals as frame times."""
import argparse
import json
import re
from pathlib import Path


def summarize(text):
    rows = []
    for line in text.splitlines():
        if 'TerrainKernel)' not in line and 'TerrainKernel:' not in line and '(TerrainKernel)' not in line:
            continue
        fields = dict(re.findall(r'([a-z-]+)=([0-9.]+)', line))
        if 'source-us' in fields and 'samples' in fields:
            rows.append({key: float(value) if '.' in value else int(value) for key, value in fields.items()})
    totals = {key: sum(row.get(key, 0) for row in rows) for key in rows[0]} if rows else {}
    peaks = {key: max(row.get(key, 0) for row in rows) for key in rows[0]} if rows else {}
    samples = totals.get('samples', 0)
    return {'sector_records': len(rows), 'inclusive_worker_totals': totals, 'sector_peaks': peaks,
        'source_ns_per_evaluation': totals.get('source-us', 0) * 1000 / samples if samples else None,
        'note': 'Diagnostic worker CPU intervals overlap across workers and are not frame times. Scratch bytes use the per-sector maximum, not the sum.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = summarize(args.log.read_text(encoding='utf-8-sig', errors='replace'))
    encoded = json.dumps(result, indent=2)
    if args.output:
        args.output.write_text(encoded)
    print(encoded)


if __name__ == '__main__':
    main()
