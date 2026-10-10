from pathlib import Path
import hashlib,json
R=Path(__file__).resolve().parents[1];O=R/"build/generated/render-contract";O.mkdir(parents=True,exist_ok=True)
def extract(s,signature):
 a=s.index(signature);b=s.index("{",a);n=1;e=b+1
 while n:n+=(s[e]=="{")-(s[e]=="}");e+=1
 return s[a:e]
s=(R/"src/projection/mesh/ProjectionRenderer.cpp").read_text()
parts=[extract(s,"template<class QuadInfo>\nbool applyNativeReplayQuadOrder("),extract(s,"struct PraxisExactReplaySubmitResult")+";",extract(s,"PraxisExactReplaySubmitResult submitPraxisExactReplayImmediately(")]
(O/"WaterOrderSubmit.inc").write_text("\n".join(parts)+"\n")
# A real captured CPU stream, not a generated image or a native-render claim.
log=R.parent/"runtime-transparency-71bae529/evidence/geometry.latest-session.log"
records=[json.loads(l.split("PRAXIS_GEOMETRY_CAPTURE",1)[1]) for l in log.read_text().splitlines() if "PRAXIS_GEOMETRY_CAPTURE" in l]
record=next(x for x in records if x["phase"]=="final" and x["first"]==0 and x["recorded"]==x["total"] and x["total"]>0)
p="inline std::vector<glm::vec3> capturedPositions(){return {"+",".join("{"+",".join(format(v[a],".9g")+("f" if "." in format(v[a],".9g") or "e" in format(v[a],".9g") else ".0f") for a in range(3))+"}" for v in record["vertices"])+"};}\n"
(O/"NativeCapturedPositions.inc").write_text(p)
(O/"WATER_ORDER_FIXTURE_SOURCE.json").write_text(json.dumps({"base":"71bae52980c22d7cc186b41b8024371ad0abc35c","source_sha256":hashlib.sha256(s.encode()).hexdigest(),"log_sha256":hashlib.sha256(log.read_bytes()).hexdigest(),"captured_batch":record["batch"],"captured_vertices":record["total"],"origin":record["origin"],"generated":{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in O.glob("*.inc")},"mocked":"SDK/ScreenContext/Tessellator/MaterialPtr/texture submission use CPU mocks; source submit owner is verbatim","native_GPU":"NOT_RUN"},indent=2)+"\n")
print("Water replay source command fixture generated; captured vertices",record["total"])

