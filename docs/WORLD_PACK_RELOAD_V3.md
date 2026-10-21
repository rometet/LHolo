# LHolo world resource-pack participant v3

This isolated change starts at `b81a4ea089664aef854a65ab68e0045ba62a52ae`,
which includes the accepted UI `908e830`. It adds a participant export and
ownership gates without changing menu layout, fonts, shader appearance,
canonical resolution, Exact policy, or placement planning. PlaceHelper changes
are admission guards around its existing normal callbacks.

**Integration candidate, not a complete native world-switch implementation.**
Production native projection completion is deliberately `Unknown`; neither
Prepare nor Ready can treat it as a GPU fence. Do not deploy this DLL as a
completed resource-pack switching feature until that missing receipt and the
coordinator/native paths have been implemented and verified.

## Frozen interface and identities

`src/resourcepacks/WorldPackReloadApi.h` preserves the frozen v3 ABI: request
56 bytes, reply 88 bytes, the original enum values and `noexcept` C export
`lholo_resource_reload_v3`. The namespace is the contract's
`praxis::companion::packs`, even in this LHolo participant.

`request.world` is a copied nonzero Praxis world/dimension generation stamp.
It is bound and echoed, never compared numerically with a Level address or a
private LHolo counter. Private Level, Dimension, IClientInstance base and
LevelRenderer addresses are observed separately on normal game/render owners.
Pause verifies the client/renderer and snapshots those physical identities.
Subsequent identity loss/change and authoritative source/level destruction
invalidate an active attempt. Bootstrap Query uses all-zero keys and exposes
no invented world stamp; the initial logical resource epoch is 1.

Tokens are unique across native attempts, including cancelled attempts.
Settlement advances the logical epoch. A post-settlement rollback uses another
token and a strictly later epoch, while preserving the same bound world stamp.
The token set is bounded at 4096; exhaustion refuses new attempts.

## Owned paths and receipts

| Path | Gate or receipt | Verification limit |
| --- | --- | --- |
| Projection frame, normal pending actions, close, hit selection, assisted native callbacks | Counted leases; new work rejected while closed | SDK compile and existing copied-body fixtures; actual engine not run |
| Mesh worker | Lease lives beyond native task-capture destruction; epoch/serial checked in the same short transaction as copied-result commit | CPU publication races and cancellation helper fixtures |
| Cancelled mesh result | Only a copied section ticket survives; normal upload owner clears that section's in-flight flag and restores dirty state | No old mesh/native payload is republished; worker/structure/revision checked |
| BlockIcon catalog, decode and retry | Counted game-tick lease; captured Store revision; atomic copied-value commit; rejected retry discards working key | Native metadata/image APIs compile, but are not executed by the fixture |
| Known pack/texture reload hooks | Atomic State-to-Store begin/end plus an observed origin-scope lease | Origin runs outside the lock; only these known scopes are counted, not all engine jobs |
| BlockIcon private SRVs | Actual D3D11 EVENT issued and polled on the existing immediate Present context, before old SRV retirement and after fresh upload | WARP exercises real event queries and private resources; synthetic pixels replace native decode |
| Native projection meshes and Buffered uploads | Production marks the owned domain Unknown | Missing native FrameBuilder completion receipt; CPU drain and matching DIRECT queue are insufficient |
| New active projection | Normal owner recreates CPU/tessellator/atlas state, but leaves GPU Unknown | CPU enable is not fresh mesh/GPU regeneration completion |

Query returns an atomically published copied snapshot. It invokes no native
getter, GPU query or blocking drain and does not take the State mutex. This is
not a claim that `atomic<shared_ptr>` is lock-free.

Pause and copied publication have one ordering point. Short publication and
mutation transactions use State mutex then Store/completed-queue mutex; native
calls, GPU calls, waits and State reentry are forbidden inside them. An engine
origin that crosses an uncoordinated Pause invalidates on scope exit.
NativeSettled refuses to pass an observed in-flight origin. A new mutation in
Reacquiring or Ready invalidates before Store revision changes. The GPU owner
also binds its rebuilt views to a captured Store revision.

Old native owners remain owned by the original projection session, and old
private SRVs remain in the original GPU cache through preparation/native work.
They are retired only on the normal owners after the required old-domain
receipts. Wrong device/context or invalidation retains owned resources and the
module. A before-native cancellation resumes the same owners/epoch; it does
not invent a GPU receipt. A rollback preserves the desired icon keys even when
the preceding fresh generation had already retired its old cache.

The enabled NativeMod is weakly registered and strongly pinned at Pause.
Disable and Pause admission have one atomic winner. Active/invalidated reload
refuses disable and keeps the pin. Unit shared-token lifetime checks do not
prove real loader unload behavior.

## Concrete missing native interface

The installed LeviLamina 26.51.5 client SDK's Mesh submission/FrameBuilder
route has no identified per-LHolo completion receipt. Opaque
FenceSyncContext/RenderContext types and frame timing values are not such a
receipt. A captured DIRECT D3D12 queue with matching device identity does not
establish ordering against deferred native FrameBuilder submissions.

Integration needs a supported completion mechanism for LHolo-owned native
mesh uploads/submissions, plus the existing normal-owner pipeline's fresh mesh
regeneration completion. It must cover both old retirement and new readiness.
Do not substitute CPU drain, a module pin, a D3D11 icon event, a chunk-rebuild
request, private discard/reload calls or invented vtable methods.

The icon event uses the documented D3D11 EVENT `GetData(S_OK, TRUE)` result:
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_query
and nonblocking polling:
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-getdata

## Validation and runtime boundary

`LHoloWorldPackReloadTests` links the actual State and C export and exercises
ABI/key/epoch rejection, publication and teardown races, before-native cancel,
fresh rollback, cancelled-job recovery, nested mutation origins, owner/device
invalidation, actual private WARP resources and old/new D3D11 event queries.
Its native settlement notifications and decode pixels are explicit doubles.
The production projection domain cannot be promoted by the icon event.

Existing UI, graphics, logic, NBT, HUD-control, language, translucency,
Materials, export-lifecycle and BlockIcon suites are regression checks.
Placement and approved-state copied-body fixtures preserve their boundary
labels. Tests do not launch Minecraft or certify actual native reload,
GameTick metadata decode, stack settlement, FrameBuilder GPU completion,
loader retention or performance. Candidate and exact final-source evidence
are prepared outside the live mod/config/world/pack directories.
