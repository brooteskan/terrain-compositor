"""Summarize ProfileTerrainGpu.py output without summing nested GPU passes."""
import argparse
from collections import defaultdict
import csv
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import statistics


def distribution(values):
    values = sorted(values)
    if not values:
        return {'n': 0}
    return {'n': len(values), 'median': statistics.median(values),
            'p95': values[max(0, math.ceil(len(values) * .95) - 1)],
            'min': values[0], 'max': values[-1]}


def analyze(folder, utilization=None):
    result = json.loads((folder / 'results.json').read_text())
    utilization_rows = []
    if utilization:
        with utilization.open() as source:
            for row in csv.DictReader(source):
                if row['engine'].lower().endswith('engtype_3d'):
                    stamp = datetime.strptime(row['utc'][:26], '%Y-%m-%dT%H:%M:%S.%f').replace(tzinfo=timezone.utc).timestamp()
                    utilization_rows.append((stamp, float(row['utilization_percent'])))
    summary = {'passed': result['passed'], 'config': result['config'], 'cases': [],
               'units': 'milliseconds; pipeline statistics are invocation/primitive counts',
               'gpu_frame_method': 'Atom Frame Gpu Time timestamp envelope; not a sum of pass durations',
               'pass_method': 'maximum duration for each pass name per capture (main scene versus zero-cost preview pipelines)',
               'limitations': ['GPU captures and CPU wall intervals are separate measurements.',
                               'Statistics are one diagnostic frame per case.',
                               'Repeated pass names lose hierarchy in the engine serializer; raw entries are retained.']}
    for case in result['cases']:
        if not all(key in case for key in ('frame_metrics', 'statistics', 'timestamps')):
            continue
        frames = defaultdict(list)
        raw = (folder / case['frame_metrics']).read_text()
        leading_comma = raw.startswith('[,')
        for event in json.loads(raw.replace('[,', '[', 1) if leading_comma else raw):
            if event.get('ph') == 'X' and event.get('dur', 0) > 0:
                frames[event['name']].append(event['dur'] / 1000)
        if not frames['Frame Gpu Time']:
            raise ValueError('Missing nonzero GPU frame measurements: ' + case['case']['name'])
        passes = defaultdict(list)
        for name in case['timestamps']:
            sample = defaultdict(list)
            data = json.loads((folder / name).read_text())['ClassData']['timestampEntries']
            for entry in data:
                sample[entry['passName']].append(entry['timestampResultInNanoseconds'] / 1e6)
            for key, values in sample.items():
                passes[key].append(max(values))
        raw_stats = json.loads((folder / case['statistics']).read_text())['ClassData']['pipelineStatisticsEntries']
        start = case.get('measurement_start_utc_epoch')
        busy = [value for stamp, value in utilization_rows
                if start is not None and start + 1 <= stamp <= start + result['config'].get('wall_seconds', 3) - .2]
        summary['cases'].append({
            'case': case['case'], 'metrics_leading_comma_repaired_for_read': leading_comma,
            'frame_ms': {key: distribution(values) for key, values in frames.items()},
            'editor_wall_ms': distribution(case['editor_wall_intervals_ms']),
            'process_3d_utilization_percent': distribution(busy),
            'passes_ms': {key: distribution(values) for key, values in passes.items() if max(values) > 0},
            'pipeline_statistics': raw_stats,
        })
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--utilization', type=Path, help='Windows GPU Engine CSV sampled during this run')
    args = parser.parse_args()
    summary = analyze(args.directory, args.utilization)
    (args.directory / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print('case,gpu_frame_median_ms,gpu_frame_p95_ms,depth_median_ms,forward_median_ms,editor_wall_median_ms')
    for case in summary['cases']:
        passes = case['passes_ms']
        gpu = case['frame_ms']['Frame Gpu Time']
        print(','.join(str(value) for value in (
            case['case']['name'], gpu['median'], gpu['p95'],
            passes.get('DepthPass', {}).get('median', 0), passes.get('Forward', {}).get('median', 0),
            case['editor_wall_ms']['median'])))


if __name__ == '__main__':
    main()
