"""Offline-only oriented Easy Place acceptance schematic.

Build each Bedrock palette entry from existing Chunker 1.26.20 exact mapping,
copy tested native stone/version NBT from the user's supplied schematic.
Do not modify original files or Minecraft worlds. Native registry placement
and server-accepted rotation remain to be tested.
"""
from pathlib import Path
from collections import defaultdict
from importlib.util import spec_from_file_location, module_from_spec
import argparse,copy,hashlib,json,re,zlib
parser=argparse.ArgumentParser()
parser.add_argument("--reader",type=Path,required=True)
parser.add_argument("--source",type=Path,required=True)
parser.add_argument("--mapping",type=Path,required=True)
parser.add_argument("--out",type=Path,required=True)
args=parser.parse_args()
spec=spec_from_file_location("native",args.reader)
native=module_from_spec(spec);spec.loader.exec_module(native)
input_bytes=args.source.read_bytes()
root_name,root=native.Reader(input_bytes).root()
assert bytes([10])+native.pname(root_name)+native.encode(10,root)==input_bytes
source_structure=native.value(root,"structure")
source_palette=native.value(native.value(native.value(source_structure,"palette"),"default"),"block_palette")
stone=next(copy.deepcopy(v) for _,v in source_palette if native.value(v,"name")=="minecraft:stone")
version=native.value(stone,"version")
raw=args.mapping.read_text(encoding="utf-8")
compressed=bytes(int(h,16) for h in re.findall(r"0x([a-fA-F0-9]{2})",raw[raw.index("kChunkerMappingCompressed[]"):]))
rows=defaultdict(list)
for line in zlib.decompress(compressed).decode("utf-8").splitlines():
    cols=line.split("\t")
    if len(cols)==5: rows[cols[1]].append(cols)
types=["minecraft:observer","minecraft:piston","minecraft:sticky_piston",
       "minecraft:dispenser","minecraft:dropper","minecraft:barrel"]
directions=["down","up","north","south","west","east"]
selected=[]
def choose_state(java_name,match):
    matching=[cols for cols in rows[java_name] if match(cols[2])]
    if len(matching)!=1: raise ValueError(f"expected unique {java_name} mapping: {len(matching)} {matching[:3]}")
    row=matching[0]
    states={}
    for token in row[4].split(","):
        if not token:continue
        key,val=token.split("=",1)
        if key=="waterlogged":continue
        if val in ("true","false"):states[key]=(1,1 if val=="true" else 0)
        elif val.lstrip("-").isdigit():states[key]=(3,int(val))
        else:states[key]=(8,val)
    assert row[3].startswith("minecraft:")
    return {"name":(8,row[3]),"states":(10,states),"version":(3,version)},row
for family in types:
    for direction in directions:
        def exact(st):
            attrs=st.split(",")
            return f"facing={direction}" in attrs and all(x not in st for x in ("powered=true","triggered=true","extended=true","open=true"))
        block,mapping=choose_state(family,exact)
        selected.append((block,{"family":family,"direction":direction,"java_state":mapping[2],
                                "bedrock_state":mapping[4]}))
# Use a dozen real orientations, not synthesized arbitrary combinations.
all_crafter=[c for c in rows["minecraft:crafter"]
    if "crafting=false" in c[2].split(",") and "triggered=false" in c[2].split(",")]
crafter=sorted({next((token.split("=",1)[1] for token in c[2].split(",")
                     if token.startswith("orientation=")),"") for c in all_crafter})
crafter=[x for x in crafter if x]
if len(crafter)<12:raise ValueError(f"missing crafter permutations: {crafter}")
chosen_crafter=crafter[:12]
for orientation in chosen_crafter:
    def same(st):
        return "crafting=false" in st.split(",") and "triggered=false" in st.split(",") and f"orientation={orientation}" in st.split(",")
    block,mapping=choose_state("minecraft:crafter",same)
    selected.append((block,{"family":"minecraft:crafter","direction":orientation,
                            "java_state":mapping[2],"bedrock_state":mapping[4]}))
assert len(selected)==48
size=[30,4,28];volume=size[0]*size[1]*size[2]
primary=[-1]*volume;secondary=[-1]*volume
def index(x,y,z):return x*size[1]*size[2]+y*size[2]+z
palette=[(10,stone)]
for idx,(block,meta) in enumerate(selected):
    if idx < 36:
        row,col=divmod(idx,6)
        x=2+col*4
        z=2+row*3
    else:
        row,col=divmod(idx-36,6)
        x=2+col*4
        z=21+row*3
    primary[index(x,1,z)]=idx+1
    primary[index(x,0,z)]=0 # actual native stone support to be built first
    meta["position"]=[x,1,z]
    meta["support_position"]=[x,0,z]
    palette.append((10,block))
index_layers=native.value(source_structure,"block_indices")
layer_type=index_layers[0][0]
def typed(v):return [(3,x) for x in v] if layer_type==9 else v
structure={
  "block_indices":(9,[(layer_type,typed(primary)),(layer_type,typed(secondary))]),
  "entities":(9,[]),
  "palette":(10,{"default":(10,{"block_palette":(9,palette),"block_position_data":(10,{})})})
}
result={
  "format_version":copy.deepcopy(root["format_version"]),
  "size":(9,[(3,v) for v in size]),
  "structure_world_origin":(9,[(3,0),(3,0),(3,0)]),
  "structure":(10,structure)
}
output=bytes([10])+native.pname(root_name)+native.encode(10,result)
assert native.Reader(output).root()[1]==result
assert args.source.read_bytes()==input_bytes
if args.out.exists():raise RuntimeError("refusing to overwrite existing fixture")
args.out.parent.mkdir(parents=True,exist_ok=True)
args.out.write_bytes(output)
manifest={
 "source_exact_native_stone_sha256":hashlib.sha256(input_bytes).hexdigest().upper(),
 "source_chunker_mapping_sha256":hashlib.sha256(args.mapping.read_bytes()).hexdigest().upper(),
 "fixture_sha256":hashlib.sha256(output).hexdigest().upper(),
 "fixture_bytes":len(output),
 "cases":len(selected),
 "six_axis":36,
 "crafter_orientations":chosen_crafter,
 "map_origin":"checked-in Chunker 1.26.20, version taken from native schematic",
 "bedrock_registry_and_server_acceptance":"NOT_RUN",
 "typed_nbt_roundtrip":True,
 "entries":[m for _,m in selected]
}
(args.out.parent/(args.out.stem+"_manifest.json")).write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
print("R13_EXACT_MAPPING_TEST_SCHEMATIC_PASS","case_count",len(selected),
      "bytes",len(output),"sha256",manifest["fixture_sha256"])
