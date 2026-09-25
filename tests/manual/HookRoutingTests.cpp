// SPDX-License-Identifier: GPL-3.0-or-later
// Compile the actual hook implementation against explicit test doubles.
#include "MockHookRuntime.h"
#include "../../src/place/PlaceHelper.cpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
int checks{};
void check(bool value) { ++checks; if (!value) throw std::runtime_error("check " + std::to_string(checks)); }
int main() {
    using namespace lholo::place;
    auto const root = std::filesystem::temp_directory_path() / ("lholo-hook-tests-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(p, ec); } } cleanup{root};
    try {
        check(initializeManualPlacementPolicy(root / "allow.txt") == ManualPlacementPolicyError::None);
        check(saveManualPlacementPolicy("dirt") == ManualPlacementPolicyError::None);
        LocalPlayerEasyPlaceHook local;
        mock::client->player = &local;
        local.inventory.items[0].reinit("minecraft:dirt", 1, 0);
        local.inventory.items[1].reinit("minecraft:stone", 1, 0);
        GameModeStartBuildHook start{local};
        GameModeBuildBlockHook build{local};
        GameModeUseItemHook use{local};
        auto& state = detail::PlacementState::getInstance();
        state.manual = state.requested = state.held = true;
        start.call({}, 1, HandSlot::Mainhand);
        check(mock::originalCalls["GameModeStartBuildHook"] == 1);
        check(!state.requested && !state.held);
        check(build.call({}, 1, HandSlot::Mainhand, false));
        check(mock::originalCalls["GameModeBuildBlockHook"] == 1);
        check(use.call(local.inventory.items[0], HandSlot::Mainhand));
        check(mock::originalCalls["GameModeUseItemHook"] == 1);
        state.requested = state.held = true; // stale press before slot/policy change
        local.call({});
        check(!state.requested && !state.held);
        check(mock::easyTicks == 0);
        check(mock::originalCalls["LocalPlayerEasyPlaceHook"] == 1);

        local.selected = 1; // restricted item still uses LHolo
        start.call({}, 1, HandSlot::Mainhand);
        check(state.requested && state.held);
        check(mock::originalCalls["GameModeStartBuildHook"] == 1);
        check(!build.call({}, 1, HandSlot::Mainhand, true));
        check(mock::originalCalls["GameModeBuildBlockHook"] == 1);
        local.call({});
        check(mock::easyTicks == 1);
        detail::status = detail::ManualTargetStatus::MissingMaterial;
        auto const hints = mock::hints;
        start.call({}, 1, HandSlot::Mainhand);
        check(mock::hints == hints + 1);
        check(!state.requested && !state.held);

        local.selected = 0;
        detail::status = detail::ManualTargetStatus::Ready;
        // A whitelisted main hand never authorizes an unrelated offhand.
        check(!use.call(local.inventory.items[1], HandSlot::Offhand));
        check(mock::originalCalls["GameModeUseItemHook"] == 1);
        state.manual = false;
        check(build.call({}, 1, HandSlot::Mainhand, false));
        check(mock::originalCalls["GameModeBuildBlockHook"] == 2);
        state.manual = true;
        Player server;
        GameModeBuildBlockHook serverBuild{server};
        auto const cancels = state.cancels;
        check(serverBuild.call({}, 1, HandSlot::Mainhand, false));
        check(state.cancels == cancels);

        // Non-listed items can still interact with a chest via the old path.
        local.selected = 1;
        local.region.block.type.interactive = true;
        start.call({}, 1, HandSlot::Mainhand);
        check(mock::originalCalls["GameModeStartBuildHook"] == 2);
        check(!state.requested && !state.held);
        local.region.block.type.interactive = false;
        detail::status = detail::ManualTargetStatus::None;
        check(use.call(local.inventory.items[1], HandSlot::Mainhand));
        check(mock::originalCalls["GameModeUseItemHook"] == 2);
        GameModeStopBuildHook stop{local};
        state.held = true;
        stop.call();
        check(!state.held);
        check(mock::originalCalls["GameModeStopBuildHook"] == 1);
        std::cout << "HookRoutingTests (mock Bedrock): " << checks << " checks, 0 failures\n";
    } catch (std::exception const& e) { std::cerr << "FAILED: " << e.what() << '\n'; return 1; }
}
