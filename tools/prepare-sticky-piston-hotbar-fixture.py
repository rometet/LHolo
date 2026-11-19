"""Four facing-specific sticky pistons for Easy Place item-identity validation.

A deliberately small Litematica file; source properties are validated against
LHolo's bundled Chunker mapping before writing to avoid unresolved palettes.
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
    "floor_fixture_writer", ROOT / "tools" / "prepare-floor-fixtures.py"
)
assert spec is not None and spec.loader is not None
w = importlib.util.module_from_spec(spec)
spec.loader.exec_module(w)

facings = ("north", "east", "south", "west")
text = (ROOT / "src" / "structure" / "java_to_bedrock" /
        "GeneratedChunkerMappings.inc").read_text(encoding="utf-8")
src = text.split("kChunkerMappingCompressed[] = {", 1)[1].split("};", 1)[0]
mapping = zlib.decompress(
    bytes(int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})", src))
).decode("utf-8")
for facing in facings:
    required = f"\tminecraft:sticky_piston\textended=false,facing={facing}\t"
    assert required in mapping, f"Unmapped sticky piston: {facing}"

palette = [
    w.compound_payload([
        w.str_tag("Name", "minecraft:sticky_piston"),
        w.compound("Properties", [
            w.str_tag("extended", "false"),
            w.str_tag("facing", direction),
        ]),
    ]) for direction in facings
]
# 4 palette entries => 2 bits per cell. +X axis across the region.
packed = sum(index << (index * 2) for index in range(4))
region = w.compound_payload([
    w.position("Position", 0, 0, 0),
    w.position("Size", 4, 1, 1),
    w.list_compound("BlockStatePalette", palette),
    w.tag(12, "BlockStates", struct.pack(">iQ", 1, packed)),
    w.list_compound("TileEntities", []),
    w.list_compound("Entities", []),
])
nbt = w.compound("", [
    w.int_tag("Version", 6),
    w.int_tag("SubVersion", 1),
    w.int_tag("MinecraftDataVersion", 3700),
    w.compound("Metadata", [
        w.str_tag("Name", "LHolo Sticky Piston Hotbar Regression"),
        w.int_tag("RegionCount", 1),
        w.int_tag("TotalBlocks", 4),
        w.long_tag("TotalVolume", 4),
        w.position("EnclosingSize", 4, 1, 1),
    ]),
    w.compound("Regions", [w.tag(10, "StickyPistons", region)]),
])
dst = ROOT / "test-fixtures" / "LHolo_StickyPiston_Hotbar_4x1.litematic"
with dst.open("wb") as stream:
    with gzip.GzipFile(filename="", fileobj=stream, mode="wb", mtime=0) as gz:
        gz.write(nbt)
assert gzip.decompress(dst.read_bytes()) == nbt
print(f"validated {dst} size={dst.stat().st_size} facing={facings}")
