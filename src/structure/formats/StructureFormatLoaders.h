// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Structure format loading. The loaders own .mcstructure/.litematic parsing
// and the loaded-structure generation counter; StructureLoader keeps the
// session state, menu and HUD orchestration.

#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace lholo::structure {

struct LoadedStructure;
struct RegionSetting;

namespace detail {

struct PreparedStructureLoad;

// .mcstructure can do file I/O, NBT parsing, validation and occupied-cell
// compaction off the render thread. The final game-registry resolution stays
// on the render/game thread.
bool supportsAsyncStructurePreparation(std::filesystem::path const& path);
std::shared_ptr<PreparedStructureLoad> prepareStructureFile(
    std::filesystem::path const& path,
    std::string&                 error
);
std::shared_ptr<LoadedStructure> finalizePreparedStructureFile(
    std::shared_ptr<PreparedStructureLoad> prepared,
    std::string&                          error
);

std::shared_ptr<LoadedStructure> loadStructureFile(std::filesystem::path const& path, std::string& error);
// Call on a fully finalized, unpublished structure on the main thread, before
// session.replaceLoaded(). The same effective cells are used by all systems.
void filterLoadedRegions(LoadedStructure& loaded,std::vector<RegionSetting> const& settings);
// Cheap world-independent metadata view. Only effective cells are copied by
// filterLoadedRegions; unfiltered native region arrays stay shared/immutable.
std::shared_ptr<LoadedStructure> makeCachedRegionView(
    std::shared_ptr<LoadedStructure const> source);

} // namespace lholo::structure::detail

} // namespace lholo::structure
