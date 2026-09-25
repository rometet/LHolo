// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <string>
#include <vector>

class Player;
class ItemStack;

namespace lholo::place {

void setEnabled(bool enabled);
bool isEnabled();
// Range placement: auto-place missing projection cells within a radius of the
// player (still bounded by the player's actual placement reach).
void setRangeEnabled(bool enabled);
bool isRangeEnabled();
void setPlacementRadius(int radius);
int  getPlacementRadius();
void setAutoPlacementBreakCooldownSeconds(int seconds);
int  getAutoPlacementBreakCooldownSeconds();
// Manual mode: only place while the right mouse button is held, instead of
// placing automatically. Applies to both easy-place and range placement.
void setManualMode(bool manual);
bool isManualMode();
std::vector<std::string> getManualPlacementAllowedItems();
bool setManualPlacementAllowedItems(std::vector<std::string> const& items);
// Exact held inventory item, never the blueprint target or a different slot.
bool isManualPlacementItemAllowed(ItemStack const& item);
bool isManualPlacementHeldItemAllowed(Player& player);
// Display name of the projected blueprint block currently under the crosshair.
std::string getAimedProjectedBlockName();

// Clears actions and coordinate-keyed caches owned by the previous dimension
// while preserving the user's active assisted-placement modes and settings.
void resetDimensionSession();

// Clears state that belongs to the current world while preserving user
// configuration such as placement radius and cooldown.
void resetWorldSession();

bool installHook();
void uninstallHook();

} // namespace lholo::place
