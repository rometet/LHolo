// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Unit tests for the LeviSchematic LGPL-3.0 material-rule port.
// Pure logic: no game process, no LeviLamina package, no renderer.

#include "io/MaterialJeExport.h"
#include "structure/MaterialBucketConverter.h"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::size_t checks{};
void check(bool ok, char const* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
using namespace lholo::structure::material_rules;

void testLiquidBucketConversion() {
    // Only a source (liquid_depth == 0) fluid is replaced by a bucket.
    auto water = substituteItem("minecraft:water", {{"liquid_depth", 0}});
    check(water && *water == "minecraft:water_bucket", "still water converts to a water bucket");
    auto lava = substituteItem("minecraft:lava", {{"liquid_depth", 0}});
    check(lava && *lava == "minecraft:lava_bucket", "still lava converts to a lava bucket");
    // A flowing level is already accounted for by its source block.
    auto flowing = substituteItem("minecraft:flowing_water", {{"liquid_depth", 3}});
    check(flowing && flowing->empty(), "flowing water level 3 needs no item");
    auto missingDepth = substituteItem("minecraft:water", {});
    check(missingDepth && *missingDepth == "minecraft:water_bucket", "absent liquid_depth behaves as source");
    auto bubble = substituteItem("minecraft:bubble_column", {});
    check(bubble && *bubble == "minecraft:water_bucket", "bubble column bills one water bucket");
    auto snow = substituteItem("minecraft:powder_snow", {});
    check(snow && *snow == "minecraft:powder_snow_bucket", "powder snow bills one bucket");
}

void testEarthenConversion() {
    for (auto name : {"minecraft:farmland", "minecraft:grass_path"}) {
        auto item = substituteItem(name, {});
        check(item && *item == "minecraft:dirt", "farmland/path are built from dirt");
    }
    auto stone = substituteItem("minecraft:stone", {});
    check(!stone, "ordinary blocks defer to the native block-to-item mapping");
}

void testUnplaceableBlocks() {
    for (auto name : {"minecraft:fire",
                      "minecraft:soul_fire",
                      "minecraft:portal",
                      "minecraft:end_portal",
                      "minecraft:end_gateway",
                      "minecraft:piston_arm_collision",
                      "minecraft:sticky_piston_arm_collision",
                      "minecraft:moving_block",
                      "minecraft:frosted_ice"}) {
        auto item = substituteItem(name, {});
        check(item && item->empty(), "engine-generated block needs no inventory item");
    }
}

void testBuildQuantity() {
    check(buildQuantity("minecraft:double_wooden_slab", {}).count == 2, "double slab needs two items");
    check(buildQuantity("minecraft:oak_slab", {}).count == 1, "single slab needs one item");
    check(buildQuantity("minecraft:snow_layer", {{"height", 0}}).count == 1, "one snow layer needs one snowball");
    check(buildQuantity("minecraft:snow_layer", {{"height", 5}}).count == 6, "snow height 5 needs six");
    check(buildQuantity("minecraft:snow_layer", {{"height", 7}}).count == 8, "snow clamps to eight");
    check(buildQuantity("minecraft:snow_layer", {{"height", 99}}).count == 8, "snow height clamps high");
    check(buildQuantity("minecraft:turtle_egg", {{"turtle_egg_count", 2}}).count == 3, "turtle egg count+1");
    check(buildQuantity("minecraft:turtle_egg", {{"turtle_egg_count", 9}}).count == 4, "turtle egg clamps to four");
    check(buildQuantity("minecraft:sea_pickle", {{"cluster_count", 1}}).count == 2, "sea pickle count+1");
    check(buildQuantity("minecraft:candle", {{"candles", 4}}).count == 4, "candles count+1");
    check(buildQuantity("minecraft:white_candle_cake", {{"candles", 3}}).count == 1,
          "candle cake is a single cake");
}

void testSecondaryHalves() {
    auto lowerDoor = buildQuantity("minecraft:wooden_door", {{"upper_block_bit", 0}});
    check(!lowerDoor.secondary, "lower door half is primary");
    check(lowerDoor.partnerOffset == std::array<int, 3>{0, 1, 0}, "lower door partner is above");
    auto upperDoor = buildQuantity("minecraft:wooden_door", {{"upper_block_bit", 1}});
    check(upperDoor.secondary, "upper door half is secondary");
    check(upperDoor.partnerOffset == std::array<int, 3>{0, -1, 0}, "upper door partner is below");
    // Bed heads are secondary and stand on their foot, mirroring the door rule.
    auto head = buildQuantity("minecraft:bed", {{"head_piece_bit", 1}, {"direction", 0}});
    check(head.secondary, "bed head is secondary");
    auto foot = buildQuantity("minecraft:bed", {{"head_piece_bit", 0}, {"direction", 0}});
    check(!foot.secondary, "bed foot is primary");
}

void testCardinalDirectionSteps() {
    // Upstream step table for head_piece_bit: 0:(0,0,1) 1:(-1,0,0) 2:(0,0,-1) 3:(1,0,0)
    struct Case { int direction; std::array<int, 3> footPartner; };
    std::vector<Case> cases{
        {0, {0, 0, 1}},
        {1, {-1, 0, 0}},
        {2, {0, 0, -1}},
        {3, {1, 0, 0}},
    };
    for (auto const& c : cases) {
        auto foot = buildQuantity("minecraft:bed", {{"head_piece_bit", 0}, {"direction", c.direction}});
        check(foot.partnerOffset == c.footPartner, "bed foot uses the upstream direction table");
        auto head = buildQuantity("minecraft:bed", {{"head_piece_bit", 1}, {"direction", c.direction}});
        check(head.partnerOffset == std::array<int, 3>{-c.footPartner[0], -c.footPartner[1], -c.footPartner[2]},
              "bed head points back at its foot");
    }
    // Out-of-range direction falls back to the zero offset rather than indexing.
    auto bogus = buildQuantity("minecraft:bed", {{"head_piece_bit", 1}, {"direction", 9}});
    check(bogus.partnerOffset == std::array<int, 3>{0, 0, 0}, "invalid direction yields no partner offset");
    // minecraft:cardinal_direction is the modern alias for direction.
    auto modern = buildQuantity("minecraft:bed", {{"head_piece_bit", 0}, {"minecraft:cardinal_direction", 2}});
    check(modern.partnerOffset == std::array<int, 3>{0, 0, -1}, "cardinal_direction is honoured");
}

void testCsvCell() {
    check(csvCell("plain") == "\"plain\"", "simple cell is quoted");
    // RFC4180: an embedded quote doubles:  "say ""hi"""
    std::string const quoted = "\"say \"";
    check(csvCell("say \"hi\"") == quoted + "\"hi\"\"\"", "inner quotes double");
    check(csvCell("") == "\"\"", "empty cell stays quoted");
}

void testCsvTextCell() {
    // The guard runs before RFC4180 quoting, so the apostrophe sits inside the
    // quoted cell and the spreadsheet sees literal text, not a formula.
    check(csvTextCell("=SUM(1,2)") == "\"'=SUM(1,2)\"", "leading equals is guarded");
    check(csvTextCell("+1") == "\"'+1\"", "leading plus is guarded");
    check(csvTextCell("-1") == "\"'-1\"", "leading minus is guarded");
    check(csvTextCell("@cmd") == "\"'@cmd\"", "leading at is guarded");
    check(csvTextCell("\tx") == "\"'\tx\"", "leading tab is guarded");
    check(csvTextCell("\rx") == "\"'\rx\"", "leading CR is guarded");
    check(csvTextCell("\nx") == "\"'\nx\"", "leading LF is guarded");
    // Ordinary text is untouched, and embedded (not leading) markers are not
    // prefixed: only the first character can start a formula.
    check(csvTextCell("石") == "\"石\"", "normal Japanese label is unchanged");
    check(csvTextCell("a=b") == "\"a=b\"", "non-leading equals is not guarded");
    check(csvTextCell("") == "\"\"", "empty cell stays quoted");
    check(csvTextCell("say \"hi\"") == "\"say \"\"hi\"\"\"", "guard still doubles inner quotes");
    check(csvTextCell("=\"a\"") == "\"'=\"\"a\"\"\"", "guard precedes quote escaping");
}

void testJsonQuote() {
    check(jsonQuote("a") == "\"a\"", "simple json string");
    check(jsonQuote("a\"b") == "\"a\\\"b\"", "json escapes quotes");
    check(jsonQuote("a\\b") == "\"a\\\\b\"", "json escapes backslash");
    check(jsonQuote(std::string{"a\nb"}) == "\"ab\"", "json drops control characters");
}

lholo::structure::detail::MaterialListSnapshot snapshot() {
    lholo::structure::detail::MaterialListSnapshot s;
    s.source = "test";
    s.requirements = {
        {"石", {}, "minecraft:stone", "minecraft:stone", 128, 64, "body:item:stone|item=stone"},
        {"水", {}, "minecraft:water", "minecraft:water_bucket", 8, 64, "liquid:water|item=water_bucket"},
        {"ラベル\"引用\"", {}, "minecraft:dirt", "minecraft:dirt", 3, 64, "body:item:dirt|item=dirt"},
    };
    s.available = {100, std::nullopt, 5};
    s.ignored = {"body:item:dirt|item=dirt"};
    return s;
}

void testJeCsv() {
    auto const data = renderJeCsv(snapshot());
    check(data.starts_with("\xEF\xBB\xBF"), "CSV starts with a UTF-8 BOM");
    check(data.find("item,aux,label,total,missing,wrong_block,wrong_state,unknown,available,to_gather\r\n")
              != std::string::npos,
          "CSV header matches the upstream column set");
    // stone: total 128, missing 28 (128-100), available 100, to_gather max(0, 28-100)=0
    check(data.find("\"minecraft:stone\",0,\"石\",128,28,0,0,0,100,0\r\n") != std::string::npos,
          "stone row carries total/shortage/available in upstream order");
    // water has no inventory reading: shortage unknown -> falls back to total.
    check(data.find("\"minecraft:water_bucket\",0,\"水\",8,8,0,0,0,0,8\r\n") != std::string::npos,
          "unknown inventory treats the whole requirement as missing");
    check(data.find("minecraft:dirt") == std::string::npos, "ignored rows are excluded");
    check(std::count(data.begin(), data.end(), '\n') == 3, "one header plus two rows");
}

void testJeCsvMultiplier() {
    auto const data = renderJeCsv(snapshot(), JeExportOptions{nullptr, 4});
    check(data.find("\"minecraft:stone\",0,\"石\",512,112,0,0,0,100,0\r\n") != std::string::npos,
          "multiplier scales totals and shortages but not carried stock");
    auto const zero = renderJeCsv(snapshot(), JeExportOptions{nullptr, 0});
    check(zero.find("\"minecraft:stone\",0,\"石\",128,28,0,0,0,100,0\r\n") != std::string::npos,
          "a zero multiplier is clamped to one, as upstream does");
}

void testJeCsvLabels() {
    std::vector<std::string> names{"Stone", "", "Dirt"};
    auto const data = renderJeCsv(snapshot(), JeExportOptions{&names, 1});
    check(data.find("\"Stone\"") != std::string::npos, "provided localised name wins");
    check(data.find("\"水\"") != std::string::npos, "empty provided name falls back to the row name");

    // Escaping must be proven on a row that is actually rendered: the fixture's
    // quoted-label row is ignored, so build a dedicated un-ignored snapshot.
    auto quoted = snapshot();
    quoted.ignored.clear();
    // RFC4180: label `ラベル"引用"` becomes "ラベル""引用""" (open quote, doubled
    // inner quotes, closing quote).
    std::string const expectedCell = "\"ラベル\"\"引用\"\"\"";
    check(renderJeCsv(quoted).find(expectedCell) != std::string::npos,
          "labels stay CSV-escaped in the CSV export");
    check(renderJeJson(quoted).find("\"label\": \"ラベル\\\"引用\\\"\"") != std::string::npos,
          "labels stay JSON-escaped in the JSON export");

    // CSV text cells are formula-guarded; JSON must keep the exact original
    // label so downstream tooling sees the unmodified name.
    auto guarded = snapshot();
    guarded.ignored.clear();
    guarded.requirements[0].displayName = "=SUM(1,2)";
    guarded.requirements[1].displayName = "+石";
    guarded.requirements[2].displayName = "@dirt";
    auto const csv = renderJeCsv(guarded);
    check(csv.find("\"'=SUM(1,2)\"") != std::string::npos, "CSV guards a leading equals label");
    check(csv.find("\"'+石\"") != std::string::npos, "CSV guards a leading plus label");
    check(csv.find("\"'@dirt\"") != std::string::npos, "CSV guards a leading at label");
    auto const json = renderJeJson(guarded);
    check(json.find("\"label\": \"=SUM(1,2)\"") != std::string::npos, "JSON keeps the exact formula label");
    check(json.find("\"label\": \"+石\"") != std::string::npos, "JSON keeps the exact plus label");
    check(json.find("\"label\": \"@dirt\"") != std::string::npos, "JSON keeps the exact at label");
    check(json.find("'=SUM") == std::string::npos, "JSON never gains the CSV apostrophe guard");

    // The item ID column is a text cell too, so it gets the same guard.
    auto itemGuard = snapshot();
    itemGuard.ignored.clear();
    itemGuard.requirements[0].itemId = "=cmd|calc";
    auto const itemCsv = renderJeCsv(itemGuard);
    check(itemCsv.find("\"'=cmd|calc\",0,") != std::string::npos, "CSV guards a leading equals item ID");
    check(renderJeJson(itemGuard).find("\"item\": \"=cmd|calc\"") != std::string::npos,
          "JSON keeps the exact item ID");
}

void testJeJson() {
    auto const data = renderJeJson(snapshot());
    check(data.starts_with("[\n") && data.ends_with("]\n"), "JSON array envelope");
    check(data.find("  {\"item\": \"minecraft:stone\", \"aux\": 0, \"label\": \"石\", \"total\": 128, "
                    "\"missing\": 28, \"available\": 100}") != std::string::npos,
          "JSON members follow upstream order");
    check(data.find(",\n") != std::string::npos, "rows are comma separated");
    check(data.find("minecraft:dirt") == std::string::npos, "ignored rows are excluded from JSON");
}

void testSaturatingScale() {
    auto const maximum = std::numeric_limits<std::uint64_t>::max();
    check(saturatingScale(10, 5) == 50, "plain scaling");
    check(saturatingScale(maximum, 2) == maximum, "overflow saturates");
    check(saturatingScale(0, 99) == 0, "zero value stays zero");
}

} // namespace

int main() {
    try {
        testLiquidBucketConversion();
        testEarthenConversion();
        testUnplaceableBlocks();
        testBuildQuantity();
        testSecondaryHalves();
        testCardinalDirectionSteps();
        testCsvCell();
        testCsvTextCell();
        testJsonQuote();
        testJeCsv();
        testJeCsvMultiplier();
        testJeCsvLabels();
        testJeJson();
        testSaturatingScale();
    } catch (std::exception const& e) {
        std::printf("FAIL: %s (after %zu checks)\n", e.what(), checks);
        return 1;
    }
    std::printf("PASS: %zu checks\n", checks);
    return 0;
}
