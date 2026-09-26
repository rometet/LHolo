#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace {
std::size_t largestAllocation{};
int checks{};
int failures{};
void check(bool value, char const* label) {
    ++checks;
    if (!value) {
        ++failures;
        std::fprintf(stderr, "FAIL %s\n", label);
    }
}
} // namespace

void* operator new(std::size_t size) {
    largestAllocation = std::max(largestAllocation, size);
    if (size > 8ull * 1024ull * 1024ull) throw std::bad_alloc{};
    if (auto* result = std::malloc(size ? size : 1)) return result;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace lholo::structure::detail {
#include "p0_extract.inc"
} // namespace lholo::structure::detail

using SubChunkKey = std::tuple<int, int, int>;
struct BlockPos { int x{}, y{}, z{}; };
struct Vec3 { float x{}, y{}, z{}; };
struct SectionState {
    Vec3 center;
    bool dirty{};
    std::uint64_t requestedRevision{};
};
struct ProjectionState {
    std::map<SubChunkKey, std::size_t> localSectionIndices;
    std::vector<SubChunkKey> localSectionKeys;
    std::vector<std::vector<std::size_t>> sectionBlockIndices;
    std::vector<std::set<SubChunkKey>> sectionExtraBlockPositions;
    std::vector<SectionState> sections;
    std::vector<int> warningFillSectionMeshes;
    std::vector<int> correctionOutlineSectionMeshes;
    std::vector<int> wrongFillSectionMeshes;
    std::vector<int> wrongOutlineSectionMeshes;
    std::vector<int> nativeLiquidSectionMeshes;
    std::vector<int> praxisCompatLiquidSections;
    std::vector<int> liquidProxySectionMeshes;
    std::vector<std::size_t> nativeLiquidSectionCellCounts;
    std::vector<std::size_t> liquidProxySectionCellCounts;
    std::vector<int> blockEntityPlaceholderSectionMeshes;
};

namespace lholo::projection::detail {
#include "section_extract.inc"
} // namespace lholo::projection::detail

struct Material { bool mLiquid{}; };
struct BlockType { Material mMaterial; };
struct Block {
    std::string name;
    bool liquid{};
    int state{};
    std::string const& getTypeName() const { return name; }
    BlockType getBlockType() const { return {{liquid}}; }
};
struct Mapping { bool waterlogged{}; };
struct ResolvedJavaBlock { bool mapped{}; Block const* block{}; Block const* liquid{}; };
struct Region {
    Block body, extra, liquid;
    int extraReads{}, liquidReads{};
    Block const& getBlock(BlockPos const&) { return body; }
    Block const& getExtraBlock(BlockPos const&) { ++extraReads; return extra; }
    Block const& getLiquidBlock(BlockPos const&) { ++liquidReads; return liquid; }
};
Block water{"minecraft:water", true, 0};
int stateComparisons{};
Block const* waterSource() { return &water; }
bool projectionStatesMatch(Block const& expected, Block const& actual) {
    ++stateComparisons;
    return expected.name == actual.name && expected.state == actual.state;
}

#include "bubble_extract.inc"

namespace {
void append16(std::string& out, std::uint16_t value) {
    out.push_back(static_cast<char>(value >> 8));
    out.push_back(static_cast<char>(value));
}
void append32(std::string& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<char>(value >> shift));
}
std::string namedTag(int type, std::string const& name, std::string const& payload) {
    std::string result(1, static_cast<char>(type));
    append16(result, static_cast<std::uint16_t>(name.size()));
    return result + name + payload;
}
std::string root(std::string const& payload) {
    std::string result("\x0a\0\0", 3);
    return result + payload + std::string(1, '\0');
}
std::string arrayLength(std::uint32_t length, std::string payload = {}) {
    std::string result;
    append32(result, length);
    return result + payload;
}
bool rejected(std::string const& bytes) {
    largestAllocation = 0;
    try {
        (void)lholo::structure::detail::JavaNbtReader{bytes}.readRoot();
    } catch (std::runtime_error const&) {
        return largestAllocation < 1024 * 1024;
    } catch (std::bad_alloc const&) {
        return false;
    }
    return false;
}
void testSectionArrays() {
    ProjectionState state;
    auto const normal = lholo::projection::detail::ensureCorrectionSection(state, {0, 0, 0}, {0, 0, 0});
    auto const extra = lholo::projection::detail::ensureCorrectionSection(state, {20, 0, 0}, {20, 0, 0});
    check(normal == 0 && extra == 1 && state.sections[extra].dirty, "SECTION_INVARIANT indexes");
    auto const count = state.sections.size();
    check(count == 2 && state.localSectionIndices.size() == count, "SECTION_INVARIANT map/sections");
    check(state.localSectionKeys.size() == count, "SECTION_INVARIANT keys");
    check(state.sectionBlockIndices.size() == count, "SECTION_INVARIANT blocks");
    check(state.sectionExtraBlockPositions.size() == count, "SECTION_INVARIANT extras");
#define CHECK_SECTION(field) do { \
    check(state.field.size() == count, "SECTION_INVARIANT " #field); \
    if (state.field.size() == count) (void)state.field.at(extra); \
} while (false)
    CHECK_SECTION(warningFillSectionMeshes);
    CHECK_SECTION(correctionOutlineSectionMeshes);
    CHECK_SECTION(wrongFillSectionMeshes);
    CHECK_SECTION(wrongOutlineSectionMeshes);
    CHECK_SECTION(nativeLiquidSectionMeshes);
    CHECK_SECTION(praxisCompatLiquidSections);
    CHECK_SECTION(liquidProxySectionMeshes);
    CHECK_SECTION(nativeLiquidSectionCellCounts);
    CHECK_SECTION(liquidProxySectionCellCounts);
    CHECK_SECTION(blockEntityPlaceholderSectionMeshes);
#undef CHECK_SECTION
    check(state.sectionBlockIndices.at(extra).empty(), "SECTION_INVARIANT extra-only section");
    check(lholo::projection::detail::ensureCorrectionSection(state, {20, 0, 0}, {20, 0, 0}) == extra,
          "SECTION_INVARIANT existing section stable");
}
void testNbt() {
    using lholo::structure::detail::JavaNbtReader;
    std::string normal;
    normal += namedTag(7, "ByteArray", arrayLength(2, std::string("\x01\x02", 2)));
    normal += namedTag(11, "IntArray", arrayLength(1, std::string("\0\0\0\x03", 4)));
    normal += namedTag(12, "LongArray", arrayLength(1, std::string(7, '\0') + "\x04"));
    normal += namedTag(9, "List", std::string("\x03", 1) + arrayLength(2, std::string(7, '\0') + "\x01"));
    normal += namedTag(10, "Compound", namedTag(8, "text", std::string("\0\x02ok", 4)) + std::string(1, '\0'));
    auto result = JavaNbtReader{root(normal)}.readRoot();
    check(std::get<lholo::structure::detail::JavaNbtTag::ByteArray>(result.at("ByteArray").value).size() == 2,
          "NBT normal ByteArray");
    check(std::get<lholo::structure::detail::JavaNbtTag::IntArray>(result.at("IntArray").value).at(0) == 3,
          "NBT normal IntArray");
    check(std::get<lholo::structure::detail::JavaNbtTag::LongArray>(result.at("LongArray").value).at(0) == 4,
          "NBT normal LongArray");
    check(std::get<lholo::structure::detail::JavaNbtTag::List>(result.at("List").value).size() == 2,
          "NBT normal List");
    check(std::get<lholo::structure::detail::JavaNbtTag::Compound>(result.at("Compound").value).size() == 1,
          "NBT normal Compound");

    std::string region;
    region += namedTag(10, "Position", namedTag(3, "x", std::string(4, '\0'))
        + namedTag(3, "y", std::string(4, '\0')) + namedTag(3, "z", std::string(4, '\0')) + '\0');
    region += namedTag(10, "Size", namedTag(3, "x", std::string("\0\0\0\x01", 4))
        + namedTag(3, "y", std::string("\0\0\0\x01", 4))
        + namedTag(3, "z", std::string("\0\0\0\x01", 4)) + '\0');
    region += namedTag(9, "BlockStatePalette", std::string("\x0a", 1) + arrayLength(1,
        namedTag(8, "Name", std::string("\0\x0f", 2) + "minecraft:stone") + '\0'));
    region += namedTag(12, "BlockStates", arrayLength(1, std::string(8, '\0')));
    std::string litematic = namedTag(3, "Version", std::string("\0\0\0\x06", 4));
    litematic += namedTag(10, "Regions", namedTag(10, "Region", region + '\0') + '\0');
    auto schematic = JavaNbtReader{root(litematic)}.readRoot();
    check(schematic.contains("Regions") && schematic.contains("Version"), "NBT Java schematic fields");

    for (int type : {7, 11, 12}) {
        check(rejected(root(namedTag(type, "", arrayLength(0xffffffffu)))), "NBT negative array length");
        check(rejected(root(namedTag(type, "", arrayLength(0x7fffffffu)))), "NBT INT_MAX array length");
        check(rejected(root(namedTag(type, "", arrayLength(2)))), "NBT truncated array");
    }
    check(rejected(root(namedTag(9, "", std::string("\x01", 1) + arrayLength(0x7fffffffu)))),
          "NBT 11-byte huge-list reproducer");
    check(rejected(root(namedTag(9, "", std::string("\x01", 1) + arrayLength(0xffffffffu)))),
          "NBT negative list length");
    check(rejected(root(namedTag(9, "", std::string("\x03", 1) + arrayLength(2, std::string(4, '\0'))))),
          "NBT truncated list");
    check(rejected(root(namedTag(9, "", std::string("\0", 1) + arrayLength(1)))),
          "NBT malformed list element type");
    std::string deep(1, '\x01');
    for (int depth = 0; depth < 258; ++depth) deep = std::string("\x09", 1) + arrayLength(1, deep);
    check(rejected(root(namedTag(9, "", deep))), "NBT excessive nesting");

    std::uint64_t volume{}, longs{};
    using lholo::structure::detail::checkedStructureVolume;
    using lholo::structure::detail::checkedPackedLongCount;
    check(checkedStructureVolume(1, 1, 1, volume) && volume == 1, "NBT normal volume");
    check(checkedStructureVolume(100, 100, 100, volume) && volume == 1000000, "NBT million cells");
    check(!checkedStructureVolume(0, 1, 1, volume), "NBT zero dimension");
    check(!checkedStructureVolume(0x7fffffffu, 0x7fffffffu, 0x7fffffffu, volume),
          "NBT extreme dimensions before multiplication");
    check(checkedPackedLongCount(1, 2, longs) && longs == 1, "NBT packed normal");
    check(!checkedPackedLongCount(std::numeric_limits<std::uint64_t>::max(), 32, longs),
          "NBT packed multiplication overflow");
}
void testBubble() {
    Block bubble{"minecraft:bubble_column", true, 0};
    Block solid{"minecraft:stone", false, 0};
    Block air{"minecraft:air", false, 0};
    auto mc = classifyMc(&bubble, &water);
    check(mc.first == &bubble && mc.second == &water, "BUBBLE mc primary bubble + water");
    mc = classifyMc(&water, &bubble);
    check(mc.first == &bubble && mc.second == &water, "BUBBLE mc secondary bubble + water");
    mc = classifyMc(&bubble, nullptr);
    check(mc.first == &bubble && mc.second == nullptr, "BUBBLE mc bubble only");
    mc = classifyMc(&water, nullptr);
    check(mc.first == nullptr && mc.second == &water, "BUBBLE mc water only");
    mc = classifyMc(&solid, &water);
    check(mc.first == &solid && mc.second == &water, "BUBBLE mc solid + water");

    auto java = classifyJavaFallback(&bubble);
    check(java.mapped && java.block == &bubble && java.liquid == &water,
          "BUBBLE Java fallback bubble");
    Mapping wet{true}, dry{false};
    java = classifyJavaMapped(&bubble, &wet);
    check(java.block == &bubble && java.liquid == &water, "BUBBLE Java mapped waterlogged bubble");
    java = classifyJavaMapped(&bubble, &dry);
    check(java.block == &bubble && java.liquid == nullptr, "BUBBLE Java mapped dry bubble");
    java = classifyJavaMapped(&solid, &wet);
    check(java.block == &solid && java.liquid == &water, "BUBBLE Java waterlogged solid");
    java = classifyJavaMapped(&solid, &dry);
    check(java.block == &solid && java.liquid == nullptr, "BUBBLE Java dry solid");
    java = classifyJavaMapped(&water, &dry);
    check(java.block == nullptr && java.liquid == &water, "BUBBLE Java normal water");

    Region region{bubble, water, air};
    stateComparisons = 0;
    check(correctionLiquidMatches(&bubble, &water, region), "BUBBLE correction extra matches");
    check(region.extraReads == 1 && region.liquidReads == 0 && stateComparisons == 1,
          "BUBBLE correction uses getExtraBlock and projectionStatesMatch");
    region.extra.state = 1;
    check(!correctionLiquidMatches(&bubble, &water, region), "BUBBLE correction state mismatch");
    Region waterRegion{air, air, water};
    check(correctionLiquidMatches(nullptr, &water, waterRegion), "BUBBLE ordinary water correction");
    check(waterRegion.extraReads == 0 && waterRegion.liquidReads == 1,
          "BUBBLE ordinary water uses getLiquidBlock");
}
} // namespace

int main() {
    testSectionArrays();
    testNbt();
    testBubble();
    std::printf("P0/bubble regressions: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
