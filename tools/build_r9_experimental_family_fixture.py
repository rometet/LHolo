"""Offline diagnostic schematic using the checked-in Chunker 1.26.20 mappings.
Native Bedrock registry confirmation is NOT_RUN until the user loads it.
Never overwrite saves or a pre-existing test output."""
from pathlib import Path
from importlib.util import spec_from_file_location, module_from_spec
import argparse, copy, hashlib, json, re, zlib
parser=argparse.ArgumentParser()
parser.add_argument("--reader",type=Path,required=True)
parser.add_argument("--source",type=Path,required=True)
parser.add_argument("--mapping",type=Path,required=True)
parser.add_argument("--out",type=Path,required=True)
a=parser.parse_args()
spec=spec_from_file_location("fixture_source",a.reader)
m=module_from_spec(spec);spec.loader.exec_module(m)
source=a.source.read_bytes()
root_name,root=m.Reader(source).root()
assert bytes([10])+m.pname(root_name)+m.encode(10,root)==source
st=m.value(root,"structure")
native_palette=m.value(m.value(m.value(st,"palette"),"default"),"block_palette")
water=next(copy.deepcopy(rec) for _,rec in native_palette
           if m.value(rec,"name")=="minecraft:water"
           and m.value(m.value(rec,"states"),"liquid_depth")==0)
version=m.value(water,"version")
compressed=a.mapping.read_text(encoding="utf-8")
packed=bytes(int(h,16) for h in re.findall(r"0x([0-9a-fA-F]{2})",
                compressed[compressed.index("kChunkerMappingCompressed[]"):]))
rows={}
for row in zlib.decompress(packed).decode("utf-8").splitlines():
    cols=row.split("\t")
    if len(cols)==5:
        rows.setdefault((cols[1],cols[2]),cols)
# Java state keys and strings are used solely to choose exact checked-in
# mapping entries. Bedrock states are derived without guessing property values.
selected=[
 ("oak_slab_bottom","minecraft:oak_slab","type=bottom,waterlogged=true"),
 ("oak_slab_top","minecraft:oak_slab","type=top,waterlogged=true"),
 ("oak_trapdoor_closed","minecraft:oak_trapdoor","facing=north,half=bottom,open=false,powered=false,waterlogged=true"),
 ("oak_trapdoor_open","minecraft:oak_trapdoor","facing=east,half=top,open=true,powered=false,waterlogged=true"),
 ("iron_trapdoor_closed","minecraft:iron_trapdoor","facing=north,half=bottom,open=false,powered=false,waterlogged=true"),
 ("bubble_up","minecraft:bubble_column","drag=false"),
 ("bubble_down","minecraft:bubble_column","drag=true")
]
pal=[]
def record(key):
    tag_name,java_name,java_states=key
    row=rows.get((java_name,java_states))
    if not row:raise ValueError("Checked-in mapping missing: "+str(key))
    assert row[3].startswith("minecraft:")
    states={}
    fragments=row[4].split(",")
    assert "waterlogged=true" in fragments
    for item in fragments:
        k,v=item.split("=",1)
        if k=="waterlogged":continue
        if v in ("true","false"):states[k]=(1,1 if v=="true" else 0)
        elif v.lstrip("-").isdigit():states[k]=(3,int(v))
        else:states[k]=(8,v)
    block={"name":(8,row[3]),"states":(10,states),"version":(3,version)}
    return block,{"name":tag_name,"java":java_name,"properties":java_states,
                  "bedrock":row[3],"states":row[4]}
metadata=[]
for entry in selected:
    data,info=record(entry)
    pal.append((10,data))
    metadata.append(info)
pal.append((10,water))
water_id=len(pal)-1
size=[12,4,20]
volume=size[0]*size[1]*size[2]
primary=[-1]*volume
secondary=[-1]*volume
def index(x,y,z):return x*size[1]*size[2]+y*size[2]+z
for i,info in enumerate(metadata):
    z=2+i*2
    primary[index(2,1,z)]=i
    secondary[index(2,1,z)]=water_id
    if i<5:primary[index(7,1,z)]=i
    info["wet_pos"]=[2,1,z]
    if i<5:info["dry_pos"]=[7,1,z]
# Preserve the source's typed array representation of the index layers.
type_code=m.value(st,"block_indices")[0][0]
def typed(v):return [(3,x) for x in v] if type_code==9 else v
result={
 "format_version":copy.deepcopy(root["format_version"]),
 "size":(9,[(3,v) for v in size]),
 "structure_world_origin":(9,[(3,0),(3,0),(3,0)]),
 "structure":(10,{
   "block_indices":(9,[(type_code,typed(primary)),(type_code,typed(secondary))]),
   "entities":(9,[]),
   "palette":(10,{"default":(10,{"block_palette":(9,pal),
                                "block_position_data":(10,{})})})
 })
}
serialized=bytes([10])+m.pname(root_name)+m.encode(10,result)
assert m.Reader(serialized).root()[1]==result
assert a.source.read_bytes()==source
assert not a.out.exists(),"Refusing to overwrite test fixture"
a.out.parent.mkdir(parents=True,exist_ok=True)
a.out.write_bytes(serialized)
manifest={
 "output":a.out.name,
 "sha256":hashlib.sha256(serialized).hexdigest().upper(),
 "native_water_palette_source_sha256":hashlib.sha256(source).hexdigest().upper(),
 "mapping_source_sha256":hashlib.sha256(a.mapping.read_bytes()).hexdigest().upper(),
 "mapping_origin":"Checked-in Chunker 1.26.20 mappings",
 "bedrock_block_registry_resolution":"NOT_RUN",
 "native_rendering":"NOT_RUN",
 "typed_nbt_roundtrip":True,
 "cases":metadata
}
(a.out.parent/(a.out.stem+"_manifest.json")).write_text(json.dumps(manifest,indent=2)+"\n")
print("R9_EXPERIMENTAL_MAPPING_FIXTURE_PASS",a.out,"bytes",len(serialized),"cases",len(metadata))
