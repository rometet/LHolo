#pragma once
#include "structure/ClientViewState.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

class LocalPlayer;

namespace lholo::structure::capture {

enum class CaptureMode : std::uint8_t { Client, Singleplayer };
enum class PointSlot : std::uint8_t { First, Second };

struct Point {
    int x{};
    int y{};
    int z{};

    bool operator==(Point const&) const = default;
};

struct Draft {
    CaptureMode         mode{CaptureMode::Client};
    std::optional<Point> first;
    std::optional<Point> second;
    bool                includeEntities{};

    bool operator==(Draft const&) const = default;
};

struct Snapshot {
    Draft       draft;
    bool        worldAvailable{};
    std::string status;
    std::uint64_t revision{};
};

struct Bounds {
    Point min;
    Point max;
};

Snapshot              getSnapshot();
std::optional<structure::detail::ClientViewSnapshot> getClientViewSnapshot();
std::optional<Bounds> getBounds();
void                  updateDraft(Draft const& draft, std::uint64_t revision);
void                  setPointFromPlayer(PointSlot slot, std::uint64_t revision);
void                  exportStructure(Draft const& draft, std::filesystem::path const& output, std::uint64_t revision);
void                  clear();
// Native operations run only from LocalPlayer::tickWorld. UI accessors above
// read cached value state and enqueue requests.
void                  tick(LocalPlayer& player);
void                  shutdown();

} // namespace lholo::structure::capture
