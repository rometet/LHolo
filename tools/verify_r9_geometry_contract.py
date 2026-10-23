"""R9 source contracts. CPU-level and source-only: no GPU visual claims."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
family=(root/"src/projection/core/WaterloggedClipFamily.h").read_text(encoding="utf-8")
builder=(root/"src/projection/mesh/ProjectionSectionBuilder.cpp").read_text(encoding="utf-8")
stream=(root/"src/projection/mesh/WaterloggedNativeStreamClip.cpp").read_text(encoding="utf-8")
render=(root/"src/projection/mesh/ProjectionRenderer.cpp").read_text(encoding="utf-8")
maps=(root/"src/structure/java_to_bedrock/JavaToBedrock.cpp").read_text(encoding="utf-8")
checks={
 "R8 stationary glass material preserved":
   "projectionPrivateBlendMaterial(state, blendMaterial)" in render,
 "R7 native liquid private blend retained":
   "? &projectedBlendMaterial" in render,
 "R8 source body opaque guard unchanged":
   "layered.bucket == RenderBucket::Opaque" in builder,
 "waterlogged family opacity gate":
   "opaqueNativeLayer ? waterloggedClipCandidate(blockName)" in family,
 "stairs and slab candidate family":
   'name.ends_with("_stairs")' in family and 'name.ends_with("_slab")' in family,
 "trapdoor cutout protected":
   'name.ends_with("_trapdoor")' in family and
   'waterloggedSafeClipFamily(' in builder,
 "native body faces not guessed from block state":
   "waterloggedAxisRect(points)" in builder,
 "same-cell ownership":
   "liquidOwners.insert(liquidOwners.end(), addedVertices, index);" in builder,
 "native cull compacts owner stream":
   "compactPerVertexField(*owners, mask.removeQuads)" in builder,
 "R9 eligible lookup sorted and binary searched":
   "std::sort(eligible.begin(), eligible.end()" in stream and
   "std::lower_bound(" in stream,
 "native stream atomic staging retained":
   all(s in stream for s in ("positions.swap(stagedPositions);",
      "uv0.swap(stagedUv0);","uv1.swap(stagedUv1);",
      "colors.swap(stagedColors);","kinds.swap(stagedKinds);",
      "owners.swap(stagedOwners);","quadInfo.swap(stagedQuadInfo);")),
 "lava untouched by waterlogged clip":
   "kinds[base] != PraxisCompatLiquidKind::Water" in stream,
 "bubble water mapping still retained":
   'if (mapping->waterlogged) result.liquid = waterSource();' in maps,
 "family counters and deferred trapdoor logs":
   "LHoloWaterloggedR9 clippedStairQuads=" in builder and
   "LHoloWaterloggedR9 trapdoorClippingDeferred=1" in builder,
 "liquid diagnostic is observational":
   "LHoloLiquidR9 exactOwnedCells=" in builder,
 "resource reload ABI stays hooked":
   "resourcepacks::state().submitted(resourcepacks::Domain::Projection)" in render,
}
for k,v in checks.items():
 print(("PASS " if v else "FAIL ")+k)
if not all(checks.values()):raise SystemExit(1)
print("R9_SOURCE_CONTRACT_PASS checks="+str(len(checks))+" native_visual=NOT_RUN")
