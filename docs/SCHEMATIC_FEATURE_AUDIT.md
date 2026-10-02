# Schematic feature layer audit (2026-10-03)

Branch: `codex/lholo-integration-20261002`.
Audited baseline: `211f67407c0b58e661da2269a875a91c728860de`.
Behavior reference: [current Lamium](https://github.com/amatouhake/Lamium),
`docs/BACKLOG.md` L-93 and its placement/session/verification/store sources.
No Lamium renderer was copied.

## Existing implementation and retained boundaries

- LHolo already had forward/inverse placement math, native Bedrock block-state
  transformation, correct-block hiding, wrong-type/state/extra correction,
  legacy Y/X/Material layers, single-placement config restore and material HUD.
- Existing `Create Structure` already exposes two corners, player-position
  buttons, XYZ editing and atomic `.mcstructure` export without a wand.
  Its capture/export implementation and both existing format loaders are retained.
- There is one authoritative ProjectionState. Multiple placements are domain
  records; the explicitly selected record is activated through existing loading
  and projection ownership. Walking/proximity never changes selection.
- ProjectionVirtualWorld, neighbor lookup, mesh builder/renderer/worker/upload,
  direct-update processing, scheduling, liquid/cull/translucency and BlockActor
  implementations are unchanged. No Map files are added or modified.
- Existing world-exit worker barriers, listener retirement, dimension suspension,
  callback drain, menu/input/cursor host and hooks remain authoritative. The new
  tick/control work uses their existing entry points. No gameplay bindings,
  WndProc, ImGui context, input system or render hook were added.

## Completed changes

| Request | Result |
|---|---|
| Transform contract | One pure `PlacementTransform`: minimum placed-box origin, mirror X/Z before rotation, forward/inverse, extent and directional layer rank. Renderer layout wrappers delegate to it. All 12 rotation/mirror combinations are exhaustively checked with asymmetric sizes and neighbor coordinates. |
| Block orientation | Existing game transformer remains authoritative. Regression tests cover native dispatch, identity/null bypass and rotation/mirror arguments for stairs, hopper, trapdoor, redstone torch/repeater. They do not execute Minecraft's native state transformation. |
| Verifier states | Ignored, Unknown, Correct, Missing, WrongType, WrongState, Extra. Readiness/placeholder checks precede classification. Expected air, uncovered region gaps and overlapping regions have explicit rules. Extras are per-placement and layer-filtered. |
| Chunk availability | Existing LevelListener accepts load/reload/unload facts with dimension/source/epoch checks. Value-only column facts expand with at most 128 steps per collection call into the existing subchunk refresh path. No native chunk is retained; existing correction budgets and direct events keep priority. |
| Six directions | Bottom/Top, West/East and North/South in both directions; All/Single/UpToCurrent, plus existing FromCurrent. Old Y=0, X=1, Material=2 and mode values are preserved; new axes append values 3–8. New placement default is BottomToTop. |
| PlacementSession | Domain records own ID, file/name, dimension, origin, rotation, mirror, visible, layers and countExtras. Explicit selection/deletion, revision checks and value snapshots; UI owns no renderer objects. |
| Persistence | Version 1, maximum 64 placements/128 KiB, depth limit, tolerant optional fields, atomic replacement and failed-load write protection. Library-relative paths only, with Windows path restrictions and canonical containment. Local documents use LevelId + dimension. |
| Migration | Existing single-placement config continues working. Explicit `Import saved projection` assigns it to the current world/dimension, preserves transform/legacy layers and copies external files into the schematic library without overwriting an existing file. The old config has no world identity, so it is not silently assigned to a world. |
| Existing menu | Schematics file/placement lists, place/move at feet, select/deselect/delete, name/XYZ, rotate/mirror, visibility, layers, extras, verification and materials. Existing Render/HUD layer controls accept the appended directions. |
| Verification presentation | Visible-layer totals and kind filters, world positions, expected/actual serialization and distance; deterministic nearest cycle over a bounded result cache. Nearest 512 each for missing, wrong-state and wrong+extra are retained; full counts are not truncated. |
| Materials | Selected placement/visible layers: total, correctly placed, remaining and existing inventory ID counts. Native block-to-item resolution plus pure torch/slab/door/bed quantity rules. Both visible halves must match for a paired item to count correctly; a visible orphan half still requires one item. Unresolved mappings remain visible with inventory unavailable. |

The selected verifier/material job consumes at most 256 scan operations per game
tick. Region traversal and overlap comparisons also consume that budget; material
row finalization is incremental. Nearest sorting is capped at 1,536 entries and
inventory reads at 36 existing slots. Counting does not run in mesh/render/upload
code. Reports describe a completed incremental pass, not an atomic world snapshot;
while the menu is open they refresh after a two-second interval, or explicitly on
Verify/placement edits. Existing renderer direct-update convergence is retained.
Hiding a placement gates drawing while the existing update pipeline continues.

## Deferred boundaries and validation limits

- **Server disk persistence:** existing LHolo has no validated current-server
  address+port snapshot API. The pure stable key rule is tested; the native glue
  is deferred. Server placements use memory only, with at most 16 dimension
  documents, and clear on world/session reset. Temporary Level identity is never
  persisted. Local LevelId stability across an actual reopen still needs game QA.
- **Simultaneous rendering of several placements:** existing projection ownership
  is single-state. This would require additional renderer/VirtualWorld/worker
  ownership and is outside the requested renderer boundary. Saved records and
  selected-placement actions are implemented without that redesign.
- **Shulker/container contents and entity-placement flag:** existing inventory
  counting safely reads ordinary slots; validated shulker-content decoding and
  entity projection APIs are absent. No new native hooks or NBT item decoding
  were added. Existing capture entity export and BlockActor rendering are retained.
- Native stairs/hopper/trapdoor/redstone orientation results, chunk event timing,
  world/dimension switching, actual cursor/F10 interaction, capture roundtrip,
  large-structure frame time and GPU visuals have not been exercised in Minecraft.
  Mock dispatch/unit tests and a successful DLL link do not establish those results.

## Validation

Baseline Logic: 10,537 checks / 0 failures. Final focused rules are in
`tests/logic/SchematicChecks.h`, and the Schematics page participates in the
existing multilingual/viewport/scale UI matrix.

| Target | Result |
|---|---|
| LHoloLogicTests (including manual placement/projection rules) | 1,181,012 checks / 0 failures |
| LHoloNbtTests | 3,091 checks / 0 failures |
| LHoloLanguageStoreTests | 16 checks / 0 failures / 0 tracked allocations left |
| LHoloUiTests | 29,349 checks / 4,860 frames / ImGui errors 0 |
| LHoloGraphicsTests | 1,638 checks / 0 failures; cross-device 10, removal 12 |
| LHoloTranslucencyTests | 573 checks / PASS |
| LHolo | Windows x64 Release client DLL full rebuild passed (35.484 s) with the existing cached toolchain/dependencies |

Commands: `xmake -P . -b <test target>`, then the executable under
`build/integrated-clean/windows/x64/release/`; `xmake -P . -r LHolo`.
Existing configuration: Windows/x64/release/client/MD, clang-cl, private network,
dependency require disabled. No dependencies/configuration were upgraded.
Logs: ignored `build/schematic-validation/`. UI uses the test fallback font and
reports missing optional fonts; this is not CJK glyph visual QA. Existing prelink
RC diagnostic and hook macro warnings do not prevent the successful DLL build.

Final diff review explicitly checks Map paths and the protected renderer,
VirtualWorld, frame pipeline, lifecycle, app/overlay/input, capture and format
implementations. Projection changes are limited to canonical transform/layer
delegation, verifier readiness/visible errors/extras, value-only availability
notifications and visibility/query gates. No push or release was performed.

## Changed file groups

- New structure domain/rules: PlacementTransform, NativeBlockTransform,
  Verification, PlacementSession, PlacementMigration, TransientPlacementCache,
  InventoryCountRules, InventoryContents and SchematicRuntime.
- Existing structure/settings integration: LayerDisplayTypes, StructureSession,
  StructureLoader, MaterialTracker, SettingsStore and PlaceHelper's existing tick.
- Projection adapter/correctness: Projection/ProjectionTypes, ProjectionRules,
  ProjectionLayoutRules/State, ProjectionCorrectionTracker, ProjectionInvalidation,
  ProjectionRenderFrame/WorldEvents/WorldEventInterest, ProjectionPlacement and new
  ChunkAvailabilityQueue. No mesh or VirtualWorld implementation is changed.
- Existing menu: LHoloMenu, MenuPages, MenuController, TextKeys and five locale
  JSON files. Tests: LogicTests/SchematicChecks, UiRenderTests; xmake test sources.
