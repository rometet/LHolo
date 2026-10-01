"""Validate the complete checked-in Chunker table without Minecraft or a JVM."""

from collections import Counter
from pathlib import Path
import re
import zlib


def validate() -> None:
    root = Path(__file__).resolve().parents[2]
    include = root / "src/structure/java_to_bedrock/GeneratedChunkerMappings.inc"
    source = include.read_text(encoding="utf-8")
    expected_size = int(re.search(r"kChunkerMappingRawSize\s*=\s*(\d+)", source).group(1))
    payload = source.split("kChunkerMappingCompressed[] = {", 1)[1].split("};", 1)[0]
    compressed = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", payload))
    stream = zlib.decompressobj()
    raw = stream.decompress(compressed) + stream.flush()
    assert stream.eof and not stream.unused_data and not stream.unconsumed_tail, "invalid zlib stream"
    assert len(raw) == expected_size, "generated raw size mismatch"
    rows = raw.decode("utf-8").splitlines()

    commit = re.search(r"Chunker source commit: ([0-9a-f]{40})", source).group(1)
    target = re.search(r"Target Bedrock version: (\S+)", source).group(1)
    assert rows[0] == "# Generated from Chunker commit " + commit
    assert rows[1] == "# Target Bedrock version: " + target
    for relative in ("THIRD_PARTY_NOTICES.md", "tools/java_to_bedrock/README.md"):
        text = (root / relative).read_text(encoding="utf-8")
        assert commit in text and target in text, f"provenance mismatch in {relative}"

    def properties(encoded: str, label: str) -> tuple[tuple[str, str], ...]:
        if not encoded:
            return ()
        items = []
        for item in encoded.split(","):
            assert item.count("=") == 1, f"invalid property encoding: {label}"
            key, value = item.split("=", 1)
            assert key and value and key.isascii() and value.isascii(), f"invalid property: {label}"
            assert not any(ord(char) < 32 for char in item), f"control in property: {label}"
            items.append((key, value))
        assert items == sorted(items), f"noncanonical property order: {label}"
        assert len({key for key, _ in items}) == len(items), f"duplicate property: {label}"
        return tuple(items)

    previous_key = None
    previous_version = None
    previous_output = None
    records = 0
    histories = Counter()
    identifier = re.compile(r"[a-z0-9_.-]+:[a-z0-9_/.-]+")
    for line_number, line in enumerate(rows, 1):
        if line.startswith("#"):
            continue
        columns = line.split("\t")
        assert len(columns) == 5, f"invalid column count at {line_number}"
        version, java_name, java_properties, bedrock_name, bedrock_states = columns
        assert version.isdecimal() and 0 < int(version) <= 2147483647, f"invalid data version at {line_number}"
        assert identifier.fullmatch(java_name) and identifier.fullmatch(bedrock_name), f"invalid ID at {line_number}"
        properties(java_properties, java_name)
        properties(bedrock_states, bedrock_name)
        key = (java_name, java_properties)
        output = (bedrock_name, bedrock_states)
        if previous_key is not None:
            assert key >= previous_key, f"noncanonical input order at {line_number}"
            if key == previous_key:
                assert int(version) > previous_version, f"ambiguous version order at {line_number}"
                assert output != previous_output, f"uncollapsed repeated output at {line_number}"
        previous_key, previous_version, previous_output = key, int(version), output
        histories[key] += 1
        records += 1
    assert records and histories, "empty generated mapping table"
    print(f"Chunker mapping data: {records} records, {len(histories)} inputs, "
          f"{sum(count > 1 for count in histories.values())} versioned inputs, "
          f"{len(raw)} raw bytes, {len(compressed)} compressed bytes; schema/order/provenance PASS")
    print(f"Source {commit}; target Bedrock {target}. Current native registry compatibility remains unverified.")


if __name__ == "__main__":
    validate()
