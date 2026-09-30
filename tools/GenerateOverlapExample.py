"""Generate editable Canvas nodes/example graph; native Canvas generates all shaders.

The supported texture evaluator preserves source-local planar mapping and uses explicit
UV derivatives. Imported arbitrary UV layouts require a separate source adapter.
"""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CANVAS = ROOT / 'integrations/o3de-material-canvas'
NODES = CANVAS / 'Assets/MaterialCanvas/Terrain/Nodes'
EXAMPLES = CANVAS / 'Assets/MaterialCanvas/Terrain/Examples'
TEXTURE_ID = '{C8DE8758-3CE1-4715-B8E4-685D19ECE20F}'
BLEND_ID = '{7E87F3CA-5B62-4F29-AD58-5C5D1C0B0677}'
COORDINATES_ID = '{23911840-2D68-4FBA-9D78-0B193A858B33}'
CHANNELS = [('baseColor', 'float3', [1.0, 1.0, 1.0]), ('normal', 'float3', [0.0, 0.0, 0.0]),
            ('roughness', 'float', 1.0), ('metalness', 'float', 0.0),
            ('specularFactor', 'float', 0.5), ('ambientOcclusion', 'float', 1.0)]


def write(path, value):
    path.write_text(json.dumps(value, indent=4) + '\n', encoding='utf-8')


def value(kind, data):
    return {'$type': {'float3': 'Vector3', 'float2': 'Vector2', 'string': 'AZStd::string'}.get(kind, kind), 'Value': data}


def slot(name, kind, default=None, output=False):
    result = dict(name=name, displayName=name, supportedDataTypeRegex=kind, defaultDataType=kind)
    if not output:
        result['settings'] = {'instructions': ['SLOTTYPE SLOTNAME = SLOTVALUE;']}
    if default is not None:
        result['defaultValue'] = value(kind, default)
    return result


def node(identity, title, inputs, outputs, instructions, properties=None):
    instructions = [re.sub(r'\boverlap[A-Z]\w*', lambda m: 'NODEID_' + m[0], line) for line in instructions]
    instructions = ['#ifndef TC_OVERLAP_LOCAL_SET', '#define TC_OVERLAP_LOCAL_SET',
                    'TerrainOverlapSet O3DE_TC_OVERLAP = TC_ResolveOverlap(O3DE_MC_POSITION_WS);', '#endif'] + instructions
    return dict(Type='JsonSerialization', Version=1, ClassName='DynamicNodeConfig', ClassData=dict(
        id=identity, category='Terrain', title=title, titlePaletteName='MathNodeTitlePalette',
        description='Source-aware overlap material. Configure Material Overlap on the composition. Slots are 1 through 4.',
        settings={'includePaths': ['TerrainCompositor/TerrainOverlap.azsli'], 'instructions': instructions},
        inputSlots=inputs, outputSlots=outputs, propertySlots=properties or []))


def main():
    inputs = []
    instructions = ['TerrainOverlapSet overlapSet = O3DE_TC_OVERLAP;',
                    'TerrainSurfaceChannels overlapMaterials[4];', 'TerrainSurfaceChannels overlapFallback;']
    for prefix in ['fallback'] + [f'layer{i}' for i in range(1, 5)]:
        for channel, kind, default in CHANNELS:
            name = prefix + channel[0].upper() + channel[1:]
            inputs.append(slot(name, kind, default))
            target = 'overlapFallback' if prefix == 'fallback' else f'overlapMaterials[{int(prefix[-1])-1}]'
            instructions.append(f'{target}.{channel} = {name};')
    instructions += ['TerrainSurfaceContext overlapContext;', 'overlapContext.worldPosition = O3DE_MC_POSITION_WS;',
                     'overlapContext.geometricNormal = O3DE_MC_NORMAL_WS;',
                     'TerrainSurfaceChannels overlapResult = TC_BlendOverlap(overlapSet, overlapMaterials, overlapFallback, overlapContext);']
    outputs = []
    for channel, kind, _ in CHANNELS:
        output = 'out' + channel[0].upper() + channel[1:]
        outputs.append(slot(output, kind, output=True))
        instructions.append(f'SLOTTYPE({output}) {output} = overlapResult.{channel};')
    contract = slot('overlapContract', 'float', 1.0)
    contract['settings'] = {'materialPropertyName': ['contract_version'], 'materialPropertyGroup': ['overlap']}
    write(NODES / 'overlap_surface.materialgraphnode', node(BLEND_ID, 'Terrain Overlap Surface', inputs, outputs, instructions, [contract]))

    # Separate one-image nodes deliberately use the stock native material-property contract.
    properties = [slot('inGroup', 'string', 'overlap'), slot('inName', 'string', ''), slot('inDescription', 'string', '')]
    for p in properties:
        p.pop('settings', None)
    for name, kind in [('inValue', 'image'), ('inSampler', 'sampler')]:
        p = slot(name, kind)
        p['settings'] = {'materialPropertyConnectionType': ['ShaderInput'], 'materialPropertySrgMember': ['STANDARD_SRG_MEMBER']}
        properties.append(p)
    tex = node(TEXTURE_ID, 'Terrain Overlap Texture', [slot('materialSlot', 'float', 1.0), slot('scale', 'float', 1.0),
               slot('normalStrength', 'float', 1.0)], [slot('color', 'float3', output=True), slot('normal', 'float3', output=True),
               slot('raw', 'float3', output=True)], [
        'uint overlapSlot = TC_OverlapSlotIndex(materialSlot);',
        'TerrainOverlapSource overlapSource = TC_OverlapMaterialSource(O3DE_TC_OVERLAP, overlapSlot);',
        'float4 overlapTexel = TC_SampleOverlapTexture(overlapSource, O3DE_MC_POSITION_WS, scale, params.inValue, params.inSampler);',
        'SLOTTYPE(color) color = TransformColor(overlapTexel.xyz, ColorSpaceId::LinearSRGB, ColorSpaceId::ACEScg);',
        'SLOTTYPE(raw) raw = overlapTexel.xyz;',
        'SLOTTYPE(normal) normal = TC_OverlapNormal(overlapSource, overlapTexel.xyz, normalStrength, params.inValue >= 0);'
    ], properties)
    tex['ClassData']['settings']['includePaths'].append('Atom/Features/ColorManagement/TransformColor.azsli')
    write(NODES / 'overlap_texture.materialgraphnode', tex)

    coord = node(COORDINATES_ID, 'Terrain Overlap Coordinates', [slot('materialSlot', 'float', 1.0)],
        [slot('localXY', 'float2', output=True), slot('sourcePosition', 'float3', output=True), slot('sourceNormal', 'float3', output=True)],
        ['TerrainOverlapSource overlapSource = TC_OverlapMaterialSource(O3DE_TC_OVERLAP, TC_OverlapSlotIndex(materialSlot));',
         'SLOTTYPE(localXY) localXY = overlapSource.localPosition;', 'SLOTTYPE(sourcePosition) sourcePosition = overlapSource.worldPosition;',
         'SLOTTYPE(sourceNormal) sourceNormal = overlapSource.normal;'])
    write(NODES / 'overlap_coordinates.materialgraphnode', coord)

    graph = json.loads((EXAMPLES / 'default_terrain.materialgraph').read_text())
    data = graph['ClassData']
    data['m_nodes'] = [data['m_nodes'][0]]
    data['m_connections'] = []
    def add(identity, key, input_values=None, property_values=None):
        n = {'$type': 'DynamicNode', 'toolId': 2034248906, 'configId': identity}
        for field, values in [('m_inputDataSlots', input_values), ('m_propertySlots', property_values)]:
            n[field] = [{'Key': {'m_name': k}, 'Value': {'m_value': v}} for k, v in (values or {}).items()]
        data['m_nodes'].append({'Key': key, 'Value': n})
    def connect(source, output, target, input_name):
        data['m_connections'].append({'m_sourceEndpoint': [source, {'m_name': output}], 'm_targetEndpoint': [target, {'m_name': input_name}]})
    add(BLEND_ID, 2, {f'layer{i}Roughness': value('float', r) for i, r in enumerate([0.95, 0.25, 0.65, 0.1], 1)})
    output_names = ['inBaseColor', 'inNormal', 'inRoughness', 'inMetalness', 'inSpecularFactor', 'inAmbientOcclusion']
    for (channel, _, _), output_name in zip(CHANNELS, output_names):
        connect(2, 'out' + channel[0].upper() + channel[1:], 1, output_name)
    for i in range(1, 5):
        for normal_map in (False, True):
            key = 3 + (i - 1) * 2 + int(normal_map)
            add(TEXTURE_ID, key, {'materialSlot': value('float', float(i)), 'scale': value('float', [2.0, 5.0, 1.0, 8.0][i-1])},
                {'inName': value('string', f'Layer{i}' + ('Normal' if normal_map else 'Color')), 'inGroup': value('string', 'overlap')})
            connect(key, 'normal' if normal_map else 'color', 2, f'layer{i}' + ('Normal' if normal_map else 'BaseColor'))
    # Existing native graph defaults are retained; editor supplies layout for newly added nodes.
    write(EXAMPLES / 'overlap_terrain.materialgraph', graph)
    write(EXAMPLES / 'overlap_terrain.material', {'materialType': 'overlap_terrain.materialtype', 'materialTypeVersion': 1,
        'propertyValues': {'overlap.layer1_color': 'grass_basecolor.png', 'overlap.layer2_color': 'rock_basecolor.png',
                           'overlap.layer3_color': 'grass_basecolor.png', 'overlap.layer4_color': 'rock_basecolor.png'}})


if __name__ == '__main__':
    main()
