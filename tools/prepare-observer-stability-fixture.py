"""Generate a 4x8 observer stress fixture from exact mapped Java states."""
from __future__ import annotations
import gzip
import importlib.util
from pathlib import Path
import re
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("writer", ROOT/"tools"/"prepare-floor-fixtures.py")
assert spec and spec.loader
w = importlib.util.module_from_spec(spec)
spec.loader.exec_module(w)
faces = ("north","east","south","west")
source = (ROOT/"src"/"structure"/"java_to_bedrock"/"GeneratedChunkerMappings.inc").read_text(encoding="utf-8")
segment = source.split("kChunkerMappingCompressed[] = {",1)[1].split("};",1)[0]
mapping = zlib.decompress(bytes(int(v,16) for v in re.findall(r"0x([0-9a-fA-F]{2})",segment))).decode()
for face in faces:
    assert f"\tminecraft:observer\tfacing={face},powered=false\t" in mapping, face
palette = [
    w.compound_payload([w.str_tag("Name","minecraft:observer"),w.compound("Properties",[
        w.str_tag("facing",f),w.str_tag("powered","false")])])
    for f in faces
]
size_x,size_z=4,8
# X varies fastest; one 64-bit NBT long stores exactly 32 two-bit entries.
indices = [(x+z)%4 for z in range(size_z) for x in range(size_x)]
packed = sum(index << (2*i) for i,index in enumerate(indices))
region = w.compound_payload([
    w.position("Position",0,0,0),
    w.position("Size",size_x,1,size_z),
    w.list_compound("BlockStatePalette",palette),
    w.tag(12,"BlockStates",struct.pack(">iQ",1,packed)),
    w.list_compound("TileEntities",[]),
    w.list_compound("Entities",[]),
])
nbt = w.compound("",[
    w.int_tag("Version",6),w.int_tag("SubVersion",1),
    w.int_tag("MinecraftDataVersion",3700),
    w.compound("Metadata",[
        w.str_tag("Name","LHolo Observer Reliability 4x8"),
        w.int_tag("RegionCount",1),
        w.int_tag("TotalBlocks",32),
        w.long_tag("TotalVolume",32),
        w.position("EnclosingSize",size_x,1,size_z),
    ]),
    w.compound("Regions",[w.tag(10,"ObserverStress",region)]),
])
dst = ROOT/"test-fixtures"/"LHolo_Observer_Stability_4x8.litematic"
with dst.open("wb") as file:
    with gzip.GzipFile(filename="",fileobj=file,mode="wb",mtime=0) as gz:
        gz.write(nbt)
assert gzip.decompress(dst.read_bytes()) == nbt
assert len(indices)==32 and len(palette)==4
print("VALID",dst,"bytes",dst.stat().st_size,"blocks",len(indices),"directions",faces)
