"""R20 material HUD and completed verifier snapshot source-safety contracts.
This does not validate a live Minecraft world/renderer.
"""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
read=lambda path:(r/path).read_text(encoding='utf8')
correction=read('src/projection/correction/ProjectionCorrectionTracker.cpp')
state=read('src/projection/core/ProjectionState.h')
invalidate=read('src/projection/runtime/ProjectionInvalidation.cpp')
lifecycle=read('src/projection/runtime/ProjectionLifecycle.cpp')
projection=read('src/projection/Projection.cpp')
tracker=read('src/structure/MaterialTracker.cpp')
material=read('src/structure/MaterialTypePresence.h')
verify=read('src/structure/Verification.h')
runtime=read('src/structure/SchematicRuntime.cpp')
model=read('src/structure/SchematicRuntime.h')
hud=read('src/structure/StructureLoader.cpp')
policy=read('src/ui/VerifierHudConsistency.h')
page=read('src/ui/VerifierPage.cpp')
menu=read('src/ui/MenuPages.cpp')
tests=read('tests/logic/LogicTests.cpp')
checks={
'body and liquid masks tracked separately':
    'MaterialBodyPresent = 1u' in material
    and 'MaterialLiquidPresent = 2u' in material,
'material presence requires loaded world':
    'if (!ready) return 0u;' in material,
'expected block identity matches rather than state':
    'expected && !bodyMissing && !bodyTypeWrong' in correction
    and 'expectedLiquid && !liquidMissing && !liquidTypeWrong' in correction,
'liquid only not obscured by incompatible solid':
    '&& !liquidCellOccupiedBySolid' in correction,
'wrong-state remains correction error not material need':
    'CorrectionState::WrongState' in correction
    and 'materialTypePresentMask(' in correction,
'material changes invalidate HUD cache even when correctness unchanged':
    'state.progressMaterialTypePresent[index] = materialPresent;' in correction
    and '++state.progressRevision;' in correction,
'mask allocated and reset with structure/dimension':
    'progressMaterialTypePresent.resize(' in lifecycle
    and 'std::fill(state.progressMaterialTypePresent.begin()' in invalidate,
'worker only copies one vector after locked progress version':
    'state.progressMaterialTypePresent' in projection
    and 'state.progressCorrect,' not in projection,
'body and liquid counted independently from type presence':
    'if (structure::materialStillNeeded(present, false))' in tracker
    and 'if (structure::materialStillNeeded(present, true))' in tracker,
'verifier item count distinguishes present from correct':
    'std::uint64_t total{}, correct{}, present{};' in verify
    and 'VerificationState::WrongState' in verify
    and 'std::min)(total, present)' in verify,
'verifier material table uses present material quantity':
    'row.count.present' in menu,
'verifier page reads immutable report tally':
    'auto const& tally = report->tally;' in page,
'HUD uses completed report tally from exact validated context':
    'completedVerifierTallyForHud()' in model
    and 'gVerification.phase()!=VerificationPhase::Completed' in runtime
    and '!reportCurrent(structure)' in runtime
    and 'schematic::completedVerifierTallyForHud()' in hud,
'HUD report progress and verifier page use same correct/errors/total':
    'result.total = tally.total();' in policy
    and 'result.visiblePlaced = tally.correct;' in policy
    and 'result.wrongState = tally.wrongState;' in policy
    and 'result.extra = tally.extra;' in policy,
'HUD labels snapshot so frozen last scan is distinguishable from live':
    'HudVerifierSnapshot' in hud,
'no self-triggered verification or world mutation':
    'schematic::verify(' not in hud
    and 'region.setBlock(' not in correction,
'new material/presentation tests installed':
    'testMaterialAndVerifierHudConsistency();' in tests,
}
for name, ok in checks.items(): print(('PASS' if ok else 'FAIL')+' '+name)
if not all(checks.values()): raise SystemExit(1)
print('R20_MATERIAL_VERIFIER_SOURCE_CONTRACT_PASS checks='+str(len(checks))+' native_server=NOT_RUN')
