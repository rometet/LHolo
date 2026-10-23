"""R19 source contract for backpack-first swap and native rotation sync.
This checks pipeline invariants; native multiplayer server behavior is NOT tested.
"""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
e=(r/'src/place/PlacementExecutor.cpp').read_text(encoding='utf8')
d=(r/'src/place/PlacementRotationDelivery.cpp').read_text(encoding='utf8')
f=(r/'tests/manual_placement/LatencyChecks.h').read_text(encoding='utf8')
t=(r/'tests/manual_placement/RotationDeliveryFixture.cpp').read_text(encoding='utf8')
v=(r/'tests/manual_placement/StairStrictChecks.h').read_text(encoding='utf8')
checks={
'backpack item swapped before native oriented plan':
  e.index('if (found.slot >= kHotbarSlots)',e.index('void tickEasyPlaceImpl(')) <
  e.index('if (!resolveOrientedPlacement(',e.index('void tickEasyPlaceImpl(')),
'no same-tick backpack swap + place':
  'placementState().setNextSwapAt(now + kSwapRetryMs)' in e,
'orientation has native predictor rather than forced raw setblock':
  'if (!placementPredictionMatches(predicted, ghost, expectedDoorUpper)) return false;' in e
  and 'region.setBlock(' not in e,
'configured every-direction rotations allowed to native search':
  'if (strictStairs) return false;' not in e,
'nonmatching yaw set on both synchronized native network inputs':
  'input.mRot.get()={request->pitch,request->yaw}' in d
  and 'input.mInteractRotation.get()={request->pitch,request->yaw}' in d,
'packet original rotation restored after forwarding':
  'input.mRot.get()=savedPlayerRotation;' in d
  and 'input.mInteractRotation.get()=savedRotation;' in d,
'no permanent player rotation edits':
  'input.mHeadRotation' not in d
  and 'player.setRotation(' not in e,
'uncertain sends preserve per-cell retry safety':
  'state.recordRecentPlacement(request->cellKey,now,now+500)' in d,
'stair and trapdoor native strict queue':
  'out.requireNativeRotationSync=true;' in e,
'backpack fast-path has offline tests':
  'R19 backpack-first starts official inventory swap before prediction' in f,
'stair off-axis fixture expects exact native predictor':
  'R19 stair prediction accepts every yaw' in v,
'network packet mock verifies restoration':
  'R19 off-axis actor rotation synchronized only within forwarded packet' in t,
}
for k,yes in checks.items():print(('PASS ' if yes else 'FAIL ')+k)
if not all(checks.values()):raise SystemExit(1)
print('R19_SOURCE_CONTRACT_PASS checks='+str(len(checks))+' server_rotation=NOT_RUN')
