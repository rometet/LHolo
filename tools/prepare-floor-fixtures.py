"""Create actual Litematica fixtures for the shared Easy Place floor path."""
from __future__ import annotations
import gzip
import pathlib
import struct

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "test-fixtures"
OUT.mkdir(parents=True, exist_ok=True)


def nstr(value: str) -> bytes:
    data = value.encode("utf-8")
    return struct.pack(">H", len(data)) + data


def tag(kind: int, name: str, payload: bytes) -> bytes:
    return bytes([kind]) + nstr(name) + payload


def int_tag(name: str, value: int) -> bytes:
    return tag(3, name, struct.pack(">i", value))


def long_tag(name: str, value: int) -> bytes:
    return tag(4, name, struct.pack(">q", value))


def str_tag(name: str, value: str) -> bytes:
    return tag(8, name, nstr(value))


def compound(name: str, children: list[bytes]) -> bytes:
    return tag(10, name, b"".join(children) + b"\x00")


def compound_payload(children: list[bytes]) -> bytes:
    return b"".join(children) + b"\x00"


def position(name: str, x: int, y: int, z: int) -> bytes:
    return compound(name, [int_tag("x", x), int_tag("y", y), int_tag("z", z)])


def list_compound(name: str, compounds: list[bytes]) -> bytes:
    return tag(9, name, b"\x0a" + struct.pack(">i", len(compounds)) + b"".join(compounds))


def packed_blockstates(size_x: int, size_z: int, materials: int) -> bytes:
    # Litematica's BlockStateContainer uses a minimum of two bits per entry.
    width = max(2, (materials - 1).bit_length())
    total = size_x * size_z
    longs = [0] * ((total * width + 63) // 64)
    for z in range(size_z):
        for x in range(size_x):
            idx = z * size_x + x
            bit = idx * width
            value = (x + z) % materials
            longs[bit // 64] |= value << (bit % 64)
            if bit % 64 + width > 64:
                longs[bit // 64 + 1] |= value >> (64 - bit % 64)
    return tag(12, "BlockStates", struct.pack(">i", len(longs)) +
               b"".join(struct.pack(">Q", word) for word in longs))


def make_fixture(size_x: int, size_z: int, names: list[str]) -> pathlib.Path:
    count = size_x * size_z
    label = f"LHolo Floor Mixed {size_x}x{size_z}"
    region = compound_payload([
        position("Position", 0, 0, 0),
        position("Size", size_x, 1, size_z),
        list_compound("BlockStatePalette", [
            compound_payload([str_tag("Name", "minecraft:" + block)]) for block in names
        ]),
        packed_blockstates(size_x, size_z, len(names)),
        list_compound("TileEntities", []),
        list_compound("Entities", []),
    ])
    metadata = compound("Metadata", [
        str_tag("Name", label), int_tag("RegionCount", 1),
        int_tag("TotalBlocks", count), long_tag("TotalVolume", count),
        position("EnclosingSize", size_x, 1, size_z),
    ])
    nbt = compound("", [
        int_tag("Version", 6), int_tag("SubVersion", 1),
        int_tag("MinecraftDataVersion", 3700), metadata,
        compound("Regions", [tag(10, "Floor", region)]),
    ])
    file = OUT / f"LHolo_Floor_Mixed_{size_x}x{size_z}.litematic"
    with gzip.open(file, "wb") as out:
        out.write(nbt)
    assert gzip.open(file, "rb").read() == nbt
    print(file, "blocks", count, "palette", names, "bytes", file.stat().st_size)
    return file


if __name__ == "__main__":
    make_fixture(4, 16, ["stone", "deepslate"])
    make_fixture(16, 16, ["stone", "deepslate", "andesite", "diorite"])
