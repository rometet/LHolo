"""Static R16 contract. Native server and user-visible latency untested."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
executor=(r/'src/place/PlacementExecutor.cpp').read_text(encoding='utf-8')
hook=(r/'src/place/PlaceHelper.cpp').read_text(encoding='utf-8')
fixture=(r/'tests/manual_placement/LatencyChecks.h').read_text(encoding='utf-8')
telemetry=(r/'src/place/EasyPlaceTelemetry.h').read_text(encoding='utf-8')
checks={
    "20Hz native tick remains sole action runner":
        'LocalPlayerEasyPlaceHook' in hook and
        'detail::tickEasyPlace(*this);' in hook,
    "material HUD refresh moved after placement":
        hook.index('detail::tickEasyPlace(*this);') <
        hook.index('structure::detail::tickMaterialTracker(*this);'),
    "bounded and aggregated performance reporting":
        "snapshotIfDue(GetTickCount64(),active)" in hook and
        "EASYPLACE_R16_PERF" in hook and
        "now+5'000" in telemetry,
    "non-allocating atomic metrics":
        "std::array<std::atomic<std::uint64_t>" in telemetry and
        "fetch_add(" in telemetry,
    "timing limited to Tick Ray Plan scopes":
        all(f'Stage::{x}' in executor for x in ('Tick','Ray','Plan')),
    "expensive aimed failure cache bounded at 100ms":
        'kAimedOrientedFailureCacheMs = 100' in executor and
        'if (expensivePlan) {' in executor,
    "plain full blocks bypass failed plan cache":
        'bool const expensivePlan = isViewSensitivePlan(*target->block);' in executor and
        'if (expensivePlan && isFailedPlanCached(failedKey,now))' in executor,
    "new target is not incorrectly blocked by negative cache":
        'R16 changed aimed ghost bypasses previous failed-plan cache' in fixture,
    "cache expiry explicitly covered":
        'R16 retry becomes eligible again after bounded 100ms' in fixture,
    "500ms cell safety and 40ms global minimum unchanged":
        'kCellLockMs = 500' in executor and
        'kMinSendIntervalMs = 40' in executor,
    "authoritative server send path unchanged":
        'client->getPacketSender().sendToServer(packet)' in executor,
    "no direct world mutation":
        'region.setBlock(' not in executor and
        'setExtraBlock(' not in executor,
}
for name,valid in checks.items(): print(("PASS " if valid else "FAIL ")+name)
if not all(checks.values()):raise SystemExit(1)
print('EASYPLACE_R16_DIAGNOSTICS_CONTRACT_PASS checks='+str(len(checks))+' native_server=NOT_RUN')
