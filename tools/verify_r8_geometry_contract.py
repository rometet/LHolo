"""R8 source-level wiring guard. Native GPU rendering still requires user test."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
p=(r/"src/projection/mesh/ProjectionSectionBuilder.cpp").read_text(encoding="utf-8")
c=(r/"src/projection/mesh/WaterloggedNativeStreamClip.cpp").read_text(encoding="utf-8")
b=(r/"src/projection/core/WaterloggedFaceClip.h").read_text(encoding="utf-8")
render=(r/"src/projection/mesh/ProjectionRenderer.cpp").read_text(encoding="utf-8")
checks={
    "R8 uses native stair body faces":
        'std::string_view{composite.body->getTypeName()}.ends_with("_stairs")' in p,
    "R8 ignores non-opaque body layers":
        "layered.bucket == RenderBucket::Opaque" in p,
    "R8 scopes water to originating cell":
        "liquidOwners.insert(liquidOwners.end(), addedVertices, index)" in p,
    "native internal cull compacts owners":
        "compactPerVertexField(*owners, mask.removeQuads)" in p,
    "R8 applies after native same-liquid cull":
        p.index("auto const cull = cullNativeLiquidInternalFaces(tessellator, &liquidKinds, &liquidOwners)")
        < p.index("auto const r8 = clipWaterloggedNativeStream("),
    "R8 limits to pure rectangular faces":
        "waterloggedAxisRect" in c and "WaterloggedMaxPieces" in b,
    "R8 doesn't reconstruct native fluids from block states":
        "getBlockType().mMaterial" not in c,
    "native UV0/UV1 interpolated":
        "sampleQuadUv(sourcePos, sourceUv0" in c
        and "sampleQuadUv(sourcePos, sourceUv1" in c,
    "position,color,owner,kinds,quad info replaced atomically":
        all(a in c for a in ["positions.swap(stagedPositions);","uv0.swap(stagedUv0);",
         "uv1.swap(stagedUv1);","colors.swap(stagedColors);",
         "kinds.swap(stagedKinds);","owners.swap(stagedOwners);",
         "quadInfo.swap(stagedQuadInfo);"]),
    "R8 does not replace normal water with proxy":
        "state.praxisCompatLiquidSections[section] = std::move(compat)" in p,
    "glass R5 projection private material retained":
        "projectionPrivateBlendMaterial(state, blendMaterial)" in render,
    "R7 exact replay private material retained":
        "? &projectedBlendMaterial" in render,
    "R8 has a runtime diagnostic marker":
        "LHoloWaterloggedR8 before=" in p,
    "R8 no changes to world saves or resource packs":
        "WorldPackReload" not in c and "WorldPackReload" not in b,
}
for name,passed in checks.items():
    print(("PASS " if passed else "FAIL ")+name)
if not all(checks.values()):raise SystemExit(1)
print(f"R8_SOURCE_CONTRACT_PASS checks={len(checks)} GPU=NOT_RUN")
