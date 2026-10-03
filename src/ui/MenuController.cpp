// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "ui/MenuController.h"

#include "place/PlaceHelper.h"
#include "projection/Projection.h"
#include "projection/core/ComparisonStyle.h"
#include "structure/StructurePaths.h"
#include "structure/StructureLoader.h"
#include "structure/MaterialTracker.h"
#include "structure/StructureSession.h"
#include "structure/StructureUiState.h"
#include "structure/capture/StructureCapture.h"
#include "structure/formats/StructureFormatLoaders.h"
#include "ui/FileDialog.h"
#include "ui/FluentTheme.h"
#include "ui/HotkeyFormat.h"
#include "ui/LHoloMenu.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include <Windows.h>

#include "imgui.h"

namespace lholo::ui {
namespace {

auto& uiState() {
    return structure::detail::StructureUiState::getInstance();
}

std::array<char, StructurePathUtf8Capacity> gPathBuffer{};
bool                   gPathInitialized{};
MenuPage               gActivePage{MenuPage::Projection};

struct HotkeyDefinition { HotkeyId id; i18n::TextKey label; };
constexpr std::array<HotkeyDefinition, input::kHotkeyCount> kHotkeyDefinitions{{
    {HotkeyId::Gui, i18n::TextKey::HotkeyOpenMenu},
    {HotkeyId::MoveLeft, i18n::TextKey::HotkeyMoveLeft},
    {HotkeyId::MoveRight, i18n::TextKey::HotkeyMoveRight},
    {HotkeyId::MoveForward, i18n::TextKey::HotkeyMoveForward},
    {HotkeyId::MoveBackward, i18n::TextKey::HotkeyMoveBackward},
    {HotkeyId::MoveUp, i18n::TextKey::HotkeyMoveUp},
    {HotkeyId::MoveDown, i18n::TextKey::HotkeyMoveDown},
    {HotkeyId::LayerIncrease, i18n::TextKey::HotkeyLayerIncrease},
    {HotkeyId::LayerDecrease, i18n::TextKey::HotkeyLayerDecrease},
    {HotkeyId::LoadProjection, i18n::TextKey::HotkeyLoadProjection},
    {HotkeyId::CloseProjection, i18n::TextKey::HotkeyCloseProjection},
    {HotkeyId::ToggleManualPlacement, i18n::TextKey::CheckboxManualPlace}
}};

} // namespace

MenuModel buildStructureMenuModel(float effectiveUiScale) {
    MenuModel model;
    auto& session = structure::detail::StructureSession::getInstance();
    auto const sessionSnapshot = session.snapshot();
    auto const hud = uiState().hud();
    model.page = gActivePage;
    model.pathBuffer = gPathBuffer.data();
    model.pathBufferSize = gPathBuffer.size();
    model.blockOpeningInput = uiState().openingInputBlocked();
    model.uiScale = effectiveUiScale;
    auto const currentLanguage = i18n::language();
    model.language = i18n::isValidLanguage(currentLanguage)
        ? static_cast<int>(currentLanguage) : 0;
    auto const captureSnapshot = structure::capture::getSnapshot();
    model.capture.mode = static_cast<int>(captureSnapshot.draft.mode);
    model.captureRevision = captureSnapshot.revision;
    model.capture.includeEntities = captureSnapshot.draft.includeEntities;
    model.captureWorldAvailable = captureSnapshot.worldAvailable;
    model.captureStatus = captureSnapshot.status;
    if (captureSnapshot.draft.first) {
        auto const& point = *captureSnapshot.draft.first;
        model.capture.first = {true, point.x, point.y, point.z};
    }
    if (captureSnapshot.draft.second) {
        auto const& point = *captureSnapshot.draft.second;
        model.capture.second = {true, point.x, point.y, point.z};
    }
    model.layerAxis = structure::toInt(sessionSnapshot.transform.layerAxis);
    model.status = sessionSnapshot.status;
    model.schematic = structure::schematic::snapshot();
    if(sessionSnapshot.loaded) { model.sizeX=sessionSnapshot.loaded->sizeX;model.sizeY=sessionSnapshot.loaded->sizeY;model.sizeZ=sessionSnapshot.loaded->sizeZ; }
    model.hasLoadedStructure = static_cast<bool>(sessionSnapshot.loaded);
    model.hasSavedProjection = sessionSnapshot.saved.available;
    model.savedAnchorX = sessionSnapshot.saved.anchorX;
    model.savedAnchorY = sessionSnapshot.saved.anchorY;
    model.savedAnchorZ = sessionSnapshot.saved.anchorZ;
    model.maxLayerY = sessionSnapshot.maxLayerY;
    model.maxLayerX = sessionSnapshot.maxLayerX;
    model.materialCount = sessionSnapshot.loaded
        ? static_cast<int>(sessionSnapshot.loaded->materialCount) : 0;
    model.structureBoundsEnabled = projection::getStructureBoundsEnabled();
    model.correctionSeeThrough = projection::getCorrectionSeeThrough();
    model.missingSeeThrough = projection::getMissingSeeThrough();
    auto const placementModes = place::getPlacementModes();
    model.easyPlaceEnabled = placementModes.enabled;
    model.manualPlace = placementModes.manual;
    model.rangeEnabled = placementModes.range;
    model.placementModesRevision = placementModes.revision;
    model.experimentalConsent = structure::experimentalConsentGiven();
    model.materialHudEnabled = structure::materialHudEnabled();
    model.materialHudPosition = std::clamp(structure::materialHudPosition(), 0, 3);
    model.manualPlacementAllowedItems = place::getManualPlacementAllowedItems();
    model.placementRadius = place::getPlacementRadius();
    model.autoPlacementBreakCooldownSeconds = place::getAutoPlacementBreakCooldownSeconds();
    model.offsetX = sessionSnapshot.transform.offsetX;
    model.offsetY = sessionSnapshot.transform.offsetY;
    model.offsetZ = sessionSnapshot.transform.offsetZ;
    model.rotation = std::clamp(sessionSnapshot.transform.rotation, 0, 3);
    model.mirror = std::clamp(sessionSnapshot.transform.mirror, 0, 2);
    model.opacity = projection::getOpacity();
    model.correctionFillOpacity = projection::getCorrectionFillOpacity();
    model.correctionOutlineOpacity = projection::getCorrectionOutlineOpacity();
    model.comparisonStrength = projection::getComparisonStrength();
    model.correctionOutlineWidth = projection::getCorrectionOutlineWidth();
    model.layerDisplayMode = structure::toInt(sessionSnapshot.transform.layerDisplayMode);
    model.displayLayer = std::clamp(
        sessionSnapshot.transform.displayLayer, 0,
        model.layerAxis == structure::toInt(structure::LayerAxis::Material)
            ? std::max(0, model.materialCount - 1)
            : (sessionSnapshot.loaded ? structure::detail::maxLayerFor(*sessionSnapshot.loaded,sessionSnapshot.transform.layerAxis,sessionSnapshot.transform.rotation) : 0)
    );
    model.hudEnabled = hud.enabled;
    model.hudPosition = std::clamp(hud.position, 0, 3);
    model.hudShowFileName = hud.showFileName;
    model.hudShowLayer = hud.showLayer;
    model.hudShowOverallProgress = hud.showOverallProgress;
    model.hudShowProgress = hud.showProgress;
    model.hudShowWrongState = hud.showWrongState;
    model.hudShowWrongType = hud.showWrongType;
    model.hudShowExtraBlocks = hud.showExtraBlocks;
    model.hudShowProjectedBlockName = hud.showProjectedBlockName;
    model.altWheelOffsetEnabled = uiState().altWheelOffsetEnabled();
    std::size_t rowIndex = 0;
    for (auto const& definition : kHotkeyDefinitions) {
        auto const binding = uiState().hotkey(static_cast<std::size_t>(definition.id));
        auto& row = model.hotkeys[rowIndex++];
        row.id = definition.id;
        row.label = i18n::tr(definition.label);
        row.display = hotkeyChordName(binding.modifiers, binding.key);
        row.capturing = binding.capturing;
    }
    auto const materials = uiState().materialRequirements();
    model.materials.reserve(materials.size());
    for (auto const& material : materials) {
        model.materials.push_back(
            {material.displayName, material.nameKey, material.typeName,
             material.count, material.stackSize}
        );
    }
    return model;
}

void applyStructureMenuModel(MenuModel const& model, float effectiveUiScale) {
    bool changed = false;
    auto& session = structure::detail::StructureSession::getInstance();
    auto const languageCount = i18n::languages().size();
    if (languageCount != 0) {
        auto const languageIndex = static_cast<std::size_t>(std::clamp(
            model.language,
            0,
            static_cast<int>(languageCount - 1)
        ));
        auto const language = static_cast<i18n::Language>(languageIndex);
        if (language != i18n::language()) {
            i18n::setLanguage(language);
            changed = true;
        }
    }
    if (std::abs(model.uiScale - effectiveUiScale) > 0.001f) {
        auto const scale = std::clamp(model.uiScale, 1.0f, 5.0f);
        if (std::abs(uiState().hud().uiScale - scale) > 0.001f)
            changed = uiState().setUiScale(scale) || changed;
    }
    if (projection::getStructureBoundsEnabled() != model.structureBoundsEnabled) {
        projection::setStructureBoundsEnabled(model.structureBoundsEnabled);
        changed = true;
    }
    if (projection::getCorrectionSeeThrough() != model.correctionSeeThrough) {
        projection::setCorrectionSeeThrough(model.correctionSeeThrough);
        changed = true;
    }
    if (projection::getMissingSeeThrough() != model.missingSeeThrough) {
        projection::setMissingSeeThrough(model.missingSeeThrough);
        changed = true;
    }
    // Assisted-placement modes are session-only safety controls. Applying a
    // mode must not dirty or rewrite the persistent settings file.
    (void)place::applyPlacementModes({model.easyPlaceEnabled, model.manualPlace,
        model.rangeEnabled, model.placementModesRevision});
    changed = place::setManualPlacementAllowedItems(model.manualPlacementAllowedItems) || changed;
    auto const radius = std::clamp(model.placementRadius, 1, 4);
    if (place::getPlacementRadius() != radius) {
        place::setPlacementRadius(radius);
        changed = true;
    }
    auto const breakCooldown = std::clamp(model.autoPlacementBreakCooldownSeconds, 0, 60);
    if (place::getAutoPlacementBreakCooldownSeconds() != breakCooldown) {
        place::setAutoPlacementBreakCooldownSeconds(breakCooldown);
        changed = true;
    }
    changed = session.setOffsetX(model.offsetX) || changed;
    changed = session.setOffsetY(model.offsetY) || changed;
    changed = session.setOffsetZ(model.offsetZ) || changed;
    changed = session.setRotation(std::clamp(model.rotation, 0, 3)) || changed;
    changed = session.setMirror(std::clamp(model.mirror, 0, 2)) || changed;

    auto const opacity = std::clamp(model.opacity, 0.0f, 1.0f);
    if (std::abs(projection::getOpacity() - opacity) > 0.0001f) {
        projection::setOpacity(opacity);
        changed = true;
    }
    auto const fill = std::clamp(model.correctionFillOpacity, 0.0f, 1.0f);
    if (std::abs(projection::getCorrectionFillOpacity() - fill) > 0.0001f) {
        projection::setCorrectionFillOpacity(fill);
        changed = true;
    }
    auto const outline = std::clamp(model.correctionOutlineOpacity, 0.0f, 1.0f);
    if (std::abs(projection::getCorrectionOutlineOpacity() - outline) > 0.0001f) {
        projection::setCorrectionOutlineOpacity(outline);
        changed = true;
    }
    auto const layerAxis = structure::layerAxisFromInt(model.layerAxis);
    auto const strength = projection::detail::normalizeComparisonStrength(model.comparisonStrength);
    if (std::abs(projection::getComparisonStrength() - strength) > 0.0001f) {
        projection::setComparisonStrength(strength);
        changed = true;
    }
    auto const width = projection::detail::normalizeCorrectionOutlineWidth(model.correctionOutlineWidth);
    if (std::abs(projection::getCorrectionOutlineWidth() - width) > 0.0001f) {
        projection::setCorrectionOutlineWidth(width);
        changed = true;
    }
    changed = session.setLayerAxis(layerAxis) || changed;
    changed = session.setLayerDisplayMode(
        structure::layerDisplayModeFromInt(model.layerDisplayMode)
    ) || changed;
    auto const sessionSnapshot = session.snapshot();
    auto const displayMax = layerAxis == structure::LayerAxis::Material
        ? std::max(0, static_cast<int>(sessionSnapshot.loaded
            ? sessionSnapshot.loaded->materialCount : 0) - 1)
        : (sessionSnapshot.loaded ? structure::detail::maxLayerFor(*sessionSnapshot.loaded,layerAxis,sessionSnapshot.transform.rotation) : 0);
    changed = session.setDisplayLayer(std::clamp(model.displayLayer, 0, displayMax)) || changed;
    auto hud = uiState().hud();
    hud.enabled = model.hudEnabled;
    hud.position = std::clamp(model.hudPosition, 0, 3);
    hud.showFileName = model.hudShowFileName;
    hud.showLayer = model.hudShowLayer;
    hud.showOverallProgress = model.hudShowOverallProgress;
    hud.showProgress = model.hudShowProgress;
    hud.showWrongState = model.hudShowWrongState;
    hud.showWrongType = model.hudShowWrongType;
    hud.showExtraBlocks = model.hudShowExtraBlocks;
    hud.showProjectedBlockName = model.hudShowProjectedBlockName;
    changed = uiState().applyHud(hud) || changed;
    // The fixed Alt+wheel gesture is an input preference of the hotkeys page;
    // flipping it is a setting change like any other and must be persisted.
    changed = uiState().setAltWheelOffsetEnabled(model.altWheelOffsetEnabled) || changed;
    if (structure::materialHudEnabled() != model.materialHudEnabled) {
        structure::setMaterialHudEnabled(model.materialHudEnabled);
        changed = true;
    }
    auto const materialPosition = std::clamp(model.materialHudPosition, 0, 3);
    if (structure::materialHudPosition() != materialPosition) {
        structure::setMaterialHudPosition(materialPosition);
        changed = true;
    }
    structure::capture::Draft captureDraft;
    captureDraft.mode = static_cast<structure::capture::CaptureMode>(
        std::clamp(model.capture.mode, 0, 1)
    );
    captureDraft.includeEntities = model.capture.includeEntities;
    if (model.capture.first.set) {
        captureDraft.first = structure::capture::Point{
            model.capture.first.x, model.capture.first.y, model.capture.first.z
        };
    }
    if (model.capture.second.set) {
        captureDraft.second = structure::capture::Point{
            model.capture.second.x, model.capture.second.y, model.capture.second.z
        };
    }
    structure::capture::updateDraft(captureDraft, model.captureRevision);
    if (changed) structure::saveSettings();
}

MenuActions buildStructureMenuActions(bool& refreshModel, std::uint64_t captureRevision) {
    MenuActions actions;
    actions.refreshSchematics = [&refreshModel] { structure::schematic::refreshFiles(); refreshModel=true; };
    actions.importSavedSchematic = [&refreshModel] { structure::schematic::importSavedProjection(); refreshModel=true; };
    actions.placeSchematic = [&refreshModel](auto const& file) { structure::schematic::place(file); refreshModel=true; };
    actions.selectPlacement = [&refreshModel](auto id) { structure::schematic::select(id); refreshModel=true; };
    actions.deletePlacement = [&refreshModel](auto id) { structure::schematic::erase(id); refreshModel=true; };
    actions.movePlacementToFeet = [&refreshModel](auto id) { structure::schematic::moveToFeet(id); refreshModel=true; };
    actions.editPlacement = [&refreshModel](auto const& p,auto revision) { structure::schematic::edit(p,revision); refreshModel=true; };
    actions.verifySchematic = [] { structure::schematic::verify(); };
    actions.cycleMistake = [](auto filter) { structure::schematic::cycleMistake(filter); };
    actions.browseStructure = [](std::string_view current) -> std::optional<std::string> {
        auto const selected = openStructureFile(structure::detail::pathFromUtf8(current));
        return selected ? std::optional<std::string>{structure::detail::pathToUtf8(*selected)}
                        : std::nullopt;
    };
    actions.loadStructure = [&refreshModel](std::string_view pathValue) {
        structure::requestStructureFileLoad(std::string{pathValue});
        // The actual .mcstructure publish may complete on a later frame after
        // background preparation. Rebuild the menu model now so status/input
        // changes are reflected immediately; subsequent frames read session
        // state normally.
        refreshModel = true;
    };
    actions.restoreProjection = [&refreshModel] {
        structure::restoreSavedProjection();
        auto const savedPath =
            structure::detail::StructureSession::getInstance().savedProjection().structurePath;
        std::snprintf(gPathBuffer.data(), gPathBuffer.size(), "%s", savedPath.c_str());
        refreshModel = true;
    };
    actions.closeProjection = [&refreshModel] {
        structure::clear();
        // clear() freezes the active transform before releasing the loaded
        // structure; persist that restore snapshot while closing from the UI.
        structure::saveSettings();
        refreshModel = true;
    };
    actions.requestMaterials = [] { structure::requestMaterialList(); };
    actions.beginHotkeyCapture = [](HotkeyId id) {
        uiState().beginHotkeyCapture(static_cast<std::size_t>(id));
    };
    actions.clearHotkey = [](HotkeyId id) {
        uiState().clearHotkey(static_cast<std::size_t>(id));
        structure::saveSettings();
    };
    actions.resetHotkey = [](HotkeyId id) {
        uiState().resetHotkey(static_cast<std::size_t>(id));
        structure::saveSettings();
    };
    actions.resetHotkeys = [] {
        // Also restores the fixed Alt+wheel gesture to its default (enabled),
        // which no other control on the page can bring back on its own.
        uiState().resetHotkeys();
        structure::resetHotkeyState();
        structure::saveSettings();
    };
    actions.resetCorrectionStyle = [] {
        projection::setCorrectionFillOpacity(0.15f);
        projection::setCorrectionOutlineOpacity(1.0f);
        projection::setComparisonStrength(projection::detail::DefaultComparisonStrength);
        projection::setCorrectionOutlineWidth(projection::detail::DefaultCorrectionOutlineWidth);
        structure::saveSettings();
    };
    actions.giveExperimentalConsent = [] {
        structure::setExperimentalConsentGiven(true);
        structure::saveSettings();
    };
    actions.usePlayerCapturePosition = [&refreshModel, captureRevision](CapturePointId point) {
        structure::capture::setPointFromPlayer(
            point == CapturePointId::First
                ? structure::capture::PointSlot::First
                : structure::capture::PointSlot::Second,
            captureRevision
        );
        refreshModel = true;
    };
    actions.clearCapture = [&refreshModel] {
        structure::capture::clear();
        refreshModel = true;
    };
    actions.exportCapture = [&refreshModel, captureRevision](CaptureDraftModel const& model) {
        auto const output = saveMcstructureFile();
        if (!output) return;
        structure::capture::Draft draft;
        draft.mode = static_cast<structure::capture::CaptureMode>(
            std::clamp(model.mode, 0, 1)
        );
        draft.includeEntities = model.includeEntities;
        if (model.first.set) {
            draft.first = structure::capture::Point{
                model.first.x, model.first.y, model.first.z
            };
        }
        if (model.second.set) {
            draft.second = structure::capture::Point{
                model.second.x, model.second.y, model.second.z
            };
        }
        structure::capture::exportStructure(draft, *output, captureRevision);
        refreshModel = true;
    };
    return actions;
}

void renderStructureMenu() {
    if (!structure::isGuiVisible()) return;
    auto const displaySize = ImGui::GetIO().DisplaySize;
    auto const configuredScale = uiState().hud().uiScale;
    auto const effectiveScale = configuredScale > 0.0f
        ? std::clamp(configuredScale, 1.0f, 5.0f)
        : std::clamp(
            std::min(displaySize.x / 1920.0f, displaySize.y / 1080.0f), 1.0f, 5.0f
        );
    if (!gPathInitialized) {
        auto const lastPath = structure::detail::StructureSession::getInstance().lastPath();
        std::snprintf(
            gPathBuffer.data(),
            gPathBuffer.size(),
            "%s",
            lastPath.c_str()
        );
        gPathInitialized = true;
    }
    auto const metrics = calculateMetrics(displaySize, effectiveScale);
    applyFluentTheme(metrics);
    auto model = buildStructureMenuModel(effectiveScale);
    bool refreshModel = false;
    auto const actions = buildStructureMenuActions(refreshModel, model.captureRevision);
    renderMenu(model, actions, metrics);
    gActivePage = model.page;
    if (!refreshModel) applyStructureMenuModel(model, effectiveScale);
    uiState().consumeOpeningInputBlockFrame();
    if (model.closeRequested) {
        uiState().setGuiVisible(false);
        uiState().setBlockGameInputUntil(GetTickCount64() + 180);
    }
}

} // namespace lholo::ui
