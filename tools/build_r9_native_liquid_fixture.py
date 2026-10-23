"""Build an offline liquid regression schematic from exact captured native
water/lava permutations; no guessed registry states and no save modification."""
from pathlib import Path
from importlib.util import spec_from_file_location, module_from_spec
import argparse, copy, hashlib, json
p=argparse.ArgumentParser()
p.add_argument("source",type=Path)
p.add_argument("reader",type=Path)
p.add_argument("--out",type=Path,required=True)
a=p.parse_args()
spec=spec_from_file_location("source_fixture",a.reader)
m=module_from_spec(spec);spec.loader.exec_module(m)
data=a.source.read_bytes()
name,root=m.Reader(data).root()
assert bytes([10])+m.pname(name)+m.encode(10,root)==data
st=m.value(root,"structure")
pal=m.value(m.value(m.value(st,"palette"),"default"),"block_palette")
native={}
for _,rec in pal:
    block=m.value(rec,"name")
    states=m.value(rec,"states")
    if block in {"minecraft:water","minecraft:lava"}:
        depth=m.value(states,"liquid_depth")
        native.setdefault((block,depth),copy.deepcopy(rec))
selected=[
 ("minecraft:water",0),("minecraft:water",1),
 ("minecraft:water",3),("minecraft:water",7),
 ("minecraft:water",11),("minecraft:lava",0),
 ("minecraft:lava",2),("minecraft:lava",4),
 ("minecraft:lava",10),("minecraft:lava",14)
]
assert all(k in native for k in selected),[k for k in selected if k not in native]
size=[16,4,12]
length=size[0]*size[1]*size[2]
layer0=[-1]*length
layer1=[-1]*length
placed=[]
def at(x,y,z):return x*size[1]*size[2]+y*size[2]+z
for i,key in enumerate(selected):
    kind=key[0].split(":")[1]
    x=2+(i%5)*2
    z=2 if kind=="water" else 7
    layer0[at(x,1,z)]=i
    placed.append({"kind":kind,"depth":key[1],"position":[x,1,z]})
# Use precisely the source NBT block-index storage subtype.
index_source=m.value(st,"block_indices")
layer_type=index_source[0][0]
def encode_array(arr):
    return [(3,v) for v in arr] if layer_type==9 else arr
result={
 "format_version":copy.deepcopy(root["format_version"]),
 "size":(9,[(3,v) for v in size]),
 "structure_world_origin":(9,[(3,0),(3,0),(3,0)]),
 "structure":(10,{
  "block_indices":(9,[(layer_type,encode_array(layer0)),
                      (layer_type,encode_array(layer1))]),
  "entities":(9,[]),
  "palette":(10,{"default":(10,{
   "block_palette":(9,[(10,native[k]) for k in selected]),
   "block_position_data":(10,{})
  })})
 })}
outbytes=bytes([10])+m.pname(name)+m.encode(10,result)
assert m.Reader(outbytes).root()[1]==result
assert a.source.read_bytes()==data
a.out.parent.mkdir(parents=True,exist_ok=True)
a.out.write_bytes(outbytes)
manifest={
 "file":a.out.name,
 "source_sha256":hashlib.sha256(data).hexdigest().upper(),
 "output_sha256":hashlib.sha256(outbytes).hexdigest().upper(),
 "origin":"exact bedrock palette from user ice.mcstructure",
 "synthetic_spatial_arrangement_only":True,
 "native_registry_and_game_visual":"NOT_RUN",
 "cases":placed,
 "size":size,
 "full_typed_nbt_roundtrip":True
}
(a.out.parent/(a.out.stem+"_manifest.json")).write_text(json.dumps(manifest,indent=2)+"\n")
print("R9_NATIVE_WATER_LAVA_FIXTURE_PASS",a.out,len(outbytes),"cases",len(selected))
