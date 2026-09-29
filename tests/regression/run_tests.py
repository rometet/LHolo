"""Compile regression probes against exact production function bodies.

The extraction keeps Minecraft-only types behind small doubles. It intentionally
does not copy the implementation into a second hand-maintained test version.
"""

from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build" / "regression"
OUT.mkdir(parents=True, exist_ok=True)


def extract_braces(source: str, marker: str, trailer: str = "") -> str:
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1] + trailer
    raise AssertionError(f"unbalanced production source: {marker}")


def extract_between(source: str, start: str, end: str, occurrence: int = 0) -> str:
    offset = 0
    for _ in range(occurrence + 1):
        offset = source.index(start, offset)
        if _ < occurrence:
            offset += len(start)
    return source[offset : source.index(end, offset)]


loaders = (ROOT / "src/structure/formats/StructureFormatLoaders.cpp").read_text(encoding="utf-8")
correction = (ROOT / "src/projection/correction/ProjectionCorrectionTracker.cpp").read_text(encoding="utf-8")
java = (ROOT / "src/structure/java_to_bedrock/JavaToBedrock.cpp").read_text(encoding="utf-8")
lifecycle = (ROOT / "src/projection/runtime/ProjectionLifecycle.cpp").read_text(encoding="utf-8")

# Section-indexed arrays initialized at activation must be extended together
# when correction creates a previously empty extra-only section.
import re
initial_arrays = set(re.findall(r"state\.(\w+)\.resize\(state\.sectionBlockIndices\.size\(\)\)", lifecycle))
extra_function = extract_braces(correction, "std::size_t ensureCorrectionSection(")
extra_arrays = set(re.findall(r"state\.(\w+)\.emplace_back\(\)", extra_function))
assert initial_arrays == extra_arrays - {"sectionBlockIndices"}, (
    sorted(initial_arrays - extra_arrays), sorted(extra_arrays - initial_arrays - {"sectionBlockIndices"})
)

limits = extract_between(loaders, "constexpr std::size_t    kMaximumInflatedFileSize", ";") + ";"
p0_parts = [
    limits,
    extract_braces(loaders, "struct JavaParserLimits", ";"),
    extract_braces(loaders, "bool checkedStructureVolume("),
    extract_braces(loaders, "bool checkedPackedLongCount("),
    extract_braces(loaders, "struct JavaNbtTag", ";"),
    extract_braces(loaders, "class JavaNbtReader", ";"),
]
(OUT / "p0_extract.inc").write_text("\n\n".join(p0_parts) + "\n", encoding="utf-8")
section_parts = [
    extract_braces(correction, "SubChunkKey localSectionKey("),
    extract_braces(correction, "std::size_t ensureCorrectionSection("),
]
(OUT / "section_extract.inc").write_text("\n\n".join(section_parts) + "\n", encoding="utf-8")

mc_layers = extract_between(loaders, "auto const assign = [&](Block const* value)", "if (!block && !liquid) continue;")
java_fallback = extract_between(java, "ResolvedJavaBlock result{.mapped = true};", "return result;", 0) + "return result;"
java_mapping = extract_between(java, "ResolvedJavaBlock result{.mapped = true};", "return result;", 1) + "return result;"
correction_liquid = extract_between(correction, "auto const isBubbleColumn =", "auto const bodyMissing")
comparison = "!projectionStatesMatch(*expectedLiquid, actualLiquid)"
assert comparison in correction
(OUT / "bubble_extract.inc").write_text(
    "std::pair<Block const*, Block const*> classifyMc(Block const* primary, Block const* secondary) {\n"
    "  Block const* block{}; Block const* liquid{};\n" + mc_layers + "  return {block, liquid};\n}\n"
    "ResolvedJavaBlock classifyJavaFallback(Block const* resolved) {\n" + java_fallback + "\n}\n"
    "ResolvedJavaBlock classifyJavaMapped(Block const* resolved, Mapping const* mapping) {\n" + java_mapping + "\n}\n"
    "bool correctionLiquidMatches(Block const* expected, Block const* expectedLiquid, Region& region) {\n"
    "  BlockPos position{}; auto const& actual = region.getBlock(position);\n"
    + correction_liquid + "  return !(expectedLiquid && " + comparison + ");\n}\n",
    encoding="utf-8",
)

compiler = os.environ.get("CXX", "clang++")
source = ROOT / "tests/regression/regression_tests.cpp"
executable = OUT / ("regression_tests.exe" if os.name == "nt" else "regression_tests")
command = [compiler, "-std=c++20", "-O0", "-g", "-Wall", "-Wextra", "-Werror", "-I", str(OUT), str(source), "-o", str(executable)]
if os.name != "nt" and os.environ.get("LHOLO_SANITIZE") == "1":
    command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-D_GLIBCXX_ASSERTIONS"]
subprocess.run(command, cwd=ROOT, check=True)
subprocess.run([str(executable)], cwd=ROOT, check=True)

# Mutation control: the old section append must fail the very same assertions.
patched = (OUT / "section_extract.inc").read_text(encoding="utf-8")
line = "state.praxisCompatLiquidSections.emplace_back();"
assert patched.count(line) == 1
(OUT / "section_extract.inc").write_text(patched.replace(line, ""), encoding="utf-8")
try:
    subprocess.run(command, cwd=ROOT, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    mutation = subprocess.run([str(executable)], cwd=ROOT, capture_output=True, text=True)
    if mutation.returncode == 0 or "SECTION_INVARIANT" not in mutation.stderr:
        raise AssertionError("old section behavior was not rejected by the regression")
    print("negative control: old section append fails as expected")
finally:
    (OUT / "section_extract.inc").write_text(patched, encoding="utf-8")
    subprocess.run(command, cwd=ROOT, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
