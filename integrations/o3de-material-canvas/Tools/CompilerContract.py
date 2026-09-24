"""Validate contextual defaults in fresh native output, before reporting success."""
import re
from pathlib import Path

OUTPUT_ID = '{EB039C35-AA34-58D2-9992-BF88368F5DFB}'
INHERITED = {'inBaseColor': 'baseColor', 'inNormal': 'normal', 'inRoughness': 'roughness',
             'inMetalness': 'metalness', 'inSpecularFactor': 'specularFactor',
             'inAmbientOcclusion': 'ambientOcclusion'}


def verify_terrain_contract(document, generated):
    graph = document['ClassData']
    outputs = [n for n in graph['m_nodes'] if n['Value'].get('configId', '').upper() == OUTPUT_ID]
    if not outputs:
        return {'contract': 'non-terrain', 'inherited': []}
    if len(outputs) != 1:
        raise ValueError('Compilation validation requires one Terrain Output per graph')
    shaders = [Path(p).read_text(encoding='utf-8') for p in generated if str(p).endswith('_Tint.azsli')]
    if len(shaders) != 1 or 'TC_CanvasSurface(' not in shaders[0]:
        raise ValueError('Missing native terrain surface implementation')
    source = shaders[0]
    connected = {c['m_targetEndpoint'][1]['m_name'] for c in graph.get('m_connections', [])
                 if c['m_targetEndpoint'][0] == outputs[0]['Key']}
    inherited = []
    for slot, field in INHERITED.items():
        expression = re.search(r'\b' + slot + r'\s*=\s*([^;]+);', source)
        if not expression:
            raise ValueError('Generated surface is missing output assignment: ' + slot)
        actual = expression.group(1).strip()
        expected = 'incoming.' + field
        if slot not in connected:
            if actual != expected:
                raise ValueError(f'MaterialCanvas lacks terrain inheritance capability: {slot} = {actual}. '
                                 'Use the executable rebuilt with TerrainCompositorCanvas; generation is not accepted.')
            inherited.append(slot)
        elif actual == expected:
            raise ValueError('Connected output unexpectedly bypassed its source: ' + slot)
    return {'contract': 'terrain-surface-v2', 'inherited': inherited,
            'connected': sorted(connected & INHERITED.keys())}
