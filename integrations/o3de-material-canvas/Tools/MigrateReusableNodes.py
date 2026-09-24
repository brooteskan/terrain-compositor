"""Replace legacy terrain math/geometry with reusable Canvas nodes.

Dry-run by default. --write creates exclusive backups and atomically replaces only
graph sources. Requires --engine-root to validate the installed stock node schemas.
Existing graphs remain supported without migration.
"""
import argparse
import copy
import json
from pathlib import Path
from GraphMigration import (STOCK, NORMAL, GEOMETRY, LEGACY_NORMAL, SURFACE,
    add_node, connect, endpoint, input_slot, literal, load_catalog, output_width,
    stock_function, validate, width, write_graph)

# Frozen legacy defaults: never substitute the target engine's defaults here.
MATH = {
    '{841DF26B-4D1C-509A-9922-201E8BF92C8F}': ('lerp', [('a', [1, 1, 1]), ('b', [1, 1, 1]), ('weight', 1)]),
    '{720F17E4-1A62-5B57-84D7-090D2A2DA9B0}': ('lerp', [('a', 0), ('b', 1), ('weight', 0)]),
    '{546044A5-97EB-5CC0-AE1A-07F5125EAAEC}': ('multiply', [('a', [1, 1, 1]), ('b', [1, 1, 1])]),
    '{DCF36928-754A-51DD-8DB5-1DCA5DAA84C7}': ('multiply', [('a', 1), ('b', 1)]),
    '{B7B9EC6B-A0E9-56D3-98E3-FF3721AA38B7}': ('smoothstep', [('low', 0), ('high', 1), ('value', 0)]),
}


def remap(graph, key, names):
    for connection in graph.get('m_connections', []):
        for field in ('m_sourceEndpoint', 'm_targetEndpoint'):
            node_id, slot = connection[field]
            if node_id == key:
                if slot.get('m_subId', 0) != 0:
                    raise ValueError('Extended legacy sockets are not supported')
                slot['m_name'] = names.get(slot['m_name'], slot['m_name'])


def migrate(document, catalog):
    validate(document, catalog)
    result = copy.deepcopy(document)
    graph = result['ClassData']
    graph.setdefault('m_connections', [])
    changes = {}

    def record(name):
        changes[name] = changes.get(name, 0) + 1

    def source(config_id, reference):
        existing = next((n['Key'] for n in graph['m_nodes']
                         if n['Value'].get('configId', '').upper() == config_id), None)
        return existing if existing is not None else add_node(graph, reference, config_id)

    # Prove conversions against the original fixed-width graph, before any rewiring.
    for node in document['ClassData']['m_nodes']:
        key, value = node['Key'], node['Value']
        contract = MATH.get(value.get('configId', '').upper())
        if not contract:
            continue
        defaults = dict(contract[1])
        promoted_widths = {name: width(literal(value)) for name, value in defaults.items()}
        for slot in value.get('m_inputDataSlots', []):
            name = slot['Key']['m_name']
            if name not in defaults or width(slot['Value']['m_value']) != width(literal(defaults[name])):
                raise ValueError(f'Unsupported legacy literal type/socket: {key}/{name}')
        for connection in document['ClassData'].get('m_connections', []):
            target = connection['m_targetEndpoint']
            if target[0] == key:
                name = target[1]['m_name']
                src = connection['m_sourceEndpoint']
                actual = output_width(document['ClassData'], catalog, src[0], src[1]['m_name'])
                expected = width(literal(defaults[name]))
                if actual > expected:
                    raise ValueError(f'Node {key}/{name}: narrowing float{actual} to float{expected} needs explicit conversion; graph unchanged')
                promoted_widths[name] = actual
        expected_output = max(width(literal(v)) for v in defaults.values())
        if max(promoted_widths.values()) != expected_output:
            raise ValueError(f'Node {key}: stock promotion would change output width; add explicit conversions, graph unchanged')

    affected = set(MATH) | {GEOMETRY, LEGACY_NORMAL}
    affected_keys = {n['Key'] for n in graph['m_nodes'] if n['Value'].get('configId', '').upper() in affected}
    for connection in graph['m_connections']:
        for field in ('m_sourceEndpoint', 'm_targetEndpoint'):
            key, slot = connection[field]
            if key in affected_keys and slot.get('m_subId', 0) != 0:
                raise ValueError('Extended legacy sockets are not supported')

    for node in list(graph['m_nodes']):
        key, value = node['Key'], node['Value']
        old_id = value.get('configId', '').upper()
        if old_id in MATH:
            kind, defaults = MATH[old_id]
            names = {name: 'inValue' + str(i + 1) for i, (name, _) in enumerate(defaults)}
            names['result'] = 'outValue'
            saved = {s['Key']['m_name']: s for s in value.get('m_inputDataSlots', [])}
            slots = []
            for name, default in defaults:
                slot = saved.get(name, input_slot(name, literal(default)))
                slot['Key']['m_name'] = names[name]
                slots.append(slot)
            value['configId'] = STOCK[kind]
            value['m_inputDataSlots'] = slots
            remap(graph, key, names)
            record(kind)
        elif old_id == GEOMETRY:
            outgoing = [c for c in graph['m_connections'] if c['m_sourceEndpoint'][0] == key]
            used = {c['m_sourceEndpoint'][1]['m_name'] for c in outgoing}
            if value.get('m_inputDataSlots') or value.get('m_propertySlots'):
                raise ValueError('Unexpected serialized geometry inputs')
            # Keep the original graph ID and its UI metadata for the principal input.
            position_first = bool(used & {'position', 'elevation'}) or not used
            value['configId'] = STOCK['position' if position_first else 'normal']
            positions = key if position_first else None
            normals = key if not position_first else None
            if used & {'normal', 'slope'} and normals is None:
                normals = source(STOCK['normal'], node)
            mapping = {}
            if positions is not None:
                mapping.update(position=(positions, 'outPosition'), elevation=(positions, 'outZ'))
            if normals is not None:
                mapping['normal'] = (normals, 'outNormal')
            if 'slope' in used:
                sat = stock_function(catalog, 'saturate(inValue)', ['inValue'])
                sub = stock_function(catalog, 'inValue1 - inValue2', ['inValue1', 'inValue2'])
                saturate = add_node(graph, node, sat, {'inValue': 0}, (280, 160))
                subtract = add_node(graph, node, sub, {'inValue1': 1, 'inValue2': 0}, (560, 160))
                graph['m_connections'].extend([
                    connect(endpoint(normals, 'outZ'), endpoint(saturate, 'inValue')),
                    connect(endpoint(saturate, 'outValue'), endpoint(subtract, 'inValue2'))])
                mapping['slope'] = (subtract, 'outValue')
            for connection in outgoing:
                old_slot = connection['m_sourceEndpoint'][1]
                replacement, new_name = mapping[old_slot['m_name']]
                connection['m_sourceEndpoint'][0] = replacement
                old_slot['m_name'] = new_name
            record('geometry')
        elif old_id == LEGACY_NORMAL:
            names = {'height': 'inHeight', 'strength': 'inStrength', 'outNormal': 'outNormalWS'}
            saved = {s['Key']['m_name']: s for s in value.get('m_inputDataSlots', [])}
            if set(saved) - {'height', 'strength'}:
                raise ValueError('Unexpected serialized legacy normal input')
            value['configId'] = NORMAL
            value['m_inputDataSlots'] = []
            for name, default in (('height', 0), ('strength', 1)):
                slot = saved.get(name, input_slot(name, literal(default)))
                if width(slot['Value']['m_value']) != 1:
                    raise ValueError('Legacy normal requires scalar height/strength')
                slot['Key']['m_name'] = names[name]
                value['m_inputDataSlots'].append(slot)
            remap(graph, key, names)
            graph['m_connections'].extend([
                connect(endpoint(source(STOCK['position'], node), 'outPosition'), endpoint(key, 'inPositionWS')),
                connect(endpoint(source(SURFACE, node), 'outNormal'), endpoint(key, 'inBaseNormalWS'))])
            record('normal')
    validate(result, catalog)
    return result, changes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('graphs', nargs='+', type=Path)
    parser.add_argument('--engine-root', required=True, type=Path)
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    catalog = load_catalog(args.engine_root)
    failed = False
    for path in args.graphs:
        try:
            if path.suffix != '.materialgraph':
                raise ValueError('Expected a .materialgraph source')
            original = path.read_bytes()
            result, changes = migrate(json.loads(original), catalog)
            if changes and args.write:
                write_graph(path, original, result)
            print(f'{path}: {changes} ' + ('migrated' if args.write else '(dry run)'))
        except (ValueError, KeyError, TypeError, OSError) as error:
            failed = True
            print(f'{path}: REFUSED: {error}')
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
