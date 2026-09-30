"""Collect AMD draw counters using qrenderdoc --python; TC_RDC selects a capture.

Writes <capture>.amd.json. Replay timings are diagnostic, not native frame time.
Uses vendor counters to avoid the built-in D3D12 stable-power-state requirement.
Run in a dedicated qrenderdoc process: this script closes it when finished.
"""
import json
import os
import traceback
import renderdoc as rd

filename = os.environ['TC_RDC']
result = {'capture': filename, 'passed': False}
cap = ctrl = None
try:
    cap = rd.OpenCaptureFile()
    status = cap.OpenFile(filename, '', None)
    result['open_status'] = str(status)
    assert status == rd.ResultCode.Succeeded, str(status)
    status, ctrl = cap.OpenCapture(rd.ReplayOptions(), None)
    result['replay_status'] = str(status)
    assert status == rd.ResultCode.Succeeded, str(status)
    result['fatal_status_after_open'] = str(ctrl.GetFatalErrorStatus())
    descriptions = {item.resourceId: item.name for item in ctrl.GetResources()}
    available = ctrl.EnumerateCounters()
    result['available_counters'] = []
    for counter in available:
        desc = ctrl.DescribeCounter(counter)
        result['available_counters'].append({'counter': int(counter), 'name': desc.name,
            'description': desc.description, 'category': desc.category, 'unit': str(desc.unit)})
    wanted = [c for c in available if ctrl.DescribeCounter(c).name in
              ('GPUTime', 'VsGsTime', 'PSTime', 'FetchSize', 'WriteSize', 'MemUnitBusyCycles', 'GPUBusyCycles')]
    assert len(wanted) == 7, 'Required AMD counters unavailable on this replay device'
    result['requested_counters'] = [int(c) for c in wanted]
    counters = {}
    samples = ctrl.FetchCounters(wanted)
    result['counter_result_count'] = len(samples)
    result['fatal_status_after_counters'] = str(ctrl.GetFatalErrorStatus())
    for sample in samples:
        row = counters.setdefault(sample.eventId, {})
        desc = ctrl.DescribeCounter(sample.counter)
        value = ((sample.value.d if desc.resultByteWidth == 8 else sample.value.f) if desc.resultType == rd.CompType.Float
                 else (sample.value.u64 if desc.resultByteWidth == 8 else sample.value.u32))
        row[desc.name] = value
        if desc.name in ('GPU Duration', 'GPUTime'):
            row['replay_ms'] = value * 1000
    actions = []
    def visit(items, parents):
        for action in items:
            name = action.customName or str(action.flags)
            if action.flags & rd.ActionFlags.Drawcall:
                actions.append({'event': action.eventId, 'name': name, 'parents': parents,
                                'indices': action.numIndices, 'instances': action.numInstances,
                                'counters': counters.get(action.eventId, {})})
            visit(action.children, parents + [name])
    visit(ctrl.GetRootActions(), [])
    for action in sorted(actions, key=lambda a: -a['counters'].get('replay_ms', 0))[:40]:
        ctrl.SetFrameEvent(action['event'], False)
        pipe = ctrl.GetPipelineState()
        action['shaders'] = {}
        for stage in (rd.ShaderStage.Vertex, rd.ShaderStage.Fragment):
            shader = pipe.GetShader(stage)
            reflection = pipe.GetShaderReflection(stage)
            action['shaders'][str(stage)] = {
                'resource': str(shader), 'name': descriptions.get(shader, ''),
                'sources': [f.filename for f in reflection.debugInfo.files] if reflection else []}
    result['draws'] = actions
    result['draw_count'] = len(actions)
    result['textures'] = [{'name': descriptions.get(t.resourceId, ''), 'width': t.width,
                           'height': t.height, 'samples': t.msSamp, 'bytes': t.byteSize,
                           'format': str(t.format.Name())} for t in ctrl.GetTextures()]
    result['structural_capture_read'] = True
    result['passed'] = bool(samples)
except BaseException:
    result['error'] = traceback.format_exc()
finally:
    with open(filename + '.amd.json', 'w') as output:
        json.dump(result, output, indent=2)
    if ctrl:
        ctrl.Shutdown()
    if cap:
        cap.Shutdown()
os._exit(0 if result['passed'] else 1)
