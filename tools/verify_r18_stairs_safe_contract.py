"""R18 fail-closed stair placement source contract; not live server verification."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
exe=(r/'src/place/PlacementExecutor.cpp').read_text(encoding='utf-8')
delivery=(r/'src/place/PlacementRotationDelivery.cpp').read_text(encoding='utf-8')
header=(r/'src/place/PlacementRotationDelivery.h').read_text(encoding='utf-8')
stats=(r/'src/place/PlaceHelper.cpp').read_text(encoding='utf-8')
fixture=(r/'tests/manual_placement/StairStrictChecks.h').read_text(encoding='utf-8')
rot=(r/'tests/manual_placement/RotationDeliveryFixture.cpp').read_text(encoding='utf-8')
checks={
    'stairs detected for all stair suffixes':
        'strictStairs = ghost.getTypeName().ends_with("_stairs")' in exe,
    'no synthetic air-mPos path for stairs':
        'if (strictStairs && at == cell) return false;' in exe,
    'stairs must plan for current natural rotation':
        'out.interactionRotation=detail::PlacementInteractionAngles{actual.x,actual.y};' in exe
        and 'out.requireNativeRotationSync=true;' in exe,
    'stairs cannot use synthetic rotation override':
        'if (strictStairs) return false;' in exe,
    'stairs go to native AuthInput instead of legacy':
        'target.requireNativeRotationSync' in exe
        and 'std::move(transactionBase),target.requireNativeRotationSync' in exe,
    'native main and interaction angles are both checked':
        'nativeRotationMatches(input.mRot.get(),request->pitch,request->yaw)' in delivery
        and 'nativeRotationMatches(input.mInteractRotation.get(),' in delivery,
    'mismatching native angles are dropped not transmitted':
        'telemetry::count(telemetry::Event::StairRotationDrift);' in delivery
        and 'return; // never attach a stair that the server may rotate differently' in delivery,
    'stair send cannot rewrite genuine native yaw':
        'if (!request->requireNativeRotationSync)' in delivery,
    'main head and camera rotations are never changed':
        'input.mRot.get()={' not in delivery
        and 'input.mHeadRotation' not in delivery,
    'hotbar changes wait one tick':
        'if (placement.requireNativeRotationSync) return;' in exe
        and 'if (target.requireNativeRotationSync) return;' in exe,
    'stair admission is only queued not claimed as ACK':
        'return detail::PlacementDispatchResult::Queued;' in exe
        and 'setNextPlaceAt(now+40)' in delivery,
    'strict native sync is opt-in so other families remain unchanged':
        'bool requireNativeRotationSync=false' in header,
    'R18 stair matrix verifies aim mismatch and exact half':
        'off-axis stair is refused' in fixture
        and 'R18 stair waits a tick after hotbar selection' in fixture,
    'R18 native fixture tests both main and interact mismatch':
        'R18 rotated actor input drops stair' in rot
        and 'R18 wrong interaction direction drops stair' in rot,
    'R18 telemetry distinguishes plan and native-rotation rejection':
        'stairQueued={} stairPlanFail={} stairSyncDrop={}' in stats,
    'no world writes and previous duplicate-cell guard unchanged':
        'region.setBlock(' not in exe
        and 'kCellLockMs = 500' in exe
        and 'kMinSendIntervalMs = 40' in exe,
}
for name,ok in checks.items(): print(('PASS ' if ok else 'FAIL ')+name)
if not all(checks.values()): raise SystemExit(1)
print('EASYPLACE_R18_STAIRS_SAFE_CONTRACT_PASS checks='+str(len(checks))+' server=NOT_RUN')
