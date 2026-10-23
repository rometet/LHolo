"""R14 Easy Place latency source guard.
These checks do not imply multiplayer server acceptance or network RTT.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
src=(root/"src/place/PlacementExecutor.cpp").read_text(encoding="utf-8")
delivery=(root/"src/place/PlacementRotationDelivery.cpp").read_text(encoding="utf-8")
api=(root/"src/place/PlacementRotationDelivery.h").read_text(encoding="utf-8")
dispatch=(root/"src/place/PlacementDispatchResult.h").read_text(encoding="utf-8")
fixture=(root/"tests/manual_placement/LatencyChecks.h").read_text(encoding="utf-8")
checks={
 "hotbar-selected item preferred without weakening item and aux check":
    "matchesPlacementItem(held, exactWant, neutralWant)" in src
    and "return {selected, &held};" in src,
 "range snapshot prefers held slot":
    "findItemSlot(inventorySnapshot, *cand.block, selectedItem)" in src,
 "crosshair priority applies only inside range radius":
    "crosshairTarget && crosshairTarget->block && selectedItem.item" in src
    and "dx*dx + dy*dy + dz*dz" in src
    and "static_cast<double>(radius)*static_cast<double>(radius)" in src,
 "crosshair still uses official native orientation validation":
    "if (resolveOrientedPlacement(player,region,placementContext," in src,
 "crosshair respects broken suppression and 500ms cell lock":
    "!recentlyPlaced(focused.cell,now)" in src
    and "autoPlacementSuppressed(packBlockPos(focused.cell),now)" in src,
 "queued/sent/rejected are distinct outcomes":
    "enum class PlacementDispatchResult" in dispatch
    and all(x in dispatch for x in ("Rejected,","Sent,","Queued")),
 "rotation queue returns admission bool not ack":
    "[[nodiscard]] bool queueRotationPlacement" in api
    and "if (detail::queueRotationPlacement(" in src,
 "range queued send stops alternative legacy transaction":
    "if (detail::consumesPlacementTick(result)) return;" in src
    and "if (detail::isPlacementSent(result))" in src,
 "one pending AuthInput is kept instead of invalidated by range":
    "if (pending && pending->expires>now && pending->epoch==state.manualInputEpoch()) return true;" in delivery,
 "native forwarding still verifies authority before attachment":
    "stillValid(*request,*client)" in delivery
    and "p.player->getSelectedItemSlot()!=p.slot" in delivery,
 "manual first press remains immediate":
    "placementState().manualPlaceRequested()" in src,
 "manual delay 100ms, repeat 50ms":
    "kManualInitialDelayMs   = 100" in src
    and "kManualRepeatIntervalMs = 50" in src,
 "native manual delivery matches 100ms and 50ms":
    "now-state.manualPressAt()<100" in delivery
    and "now-state.lastManualPlaceAt()<50" in delivery,
 "failed-plan timeout shortened but bounded":
    "kFailedPlanCacheMs          = 150" in src,
 "server safety interval and same-cell lock unchanged":
    "kCellLockMs = 500" in src
    and "kMinSendIntervalMs = 40" in src
    and "kSwapRetryMs = 200" in src,
 "actual packet sender still includes item net IDs":
    "transaction.mItem.get().mIncludeNetIds = true" in src,
 "no raw setblock or magic bypass":
    all(x not in src for x in ("region.setBlock(", "setExtraBlock(")),
 "latency cases include actual production source":
    "R14 aimed and already-held stone sends in the first game tick" in fixture
    and "R14 queued native input prevents a competing legacy send" in fixture,
}
for label,ok in checks.items():
    print(("PASS " if ok else "FAIL ")+label)
if not all(checks.values()):raise SystemExit(1)
print("R14_EASYPLACE_LATENCY_CONTRACT_PASS checks="+str(len(checks))
      +" runtime_server=NOT_RUN")
