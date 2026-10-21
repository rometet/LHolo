"""Deterministic render-contract source fixture for Glass R5 + resource ABI v4.
The immutable 424-vertex CPU-captured sample is copied from the audited
R9 build, verified against its published WATER_ORDER_FIXTURE_SOURCE.json.
Only the live GlassR5+R9 merged renderer source generates WaterOrderSubmit.inc.
Never substitutes mock renderer code for the current source.
"""
from pathlib import Path
import hashlib
import json
import shutil

ROOT=Path(__file__).resolve().parents[1]
OUTPUT=ROOT/"build/generated/render-contract"
FIXTURE=ROOT/"tests/translucency/fixtures/NativeCapturedPositions.inc"
METADATA=ROOT/"tests/translucency/fixtures/WATER_ORDER_FIXTURE_SOURCE_R9.json"

def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def extract(text,signature):
    start=text.index(signature)
    begin=text.index("{",start)
    depth=1; end=begin+1
    while depth:
        depth+=(text[end]=="{")-(text[end]=="}")
        end+=1
    return text[start:end]

def main():
    provenance=json.loads(METADATA.read_text(encoding="utf-8"))
    assert provenance.get("captured_vertices")==424
    assert digest(FIXTURE)==provenance["generated"]["NativeCapturedPositions.inc"],"Fixture source SHA mismatch"
    source=ROOT/"src/projection/mesh/ProjectionRenderer.cpp"
    text=source.read_text(encoding="utf-8")
    pieces=[
        extract(text,"template<class QuadInfo>\nbool applyNativeReplayQuadOrder("),
        extract(text,"struct PraxisExactReplaySubmitResult")+";",
        extract(text,"PraxisExactReplaySubmitResult submitPraxisExactReplayImmediately("),
    ]
    OUTPUT.mkdir(parents=True,exist_ok=True)
    (OUTPUT/"WaterOrderSubmit.inc").write_text("\n".join(pieces)+"\n",encoding="utf-8")
    shutil.copy2(FIXTURE, OUTPUT/"NativeCapturedPositions.inc")
    report={
        "renderer_source_sha256":digest(source),
        "source_test":"merged current GlassR5+PraxisR9 code, extracted verbatim",
        "captured_424_vertex_fixture_sha256":digest(FIXTURE),
        "captured_fixture_origin":"R9 captured geometry; read-only preserved fixture",
        "generator":"tools/Prepare-WaterOrderR5V4.py",
        "render_fixture_sha256":digest(OUTPUT/"WaterOrderSubmit.inc"),
        "gpu_visual_confirmation":"NOT_RUN"
    }
    (OUTPUT/"R5V4_WATERORDER_SOURCE.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print("R5V4_WATER_FIXTURE_VALID",report)

if __name__=="__main__":
    main()
