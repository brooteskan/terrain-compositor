# Material Canvas compiler compatibility

The follow-up to issue #15 retains the small, hash-pinned compiler override.
Review date: 2026-09-24. Engine revision:
`061180bf24f1666eb30315b35da292eb14f4659c`.

## Extension review and decision

The pinned `MaterialGraphCompiler::GetAzslValueFromSlot` follows an incoming
connection when present, otherwise serializes the slot's literal. Its existing
node/template settings cannot express a value conditional on connection presence.
`GetInstructionsFromSlot` also emits input-slot instructions when disconnected;
leaving an inherited initializer in a template would therefore not preserve it.
A connected constant equal to a slot's default must remain an explicit override,
so testing a numeric default or sentinel cannot implement inheritance.

The relevant substitution methods are private, not overridable callbacks.
`MaterialCanvasApplication::InitMaterialGraphDocumentType` supplies the concrete
compiler factory to `GraphDocument::BuildDocumentTypeInfo`. It does not expose a
Gem setting/bus for replacing only value resolution. A replacement application or
compiler would have a larger maintenance surface than the current patch.
Document preprocessing that inserts connections would also need to distinguish
temporary defaults from saved graph edits and run identically in interactive and
headless compilation. No supported hook satisfying that contract was found in
the pinned source. Generated-text rewriting was rejected because it would create
a second interpretation of Canvas graph semantics.

Keep the opt-in `unconnectedValueExpression` patch: connections take precedence,
and nodes without this setting keep native literal behavior. Reusable procedural
nodes do not use this extension. Their non-terrain compilation uses stock Canvas
includes and explicit sockets.

The second patch hunk, `SourceGenerationOnly`, bypasses the tool's asset-status
wait for automation. It remains independently opt-in, with interactive behavior
unchanged. It does not prove shader compilation or fix the previously observed
headless shutdown fault.

## Compatibility checks

`EngineOverrides.cmake` normalizes LF/CRLF before validating the pinned source
and patched output hashes. CMake requires exactly one original compiler source
in the MaterialCanvas target and replaces it with the generated override. Runtime
Gem configuration remains outside this host-tools-only block.

`Tests/TestCompilerOverride.py` invokes the real CMake patcher against the selected
engine. It checks LF/CRLF parity and deliberate upstream-source and patch-output
drift. Configure standalone tests with `-DTC_CANVAS_ENGINE_ROOT=<engine>` to enable
this suite. The unchanged `.gitattributes` rules preserve LF patch inputs.

`Tools/CompileExamples.py` records the executable path/hash, engine revision,
compiler source/patch/config hashes, and graph hashes. Its generated-source
contract check rejects terrain shaders that substitute literals for disconnected
channels. `Tests/Fixtures/Compiler` supplies fully disconnected and explicitly
connected default-value probes; the existing six-channel fixture tests explicit
non-default values. Native generation and GPU tests verify the behavior; source
inspection alone is not shader compilation evidence.

Reports remain separate: `compile-results.json` describes fresh native generation,
`compile-environment.json` records provenance, Asset Processor jobs establish
shader/material success, and the launcher records the process exit code.
`TC_CANVAS_REPORT_ROOT` places these reports outside source assets.
`TC_CANVAS_ROUNDTRIP=1` additionally saves test copies through Canvas and verifies
that native serialization retains node identities and wiring.

## Engine upgrade procedure

1. Review upstream changes to value substitution, dependency collection, document
   factories, and asset-status reporting. Reassess the availability of a smaller
   supported extension for each patch hunk independently.
2. Update the patch and expected hashes only after reviewing the changed source.
   A mismatch is a configuration failure, not permission to disable the checks.
3. Rebuild MaterialCanvas in the consuming project's build tree and restart tools.
   Run the patcher compatibility tests, native disconnected/explicit probes,
   legacy/migrated examples, and stock Standard PBR fixtures.
4. Require fresh successful Asset Processor jobs and GPU parity. Record the exact
   executable used. Do not copy a similarly named executable from another engine
   installation or accept successful generation with incorrect inheritance.
