# LHolo R12 native Bedrock bubble particle integration — feasibility gate

Date: 2026-10-23 JST. Research branch:
`codex/vanilla-particle-r12-research-20261023`.
Based on R11 `020ec95`, installed user-accepted *vanilla-inspired*
animated ghost bubbles remain untouched.

## Grounded native references

The installed 1.26.51.01 resource pack contains
`data/resource_packs/vanilla/__brarchive/particles.brarchive`.
The archive table contains these exact vanilla files:
`bubble_column_up.json`, `bubble_column_down.json`, and
`bubble_column_bubble.json`.

Known Bedrock effect identifiers:
- `minecraft:bubble_column_up_particle`
- `minecraft:bubble_column_down_particle`
- `minecraft:bubble_column_bubble` (surface/actor bubbles, different effect).

Installed LeviLamina 26.51.5 SDK exposes:
`LevelRenderer::mParticleSystemEngine` (NonOwnerPointer),
`ParticleSystemEngine::createParticleEmitter(HashedString,Vec3,MolangVariableMap)`,
and `ParticleEmitter::expire()`, `setEnableRender()`,
`setEnableUpdate()`, and `setManualParticleEmission()`.

## Blocker to **genuine vanilla** safe integration

`Bedrock::slot_map_handle<T,64,32>` in the installed 26.51 SDK is
**opaque/empty** in the generated public header. The engine's emitter
slot map is similarly an opaque `dense_slot_map` type.

Although the native creation function returns a slot-map handle,
the available typed headers do NOT provide a supported way to:
1. Resolve the returned handle to the corresponding `ParticleEmitter`.
2. Call `expire()` on **only the emitters LHolo created**.
3. Validate a handle after a dimension/renderer reset or resource reload.

Calling `ParticleSystemEngine::clear()` is NOT acceptable because it
would wipe unrelated vanilla/world particle effects too.

Recreating emitters blindly is also unacceptable: hidden/removed
schematics could leave ghost particles visible, and emitters could
persist across world changes. Never store engine raw pointers without
owner/lifecycle checks or patch private offsets just to bypass this.

## Required R12 native integration gate

1. Obtain/implement a **version-pinned, typed emitter handle lifecycle**
   wrapper, independently reviewed against the 1.26.51.01 binary. It
   must give an exact LHolo-owned emitter removal/expiry operation.
   Avoid unsigned private offsets, unsafe hook injection or engine-wide clear.
2. Verify `createParticleEmitter` effect-name lookup resolves both
   bubble-column particle IDs in a **disposable local offline world**.
3. Spawn **only** for visible missing schematic bubble cells,
   cap count by camera distance and chunk, store handles tagged with
   source-generation + dimension + source cell.
4. Stop/expire exactly those handles on layer/visibility changes, cell
   correction, structure swap, dimension switch, resource reload, and
   LHolo unload. Confirm none remain before restoring R11.
5. Verify water-layer parity, shader/material behavior, FPS and no
   side-effects to vanilla particles or BDS. Do not publish R12 until
   its runtime acceptance suite is passed.

## Current outcome

Native effect assets/IDs are located and native engine APIs identified.
**No live engine particle injection attempted. No R12 DLL installed.**
R11 remains the tested client-only fallback while safe exact lifecycle
ownership is unresolved. This is an explicit blocker, not a successful
pixel-identical vanilla implementation.
