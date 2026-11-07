# Ground contact texture footprints

The retained terrain samples the ground material during the contact transition.
Its original texture gradients came from the sloping mesh. The flat ground has
a different screen-to-world mapping, so the two sides can select different
texture mip levels even when the contact weight is one. Lower render resolutions
make this visible as a material line. Centroid interpolation also moves the
derivative evaluation points at partially covered MSAA pixels; Microsoft's
[rasterization rules](https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-rasterizer-stage-rules)
describe why those derivatives can be unsuitable for texture LOD.

The contact path now evaluates the world-position attribute at the pixel centre
and projects its texture footprint onto the flat ground along the camera ray.
Texture coordinates remain tied to world XY; only the sampling gradients change.
The orthographic path uses pixel-centre XY gradients directly. Geometry, contact
weights, cutoffs and material values are unchanged.

`TC_GroundFootprintPosition` is shared by the Vue consumer sampling include and
the native terrain ground-sampling library. The consumer transition calls it
before the varying blend branch.

The headless D3D11 hardware fixture `tests/tg/GroundBlendFootprintGpu.cpp` compares
ground, original contact and corrected contact against a texture with distinct
colours per mip. It covers 1280x720, 1600x900, 1920x1080 and 2560x1440, two slopes,
and 1x/2x/4x MSAA, including partially covered edge pixels. The original path must
reproduce a mismatch; the corrected path must agree within 0.003 in linear colour.
The TG project's `Gem/Tests/verify_ground_blend_footprint.py` extracts the actual
production helper, builds the fixture and writes a JSON report. The measured run
passed all 24 cases: maximum original error 0.165625, corrected error 0.000390589.
This isolates sampling correctness; in-game visual confirmation remains separate.

Collision-only heightmap footprints are also published as direct-material
contact regions. The retained mesh evaluates ground textures directly, so the
visible ground beneath and around that footprint must use the same evaluator.
Otherwise the far-distance graph material clipmap can replace only the ground
side with its baked/resampled colour and normal channels. Near views use the
direct path naturally and can hide that mismatch. The existing four-metre
contact-region falloff restores caching outside the footprint; other ground
regions retain the cache. This changes material-path selection, not mesh LOD,
normal reconstruction, shadows or collision.

The native `CollisionOnlyFootprintsKeepFarGroundOnTheContactMaterialPath` test
checks the region is present after render-height filtering and stays present
without duplication after source acquisition. The headless
`tests/mesh_blend/TerrainMeshBlendGpuTests.cpp` fixture executes the production
contact decoder and its inside/boundary/falloff/outside decisions.
