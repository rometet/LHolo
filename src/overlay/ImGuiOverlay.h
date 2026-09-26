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

#include <cstddef>

namespace lholo::overlay {

using CompanionGuiDraw = void (*)(void* imguiContext) noexcept;
using CompanionGuiState = void (*)(bool visible) noexcept;
using CompanionHudNeeded = bool (*)() noexcept;

bool ensureInstalled();
void shutdown();
bool companionGuiVisible() noexcept;
bool companionGuiRegistered(void* owner) noexcept;
bool registerCompanionGui(void* owner, unsigned key, CompanionGuiDraw draw,
                          CompanionGuiDraw hud, CompanionHudNeeded hudNeeded,
                          CompanionGuiState state, unsigned imguiVersion,
                          std::size_t ioSize, std::size_t styleSize,
                          std::size_t drawVertSize) noexcept;
bool unregisterCompanionGui(void* owner) noexcept;
void requestCompanionGuiClose() noexcept;

} // namespace lholo::overlay
