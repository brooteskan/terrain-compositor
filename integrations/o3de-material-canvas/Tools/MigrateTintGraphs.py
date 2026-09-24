"""Expose legacy tint connections as Base Color multiplication. Dry-run by default.

Usage: python MigrateTintGraphs.py graph.materialgraph --engine-root <o3de> [--write]
Only graph sources are modified; user-owned materials and generated assets are not.
Recompile migrated graphs with Material Canvas. Existing graphs also work without
migration through the retained, hidden inTint compatibility input.
"""
import argparse
import copy
import json
from GraphMigration import STOCK, load_catalog, output_width, validate, write_graph
from pathlib import Path

NODES = Path(__file__).resolve().parents[1] / 'Assets/MaterialCanvas/Terrain/Nodes'


def config_id(name):
    if name == 'stock_multiply':
        return STOCK['multiply']
    return json.loads((NODES / (name + '.materialgraphnode')).read_text(encoding='utf-8'))['ClassData']['id']


def migrate(document, catalog=None):
    result = copy.deepcopy(document)
    graph = result['ClassData']
    nodes = graph['m_nodes']
    connections = graph.setdefault('m_connections', [])
    metadata = graph.setdefault('m_uiMetadata', {}).setdefault('m_nodeMetadata', [])
    next_id = max((n['Key'] for n in nodes), default=0) + 1
    changed = 0
    for output in list(nodes):
        value = output['Value']
        if value.get('configId') != config_id('output'):
            continue
        output_id = output['Key']
        tint_connections = [c for c in connections if c['m_targetEndpoint'] == [output_id, {'m_name': 'inTint'}]]
        base_connections = [c for c in connections if c['m_targetEndpoint'] == [output_id, {'m_name': 'inBaseColor'}]]
        if len(tint_connections) > 1 or len(base_connections) > 1:
            raise ValueError('Multiple connections to one output socket')
        tint_slot = next((s for s in value.get('m_inputDataSlots', []) if s['Key']['m_name'] == 'inTint'), None)
        tint_value = copy.deepcopy(tint_slot['Value']['m_value'] if tint_slot else {'$type': 'Vector3', 'Value': [1, 1, 1]})
        if not tint_connections and tint_value['Value'] == [1, 1, 1]:
            continue
        if catalog is not None:
            widths = []
            for inputs in (base_connections, tint_connections):
                if inputs:
                    source = inputs[0]['m_sourceEndpoint']
                    widths.append(output_width(graph, catalog, source[0], source[1]['m_name']))
                else:
                    widths.append(3)
            if max(widths) != 3:
                raise ValueError('Tint migration requires explicit float3 conversions to preserve the old socket width')

        def add(kind, slots=None):
            nonlocal next_id
            key = next_id
            next_id += 1
            node = {'$type': 'DynamicNode', 'toolId': copy.deepcopy(value['toolId']), 'configId': config_id(kind)}
            if slots:
                node['m_inputDataSlots'] = slots
            nodes.append({'Key': key, 'Value': node})
            original_meta = next((m for m in metadata if m['Key'] == output_id), None)
            position = [0, 0]
            if original_meta:
                for component in original_meta['Value'].get('ComponentData', {}).values():
                    if component.get('$type') == 'GeometrySaveData':
                        position = component.get('Position', position)
            metadata.append({'Key': key, 'Value': {'ComponentData': {
                '{7CC444B1-F9B3-41B5-841B-0C4F2179F111}': {'$type': 'GeometrySaveData', 'Position': [position[0] - 400, position[1] + 300 * (key - output_id)]}}}})
            return key

        multiply = add('stock_multiply', [
            {'Key': {'m_name': 'inValue1'}, 'Value': {'m_value': {'$type': 'Vector3', 'Value': [1, 1, 1]}}},
            {'Key': {'m_name': 'inValue2'}, 'Value': {'m_value': tint_value}}])
        if base_connections:
            base_connections[0]['m_targetEndpoint'] = [multiply, {'m_name': 'inValue1'}]
        else:
            incoming = add('surface_inputs')
            connections.append({'m_sourceEndpoint': [incoming, {'m_name': 'outBaseColor'}], 'm_targetEndpoint': [multiply, {'m_name': 'inValue1'}]})
        if tint_connections:
            tint_connections[0]['m_targetEndpoint'] = [multiply, {'m_name': 'inValue2'}]
        connections.append({'m_sourceEndpoint': [multiply, {'m_name': 'outValue'}], 'm_targetEndpoint': [output_id, {'m_name': 'inBaseColor'}]})
        if tint_slot:
            tint_slot['Value']['m_value']['Value'] = [1, 1, 1]
        changed += 1
    return result, changed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('graphs', nargs='+', type=Path)
    parser.add_argument('--engine-root', required=True, type=Path, help='Validate the stock Canvas node contract')
    parser.add_argument('--write', action='store_true', help='Save graphs, creating an exclusive .bak backup first')
    args = parser.parse_args()
    catalog = load_catalog(args.engine_root)
    failed = False
    for path in args.graphs:
        try:
            if path.suffix != '.materialgraph':
                raise ValueError('Expected a .materialgraph source')
            original = path.read_bytes()
            document = json.loads(original)
            validate(document, catalog)
            converted, count = migrate(document, catalog)
            validate(converted, catalog)
            if count and args.write:
                write_graph(path, original, converted)
            print(f'{path}: {count} output(s) ' + ('migrated' if args.write else 'would migrate (dry run)'))
        except (ValueError, KeyError, TypeError, OSError) as error:
            failed = True
            print(f'{path}: REFUSED: {error}')
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
