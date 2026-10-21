# World resource-pack reload candidate (ABI4)

Source-proven: normal `BaseActorRenderContext` supplies the exact Level/client/
renderer/dimension owner before projection and bounds submission. All projection
submissions use the existing Minecraft `mce::Mesh`, `MeshHelpers`, Tessellator,
TexturePtr and MaterialPtr routes. They do not own a private D3D queue/SRV.
`NativeManaged` records this lifetime policy and is never a GPU-completion receipt.
The old v3 export rejects calls; the coordinator and participant must both use v4.

The participant closes new work, drains actual CPU/jobs/native borrowers, retains
old handles through the normal game reload, and waits for its matching settled
origin/task-body/disposal boundary. In the next normal render-owner callback it
uses `resetProjectionState` and `captureBounds.clear`, keeps copied structure/anchor
intent, and reacquires the terrain atlas, BlockTessellator, section data and UVs
through `enableStructureProjection`. No CPU rebuild is counted as GPU submission.
Ready requires both owner domains rebuilt and all real leases released.

Minecraft's own BlockGraphics/model/material/texture cache regeneration and
submitted-native-frame lifetime remain the responsibility of the normal native
reload. Their private completion fence is not claimed as inspected or proven.
The pinned SDK 26.51.5 exposes normal Mesh constructors/destructors/reset and
resource-service-backed handles; the candidate uses those existing operations.
Engine-internal queue retention is Inferred, not a locally measured fence.

Directly owned block-icon D3D11 SRVs still require the actual old and fresh EVENT
queries on their owning immediate context, including D3D11On12. Unknown/Pending
private resources block Prepare/Ready. Unknown projection submissions cannot be
promoted after submission, and closed attempts cannot register a new policy.
Wrong physical owner, stale token/epoch, late publication, unknown native task,
rollback ambiguity and Stop preserve the existing fail-closed behavior.

The Android reference suggests normal FinalizeLoad/cache refresh/next-frame
reacquisition responsibility. Its raw Block client-data writes and conditional
branch NOPs are not transplanted. No world objects, server-required pack state,
signature/DRM/permission conditions or game settings are changed by this candidate.

Runtime result: not-tested. Offline State/actual-C-export/D3D EVENT fixtures are
Test-proven only after their successful execution is recorded. They do not prove
Minecraft reload, visual correctness, fonts/shaders/behavior-pack hot reload, or
first-frame GPU completion. User real-machine acceptance remains required before
main commit, final build and release. No game launch or live deployment is part
of this source candidate task.

## Integrated trial limits

Task counters establish only observed task-body exit and ticket release. They do
not establish closure destruction completion, the total order of all FinalizeLoad
work, native mesh publication, GPU completion, or correct framebuffer appearance.

This trial also integrates the reviewed ae1f788 native liquid ExactReplay primitive
ordering patch onto the unchanged ABI4 reload contract. Water/lava ordering within
one accepted replay submission is the target; ordering between separate passes is
out of scope. Per-section fallback can retain an old order while the shared budget
is exhausted and may lag during continuous camera movement. Replay staging adds
CPU copies. Visual correctness, movement responsiveness, and the copy cost remain
unverified until the user tests this exact pair in Minecraft.
