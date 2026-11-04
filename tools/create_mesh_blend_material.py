"""Adapt a StandardPBR material to terrain blending using a shared scene profile.

No Blender access, atlas baking, UV changes, masks, or asset-specific shader code.
Usage: python create_mesh_blend_material.py --material source.material
       --profile terrain-profile.json --output blended.material
"""
import argparse
import json
import os
from pathlib import Path

TYPE='Materials/Types/TerrainMeshBlend/TerrainMeshBlend.materialtype'

def resolve(path,seen=None):
    path=Path(path).resolve(); seen=set() if seen is None else seen
    if path in seen: raise ValueError('Material inheritance cycle: '+str(path))
    seen.add(path); data=json.loads(path.read_text(encoding='utf-8-sig'))
    props={}
    if data.get('parentMaterial'):
        props.update(resolve(path.parent/data['parentMaterial'],seen))
    kind=data.get('materialType','')
    if kind and Path(kind).name not in ('StandardPBR.materialtype','TerrainMeshBlend.materialtype'):
        raise ValueError('Only StandardPBR-compatible material sources are supported: '+kind)
    for name,value in data.get('propertyValues',{}).items():
        if (name.endswith('.textureMap') or name in ('terrainBlend.colorTexture','terrainBlend.normalTexture','terrainBlend.heightTexture')) and value:
            value=(path.parent/value).resolve()
            if not value.is_file(): raise ValueError('Missing material texture: '+str(value))
        props[name]=value
    return props

def create(material,profile,output):
    material,profile,output=map(lambda p:Path(p).resolve(),(material,profile,output))
    props=resolve(material)
    settings=json.loads(profile.read_text(encoding='utf-8-sig'))['propertyValues']
    for name,value in settings.items():
        if not name.startswith('terrainBlend.'): raise ValueError('Unexpected terrain-profile property: '+name)
        if name.endswith(('colorTexture','normalTexture','heightTexture')) and value:
            value=(profile.parent/value).resolve()
            if not value.is_file(): raise ValueError('Missing terrain texture: '+str(value))
        props[name]=value
    distance=props.get('terrainBlend.distance',.15)
    if not isinstance(distance,(int,float)) or not 0<=distance<100000: raise ValueError('Invalid blend distance')
    if not props.get('terrainBlend.colorTexture'): raise ValueError('A shared terrain color texture is required')
    props={k:os.path.relpath(v,output.parent).replace('\\','/') if isinstance(v,Path) else v for k,v in props.items()}
    data={'description':'Imported PBR material with reusable terrain-compositor ground blending.',
          'materialType':TYPE,'materialTypeVersion':1,'propertyValues':props}
    text=json.dumps(data,indent=2)+'\n'; output.parent.mkdir(parents=True,exist_ok=True)
    if not output.exists() or output.read_text()!=text: output.write_text(text)
    return data

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('material','profile','output'): parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args(); create(args.material,args.profile,args.output)
