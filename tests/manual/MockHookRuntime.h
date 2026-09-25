// Test doubles only: simulate hook dispatch; no real Minecraft process/ABI.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MockBedrock.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <type_traits>
#include <utility>

using uchar = unsigned char;
enum class HandSlot { Mainhand, Offhand };
struct Tick {};
struct BlockPos { int x{}, y{}, z{}; };
struct BlockSource { Block block; Block const& getBlock(BlockPos const&) { return block; } };
class Inventory {
public:
    std::array<ItemStack, 36> items{};
    ItemStack const& getItem(int slot) { return items.at(static_cast<std::size_t>(slot)); }
};
class Player {
public:
    Inventory inventory;
    int selected{};
    BlockSource region;
    Inventory& getInventory() { return inventory; }
    int getSelectedItemSlot() const { return selected; }
    BlockSource& getDimensionBlockSource() { return region; }
};
class LocalPlayer : public Player {};
class GameMode { public: Player& mPlayer; explicit GameMode(Player& p) : mPlayer(p) {} };
class ClientInstance { public: LocalPlayer* player{}; LocalPlayer* getLocalPlayer() { return player; } };
using IClientInstance = ClientInstance;
namespace mock {
inline auto client = std::make_shared<ClientInstance>();
inline std::map<std::string, int> originalCalls;
inline int easyTicks{};
inline int hints{};
inline int keyState = 0x8000;
inline std::uint64_t now = 1000;
template<class R, class... Args> R original(char const* name, Args&&...) {
    ++originalCalls[name];
    if constexpr (!std::is_void_v<R>) return true;
}
struct Logger {
    template<class... A> void info(A&&...) {}
    template<class... A> void warn(A&&...) {}
    template<class... A> void error(A&&...) {}
};
struct Mod {
    Logger logger;
    Logger& getLogger() { return logger; }
    std::filesystem::path getConfigDir() { return std::filesystem::temp_directory_path() / "unused-mock-lholo"; }
};
}
namespace ll::service { inline auto getClientInstance() { return mock::client; } }
namespace ll::memory { enum class HookPriority { Normal }; }
inline constexpr int VK_RBUTTON = 2;
inline int GetAsyncKeyState(int) { return mock::keyState; }
inline std::uint64_t GetTickCount64() { return mock::now; }
#define LL_TYPE_INSTANCE_HOOK(Name, Priority, Type, Method, Return, ...) \
struct Name : Type { using Type::Type; static int hook() { return 0; } static void unhook() {} \
    template<class... A> Return origin(A&&... a) { return mock::original<Return>(#Name, std::forward<A>(a)...); } \
    Return call(__VA_ARGS__); }; Return Name::call(__VA_ARGS__)

namespace lholo {
class LHolo { public: static LHolo& getInstance() { static LHolo x; return x; } mock::Mod mod; mock::Mod& getSelf() { return mod; } };
namespace i18n {
enum class TextKey { ActionHintNoMatchingItem, ActionHintManualModeBlocked };
struct Message { TextKey key; };
}
namespace structure {
inline void showActionHint(i18n::Message) { ++mock::hints; }
namespace detail { inline void tickMaterialTracker(LocalPlayer&) {} }
}
namespace place::detail {
enum class ManualTargetStatus { None, Ready, MissingMaterial };
inline ManualTargetStatus status = ManualTargetStatus::Ready;
inline ManualTargetStatus manualTargetStatusUnderCrosshair() { return status; }
inline void tickEasyPlace() { ++mock::easyTicks; }
struct PlacementState {
    bool manual{}, automatic{}, range{}, requested{}, held{};
    int radiusValue{4}, cooldown{10}, begins{}, cancels{};
    std::string aimed;
    static PlacementState& getInstance() { static PlacementState x; return x; }
    bool manualMode() const { return manual; }
    void setManualMode(bool v) { manual = v; }
    bool enabled() const { return automatic; }
    void setEnabled(bool v) { automatic = v; }
    bool rangeEnabled() const { return range; }
    void setRangeEnabled(bool v) { range = v; }
    int radius() const { return radiusValue; }
    void setRadius(int v) { radiusValue = v; }
    int autoPlacementBreakCooldownSeconds() const { return cooldown; }
    void setAutoPlacementBreakCooldownSeconds(int v) { cooldown = v; }
    std::string aimedProjectedBlockName() const { return aimed; }
    bool beginManualPress(std::uint64_t) { ++begins; requested = held = true; return true; }
    void releaseManualPress() { held = false; }
    void cancelManualPress() { ++cancels; requested = held = false; }
    void resetManualInput() { requested = held = false; }
    void resetDimensionSession() { resetManualInput(); }
    void resetWorldSession() { resetManualInput(); }
};
}
}
