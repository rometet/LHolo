// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <utility>
#include <vector>

class Block;

namespace lholo::structure {

struct ResolvedJavaBlock {
    Block const* block{};
    Block const* liquid{};
    bool         mapped{};
    // An explicit state must fail the load instead of silently using a default.
    bool         stateResolutionFailed{};
};

// Resolve one exact Java block state through the table generated from Chunker.
// javaDataVersion is the MinecraftDataVersion stored in the litematic.
ResolvedJavaBlock resolveJavaBlockState(
    std::string const&                                      javaName,
    std::vector<std::pair<std::string, std::string>> const& properties,
    int                                                     javaDataVersion
);

// Minecraft owns the Block pointers. Clear them at every world lifetime
// boundary; the generated string mapping itself is process-lifetime data.
void resetJavaBlockMappingCache();

} // namespace lholo::structure
