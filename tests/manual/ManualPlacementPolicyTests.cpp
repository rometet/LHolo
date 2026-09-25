// SPDX-License-Identifier: GPL-3.0-or-later
#include "place/ManualPlacementPolicy.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace lholo::place;
int checks{};
void check(bool value) { ++checks; if (!value) throw std::runtime_error("check " + std::to_string(checks)); }
std::string read(std::filesystem::path const& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}
void write(std::filesystem::path const& path, std::string const& text) {
    std::ofstream out(path, std::ios::binary); out << text;
    if (!out) throw std::runtime_error("fixture write failed");
}
int main() {
    auto const root = std::filesystem::temp_directory_path() / ("lholo-manual-tests-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
    try {
        check(parseManualAllowedItems("").valid());
        check(parseManualAllowedItems(" , ;\r\n\t").items.empty());
        check(parseManualAllowedItems("\xEF\xBB\xBF" "dirt\r\n").items == std::vector<std::string>{"minecraft:dirt"});
        check(!parseManualAllowedItems("dirt,\xEF\xBB\xBF" "stone").valid());
        check(normalizeManualAllowedItem(" DIRT ") == "minecraft:dirt");
        check(normalizeManualAllowedItem("pack:Temporary/Block") == "pack:temporary/block");
        for (auto const* text : {"*", "minecraft:*", "#minecraft:logs", ":dirt", "minecraft:", "a:b:c", "a/b:c", "a b", "stone[foo=1]", "stone{}", "日本語"}) {
            check(!parseManualAllowedItems(text).valid());
        }
        auto parsed = parseManualAllowedItems("DIRT, minecraft:scaffolding\nminecraft:dirt; pack:block\r\n");
        check(parsed.valid());
        check(parsed.items.size() == 3);
        check(manualAllowedItemContains(parsed.items, "minecraft:dirt"));
        check(manualAllowedItemContains(parsed.items, "pack:block"));
        check(!manualAllowedItemContains(parsed.items, "minecraft:dirty"));
        check(!manualAllowedItemContains(parsed.items, "other:dirt"));
        check(!manualAllowedItemContains(parsed.items, ""));
        check(!manualAllowedItemContains(parsed.items, "dirt")); // runtime lookup is exact
        check(!parseManualAllowedItems("dirt,*").valid());
        check(parseManualAllowedItems("dirt,*").items.empty()); // no partial application
        check(!parseManualAllowedItems(std::string(kManualAllowlistMaxTextLength + 1, ' ')).valid());
        check(!parseManualAllowedItems(std::string(kManualAllowlistMaxIdLength + 1, 'a')).valid());
        std::string many;
        for (std::size_t i = 0; i < kManualAllowlistMaxItems; ++i) many += "pack:b" + std::to_string(i) + '\n';
        check(parseManualAllowedItems(many).valid());
        check(!parseManualAllowedItems(many + "pack:extra").valid());
        check(parseManualAllowedItems(manualAllowedItemsText(parsed.items)).items == parsed.items);

        auto const path = root / "config" / "manual-placement-allowlist.txt";
        check(initializeManualPlacementPolicy(path) == ManualPlacementPolicyError::None);
        check(!manualPlacementAllows("minecraft:dirt"));
        check(!std::filesystem::exists(path)); // no unsolicited preference rewrite
        check(saveManualPlacementPolicy("dirt,scaffolding") == ManualPlacementPolicyError::None);
        check(manualPlacementAllows("minecraft:dirt"));
        check(!manualPlacementAllows("minecraft:stone"));
        check(read(path) == "minecraft:dirt\nminecraft:scaffolding\n");
        auto const saved = read(path);
        check(saveManualPlacementPolicy("*") == ManualPlacementPolicyError::InvalidInput);
        check(read(path) == saved);
        check(manualPlacementAllows("minecraft:dirt"));
        check(reloadManualPlacementPolicy() == ManualPlacementPolicyError::None);
        check(manualPlacementAllows("minecraft:scaffolding"));
        write(path, "broken id\n");
        check(reloadManualPlacementPolicy() == ManualPlacementPolicyError::InvalidInput);
        check(manualPlacementAllows("minecraft:dirt")); // retain last known good value
        write(path, std::string(kManualAllowlistMaxTextLength + 1, 'x'));
        check(reloadManualPlacementPolicy() == ManualPlacementPolicyError::ReadFailed);
        check(manualPlacementAllows("minecraft:scaffolding"));
        write(path, "stone\n");
        check(reloadManualPlacementPolicy() == ManualPlacementPolicyError::None);
        check(manualPlacementAllows("minecraft:stone"));
        check(!manualPlacementAllows("minecraft:dirt"));

        // Snapshot publication while the render/input reader is active.
        std::atomic<bool> stop{};
        std::atomic<bool> bad{};
        std::thread reader([&] {
            while (!stop.load()) {
                if (manualPlacementAllows("minecraft:not_listed")) bad.store(true);
                (void)manualPlacementAllows("minecraft:stone");
            }
        });
        bool savesOkay = true;
        for (int i = 0; i < 80; ++i) {
            savesOkay &= saveManualPlacementPolicy(i % 2 ? "stone" : "dirt") == ManualPlacementPolicyError::None;
        }
        stop.store(true); reader.join();
        check(savesOkay);
        check(!bad.load());
        check(saveManualPlacementPolicy("") == ManualPlacementPolicyError::None);
        check(!manualPlacementAllows("minecraft:stone"));
        check(read(path).empty());

        // A directory at the destination forces a replacement failure even as
        // root. Do not delete that destination or publish the unsaved list.
        std::filesystem::remove(path);
        std::filesystem::create_directory(path);
        write(path / "keep", "untouched");
        check(saveManualPlacementPolicy("dirt") == ManualPlacementPolicyError::WriteFailed);
        check(read(path / "keep") == "untouched");
        check(!manualPlacementAllows("minecraft:dirt"));
        std::filesystem::remove_all(path);
        check(reloadManualPlacementPolicy() == ManualPlacementPolicyError::None);
        check(manualPlacementPolicySnapshot().items.empty());
        for (auto const& entry : std::filesystem::directory_iterator(path.parent_path())) {
            check(entry.path().filename().string().find(".tmp.") == std::string::npos);
        }
        auto const other = root / "other-client.txt";
        write(other, "*\n");
        check(initializeManualPlacementPolicy(other) == ManualPlacementPolicyError::InvalidInput);
        check(manualPlacementPolicySnapshot().items.empty());
        std::cout << "ManualPlacementPolicyTests: " << checks << " checks, 0 failures\n";
    } catch (std::exception const& e) {
        std::cerr << "FAILED: " << e.what() << '\n'; return 1;
    }
}
