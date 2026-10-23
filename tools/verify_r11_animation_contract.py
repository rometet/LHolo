"""R11 source-level contract: preserve R8-R10 validated projection paths.
Offline checks are not native GPU or frame-rate acceptance tests."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
renderer = (root/"src/projection/mesh/ProjectionRenderer.cpp").read_text(encoding="utf-8")
body = (root/"src/projection/mesh/ProjectionSectionBuilder.cpp").read_text(encoding="utf-8")
draw = (root/"src/projection/mesh/ProjectionAnimatedBubblePass.cpp").read_text(encoding="utf-8")
state = (root/"src/projection/core/ProjectionState.h").read_text(encoding="utf-8")
geometry = (root/"src/projection/core/AnimatedBubbleGeometry.h").read_text(encoding="utf-8")
liquid = (root/"src/projection/mesh/WaterloggedNativeStreamClip.cpp").read_text(encoding="utf-8")

checks = {
    "R8 opaque stair and R9 slab clipping preserved":
        "clipWaterloggedNativeFace(rawPoints, cell.solidFaces)" in liquid
        and "waterloggedSafeClipFamily(" in body,
    "R10 cutout trapdoor water inset preserved":
        "waterloggedCutoutInset(" in liquid and "coverage.cutoutFaces" in body,
    "R5 glass and R7 exact replay private material preserved":
        "projectionPrivateBlendMaterial(state, blendMaterial)" in renderer
        and "*exactReplayMaterial" in renderer,
    "real water geometry still Exact Replay owned":
        "state.praxisCompatLiquidSections[section] = std::move(compat)" in body,
    "no particle API or world block edit":
        all(s not in draw for s in ("ParticleEngine","ParticleSystemEngine",
            "spawnParticle(", "addParticle(", "setBlock(", "setExtraBlock(")),
    "static R10 glyph no longer emitted":
        "emitBubbleColumnGlyph(" not in body
        and "bubbleGlyphCellsBuilt" not in body,
    "native bubble zero-tessellation skips placeholder cube":
        'if (layered.block->getTypeName() == "minecraft:bubble_column") continue;' in body,
    "new bubbles only drawn in projection alpha pass":
        "if (renderAlphaLayer) {" in renderer
        and "submitAnimatedBubbleOverlay(" in renderer,
    "bubble exceptions cannot reset entire schematic":
        'logger().warn("LHoloBubbleR11 failed' in renderer,
    "bounded per-frame cells and vertex count":
        "AnimatedBubbleMaxVisibleCells = 48U" in geometry
        and "AnimatedBubbleVerticesPerCell =" in geometry
        and "visible.resize(nearest)" in draw,
    "bounded scan and bounded cache":
        "AnimatedBubbleMaxCellsScannedPerFrame" in draw
        and "AnimatedBubbleMaxCachedCells" in draw,
    "cache invalidates on structure generation and mirror/rotation":
        all(s in draw for s in (
            "state.animatedBubbleGeneration != state.structureGeneration",
            "state.animatedBubbleMirror != state.cachedMirror",
            "state.animatedBubbleRotation != state.cachedRotation")),
    "layer and correction visibility retained":
        "projectionBodyShouldRender(" in draw and "isLayerVisible(" in draw,
    "separate tessellator does not overwrite UI shared one":
        "Tessellator t{renderContext.mScreenContext.tessellator.mBufferResourceService}" in draw,
    "animation tracks up/down and fades on cycle":
        "dragDown ? 1.F - phase : phase" in geometry
        and "std::min(fadeIn,fadeOut)" in geometry,
    "water remains bound to current terrain atlas":
        "std::cref(*state.terrainTexture)" in draw
        and "resourcepacks::state().submitted(resourcepacks::Domain::Projection)" in draw,
    "no stale world particle when projection hidden":
        "renderAlphaLayer" in renderer and "worldParticles" not in draw,
    "R11 runtime marker explains animated fallback":
        "LHoloBubbleR11 animatedBillboards=1" in draw,
}
for name, ok in checks.items():
    print(("PASS " if ok else "FAIL ")+name)
if not all(checks.values()):
    raise SystemExit(1)
print(f"R11_SOURCE_CONTRACT_PASS checks={len(checks)} gpu_visual=NOT_RUN")
