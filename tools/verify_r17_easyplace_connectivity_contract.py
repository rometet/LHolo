"""R17 source safety contract; native server acceptance untested."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
rules=(root/'src/place/ManualPlacementRules.h').read_text(encoding='utf-8')
executor=(root/'src/place/PlacementExecutor.cpp').read_text(encoding='utf-8')
stats=(root/'src/place/PlaceHelper.cpp').read_text(encoding='utf-8')
fixture=(root/'tests/manual_placement/LatencyChecks.h').read_text(encoding='utf-8')
logic=(root/'tests/logic/ManualPlacementChecks.h').read_text(encoding='utf-8')
checks={
 "vanilla block family allowlist": 'isVanillaConnectivityPlacementBlock' in rules and 'name.starts_with("minecraft:")' in rules and all(k in rules for k in ('iron_bars','copper_bars','glass_pane','_fence','_wall')),
 "only exact four cardinal directions": all(k in rules for k in ('minecraft:connection_north','minecraft:connection_south','minecraft:connection_east','minecraft:connection_west')),
 "unknown state strict": 'state.starts_with("connection_")' not in rules and 'isConnectivityDerivedStateKey(state)' in rules,
 "Auto/Range fallback narrow": 'detail::isVanillaConnectivityPlacementBlock(name)' in executor and '&& manualSerializedPlacementMatches(predicted, ghost)) return true;' in executor,
 "other mode retains manual fallback": 'return placementState().manualMode() && manualSerializedPlacementMatches(predicted, ghost);' in executor,
 "identity check precedes fallback": executor.index('block::placeableBaseName(predicted.getTypeName())') < executor.index('if (detail::isVanillaConnectivityPlacementBlock(name)'),
 "direction checks precede fallback": executor.index('placementDirectionMatches(predicted, ghost)') < executor.index('if (detail::isVanillaConnectivityPlacementBlock(name)'),
 "connectivity summary counters": 'ConnectivityAccepted' in executor and 'ConnectivityRejected' in executor and 'connectOK={} connectFail={}' in stats,
 "real executor native plan fixture": 'R17 native extractor planner accepts safe connectivity state' in fixture and 'R17 native predicted connection permutations accepted in Auto' in fixture,
 "color addon and piston cases": all(x in fixture for x in ('R17 stained pane color identity remains strict','R17 arbitrary addon blocks cannot bypass strict permutations','R17 piston facing remains strictly validated')),
 "logic rules exact tests": 'manualPlacementStateMapsMatch("addon:iron_bars"' in logic and 'minecraft:blue_stained_glass_pane' in logic,
 "authoritative transaction no mutations": 'client->getPacketSender().sendToServer(packet)' in executor and 'region.setBlock(' not in executor and 'kCellLockMs = 500' in executor and 'kMinSendIntervalMs = 40' in executor,
}
for name,ok in checks.items(): print(('PASS ' if ok else 'FAIL ')+name)
if not all(checks.values()):raise SystemExit(1)
print('EASYPLACE_R17_CONNECTIVITY_CONTRACT_PASS checks='+str(len(checks))+' native_server=NOT_RUN')
