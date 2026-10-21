# World pack v3: concrete GPU ownership follow-up

This source-only follow-up starts at
`2e1fd2fb4f0edd650e374db0486616df6622125e`. It adds an actual D3D11On12
WARP fixture and records the native resource boundary. All production source
Git blobs and the frozen ABI remain identical to that participant candidate.
It does not complete world resource-pack switching with an active projection.

## Ownership and retirement boundary

| Path | Allocator and retained owner | Available completion / retirement |
| --- | --- | --- |
| BlockIcon textures and SRVs | LHolo creates and caches private D3D11 textures / SRVs on the existing overlay device. | Implemented `GpuOwner`: immediate-context EVENT, `End`, `Flush`, nonblocking `GetData(S_OK, TRUE)`. Old views remain held until the receipt; fresh views receive another EVENT before Ready. |
| Overlay D3D11On12 icons | Same private LHolo icons, on the existing interop D3D11 immediate context and its supplied D3D12 DIRECT queue. | The same production EVENT path is valid on this private context. The new fixture verifies it with actual D3D12 WARP and D3D11On12, including a GPU copy and nonblocking readback. |
| Cached solid / correction / outline / liquid fallback meshes | LHolo owns `unique_ptr<mce::Mesh>` created by the native BufferResourceService or `Tessellator::end(Buffered)`. Native backend allocates buffers; LHolo retains client-side mesh and resource handles. | CPU worker/upload/render leases are implemented. No identified supported completion receipt covers these handles and deferred native submit. Keep these owners retained while projection completion is Unknown. |
| Exact immediate replay | Local isolated Tessellator uses the native BufferResourceService; LHolo copies CPU MeshData and borrows native material / texture. Native MeshHelpers performs the immediate submission. | No cached LHolo Mesh is created by this path, but the method returns no receipt documenting subsequent native GPU retirement. Its normal stack lifetime alone is not a reload fence. |
| Projected BlockActor dispatch | LHolo retains CPU projected actors; the game's dispatcher and model renderer own the rendering backend. | Do not require independent proof for every engine-owned model/resource. The current conservative projection domain includes this path; the concrete unresolved LHolo retained-buffer obligation is the cached Mesh row above. |
| Native atlas, native renderer and world chunk rebuild | Game owns its resources and normal reload pipeline. LHolo retains borrowed / client texture and material handles until native settlement, then reacquires on the normal owner. | The coordinator's matched normal reload task lineage is the native settlement boundary. No extra invented all-engine GPU fence is introduced. Its CPU/native task settlement and chunk-dirty request do not supply a per-LHolo cached Mesh retirement receipt. |

The distinction is **engine-managed allocation**, **LHolo client owner retention**,
and **completion unverified**. These are separate facts. A managed allocation or
a shared handle is not `NoOwnedSubmission`. That status is appropriate only when
the domain has no owned upload or submit obligation.

## Exact unavailable boundary in the pinned SDK

The examined client SDK is LeviLamina 26.51.5 for game 1.26.51.01.
`OWNERSHIP_API_BOUNDARY_EVIDENCE.json` records every referenced header hash and
line-numbered content; `OWNERSHIP_SOURCE_EVIDENCE.json` records actual LHolo
upload and submit call sites.

* `mc/deps/minecraft_renderer/renderer/Mesh.h` stores a weak native service,
  CPU MeshData and vertex / attribute client resource pointers. Their variant
  contains `mce::Buffer` or resolved native vertex / index handles. The relevant
  `Buffer.h`, `ResolvedVertexBufferResource.h` and
  `ResolvedIndexBufferResource.h` are empty declarations in this SDK. No typed
  ID3D11Device, ID3D11DeviceContext, ID3D12Resource or queue getter is available
  from them. `IndexBufferContainer.h` separately has opaque 24-byte storage;
  its ownership type must not be invented from the vertex fields.
* `MeshContext.h` exposes native RenderContext, BufferResourceService and
  ImmediateBuffer handles. Native RenderContext / BufferResourceService and
  fallback-description declarations do not expose the actual device/context
  or a submit receipt. `ResourcePointer` uses a shared ResourceBlock containing
  a unique resource. PerFrame / Unchecked trackers concern validity/handles;
  they do not expose GPU completion or documented deferred-payload pinning.
* `FrameBuilder.h` exposes `startFrame`, `endFrame` and
  `discardFrame(bool wait)`. Native rendering accepts fallback descriptions by
  reference. The declarations do not show when native queued work is submitted,
  how those payloads are pinned, what `wait` covers, or a per-resource completion
  result. Calling `discardFrame` ourselves would discard game-owned work and
  is not an available safe participant barrier.
* `bgfx::d3d12::FenceSyncContext` and the RenderDragon frame renderer context
  are empty declarations. Frame timings, `areBuffersValid()` and native reload
  task completion cannot be substituted for a cached-buffer fence.

The existing overlay `ExecuteCommandLists` hook captures a DIRECT queue by device
identity. Signaling it while LHolo CPU leases are drained only orders work already
submitted to that queue. A deferred native FrameBuilder payload can reach that
queue later, or an upload queue can still own work. The installed typed API does
not provide the join between LHolo's render request and that later submit.
Even a successful real D3D12 fence would not establish that missing ordering.

Delaying owner destruction for a fixed number of frames or seconds has the same
limitation. Retained owners may safely remain pinned; they cannot be retired on
elapsed time. A native normal-origin completion barrier can replace Unknown only
after its ordering and coverage for the cached Buffered/fallback paths are shown.
This is a specific missing native-submit association, not a request to prove all
game GPU work. No offsets, private context casts, vtable guesses, engine discard,
new graphics backend or unverified completion adapter were introduced.

## Actual fixture coverage

Run `LHoloWorldPackReloadTests` and, in a separate fresh process,
`LHoloWorldPackReloadTests --on12`. The latter creates a WARP D3D12 device, a
private DIRECT queue and D3D11On12 device/context using the documented creation
API. Both execute the actual State, C export, BlockIconGpu and GpuOwner.

The old icon texture is copied by the actual immediate context. After the
production EVENT receipt, `Map(DO_NOT_WAIT)` succeeds and copied pixels match.
The fixture then covers old-owner retention, retirement, pending decode, a new
rollback epoch, fresh SRV creation and another actual EVENT before Ready.
Device replacement invalidates and retains the original cache. A projection
Unknown remains Unknown after the icon receipt and still blocks Prepare.
An invalid CLI option returns 2 without running the fixture.

Native engine settlement and catalog pixels remain explicit fixture doubles.
No Minecraft process, native Mesh constructor or FrameBuilder submit is run.
Passing WARP tests does not make the blocked native world-switch path deployable.

Documented interop creation:
https://learn.microsoft.com/en-us/windows/win32/api/d3d11on12/nf-d3d11on12-d3d11on12createdevice

EVENT result and nonblocking polling:
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_query
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-getdata
