# Imported mesh blending with terrain

`TerrainMeshBlend.materialtype` is a reusable opaque StandardPBR material provided by the TerrainCompositor Gem. It retains imported atlas UVs, normal maps, roughness, specular, and parallax/POM properties, then blends surface channels toward a shared world-space ground appearance before lighting. It does not require a Blender-specific shader, per-face labels, vertex-color masks, or rock exclusions.

## Authoring

1. Give the imported object a ground skirt wide enough to meet or slightly enter the surrounding ground. Keep rocks sufficiently far above the blend band. Skirt radius and shape are Blender authoring decisions.
2. Create one shared scene terrain profile, referencing the actual ground color/normal textures, world texture size, tiling mode, tint, roughness, specular factor, and ambient occlusion. Profiles are reused across exported objects.
3. Adapt an object's StandardPBR material with the supplied tool:

   ```text
   python tools/create_mesh_blend_material.py --material object.material --profile terrain-profile.json --output object-terrain-blend.material
   ```

4. Assign the resulting material to the imported mesh's existing material slot. Keep its mesh collider. Save the object as an ordinary O3DE prefab and place/duplicate it through Asset Browser.
5. Add **Terrain Mesh Blend** to the prefab root. On each placed instance select the level's **Target Compositor** and adjust **Blend Distance** in metres. The component registers with that compositor and controls all descendant terrain-blend materials. It creates independent material instances, so changing one prefab does not change its neighbours. Zero disables blending. An unset or unavailable compositor disables blending. Keep the material's hidden distance at zero; the root is the authoring control.

There is no separate rock mask. Every surface uses the same distance calculation; use a smaller blend distance or a wider skirt to keep the transition away from rocks. The object still has one atlas and one material, while the shader additionally samples the scene's shared terrain textures.

## Terrain profile

```json
{
  "propertyValues": {
    "terrainBlend.distance": 0.0,
    "terrainBlend.groundHeight": 0.0,
    "terrainBlend.colorTexture": "ground_color.png",
    "terrainBlend.textureScale": 1000.0,
    "terrainBlend.stochasticTiling": true,
    "terrainBlend.colorTint": [1.0, 1.0, 1.0, 1.0],
    "terrainBlend.roughness": 1.0,
    "terrainBlend.specularFactor": 0.5,
    "terrainBlend.ambientOcclusion": 1.0
  }
}
```

Texture paths are relative to their originating material/profile files. The adapter resolves them and emits portable paths relative to the new material. Inherited StandardPBR properties are resolved without editing the original files. Unsupported material graphs and missing textures fail before replacing an existing output.

Blend Distance is a vertical world-space contact band, matching the Vue ground-contact approach: terrain weight is `1 - smoothstep(0, distance, max(0, meshZ - terrainZ))`. It is not distance measured inward from the polygon boundary. The artist controls the corresponding horizontal width with the skirt's slope and radius. Color, normals, vertex normal, roughness, specular F0, and occlusion are blended together, with emissive/clear-coat response faded toward ground. Depth and collision geometry are not displaced or replaced by this material.

The optional **Terrain Height Image**, **Height Image Bounds** (world XY origin and extent), and **Height Image Scale** support a supplied linear terrain-height field. Without that image, Terrain Height is a flat world plane. Height imagery and appearance profiles are explicit inputs; this material does not automatically convert arbitrary Material Canvas graphs, scan scenes, or track moving terrain at runtime. A configured profile must match the underlying terrain. The shared stochastic sampler reproduces the existing Vue world-ground sampling.

Where the terrain uses an appearance cache, registered mesh bounds select direct graph evaluation under the mesh and feather back to the cache over four metres outside its bounds. This keeps the mesh's directly sampled ground and the terrain consistent at a distance. Registration, material loading, configuration and root-transform notifications drive updates; the component does not continuously poll the scene.

For packed atlases, **Atlas Parallax Mapping** converts physical POM height into UV offsets using the mesh's actual UV derivatives. All four PBR maps use that same shifted UV. **Atlas Gutter** bounds offsets in texture pixels to preserve atlas island padding. Height must be a linear image; use an R16 preset for 16-bit exported height. Ground regions should have neutral normal/height data. Keep imported stone UVs and sufficient gutters when baking the skirt. Verify imported tangents against the baking application; an explicit **Tangents > MikkT > TSpaceBasic** import rule generates a normal-map basis from the retained UVs instead of relying on FBX tangent data.

The gutter bound applies to the full POM search path, including its starting depth offset, before any height sampling. Final-hit clamping alone still lets the ray read another island or black atlas background. Padding must exceed the permitted search travel by the texture-filter footprint: the stone-circle example uses ten pixels of padding for a six-pixel search bound. Dilation must preserve all occupied texels and pad color, normal, roughness and height consistently; it is unrelated to terrain blending masks.

## Validation

Run `python -m unittest discover -s tests -p test_mesh_blend_material.py`. O3DE Asset Processor must additionally compile the material pipelines and instantiate a consumer material. TG's stone-circle integration verifies the root's exposed properties, compositor registration, disabled/enabled distance reaching the material, independent instances, runtime conversion and material assignment, static collision, and preservation of unrelated level entities. Visual acceptance remains an in-game inspection task.
