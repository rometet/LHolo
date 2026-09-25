// LHolo - Per-installation manual-placement preferences
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "place/ManualPlacementRules.h"
#include <filesystem>

namespace lholo::place {

enum class ManualPlacementPolicyError { None, NotInitialized, InvalidInput, ReadFailed, WriteFailed };

struct ManualPlacementPolicySnapshot {
    std::vector<std::string> items;
    ManualPlacementPolicyError error{ManualPlacementPolicyError::None};
};

// Initialize/reload once at hook installation, not on the game-tick hot path.
// Missing file = empty allow-list. Invalid input never authorizes all items.
ManualPlacementPolicyError initializeManualPlacementPolicy(std::filesystem::path const& path);
ManualPlacementPolicyError reloadManualPlacementPolicy();
ManualPlacementPolicyError saveManualPlacementPolicy(std::string_view text);
ManualPlacementPolicySnapshot manualPlacementPolicySnapshot();

// Reads an immutable snapshot. No file I/O, registry access or renderer state.
bool manualPlacementAllows(std::string_view itemId);

} // namespace lholo::place
