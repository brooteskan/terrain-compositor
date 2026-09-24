"""Shared, lossless graph editing and pinned stock-node contract checks."""
import copy
import json
from pathlib import Path

GEM = Path(__file__).resolve().parents[1]
STOCK = {
    'lerp': '{8DD887CA-DA8E-4F55-9DFE-1B89EA0310AB}',
    'multiply': '{7DE0F2F0-1ADC-4B4B-ADDB-CA21B8D97816}',
    'smoothstep': '{8C2C6CEF-76DB-4567-A9E5-F39491A23454}',
    'position': '{20D18D54-9A1D-4C4E-B676-F72097AC3A93}',
    'normal': '{F1C815E8-0B44-43EA-8176-D075B5B2EA41}',
}
NORMAL = '{2063FE4F-D452-5519-98FD-B77B9F0A8F50}'
GEOMETRY = '{1A73E777-16E3-5B32-B0DA-DAF3BB0BF1D8}'
LEGACY_NORMAL = '{CFBE3398-6EEF-5166-A9B6-A0C8EF809636}'
SURFACE = '{07F3EA17-9E79-5C38-AB32-CBE05B06FC28}'
OUTPUT = '{EB039C35-AA34-58D2-9992-BF88368F5DFB}'


def literal(value):
    return {'$type': 'Vector3' if isinstance(value, list) else 'float', 'Value': copy.deepcopy(value)}


def input_slot(name, value):
    return {'Key': {'m_name': name}, 'Value': {'m_value': copy.deepcopy(value)}}


def endpoint(node, name):
    return [node, {'m_name': name}]


def connect(source, target):
    return {'m_sourceEndpoint': source, 'm_targetEndpoint': target}


def load_catalog(engine_root):
    engine_nodes = Path(engine_root) / 'Gems/Atom/Tools/MaterialCanvas/Assets/MaterialCanvas/GraphData/Nodes'
    if not engine_nodes.is_dir():
        raise ValueError('Engine root has no Material Canvas stock node library: ' + str(engine_root))
    catalog = {}
    for folder in (engine_nodes, GEM / 'Assets/MaterialCanvas'):
        for path in folder.rglob('*.materialgraphnode'):
            config = json.loads(path.read_text(encoding='utf-8'))['ClassData']
            key = config['id'].upper()
            if key in catalog:
                raise ValueError('Duplicate node config UUID: ' + key)
            catalog[key] = config
    # These are compiler-facing semantics, not palette titles or file names.
    for name, inputs, expression in (
            ('lerp', ['inValue1', 'inValue2', 'inValue3'], 'lerp(inValue1, inValue2, inValue3)'),
            ('multiply', ['inValue1', 'inValue2'], 'inValue1 * inValue2'),
            ('smoothstep', ['inValue1', 'inValue2', 'inValue3'], 'smoothstep(inValue1, inValue2, inValue3)')):
        config = catalog.get(STOCK[name], {})
        outputs = config.get('outputSlots', [])
        if ([s['name'] for s in config.get('inputSlots', [])] != inputs or
                len(outputs) != 1 or outputs[0]['name'] != 'outValue' or
                outputs[0].get('settings', {}).get('instructions') != [f'SLOTTYPE SLOTNAME = {expression};'] or
                config.get('slotDataTypeGroups') != ['|'.join(inputs + ['outValue'])] or
                any(s.get('supportedDataTypeRegex') != '(color|bool|int|uint|float)([1-4])?'
                    for s in config.get('inputSlots', []))):
            raise ValueError('Unsupported stock node contract: ' + name)
    for name, prefix in (('position', 'POSITION'), ('normal', 'NORMAL')):
        config = catalog.get(STOCK[name], {})
        slots = {s['name']: s for s in config.get('outputSlots', [])}
        first = 'outPosition' if name == 'position' else 'outNormal'
        for slot, suffix in ((first, ''), ('outZ', '.z')):
            if slots.get(slot, {}).get('settings', {}).get('instructions') != [
                    f'SLOTTYPE SLOTNAME = O3DE_MC_{prefix}_WS{suffix};']:
                raise ValueError('Unsupported stock geometry contract: ' + name)
    return catalog


def stock_function(catalog, expression, inputs, output='outValue'):
    """Resolve by verified operation/schema, never by a user-facing title."""
    candidates = [c for c in catalog.values()
                  if [s['name'] for s in c.get('inputSlots', [])] == inputs
                  and len(c.get('outputSlots', [])) == 1
                  and c['outputSlots'][0]['name'] == output
                  and c['outputSlots'][0].get('settings', {}).get('instructions') ==
                  [f'SLOTTYPE SLOTNAME = {expression};']
                  and c.get('slotDataTypeGroups') == ['|'.join(inputs + [output])]]
    if len(candidates) != 1:
        raise ValueError('Missing or ambiguous stock operation: ' + expression)
    return candidates[0]['id']


def validate(document, catalog):
    if document.get('ClassName') != 'Graph' or document.get('Version') != 1:
        raise ValueError('Unsupported graph serialization envelope')
    graph = document['ClassData']
    nodes = {}
    for node in graph['m_nodes']:
        key = node['Key']
        if not isinstance(key, int) or key in nodes:
            raise ValueError('Invalid or duplicate graph node ID')
        nodes[key] = node['Value']
        for field in ('m_inputDataSlots', 'm_propertySlots'):
            serialized = node['Value'].get(field, [])
            names = [json.dumps(s['Key'], sort_keys=True) for s in serialized]
            if len(names) != len(set(names)):
                raise ValueError('Duplicate serialized socket on node ' + str(key))
    targets = set()
    for connection in graph.get('m_connections', []):
        for field, direction in (('m_sourceEndpoint', 'outputSlots'), ('m_targetEndpoint', 'inputSlots')):
            key, slot = connection[field]
            if key not in nodes:
                raise ValueError('Connection references missing node: ' + str(key))
            config = catalog.get(nodes[key].get('configId', '').upper())
            if config and slot['m_name'] not in {s['name'] for s in config.get(direction, [])}:
                raise ValueError(f'Unknown {direction} endpoint: {key}/{slot["m_name"]}')
        target = json.dumps(connection['m_targetEndpoint'], sort_keys=True)
        if target in targets:
            raise ValueError('Multiple incoming connections to a socket')
        targets.add(target)
    return graph


def width(value):
    return {'float': 1, 'int': 1, 'unsigned int': 1, 'bool': 1,
            'Vector2': 2, 'Vector3': 3, 'Vector4': 4, 'Color': 4}.get(value.get('$type'))


def output_width(graph, catalog, key, name, visiting=None):
    """Conservatively resolve the compiler's input-driven vector-size groups."""
    visiting = set() if visiting is None else set(visiting)
    if (key, name) in visiting:
        raise ValueError('Cyclic graph connection')
    visiting.add((key, name))
    node = next(n['Value'] for n in graph['m_nodes'] if n['Key'] == key)
    config = catalog.get(node.get('configId', '').upper())
    if not config:
        raise ValueError(f'Cannot prove connected source type for node {key}; keep legacy node')
    slot = next(s for s in config.get('outputSlots', []) if s['name'] == name)
    groups = [g.split('|') for g in config.get('slotDataTypeGroups', []) if name in g.split('|')]
    inputs = [s for s in config.get('inputSlots', []) if groups and s['name'] in groups[0]]
    if not inputs:
        kind = slot['defaultDataType']
        if kind in ('float', 'int', 'uint', 'bool'):
            return 1
        if kind in ('float2', 'float3', 'float4', 'color'):
            return 4 if kind == 'color' else int(kind[-1])
        raise ValueError('Unsupported source type: ' + kind)
    result = 1
    saved = {s['Key']['m_name']: s['Value']['m_value'] for s in node.get('m_inputDataSlots', [])}
    for input_config in inputs:
        slot_name = input_config['name']
        source = next((c['m_sourceEndpoint'] for c in graph.get('m_connections', [])
                       if c['m_targetEndpoint'][0] == key and c['m_targetEndpoint'][1]['m_name'] == slot_name), None)
        if source:
            size = output_width(graph, catalog, source[0], source[1]['m_name'], visiting)
        else:
            default = saved.get(slot_name, input_config.get('defaultValue', {}))
            size = width(default)
            if size is None:
                kind = input_config['defaultDataType']
                size = {'float': 1, 'float2': 2, 'float3': 3, 'float4': 4, 'color': 4}.get(kind)
        if size is None:
            raise ValueError('Cannot resolve input type for node ' + str(key))
        minimum = input_config.get('settings', {}).get('materialPropertyMinVectorSize', ['1'])
        result = max(result, size, int(minimum[0]))
    return result


def add_node(graph, reference, config_id, slots=None, offset=(-280, 180)):
    key = max((n['Key'] for n in graph['m_nodes']), default=0) + 1
    value = {'$type': 'DynamicNode', 'toolId': reference['Value']['toolId'], 'configId': config_id}
    if slots:
        value['m_inputDataSlots'] = [input_slot(name, literal(v)) for name, v in slots.items()]
    graph['m_nodes'].append({'Key': key, 'Value': value})
    metadata = graph.setdefault('m_uiMetadata', {}).setdefault('m_nodeMetadata', [])
    original = next((m for m in metadata if m['Key'] == reference['Key']), {})
    position = next((c.get('Position', [0, 0]) for c in original.get('Value', {}).get('ComponentData', {}).values()
                     if c.get('$type') == 'GeometrySaveData'), [0, 0])
    metadata.append({'Key': key, 'Value': {'ComponentData': {
        '{7CC444B1-F9B3-41B5-841B-0C4F2179F111}': {'$type': 'GeometrySaveData',
            'Position': [position[0] + offset[0], position[1] + offset[1]]}}}})
    return key


def write_graph(path, original, document):
    path = Path(path)
    if path.suffix != '.materialgraph':
        raise ValueError('Only .materialgraph sources can be migrated')
    if path.read_bytes() != original:
        raise ValueError('Graph changed during migration; close it in Canvas and retry')
    # Serialize before making a backup so unsupported values cannot cause a partial write.
    converted = (json.dumps(document, indent=4, allow_nan=False) + '\n').encode('utf-8')
    backup = path.with_suffix(path.suffix + '.bak')
    temporary = path.with_suffix(path.suffix + '.migration.tmp')
    with backup.open('xb') as stream:
        stream.write(original)
    created = False
    try:
        with temporary.open('xb') as stream:
            created = True
            stream.write(converted)
        temporary.replace(path)
    finally:
        if created and temporary.exists():
            temporary.unlink()
