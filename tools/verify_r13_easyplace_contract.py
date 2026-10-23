"""R13 Easy Place source-level safety and test-scope contract.
Never confuse supplied predictor fixtures with real server acceptance."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
candidate=(root/"src/place/PlacementRotationCandidates.h").read_text(encoding="utf-8")
executor=(root/"src/place/PlacementExecutor.cpp").read_text(encoding="utf-8")
delivery=(root/"src/place/PlacementRotationDelivery.cpp").read_text(encoding="utf-8")
interface=(root/"src/place/PlacementRotationDelivery.h").read_text(encoding="utf-8")
checks={
 "direction candidates are opt-in only":
   'if (!name.starts_with("minecraft:"))' in candidate
   and 'PlacementRotationSearch::None' in candidate,
 "6-facing predictor includes observer/piston/dispenser":
   all(s in candidate for s in ('"minecraft:observer"','"minecraft:piston"',
       '"minecraft:sticky_piston"','"minecraft:dispenser"',
       '"minecraft:dropper"','"minecraft:barrel"')),
 "crafter rotation prediction bounded":
   'name == "minecraft:crafter"' in candidate and
   "MaxPlacementRotationProbes = 12U" in candidate,
 "world support determines hopper/button/lightning rod instead":
   'name == "minecraft:hopper"' not in candidate
   and 'name.ends_with("_button")' not in candidate
   and 'name.ends_with("lightning_rod")' not in candidate,
 "no rails or modded block override":
   'name == "minecraft:rail"' not in candidate
   and 'starts_with("minecraft:")' in candidate,
 "scoped native logic rotation is restored in RAII":
   'ScopedPlacementRotation' in candidate
   and 'rotation=original;' in candidate,
 "strict native predictor direction matching retained":
   "getPlacementBlock(" in executor
   and "if (!placementPredictionMatches(predicted, ghost, expectedDoorUpper))" in executor,
 "no direct world state mutation":
   all(s not in executor for s in ("setBlockAndData(","setExtraBlock(")),
 "native sender uses exact matched pitch/yaw":
   "angles.pitch,angles.yaw" in executor and
   "input.mInteractRotation.get()={request->pitch,request->yaw}" in delivery,
 "player visible main and head remain unchanged":
   "savedRotation=input.mInteractRotation.get()" in delivery
   and "input.mInteractRotation.get()=savedRotation" in delivery
   and "input.mRot.get()=" not in delivery
   and "mHeadRot" not in delivery,
 "sender refuses native busy transactions":
   "PerformItemInteraction" in delivery
   and "PerformItemStackRequest" in delivery
   and "PerformBlockActions" in delivery,
 "queue validates finite pitch/yaw":
   "std::isfinite(pitch)" in delivery
   and "std::isfinite(yaw)" in delivery and "std::fabs(pitch)>90.F" in delivery,
 "queue is not false server ack":
   "return false;" in executor[executor.index("if (target.interactionRotation)"):
       executor.index("if (target.interactionRotation)")+750],
 "range rotation cap leaves other candidates eligible":
   "kRangeRotationPlanBudgetPerTick = 4" in executor
   and "rotationPlannedCandidates" in executor,
 "stale candidate validation retained":
   "stillValid(*request,*client)" in delivery
   and "request->epoch" in delivery,
 "R11 rendering remains in source":
   (root/"src/projection/mesh/ProjectionAnimatedBubblePass.cpp").exists()
   and (root/"src/projection/core/WaterloggedFaceClip.h").exists(),
}
for name,passed in checks.items():
 print(("PASS " if passed else "FAIL ")+name)
if not all(checks.values()):raise SystemExit(1)
print(f"R13_EASYPLACE_SOURCE_CONTRACT_PASS checks={len(checks)} native_server=NOT_RUN")
