"""Summarize matched editor captures; reject stale GPU frame envelopes."""
import argparse,collections,json,statistics
from pathlib import Path

def read(path):
    raw=path.read_text(encoding='utf-8')
    return json.loads(raw.replace('[,','[',1) if raw.startswith('[,') else raw)

def stats(values):
    values=sorted(values)
    if not values:return None
    return {'samples':len(values),'median_ms':statistics.median(values),'mean_ms':statistics.mean(values),
            'p95_ms':values[int(.95*(len(values)-1))],'min_ms':values[0],'max_ms':values[-1]}

def analyze(folder):
    original=read(folder/'results.json')
    result={k:original.get(k) for k in ('passed','camera','viewport','height_precision','original_pacing',
                                      'original_level_sha256','restoration','meshes','exception','restore_exception')}
    result['capture_directory']=str(folder);result['cases']=[]
    if (folder/'metadata.json').exists():result['metadata']=read(folder/'metadata.json')['ClassData']
    for case in original['cases']:
        filename=case.get('frame_metrics')
        if not filename or not (folder/filename).exists():continue
        groups=collections.defaultdict(list)
        for event in read(folder/filename):
            if event.get('ph')=='X':groups[event['name']].append(event['dur']/1000)
        row={'name':case['name'],'configuration':case['configuration'],
             'metrics':{name:stats(values) for name,values in groups.items()},
             'cache_before':case.get('cache_before'),'cache_after':case.get('cache_after')}
        cpu=row['metrics'].get('Engine Cpu Time');gpu=row['metrics'].get('Frame Gpu Time')
        row['gpu_frame_envelope_usable']=bool(gpu and cpu and gpu['median_ms']<=2*cpu['median_ms'])
        if not row['gpu_frame_envelope_usable']:
            row['gpu_frame_envelope_warning']='Disabled/recreated passes retain old timestamp results; do not use this frame envelope as GPU work time.'
        if cpu:row['approximate_engine_fps']=1000/cpu['median_ms']
        path=folder/(case['name']+'-statistics.json')
        if path.exists():
            entries=read(path)['ClassData']['pipelineStatisticsEntries']
            row['pipeline_statistics']=[e for e in entries if e['pipelineStatisticsResult']['primitiveCount'] and e['passName'] in ('Root','Forward','DepthPass','Shadows')]
        # Preserve flat-list ordinal: preview/main passes share display names.
        # Do not merge these names or add parent and child durations together.
        samples=collections.defaultdict(list);names={}
        for path in sorted(folder.glob(case['name']+'-passes-*.json')):
            for index,entry in enumerate(read(path)['ClassData']['timestampEntries']):
                names[index]=entry['passName'];samples[index].append(entry['timestampResultInNanoseconds']/1e6)
        row['pass_timestamps_by_ordinal']=[{'ordinal':index,'name':names[index],'timings':stats(values)} for index,values in sorted(samples.items())]
        result['cases'].append(row)
    result['cpu_traces']=[]
    for path in sorted(folder.glob('*-cpu-trace.json')):
        raw=path.read_bytes();encoding_warning=False
        try:text=raw.decode('utf-8')
        except UnicodeDecodeError:
            # This SDK capture sometimes retains invalid bytes in dynamic scope
            # names. Numeric ticks and stable ASCII scope names remain usable.
            text=raw.decode('utf-8',errors='replace');encoding_warning=True
        data=json.loads(text)['ClassData'];frequency=data['timeTicksPerSecond'];totals=collections.defaultdict(list)
        for event in data['cpuProfilingStatisticsSerializerEntries']:
            totals[(event['groupName'],event['regionName'])].append((event['endTick']-event['startTick'])/frequency*1000)
        result['cpu_traces'].append({'filename':path.name,'invalid_utf8_in_dynamic_names':encoding_warning,'time_ticks_per_second':frequency,'scopes':sorted([
            {'group':key[0],'name':key[1],'inclusive_sum_ms':sum(values),'timings':stats(values)} for key,values in totals.items()],key=lambda e:-e['inclusive_sum_ms'])}
        )
    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('folders',type=Path,nargs='+');parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    result={'runs':[analyze(folder) for folder in args.folders]}
    args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    for run in result['runs']:
        print(run['capture_directory'],'restored:',run['restoration'])
        for case in run['cases']:
            cpu=case['metrics'].get('Engine Cpu Time')
            if cpu:print(case['name'],round(cpu['median_ms'],3),'ms',round(case['approximate_engine_fps'],2),'engine FPS','GPU envelope usable:',case['gpu_frame_envelope_usable'])
