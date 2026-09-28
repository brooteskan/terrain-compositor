# Terrain vertex height precision

The O3DE integration stores normalized terrain vertex heights as `R32_FLOAT`.
The native renderer's `R16_UNORM` encoding used only even integer values, reserving
the intervening values for continuous-LOD midpoints. Over a 2048 m world-height
range this rounded primary vertices to approximately 6.25 cm intervals. Smooth
material normals hide much of that terracing, but screen-space ambient occlusion
reconstructs its own normals from depth and can reveal contour bands.

The renderer override now retains the queried floating-point heights during
packing. The vertex shader still reconstructs world height from the existing
Terrain World minimum and extent; terrain bounds, source mappings, and stamp
assets require no migration. Normalized heights 0 and 1 are both valid. A negative
height (`-1`) is the missing-terrain sentinel in both CPU and shader code.

Continuous LOD averages floating-point heights without reserving a bit or
rounding to integers. Ray-tracing vertices use the same normalized heights as
raster rendering. Normal XY components remain `R8G8_SNORM`.

Each interleaved height/normal record is eight bytes: four for height, two for
normal XY, and two explicit padding bytes. The regular and CLOD height/normal
buffers therefore double in size from four bytes per vertex each. Their memory
accounting follows `sizeof(HeightNormalVertex)`. This does not double all terrain
memory; shared XY/index buffers, assets, and ray-tracing vertex formats are unchanged.

Regression tests exercise millimeter-scale relief over 2048 m bounds through the
production gather, vertex remapping, CLOD interpolation, and ray-tracing conversion.
They also cover hole fallback, both height-range endpoints, and the GPU input
stream formats and strides.

Deploy the renderer binaries and terrain shaders together. The height buffer
format and hole sentinel are incompatible with the old shader/binary combination,
so restart the editor or game after rebuilding.
