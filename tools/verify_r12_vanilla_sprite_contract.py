"""R12 vanilla particle sheet integration checks.
This checks source contracts and cannot prove native graphics output."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
sprite=(root/"src/projection/core/VanillaBubbleSprite.h").read_text(encoding="utf-8")
renderer=(root/"src/projection/mesh/ProjectionAnimatedBubblePass.cpp").read_text(encoding="utf-8")
projection=(root/"src/projection/mesh/ProjectionRenderer.cpp").read_text(encoding="utf-8")
native=(root/"src/projection/mesh/WaterloggedNativeStreamClip.cpp").read_text(encoding="utf-8")
geometry=(root/"src/projection/core/AnimatedBubbleGeometry.h").read_text(encoding="utf-8")
checks={
 "Minecraft particle atlas path":
    'Core::PathView{"textures/particle/particles"}' in renderer,
 "engine TextureGroup owned rather than copied assets":
    "levelRenderer->mTextureGroup.get()" in renderer
    and "textureGroup->getTexture(" in renderer,
 "do not sample missing texture checkerboard":
    "mIsMissingTexture" in renderer
    and "IsMissingTexture::Yes" in renderer,
 "UV exact bounding square from native binary":
    all(s in sprite for s in ("VanillaBubbleU0 = 0.F/",
     "VanillaBubbleV0 = 16.F/","VanillaBubbleU1 = 8.F/",
     "VanillaBubbleV1 = 24.F/")),
 "sprite size uses native two-factor variation":
    ".05F*bubbleUnit" in sprite
    and ".1F)" in sprite
    and ".9F*bubbleUnit" in sprite
    and "+.2F" in sprite,
 "vanilla texture bound on direct draw":
    "{std::cref(*vanillaParticleAtlas)}" in renderer,
 "R11 fallback preserved for unavailable atlas":
    "emitAnimatedBubbleCell(" in renderer
    and "{std::cref(*state.terrainTexture)}" in renderer,
 "native texture and fallback mutually exclusive":
    "if (vanillaParticleAtlas)" in renderer
    and "} else {" in renderer,
 "no persistent world mutation":
    all(s not in renderer for s in ("spawnParticleEffect(", "addParticle(",
      "setBlock(", "setExtraBlock(")),
 "static glyph not restored":
    "emitBubbleColumnGlyph(" not in renderer,
 "alpha pass and bounded source scan preserved":
    "AnimatedBubbleMaxVisibleCells" in renderer
    and "AnimatedBubbleMaxCellsScannedPerFrame" in renderer,
 "R8/R10 accepted native geometry unchanged":
    "waterloggedCutoutInset(" in native
    and "clipWaterloggedNativeFace(" in native,
 "R5/R7 projection material preserved":
    "projectionPrivateBlendMaterial(state, blendMaterial)" in projection,
 "native ID diagnostic distinguishes approximation":
    "fullNativeEmitter=0 virtualWorldAirSafe=1" in renderer,
}
for k,v in checks.items():
 print(("PASS " if v else "FAIL ")+k)
if not all(checks.values()):raise SystemExit(1)
print(f"R12_SOURCE_CONTRACT_PASS checks={len(checks)} native_gpu=NOT_RUN")
