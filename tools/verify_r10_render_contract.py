"""R10 source invariants. CPU/source checks cannot certify native GPU visuals."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
build=(root/"src/projection/mesh/ProjectionSectionBuilder.cpp").read_text(encoding="utf-8")
clip=(root/"src/projection/mesh/WaterloggedNativeStreamClip.cpp").read_text(encoding="utf-8")
h=(root/"src/projection/mesh/WaterloggedNativeStreamClip.h").read_text(encoding="utf-8")
face=(root/"src/projection/core/WaterloggedFaceClip.h").read_text(encoding="utf-8")
glyph=(root/"src/projection/core/ProjectionBubbleGlyph.h").read_text(encoding="utf-8")
render=(root/"src/projection/mesh/ProjectionRenderer.cpp").read_text(encoding="utf-8")
checks={
 "R8/R9 retained native clipping":
   "clipWaterloggedNativeFace(rawPoints, cell.solidFaces)" in clip,
 "waterlogged native owner match":
   "(*it)->structureIndex != owners[base]" in clip,
 "cutout trapdoors never cull native water":
   "if (!cell.cutoutFaces.empty()" in clip
   and "appendOriginal();" in clip
   and "waterloggedCutoutInset(" in clip,
 "cutout inset uses measured near-plane contract":
   "WaterloggedCutoutConflictRange = 0.0025F" in face
   and "WaterloggedCutoutInsetDistance = 0.006F" in face,
 "cutout pixels stay visible":
   "liquid->plane > cellCenter[liquid->axis]" in face,
 "cutout no world writes":
   all(x not in clip for x in ("setBlock(", "setExtraBlock(")),
 "native alpha layer captured without culling":
   "layered.bucket == RenderBucket::AlphaOneSided" in build
   and "coverage.cutoutFaces" in build,
 "water UV and color remain byte-identical under inset":
   "stagedUv0.push_back(uv0[i])" in clip and
   "stagedColors.push_back(colors[i])" in clip,
 "bubble Column native nongeometry handled separately":
   'typeName == "minecraft:bubble_column"' in build
   and "appendLayer(BlockRenderLayer::RenderlayerBlend)" in build,
 "bubble rings stay schematic-only":
   "emitBubbleColumnGlyph(" in build and
   "worldParticlesSpawned=0" in build,
 "bubble geometry bounded":
   "bubbleGlyphCellsBuilt < 128U" in build
   and "BubbleGlyphVerticesPerCell" in glyph,
 "bubble direction follows drag_down native state":
   'key == "drag_down"' in build and
   "state.get<::ByteTag>().data != 0" in build,
 "bubble texture sampled from native atlas":
   'HashedString("minecraft:white_concrete")' in build
   and "whiteGraphics->getTexture(0, 0)" in build,
 "bubble markers are double-sided":
   "face[3],face[2],face[1],face[0]" in glyph,
 "R5 glass and R7 liquid material still used":
   "projectionPrivateBlendMaterial(state, blendMaterial)" in render
   and "exactReplayMaterial" in render,
 "no monkeypatch of particle engines":
   "ParticleSystemEngine" not in build
   and "ParticleEmitter" not in build,
}
for desc,ok in checks.items():
    print(("PASS " if ok else "FAIL ")+desc)
if not all(checks.values()):raise SystemExit(1)
print("R10_SOURCE_CONTRACT_PASS checks="+str(len(checks))+" GPU_VISUAL=NOT_RUN")
