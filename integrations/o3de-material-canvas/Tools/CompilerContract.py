"""Verify the current direct terrain contract in fresh native Canvas output."""
import json
import math
import re
from pathlib import Path

OUTPUT_ID = '{EB039C35-AA34-58D2-9992-BF88368F5DFB}'
DEFAULTS = {'inBaseColor': [1, 1, 1], 'inNormal': [0, 0, 0], 'inRoughness': 1,
            'inMetalness': 0, 'inSpecularFactor': .5, 'inAmbientOcclusion': 1}


def verify_terrain_contract(document, generated):
    graph = document['ClassData']
    outputs = [n for n in graph['m_nodes'] if n['Value'].get('configId', '').upper() == OUTPUT_ID]
    if not outputs:
        return {'contract': 'non-terrain'}
    if len(outputs) != 1:
        raise ValueError('Exactly one Terrain Output is required')
    files = {Path(p).name: Path(p) for p in generated}
    def source(suffix):
        matches = [p for n, p in files.items() if n.endswith(suffix)]
        if len(matches) != 1:
            raise ValueError('Missing or ambiguous generated file: ' + suffix)
        return matches[0].read_text(encoding='utf-8')
    shader = source('_Tint.azsli')
    if re.search(r'\bincoming\b|\binTint\b|m_canvasTint|TC_EvaluateTerrainTint', shader):
        raise ValueError('Obsolete incoming surface or tint evaluation')
    if 'const MaterialParameters params' not in shader:
        raise ValueError('Graph does not accept native material parameters')
    connected = {c['m_targetEndpoint'][1]['m_name'] for c in graph.get('m_connections', [])
                 if c['m_targetEndpoint'][0] == outputs[0]['Key']}
    values = dict(DEFAULTS)
    for slot in outputs[0]['Value'].get('m_inputDataSlots', []):
        if slot['Key']['m_name'] in values:
            values[slot['Key']['m_name']] = slot['Value']['m_value']['Value']
    for slot, expected in values.items():
        expr = re.search(r'\b' + slot + r'\s*=\s*([^;]+);', shader)
        if not expr:
            raise ValueError('Missing channel: ' + slot)
        if slot not in connected:
            actual = expr.group(1).strip()
            actual = re.sub(r'float[234]\s*\(', '(', actual)
            numbers = [float(v) for v in re.findall(r'[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?', actual)]
            expected_numbers = expected if isinstance(expected, list) else [expected]
            if len(numbers) != len(expected_numbers) or not all(
                    math.isclose(a, b, rel_tol=1e-5, abs_tol=1e-6) for a, b in zip(numbers, expected_numbers)):
                raise ValueError(f'Wrong default for {slot}: {actual}')
    params = source('_Parameters.azsli')
    if 'struct MaterialParameters' not in params or 'm_meshCutout' in params:
        raise ValueError('Missing graph parameter structure or renderer fields placed in m_params')
    declaration = source('_Material.azsli')
    if '_Parameters.azsli' not in declaration or 'TC_GRAPH_MATERIAL' not in declaration:
        raise ValueError('Material declaration does not include graph parameters')
    for suffix in ['_Tint.azsli', '_Depth.azsl']:
        if '_Material.azsli' not in source(suffix):
            raise ValueError('Raster passes do not share the material declaration')
    material = json.loads(source('.materialtype'))
    if len(material['shaders']) != 3:
        raise ValueError('Forward, depth, and shadow shaders are required')
    if any('Shaders/Terrain/' in s['file'] for s in material['shaders']):
        raise ValueError('Shared shader references cannot carry graph-specific layouts')
    return {'contract': 'terrain-graph', 'connected': sorted(connected), 'parameters': len(re.findall(r';', params))}
