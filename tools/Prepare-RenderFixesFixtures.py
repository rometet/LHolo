from pathlib import Path
import hashlib, json, subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/generated/render-contract'
OUT.mkdir(parents=True, exist_ok=True)
BASE = '7247d5476f4478bfcd8387fa1ba2fdc3c16ca978'
builder = 'src/projection/mesh/ProjectionSectionBuilder.cpp'

def extract(s, signature):
    start = s.rindex(signature)
    opening = s.index('{', start)
    depth, end = 1, opening+1
    while depth:
        depth += (s[end] == '{') - (s[end] == '}')
        end += 1
    return s[start:end]

def expression(s, start, end):
    a = s.index(start)
    return s[a:s.index(end, a)+len(end)]

current = (ROOT / builder).read_text(encoding='utf-8-sig')
base = subprocess.check_output(['git', '-C', str(ROOT), 'show', BASE+':'+builder]).decode('utf-8')
for label, s in [('Current', current), ('Baseline', base)]:
    color = extract(s, 'std::vector<std::size_t> buildPraxisCompatLiquidSectionData(')
    derived = expression(color, 'auto const derived =', ');')
    proxy = extract(s, 'void buildLiquidProxySectionMesh(')
    alpha = expression(proxy, 'auto const alpha =', '));')
    (OUT / (label+'DerivedAlpha.inc')).write_text(derived+'\n', encoding='utf-8')
    (OUT / (label+'ProxyAlpha.inc')).write_text(alpha+'\n', encoding='utf-8')
opacity_stage = subprocess.check_output(['git', '-C', str(ROOT), 'show',
    '46d3c9a:'+builder]).decode('utf-8')
for name, source in [('CurrentProxyBody', current), ('OpacityStageProxyBody', opacity_stage)]:
    (OUT / (name+'.inc')).write_text(extract(source, 'void buildLiquidProxySectionMesh(')+'\n', encoding='utf-8')
(OUT / 'ProxyColorEmitter.inc').write_text(extract(current, 'void setColorAbgr(')+'\n', encoding='utf-8')
renderer = 'src/projection/mesh/ProjectionRenderer.cpp'
def liquid_order(source):
    body = extract(source, 'void submitProjectionMeshPass(')
    markers = [('prime', 'ActorShaderManager::setupShaderParameters('),
               ('exact', 'submitPraxisExactReplayImmediately('),
               ('retained', 'mesh->renderMesh('), ('normal', 'auto renderMeshes = ')]
    found = []
    for name, marker in markers:
        start = 0
        while (pos := body.find(marker, start)) >= 0:
            found.append((pos, name))
            start = pos + len(marker)
    return [name for _, name in sorted(found)]
before = subprocess.check_output(['git','-C',str(ROOT),'show',BASE+':'+renderer]).decode('utf-8')
now = (ROOT / renderer).read_text(encoding='utf-8-sig')
assert liquid_order(before) == ['exact','retained','prime','normal']
assert liquid_order(now) == ['prime','exact','retained','prime','normal']
(OUT/'LightingSubmissionOrder.inc').write_text(
    'inline constexpr std::array<const char*,4> baselineLightingOrder{"exact","retained","prime","normal"};\n'
    'inline constexpr std::array<const char*,5> currentLightingOrder{"prime","exact","retained","prime","normal"};\n',encoding='utf-8')
# Preserve the original pre-normal guard byte for byte. Require the additional
# liquid-only guard to use precisely the same SDK statement and MAX arguments.
original = extract(before,'if (auto* player = client.getLocalPlayer())')
normal_now = now[now.index('    // The ItemInHand/Entity materials'):]
assert extract(normal_now,'if (auto* player = client.getLocalPlayer())') == original
liquid_now = now[now.index('        if (!nativeLiquidSections.empty())'):now.index('            auto const signText = render::resolveSignTextMaterial(blendMaterial);')]
assert [line.strip() for line in extract(liquid_now,'if (auto* player = client.getLocalPlayer())').splitlines()] == [line.strip() for line in original.splitlines()]
assert now.count('ActorShaderManager::setupShaderParameters(') == 2
assert before.count('ActorShaderManager::setupShaderParameters(') == 1
world = (ROOT/'src/projection/world/ProjectionVirtualWorld.cpp').read_text(encoding='utf-8-sig')
header = (ROOT/'src/projection/world/ProjectionVirtualWorld.h').read_text(encoding='utf-8-sig')
scope = extract(header,'class ScopedRegionWriteSuppression')+';\n'
scope += 'thread_local bool gSuppressRegionWrites{};\n'
for signature in ['ScopedRegionWriteSuppression::ScopedRegionWriteSuppression()',
                  'ScopedRegionWriteSuppression::~ScopedRegionWriteSuppression()', 'bool regionWritesSuppressed()']:
    scope += extract(world,signature)+'\n'
(OUT/'RegionWriteScope.inc').write_text(scope,encoding='utf-8')
for path, signature, call in [
    ('src/projection/world/ProjectionPlacement.cpp','void pairProjectedChests(', 'chest->_tryToPairWith('),
    (renderer,'void submitProjectedBlockActorPass(', 'dispatcher.render(')]:
    body = extract((ROOT/path).read_text(encoding='utf-8-sig'),signature)
    old_body = extract(subprocess.check_output(['git','-C',str(ROOT),'show',BASE+':'+path]).decode('utf-8'),signature)
    assert 'ScopedRegionWriteSuppression projectedWrites;' not in old_body
    assert body.index('ScopedRegionWriteSuppression projectedWrites;') < body.index(call)
    assert body.replace('    ScopedRegionWriteSuppression projectedWrites;\n','') == old_body
record = {'base': BASE, 'generated': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
           for p in OUT.glob('*.inc')}, 'native_GPU': 'NOT_RUN',
          'scope': 'Verbatim production color expressions and whole proxy owner; CPU commands, no engine ownership proof'}
(OUT / 'FIXTURE_SOURCE.json').write_text(json.dumps(record, indent=2)+'\n', encoding='utf-8')
print('Verbatim render-contract fixtures generated')
