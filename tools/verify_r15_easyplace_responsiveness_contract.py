"""R15 source-safety checks for focused Easy Place placement scheduling.

This is a static contract; it does not simulate the Minecraft server.
"""
from pathlib import Path

r=Path(__file__).resolve().parents[1]
runner=(r/"src/place/PlacementExecutor.cpp").read_text(encoding="utf-8")
proj=(r/"src/projection/Projection.cpp").read_text(encoding="utf-8")
api=(r/"src/projection/Projection.h").read_text(encoding="utf-8")
rotation=(r/"src/place/PlacementRotationDelivery.cpp").read_text(encoding="utf-8")
fixture=(r/"tests/manual_placement/LatencyChecks.h").read_text(encoding="utf-8")
checks = {
    "query preserves active schematic and world lifecycle gating":
        "auto result = queryProjection(player, worldPos);" in proj,
    "stale comparison is repaired only for real air and expected block":
        "result.block && !result.missing" in proj and
        "player.getDimensionBlockSource().getBlock(worldPos).isAir()" in proj,
    "placement-only lookup is public without altering render query":
        "ProjectionQuery queryPlacementProjection(" in api,
    "crosshair ray and door upper use current world air fallback":
        runner.count("projection::queryPlacementProjection(player, ")==3,
    "native queued rotation checks world-backed expected ghost":
        "projection::queryPlacementProjection(*p.player,p.cell)" in rotation,
    "focus validation runs before range query and sort":
        runner.index("if (crosshairTarget &&") < runner.index("auto candidates = projection::queryMissingCellsInRange("),
    "range fallback preserved after focus":
        "if (candidates.empty()) return;" in runner and
        "const start = placementState().rangePlanCursor()" in runner,
    "server-authoritative sender unchanged":
        "client->getPacketSender().sendToServer(packet)" in runner,
    "global intersend and same cell safety lock preserved":
        "kCellLockMs = 500" in runner and
        "kMinSendIntervalMs = 40" in runner,
    "held material still requires item and auxiliary identity":
        "matchesPlacementItem(*selectedItem.item, exactWant, neutralWant)" in runner,
    "new offline tests assert radius bypass and fallback":
        "R15 focused block dispatch skips full radius query/sort" in fixture and
        "R15 failed focused plan still falls back to range candidates" in fixture,
    "no unauthorized world write shortcuts":
        "region.setBlock(" not in runner and
        "setExtraBlock(" not in runner,
}
for key,ok in checks.items():
    print(("PASS" if ok else "FAIL")+" "+key)
if not all(checks.values()):
    raise SystemExit(1)
print("EASYPLACE_R15_SOURCE_CONTRACT_PASS",len(checks),"checks; native_server=NOT_RUN")
