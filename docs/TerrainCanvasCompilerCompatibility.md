# Native terrain graph compiler integration

The integration pins the MaterialGraphCompiler input and generated override hashes. CMake applies repository patches to a private build-directory copy and fails on input or patch drift. The shared engine checkout is unchanged.

The current extension generates a `MaterialParameters` declaration using Atom's `CreateMaterialShaderParameterLayout` and `WriteMaterialParameterStructureAzsli`. Graph properties enter `m_params`; fixed renderer settings remain directly in TerrainMaterialSrg. The forward, depth and shadow wrappers include the same generated declaration. Reserved renderer names are rejected during compilation.

Disconnected output values are ordinary numeric defaults. No contextual inheritance or historical contract branch is supported. Compilation does not generate or replace user-owned `.material` instances.

`SourceGenerationOnly` is a separate headless-tool setting that bypasses the tool's Asset Processor status wait. Native generation reports do not prove successful asset processing, rendering or process shutdown. Record those outcomes separately.

Run `Tests/TestCompilerOverride.py <engine-root> <cmake>` to verify LF/CRLF normalization, input drift rejection and output drift rejection. `Tests/TestCompilerContract.py` checks numeric defaults, shared declarations and rejection of old surface expressions. `Tools/CompileExamples.py` validates fresh native outputs and copies the generated default into the base Gem.
