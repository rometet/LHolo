"""Prepare a portable CPU fixture from the current product submit functions.

The geometry is deterministic synthetic test data. No game, world, capture log,
or native renderer is accessed. Generated files live only under build/.
"""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build/generated/render-contract"
OUTPUT.mkdir(parents=True, exist_ok=True)


def extract(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


source_path = ROOT / "src/projection/mesh/ProjectionRenderer.cpp"
source = source_path.read_text(encoding="utf-8")
parts = [
    extract(source, "template<class QuadInfo>\nbool applyNativeReplayQuadOrder("),
    extract(source, "struct PraxisExactReplaySubmitResult") + ";",
    extract(source, "PraxisExactReplaySubmitResult submitPraxisExactReplayImmediately("),
]
(OUTPUT / "WaterOrderSubmit.inc").write_text("\n".join(parts) + "\n", encoding="utf-8")

# 106 quads exercise all attribute arrays and metadata permutations. These
# positions are invented here, independent of any user's schematic or world.
vertices = []
for quad in range(106):
    x, y, z = quad % 11, (quad // 11) % 3, (quad * 7) % 23
    vertices.extend((x + dx, y + dy, z) for dx, dy in ((0, 0), (1, 0), (1, 1), (0, 1)))
positions = ",".join("{" + ",".join(f"{value}.0f" for value in vertex) + "}" for vertex in vertices)
# Keep the include/function names for the existing CPU harness; their names do
# not imply a capture. The public fixture source is explicitly synthetic.
(OUTPUT / "NativeCapturedPositions.inc").write_text(
    "// SYNTHETIC_CPU_FIXTURE: no native capture or game execution.\n"
    + "inline std::vector<glm::vec3> capturedPositions(){return {" + positions + "};}\n",
    encoding="utf-8",
)
generated = {
    name: hashlib.sha256((OUTPUT / name).read_bytes()).hexdigest()
    for name in ("WaterOrderSubmit.inc", "NativeCapturedPositions.inc")
}
(OUTPUT / "WATER_ORDER_FIXTURE_SOURCE.json").write_text(
    json.dumps({
        "source": "src/projection/mesh/ProjectionRenderer.cpp",
        "source_sha256": hashlib.sha256(source_path.read_bytes()).hexdigest(),
        "fixture_kind": "SYNTHETIC_CPU_FIXTURE",
        "fixture_quads": 106,
        "fixture_vertices": len(vertices),
        "generated": generated,
        "mocked": "SDK/ScreenContext/Tessellator/MaterialPtr/texture submission use CPU mocks; product submit body is verbatim",
        "native_GPU": "NOT_RUN",
    }, indent=2) + "\n", encoding="utf-8",
)
print("Water replay source command fixture generated; synthetic vertices", len(vertices))
