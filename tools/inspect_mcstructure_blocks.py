"""Read-only, stdlib-only Bedrock little-endian NBT structure palette inspector."""
import collections
import json
import struct
import sys
from array import array
from pathlib import Path

class Reader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def take(self, size):
        end = self.pos + size
        if end > len(self.data):
            raise ValueError("Truncated NBT data")
        x = self.data[self.pos:end]
        self.pos = end
        return x

    def unpack(self, fmt):
        size = struct.calcsize(fmt)
        return struct.unpack(fmt, self.take(size))[0]

    def name(self):
        n = self.unpack("<H")
        return self.take(n).decode("utf-8", "replace")

    def payload(self, tag):
        if tag == 1:
            return self.unpack("<b")
        if tag == 2:
            return self.unpack("<h")
        if tag == 3:
            return self.unpack("<i")
        if tag == 4:
            return self.unpack("<q")
        if tag == 5:
            return self.unpack("<f")
        if tag == 6:
            return self.unpack("<d")
        if tag == 7:
            return {"byte_array_length": len(self.take(self.unpack("<i")))}
        if tag == 8:
            return self.name()
        if tag == 9:
            subtag = self.unpack("<B")
            n = self.unpack("<i")
            if n < 0 or n > 20000000:
                raise ValueError("Bad NBT list length")
            return [self.payload(subtag) for _ in range(n)]
        if tag == 10:
            obj = {}
            while True:
                subtag = self.unpack("<B")
                if not subtag:
                    return obj
                name = self.name()
                obj[name] = self.payload(subtag)
        if tag == 11:
            n = self.unpack("<i")
            if n < 0 or n > 30000000:
                raise ValueError("Bad NBT int array length")
            vals = array("i")
            vals.frombytes(self.take(n * 4))
            if sys.byteorder != "little":
                vals.byteswap()
            return vals
        if tag == 12:
            n = self.unpack("<i")
            if n < 0 or n > 30000000:
                raise ValueError("Bad NBT long array length")
            vals = array("q")
            vals.frombytes(self.take(n * 8))
            if sys.byteorder != "little":
                vals.byteswap()
            return vals
        raise ValueError("Unexpected NBT tag " + str(tag))

def main(path):
    p = Path(path)
    r = Reader(p.read_bytes())
    tag = r.unpack("<B")
    assert tag == 10, f"Expected compound root; got {tag}"
    root_name = r.name()
    root = r.payload(tag)
    size = root.get("size", [])
    origin = root.get("structure_world_origin", [])
    structure = root.get("structure", {})
    palette_tree = structure.get("palette", {})
    default = palette_tree.get("default", {})
    palette = default.get("block_palette", [])
    arrays = structure.get("block_indices", [])
    print(json.dumps({"path": str(p), "bytes": p.stat().st_size, "root": root_name, "size": size, "origin": origin, "palette_entries": len(palette), "block_indices_layers": [len(a) for a in arrays], "entities": len(structure.get("entities", [])), "block_position_data": len(default.get("block_position_data", {})), "consumed": r.pos}, ensure_ascii=False))
    for layer, arr in enumerate(arrays):
        counts = collections.Counter(arr)
        print("LAYER", layer, "PALETTE COUNTS", len(counts))
        rows = []
        for index, n in counts.most_common():
            if index < 0:
                name = "<unset>"
            elif index < len(palette):
                entry = palette[index]
                name = entry.get("name", "unknown")
            else:
                name = f"<invalid {index}>"
            rows.append((name, n, index))
        for name, n, index in sorted(rows, key=lambda v: -v[1])[:45]:
            print(f"{n:>9}  {index:>5} {name}")
        interesting = [(name, n, index) for name,n,index in rows if any(key in name.lower() for key in ["piston","moving_block","ice","observer","redstone","slime","honey"])]
        print("INTERESTING", json.dumps(interesting, ensure_ascii=False))
    print("PISTON PALETTE DETAILS")
    for i, block in enumerate(palette):
        if "piston" in block.get("name", "").lower():
            print(i, json.dumps(block, ensure_ascii=False, default=str)[:500])

if __name__ == "__main__":
    main(sys.argv[1])
