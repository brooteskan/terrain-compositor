# Filtered Noise 2D

**Smooth Interpolation** is an optional Boolean input, defaulting to **off** to preserve the previously approved appearance. Enable it to use cubic B-spline blending of four neighboring lattice corner values per axis instead of the original two-corner quintic interpolation. This softens square patches and reduces contrast. Both modes retain analytic pixel-area filtering, stable terrain coordinates, and the [0,1] range. Restart Material Canvas to reload the node definition; if an existing node does not acquire the new input, replace that node and reconnect it before compiling.

Smooth mode integrates the cubic basis exactly over the same footprint rectangle, with at most 100 hashes per evaluation (versus 36 in the original mode). Small intervals are integrated directly; large ones use the compact primitive at the interval boundaries. The switch controls reconstruction, not whether antialiasing is enabled. The legacy two-argument shader overload preserves the original mode for graphs that have not yet been recompiled.

Validation: AZSL and DXC compilation pass. D3D11 hardware pixel tests cover both reconstructions against independent cell-split Gaussian quadrature references, zero/tiny/large/elongated footprints, cell and integration-branch boundaries, the Boolean switch, and legacy behavior. Noise errors were below 0.000001. Appearance is inspected by the user in the project.

Connect **World Position -> World XY / Scale -> Position**, keep Seed constant, and use Value in the material graph. The shader library is installed in TGProject as well as the source checkout. Existing graphs keep the same node ID and function signature; compile the graph and allow Asset Processor to rebuild its shaders. Restart Canvas to refresh the node description if needed.

The node now evaluates a stationary, spatially hashed, quintic-interpolated lattice field. There are no global sine waves, repeating texture tiles, camera-dependent seeds, distance thresholds, or explicit blends toward gray. Coordinates must be continuous: do not wrap them with frac or restart them per terrain patch.

This is an **integrable lattice variant**, not the identical pattern produced by Lattice Value Noise 2D. For independent hashed values h, its lattice corner value is `0.5 + 0.25 * (h(i,j) - h(i-1,j) - h(i,j-1) + h(i-1,j-1))`. Quintic interpolation keeps the field continuous and bounded in [0,1]. These correlated corners let the rectangular area integral telescope to boundary terms: at most 36 hashes, independent of how many cells a pixel covers. There is no truncated sampling budget or distant fallback to gray. The correlations also change the spectrum and contrast relative to ordinary independent-corner value noise.

The shader averages this fixed field over a rectangle of width `abs(ddx(position)) + abs(ddy(position))`. This is the exact integral over that rectangle, including footprints that cross cells or span many cells. For small footprints it integrates each polynomial segment directly to avoid numerical cancellation. Each axis is treated separately, including zero-width axes.

The rectangle approximates the actual projected pixel footprint; it can overblur a rotated or sheared footprint. Very small detail still loses contrast as it averages out, which is inherent to filtering. There is no camera radius or common contrast-fade factor. The integrable basis has less low-frequency energy than ordinary value noise, so its broad averages approach 0.5 faster.

Evaluate uniformly in a fragment shader with valid derivatives. Large-coordinate floating-point precision limits remain. Nonlinear operations or bump/specular shading after this node can introduce their own aliasing. Normal From Height interprets its input as meters; coordinate scale and bump amplitude are separate controls.

The existing Lattice Value Noise 2D node and user graph wiring are unchanged. Visual acceptance is left to the user in the running project.
