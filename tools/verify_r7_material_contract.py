"""R7 source-level regression guard for projection-owned exact water material.

This is only a source contract; no claim about RenderDragon pixels or shader execution.
"""
import re
from pathlib import Path

repo = Path(__file__).resolve().parents[1]
source = (repo / "src/projection/mesh/ProjectionRenderer.cpp").read_text(encoding="utf-8")
pass_start = source.index("void submitProjectionMeshPass(")
body = source[pass_start:]
checks = {
    "private material keeps depth test and disables depth write":
        "depth.depthWriteMask = static_cast<mce::DepthWriteMask>(0);" in source,
    "private material resolved ahead of Exact Replay":
        body.index("auto const& projectedBlendMaterial =")
        < body.index("std::vector<std::size_t> nativeLiquidSections;"),
    "exact replay prefers the projected blend material":
        re.search(r"exactReplayMaterial\s*=\s*ActivePraxisLiquidMaterial\s*"
                  r"== PraxisLiquidMaterialCandidate::BlendBlock\s*"
                  r"\? &projectedBlendMaterial", body) is not None,
    "retained fallback shares projection-owned blend":
        "? &*signText : &projectedBlendMaterial);" in body,
    "alternate sign-text remains unchanged":
        "signText ? &*signText : nullptr" in body,
    "R6 proxy fallback remains projection-owned":
        re.search(r"for \(auto const liquidSection : liquidSections\)"
                  r".*?mesh.renderMesh\(\s*renderContext.mScreenContext,"
                  r"\s*projectedBlendMaterial", body, re.S) is not None,
    "R6 block actor placeholder remains projection-owned":
        re.search(r"placeholder->renderMesh\(\s*renderContext.mScreenContext,"
                  r"\s*projectedBlendMaterial", body, re.S) is not None,
    "native exact replay call retains canonical material argument":
        "*exactReplayMaterial,\n" in body,
    "resource callback retains original admission":
        "resourcepacks::state().submitted(resourcepacks::Domain::Projection)" in body,
    "runtime marker reports effective state":
        "LHoloExactR7 path=PraxisExactReplay sections=" in body
}
for label, good in checks.items():
    print(("PASS " if good else "FAIL ") + label)
if not all(checks.values()):
    raise SystemExit(1)
print(f"R7_STATIC_CONTRACT_PASS checks={len(checks)} GPU_VISUAL=NOT_RUN")
