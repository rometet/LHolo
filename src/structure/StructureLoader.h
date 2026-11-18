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

#include "i18n/Message.h"
#include "input/MenuRoute.h"
#include "structure/LayerDisplayTypes.h"
#include "structure/StructureBlocks.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class Block;
class CompoundTag;

namespace lholo::structure {

inline constexpr std::uint64_t kProjectionLifecycleHintDurationMs = 2500;

struct LoadedStructure {
    struct RegionBox {
        int x{};
        int y{};
        int z{};
        int sizeX{};
        int sizeY{};
        int sizeZ{};
        std::string name; // stable source key, never the editable display name
    };
    std::filesystem::path                 sourcePath;
    // Bedrock root NBT structure_world_origin: original capture corner in
    // world coordinates. Not inferred when absent or malformed.
    std::optional<std::array<int,3>>     sourceWorldOrigin;
    int                                  sizeX{};
    int                                  sizeY{};
    int                                  sizeZ{};
    std::uint64_t                        volume{};
    std::uint64_t                        primaryBlocks{};
    std::uint64_t                        secondaryBlocks{};
    std::uint64_t                        paletteEntries{};
    std::uint64_t                        generation{};
    std::size_t                          materialCount{};
    // Cells covered by the source format. mcstructure contributes one box;
    // litematic contributes one per region so gaps between regions are not
    // mistaken for schematic air by projection correction.
    std::vector<RegionBox>               regions;
    using RenderBlock = StructureBlock;
    // Compact palette/coordinate representation for large published sources.
    // Small structures keep vector semantics without packed lookup overhead.
    StructureBlocks                        renderBlocks;    // Source-side definitions survive enable/disable and filtered world views.
    std::vector<RegionBox>                sourceRegions;
    // Original per-region non-air cells preserve overlap ownership and permit
    // revealing an older region after a later region is switched off.
    std::vector<std::vector<RenderBlock>>  regionRenderBlocks;
    // Disjoint composite fast path. One owner byte per block in renderBlocks;
    // when populated, source regionRenderBlocks may be released.
    std::vector<std::uint8_t>             disjointSourceOwners;
    // A filtered view may borrow immutable per-region source cells from the
    // one-world cache. Never duplicate every native region's source storage.
    std::shared_ptr<LoadedStructure const> sourceBacking;
};

void requestOpenGui();
bool isGuiVisible();
bool shouldShowProjectedBlockName();
bool isInputTransitionBlocked();
// True while the LHolo menu owns keyboard and mouse input. Keep input guards
// on every platform-facing path behind this single policy predicate.
bool isMenuInputCaptured();
bool handleGuiHotkeyKeyDown(unsigned int virtualKey, bool allowDirectRoutes=false,std::uintptr_t gameWindow=0);
bool handleGuiHotkeyKeyUp(unsigned int virtualKey);
bool handleProjectionOffsetWheel(short wheelDelta);
void resetHotkeyState();
// Present-frame control plane for deferred world-lifecycle and hotkey actions.
void processPendingActions();
// Invalidates world-derived HUD snapshots after a dimension change while
// preserving the loaded structure and all HUD preferences.
void resetDimensionSession();
// Clears transient projection, UI and input state owned by the current world
// while preserving user configuration such as HUD layout and hotkeys.
void resetWorldSession();
bool hasHudInfo();
void renderHud();
// Current-visible-range material HUD: missing projected cells vs. inventory.
void renderMaterialHud();
// JE-style transient hint shown centered above the hotbar for a moment (e.g.
// when manual mode blocks an action, or a placement hotkey is toggled). The
// hint is stored as a message so it follows a language switch while pending.
void showActionHint(i18n::Message message, std::uint64_t durationMs = 1600);
void renderActionHint();
// True while a hint is still on screen, so the overlay keeps drawing even with
// no projection loaded and the menu closed.
bool actionHintActive();
// One-time consent for the experimental assisted-placement features (they may be
// flagged as cheating by server anti-cheat). Persisted once acknowledged.
bool experimentalConsentGiven();
void setExperimentalConsentGiven(bool given);
// Opt-in on-screen material-progress HUD, configured on the HUD settings page
// (default off). Independent of the main projection HUD toggle.
bool materialHudEnabled();
void setMaterialHudEnabled(bool enabled);
// Material HUD corner: 0 top-left, 1 bottom-left, 2 top-right, 3 bottom-right
// (same encoding as the projection HUD position). Default 3.
int  materialHudPosition();
void setMaterialHudPosition(int position);
void renderGui();
void requestMaterialList();
void loadSettings();
void saveSettings();
std::shared_ptr<LoadedStructure const> getLoaded();
int getRotationQuarterTurns();
int getMirrorMode();
int getOffsetX();
int getOffsetY();
int getOffsetZ();
LayerDisplayMode getLayerDisplayMode();
int getDisplayLayer();
LayerAxis getLayerAxis();
void recordProjectionAnchor(int x, int y, int z);
bool recordProjectionAnchor(std::shared_ptr<LoadedStructure const> const& expected,std::uint64_t generation,int x,int y,int z);
// True when the Bedrock mouse-input boundary should give the wheel to projection
// movement: a projection is loaded and the fixed Alt trigger is held.
bool scrollLockActive();
void clear(bool preserveQueuedVerification=false);
// Drain any in-flight asynchronous structure preparation before native module
// teardown. This is stronger than cancellation: it joins code executing inside
// the LHolo DLL so no worker can return into an unloaded image.
void shutdownPendingStructureLoad();
// Queue a normal structure load. .mcstructure preparation runs off the render
// thread; unsupported formats retain the existing synchronous path.
// Direct source-origin mode resolves the saved anchor BEFORE publishing
// the structure; it never briefly projects at player feet.
void requestStructureFileLoad(std::string path, bool useSourceWorldOrigin=false);
// Reload the last saved projection at its saved anchor/transform. Standalone so
// both the menu action and the load hotkey can trigger it.
void restoreSavedProjection(std::uint64_t placementId=0);
// Status line for a freshly loaded structure. File names and dimensions are
// language-neutral fragments; the table entry supplies the wording.
i18n::Message makeLoadedStatusMessage(LoadedStructure const& loaded);

} // namespace lholo::structure
