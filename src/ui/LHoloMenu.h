// LHolo - Fluent-style menu
#pragma once

#include "i18n/Translator.h"
#include "ui/FluentTheme.h"
#include "ui/BlockIcon.h"
#include "input/HotkeyTypes.h"
#include "input/MenuRoute.h"
#include "structure/SchematicRuntime.h"
#include "ui/VerifierViewState.h"
#include "ui/MaterialsViewState.h"
#include "io/MaterialExport.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lholo::ui {

using HotkeyId = input::HotkeyId;

enum class MenuPage : std::uint8_t {
    Projection,
    CreateStructure,
    Transform,
    Render,
    Hud,
    Hotkeys,
    Interface,
    Experimental,
    Schematics,
    Verification,
    Materials,
    Count
};

// Derived from the trailing Count enumerator, matching HotkeyId/TextKey: adding
// a page anywhere above Count grows this and makes the navigation-label table
// in MenuPages.cpp fail its completeness assertion.
inline constexpr std::size_t kMenuPageCount = static_cast<std::size_t>(MenuPage::Count);

enum class CapturePointId : std::uint8_t { First, Second };

struct CapturePointModel {
    bool set{};
    int  x{};
    int  y{};
    int  z{};
};

struct CaptureDraftModel {
    int               mode{};
    CapturePointModel first;
    CapturePointModel second;
    bool              includeEntities{};
};

struct HotkeyRow {
    HotkeyId    id{};
    std::string label;
    std::string display;
    bool        capturing{};
    std::string conflict;
    bool reserved{};
};

struct MaterialRow {
    std::string displayName;
    // Language-table override for names with no in-game item; empty otherwise.
    std::optional<i18n::TextKey> nameKey;
    std::string typeName;
    std::uint64_t count{};
    int stackSize{64};
};

struct MenuModel {
    BlockIconLookup blockIcons;
    MenuPage page{MenuPage::Projection};
    input::MenuRoute directMenuRoute{input::MenuRoute::None};
    bool directMenuRoutesReady{true};
    char* pathBuffer{};
    std::size_t pathBufferSize{};
    bool blockOpeningInput{};
    std::string status;
    bool hasLoadedStructure{};
    bool hasSavedProjection{};
    int savedAnchorX{};
    int savedAnchorY{};
    int savedAnchorZ{};
    float uiScale{1.0f};
    // Runtime i18n::Language index; the model never persists this value.
    int language{};

    CaptureDraftModel capture;
    std::uint64_t     captureRevision{};
    bool              captureWorldAvailable{};
    std::string       captureStatus;

    bool structureBoundsEnabled{};
    bool correctionSeeThrough{};
    bool missingSeeThrough{};
    bool easyPlaceEnabled{};
    bool manualPlace{};
    bool rangeEnabled{};
    std::uint64_t placementModesRevision{};
    bool experimentalConsent{};
    std::vector<std::string> manualPlacementAllowedItems;
    int placementRadius{4};
    int autoPlacementBreakCooldownSeconds{10};
    int offsetX{};
    int offsetY{};
    int offsetZ{};
    int rotation{};
    int mirror{};

    float opacity{1.0f};
    float correctionFillOpacity{0.15f};
    float correctionOutlineOpacity{1.0f};
    float comparisonStrength{1.0f};
    float correctionOutlineWidth{1.0f};
    int layerAxis{};
    int layerDisplayMode{};
    int displayLayer{};
    int maxLayerY{};
    int maxLayerX{};
    int sizeX{}, sizeY{}, sizeZ{};
    int materialCount{};

    // Fixed-gesture input switch on the hotkeys page. Deliberately outside
    // hotkeys[]: the trigger key is fixed to Alt, so it has no rebindable slot.
    bool altWheelOffsetEnabled{true};
    std::array<HotkeyRow, input::kHotkeyCount> hotkeys{};
    bool hudEnabled{true};
    int hudPosition{1};
    bool hudShowFileName{true};
    bool hudShowLayer{true};
    bool hudShowOverallProgress{false};
    bool hudShowProgress{true};
    bool hudShowWrongState{true};
    bool hudShowWrongType{true};
    bool hudShowExtraBlocks{true};
    bool hudShowProjectedBlockName{true};

    std::vector<MaterialRow> materials; // Legacy popup/test adapter; production acquires an immutable view.
    std::shared_ptr<structure::detail::MaterialListSnapshot const> materialList;
    std::shared_ptr<MaterialsViewState> materialsView;
    io::MaterialExportResult materialExport;
    bool materialPopupRequested{};
    bool materialHudEnabled{};
    int  materialHudPosition{3};
    bool closeRequested{};
    structure::schematic::Snapshot schematic;
    std::shared_ptr<VerifierViewState> verifierView;
};

struct MenuActions {
    std::function<std::optional<std::string>(std::string_view)> browseStructure;
    std::function<void(std::string_view)> loadStructure;
    std::function<void()> restoreProjection;
    std::function<void()> closeProjection;
    std::function<void()> requestMaterials;
    std::function<void(structure::detail::MaterialListScope,std::string const&,bool)> ignoreMaterial;
    std::function<void(structure::detail::MaterialListScope)> clearIgnoredMaterials;
    std::function<void(io::MaterialExportRequest)> exportMaterials;
    std::function<void(HotkeyId)> beginHotkeyCapture;
    std::function<void(HotkeyId)> clearHotkey;
    std::function<void(HotkeyId)> resetHotkey;
    std::function<void()> resetHotkeys;
    std::function<void()> resetCorrectionStyle;
    std::function<void()> giveExperimentalConsent;
    std::function<void(CapturePointId)> usePlayerCapturePosition;
    std::function<void()> clearCapture;
    std::function<void(CaptureDraftModel const&)> exportCapture;
    std::function<void()> refreshSchematics, importSavedSchematic, verifySchematic, cancelVerification, resetVerification;
    std::function<void(std::string const&)> placeSchematic;
    std::function<void(std::uint64_t)> selectPlacement, deletePlacement, movePlacementToFeet;
    std::function<void(structure::SavedPlacement const&,std::uint64_t)> editPlacement;
    std::function<void(structure::MistakeFilter)> cycleMistake;
    std::function<void(structure::MistakeFilter)> setMistakeFilter;
    std::function<void(structure::schematic::ReportStamp const&, std::size_t)> selectMistake;
    std::function<void()> clearMistakeTarget;
};

void renderMenu(MenuModel& model, MenuActions const& actions, UiMetrics const& metrics);

} // namespace lholo::ui
