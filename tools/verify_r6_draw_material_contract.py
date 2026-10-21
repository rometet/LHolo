"""Static regression guard for R6's projection-owned transparent draw routes.

This deliberately checks source call sites. CPU command tests cannot exercise
RenderDragon's GPU material binding; a live Minecraft visual test is still required.
"""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
source = (root / "src/projection/mesh/ProjectionRenderer.cpp").read_text(
    encoding="utf-8"
)

checks = {
    "projection private material disables depth writes":
        "depth.depthWriteMask = static_cast<mce::DepthWriteMask>(0);" in source,
    "private material selected only on alpha pass":
        re.search(r"projectedBlendMaterial\s*=\s*renderAlphaLayer\s*\?\s*"
                  r"projectionPrivateBlendMaterial\(state, blendMaterial\)\s*:\s*"
                  r"blendMaterial", source) is not None,
    "normal transparent meshes use projected material":
        len(re.findall(r"renderMeshes\(transparentMeshes, projectedBlendMaterial\)",
                       source)) == 2,
    "liquid proxy fallback uses projected material":
        re.search(r"for \(auto const liquidSection : liquidSections\)"
                  r".*?mesh\.renderMesh\(\s*renderContext\.mScreenContext,\s*"
                  r"projectedBlendMaterial", source, re.S) is not None,
    "block-entity placeholder uses projected material":
        re.search(r"for \(auto const& placeholder : "
                  r"state\.blockEntityPlaceholderSectionMeshes\)"
                  r".*?placeholder->renderMesh\(\s*renderContext\.mScreenContext,"
                  r"\s*projectedBlendMaterial", source, re.S) is not None,
    "resource callback remains isolated":
        "resourcepacks::state().submitted(resourcepacks::Domain::Projection)" in source,
    "diagnostic can distinguish fallback and private state":
        "LHoloLiquidR6 fallbackProxySections=" in source,
}

for name, ok in checks.items():
    print(f"{'PASS' if ok else 'FAIL'} {name}")
if not all(checks.values()):
    sys.exit(1)
print(f"R6_STATIC_CONTRACT_PASS {len(checks)} checks "
      "(GPU visual behavior not verified)")
