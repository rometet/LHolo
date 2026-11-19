"""Generate a four-orientation observer Litematica for slot-switch regression.
Uses the proven floor-fixture NBT writer; writes only into test-fixtures.
"""
from __future__ import annotations

import gzip
import importlib.util
import pathlib
import re
import struct
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "lholo_floor_fixture_writer", ROOT / "tools" / "prepare-floor-fixtures.py"
)
assert spec is not None and spec.loader is not None
writer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(writer)

facings = ("north", "east", "south", "west")
# Fail the fixture generation if any palette state is not an exact key in
# LHolo's bundled Chunker mapping. Missing 'powered' caused the original error.
mapping_source = (ROOT / "src" / "structure" / "java_to_bedrock" /
                  "GeneratedChunkerMappings.inc").read_text(encoding="utf-8")
compressed_literal = mapping_source.split("kChunkerMappingCompressed[] = {", 1)[1].split("};", 1)[0]
compressed = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", compressed_literal))
chunker_mapping = zlib.decompress(compressed).decode("utf-8")
for facing in facings:
    key = f"\tminecraft:observer\tfacing={facing},powered=false\t"
    assert key in chunker_mapping, f"Observer orientation unmapped: {facing}"
size_x = len(facings)
count = size_x
palette = [
    writer.compound_payload([
        writer.str_tag("Name", "minecraft:observer"),
        # Chunker's observer mapping requires BOTH Java properties. Omitting
        # powered previously made LHolo reject the palette as unresolved.
        writer.compound("Properties", [
            writer.str_tag("facing", face),
            writer.str_tag("powered", "false"),
        ]),
    ])
    for face in facings
]
# With two bits per entry, index x selects each of the 4 orientation variants.
packed = sum(i << (i * 2) for i in range(count))
region = writer.compound_payload([
    writer.position("Position", 0, 0, 0),
    writer.position("Size", size_x, 1, 1),
    writer.list_compound("BlockStatePalette", palette),
    writer.tag(12, "BlockStates", struct.pack(">iQ", 1, packed)),
    writer.list_compound("TileEntities", []),
    writer.list_compound("Entities", []),
])
nbt = writer.compound("", [
    writer.int_tag("Version", 6),
    writer.int_tag("SubVersion", 1),
    writer.int_tag("MinecraftDataVersion", 3700),
    writer.compound("Metadata", [
        writer.str_tag("Name", "LHolo Observer Nonselected Hotbar"),
        writer.int_tag("RegionCount", 1),
        writer.int_tag("TotalBlocks", count),
        writer.long_tag("TotalVolume", count),
        writer.position("EnclosingSize", size_x, 1, 1),
    ]),
    writer.compound("Regions", [writer.tag(10, "Observers", region)]),
])
dst = ROOT / "test-fixtures" / "LHolo_Observer_Hotbar_4x1.litematic"
with dst.open("wb") as raw:
    with gzip.GzipFile(filename="", fileobj=raw, mode="wb", mtime=0) as f:
        f.write(nbt)
assert gzip.open(dst, "rb").read() == nbt
print(f"{dst} {dst.stat().st_size} bytes facings={facings}")
