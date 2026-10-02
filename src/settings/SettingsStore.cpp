// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "settings/SettingsStore.h"
#include "place/ManualPlacementRules.h"

#include <fstream>
#include <stdexcept>
#include <atomic>
#include <limits>
#include <system_error>
#include <utility>

#include <Windows.h>

#include <nlohmann/json.hpp>

namespace lholo::settings {
namespace {

template <class T>
T valueWithLegacyKey(nlohmann::json const& json, char const* currentKey, char const* legacyKey, T fallback) {
    // Parse only the selected field. json.value(current, json.value(legacy,
    // fallback)) eagerly validates obsolete data even when current is present.
    if (auto const current = json.find(currentKey); current != json.end()) {
        return current->get<T>();
    }
    return json.value(legacyKey, fallback);
}

[[noreturn]] void throwFileError(char const* operation, DWORD error) {
    throw std::system_error(static_cast<int>(error), std::system_category(), operation);
}

void writeAtomically(std::filesystem::path const& path, std::string const& contents) {
    static std::atomic_uint64_t sequence{};
    struct TemporaryFile {
        std::filesystem::path path;
        HANDLE handle{INVALID_HANDLE_VALUE};
        bool owned{};
        ~TemporaryFile() {
            if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
            if (owned) DeleteFileW(path.c_str());
        }
    } temporary;
    for (int attempt = 0; attempt < 64; ++attempt) {
        temporary.path = path.native() + L".lholo-tmp-" + std::to_wstring(GetCurrentProcessId())
            + L"-" + std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed));
        temporary.handle = CreateFileW(temporary.path.c_str(), GENERIC_WRITE, 0,
            nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (temporary.handle != INVALID_HANDLE_VALUE) { temporary.owned = true; break; }
        auto const error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
            throwFileError("create settings temporary file", error);
        }
    }
    if (!temporary.owned) throwFileError("allocate settings temporary filename", ERROR_FILE_EXISTS);
    std::size_t offset{};
    while (offset < contents.size()) {
        auto const amount = static_cast<DWORD>((std::min)(contents.size() - offset,
            static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        DWORD written{};
        if (!WriteFile(temporary.handle, contents.data() + offset, amount, &written, nullptr)) {
            throwFileError("write settings", GetLastError());
        }
        if (!written) throwFileError("write settings", ERROR_WRITE_FAULT);
        offset += written;
    }
    if (!FlushFileBuffers(temporary.handle)) throwFileError("flush settings", GetLastError());
    auto const handle = std::exchange(temporary.handle, INVALID_HANDLE_VALUE);
    if (!CloseHandle(handle)) throwFileError("close settings", GetLastError());
    if (!MoveFileExW(temporary.path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throwFileError("replace settings", GetLastError());
    }
    temporary.owned = false;
}

} // namespace

bool loadSettingsFile(std::filesystem::path const& path, Settings& out) {
    if (!std::filesystem::exists(path)) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("无法打开配置文件");
    auto const json = nlohmann::json::parse(input, nullptr, true, true);
    auto const schemaVersion = json.value("version", 0);

    auto parsed = out;
    parsed.lastStructurePath = json.value("lastStructurePath", parsed.lastStructurePath);
    if (auto const language = json.find("language");
        language != json.end() && language->is_string()) {
        parsed.language = language->get<std::string>();
    } else {
        // Language preferences intentionally have no old integer migration:
        // missing or malformed values use the default locale.
        parsed.language = "ja_JP";
    }
    parsed.uiScale = json.value("uiScale", parsed.uiScale);
    parsed.opacity = json.value("opacity", parsed.opacity);
    parsed.correctionFillOpacity = json.value("correctionFillOpacity", parsed.correctionFillOpacity);
    parsed.correctionOutlineOpacity = json.value("correctionOutlineOpacity", parsed.correctionOutlineOpacity);
    parsed.structureBoundsEnabled = json.value("structureBoundsEnabled", parsed.structureBoundsEnabled);
    parsed.correctionSeeThrough = json.value("correctionSeeThrough", parsed.correctionSeeThrough);
    parsed.missingSeeThrough = json.value("missingSeeThrough", parsed.missingSeeThrough);
    parsed.experimentalConsent = json.value("experimentalConsent", parsed.experimentalConsent);
    parsed.materialHudEnabled = json.value("materialHudEnabled", parsed.materialHudEnabled);
    parsed.materialHudPosition = json.value("materialHudPosition", parsed.materialHudPosition);
    // Malformed/missing lists fail closed without invalidating unrelated settings.
    parsed.manualPlacementAllowedItems.clear();
    if (auto const allowed = json.find("manualPlacementAllowedItems");
        allowed != json.end() && allowed->is_array()
        && allowed->size() <= place::detail::kMaxManualPlacementAllowedItems) {
        std::vector<std::string> items;
        bool valid = true;
        for (auto const& value : *allowed) {
            if (!value.is_string()) { valid = false; break; }
            items.push_back(value.get<std::string>());
        }
        if (valid) {
            if (auto normalized = place::detail::normalizeManualPlacementAllowedItems(items)) {
                parsed.manualPlacementAllowedItems = std::move(*normalized);
            }
        }
    }
    parsed.placementRadius = json.value("placementRadius", parsed.placementRadius);
    parsed.autoPlacementBreakCooldownSeconds = json.value(
        "autoPlacementBreakCooldownSeconds",
        parsed.autoPlacementBreakCooldownSeconds
    );
    parsed.hudEnabled = json.value("hudEnabled", parsed.hudEnabled);
    parsed.hudShowFileName = json.value("hudShowFileName", parsed.hudShowFileName);
    parsed.hudShowLayer = json.value("hudShowLayer", parsed.hudShowLayer);
    parsed.hudShowOverallProgress = json.value("hudShowOverallProgress", parsed.hudShowOverallProgress);
    parsed.hudShowProgress = json.value("hudShowProgress", parsed.hudShowProgress);
    parsed.hudShowWrongState = json.value("hudShowWrongState", parsed.hudShowWrongState);
    parsed.hudShowWrongType = json.value("hudShowWrongType", parsed.hudShowWrongType);
    parsed.hudShowExtraBlocks = json.value("hudShowExtraBlocks", parsed.hudShowExtraBlocks);
    parsed.hudShowProjectedBlockName = valueWithLegacyKey(
        json,
        "hudShowProjectedBlockName",
        "hudShowBlockEntity",
        parsed.hudShowProjectedBlockName
    );
    parsed.hudPosition = json.value("hudPosition", parsed.hudPosition);
    parsed.guiHotkey = json.value("guiHotkey", parsed.guiHotkey);
    parsed.guiHotkeyModifiers = json.value("guiHotkeyModifiers", parsed.guiHotkeyModifiers);
    // Upstream schema 12 used Simplified Chinese and Alt+M as defaults. On the
    // first launch of this Japanese fork, migrate only those exact defaults;
    // custom languages and key bindings remain untouched. Schema 13 prevents
    // the migration from running again after the user changes a preference.
    if (schemaVersion <= 12) {
        if (parsed.language == "zh_CN") parsed.language = "ja_JP";
        if (parsed.guiHotkey == 'M' && parsed.guiHotkeyModifiers == 2) {
            parsed.guiHotkey = 0x2D; // VK_INSERT
            parsed.guiHotkeyModifiers = 0;
        }
    }
    parsed.layerIncreaseHotkey = json.value("layerIncreaseHotkey", parsed.layerIncreaseHotkey);
    parsed.layerDecreaseHotkey = json.value("layerDecreaseHotkey", parsed.layerDecreaseHotkey);
    parsed.layerIncreaseHotkeyModifiers
        = json.value("layerIncreaseHotkeyModifiers", parsed.layerIncreaseHotkeyModifiers);
    parsed.layerDecreaseHotkeyModifiers
        = json.value("layerDecreaseHotkeyModifiers", parsed.layerDecreaseHotkeyModifiers);
    parsed.loadProjectionHotkey = json.value("loadProjectionHotkey", parsed.loadProjectionHotkey);
    parsed.loadProjectionHotkeyModifiers
        = json.value("loadProjectionHotkeyModifiers", parsed.loadProjectionHotkeyModifiers);
    parsed.closeProjectionHotkey = json.value("closeProjectionHotkey", parsed.closeProjectionHotkey);
    parsed.closeProjectionHotkeyModifiers
        = json.value("closeProjectionHotkeyModifiers", parsed.closeProjectionHotkeyModifiers);
    // Restore the historic manual-placement toggle field only. Easy/range
    // toggles remain intentionally absent from the current UI/runtime.
    parsed.toggleManualHotkey = json.value("toggleManualHotkey", parsed.toggleManualHotkey);
    parsed.toggleManualHotkeyModifiers
        = json.value("toggleManualHotkeyModifiers", parsed.toggleManualHotkeyModifiers);
    parsed.altWheelOffsetEnabled
        = json.value("altWheelOffsetEnabled", parsed.altWheelOffsetEnabled);

    // Slot order: left, right, forward, backward, up, down. These are the
    // directions the slots produce now; configs written while the slots were
    // world-axis offsets carry the old names and are still read as a fallback,
    // so upgrading never resets a binding.
    static char const* moveKeyNames[]{
        "moveLeftHotkey",
        "moveRightHotkey",
        "moveForwardHotkey",
        "moveBackwardHotkey",
        "moveUpHotkey",
        "moveDownHotkey"
    };
    static char const* moveModifierNames[]{
        "moveLeftHotkeyModifiers",
        "moveRightHotkeyModifiers",
        "moveForwardHotkeyModifiers",
        "moveBackwardHotkeyModifiers",
        "moveUpHotkeyModifiers",
        "moveDownHotkeyModifiers"
    };
    static char const* legacyMoveKeyNames[]{
        "moveXMinusHotkey",
        "moveXPlusHotkey",
        "moveZMinusHotkey",
        "moveZPlusHotkey",
        "moveYPlusHotkey",
        "moveYMinusHotkey"
    };
    static char const* legacyMoveModifierNames[]{
        "moveXMinusHotkeyModifiers",
        "moveXPlusHotkeyModifiers",
        "moveZMinusHotkeyModifiers",
        "moveZPlusHotkeyModifiers",
        "moveYPlusHotkeyModifiers",
        "moveYMinusHotkeyModifiers"
    };
    for (std::size_t index = 0; index < parsed.moveHotkeys.size(); ++index) {
        parsed.moveHotkeys[index] = valueWithLegacyKey(
            json,
            moveKeyNames[index],
            legacyMoveKeyNames[index],
            parsed.moveHotkeys[index]
        );
        parsed.moveHotkeyModifiers[index] = valueWithLegacyKey(
            json,
            moveModifierNames[index],
            legacyMoveModifierNames[index],
            parsed.moveHotkeyModifiers[index]
        );
    }

    parsed.hasSavedProjection = json.value("hasSavedProjection", parsed.hasSavedProjection);
    parsed.savedAnchorX = json.value("savedAnchorX", parsed.savedAnchorX);
    parsed.savedAnchorY = json.value("savedAnchorY", parsed.savedAnchorY);
    parsed.savedAnchorZ = json.value("savedAnchorZ", parsed.savedAnchorZ);
    parsed.savedRotation = json.value("savedRotation", parsed.savedRotation);
    parsed.savedMirror = json.value("savedMirror", parsed.savedMirror);
    parsed.savedVisible = json.value("savedVisible", true);
    parsed.savedCountExtras = json.value("savedCountExtras", true);
    parsed.savedOffsetX = json.value("savedOffsetX", parsed.savedOffsetX);
    parsed.savedOffsetY = json.value("savedOffsetY", parsed.savedOffsetY);
    parsed.savedOffsetZ = json.value("savedOffsetZ", parsed.savedOffsetZ);
    parsed.savedLayerDisplayMode = json.value("savedLayerDisplayMode", parsed.savedLayerDisplayMode);
    parsed.savedDisplayLayer = json.value("savedDisplayLayer", parsed.savedDisplayLayer);
    parsed.savedLayerAxis = json.value("savedLayerAxis", parsed.savedLayerAxis);
    parsed.savedStructurePath = json.value("savedStructurePath", parsed.savedStructurePath);
    out = std::move(parsed);
    return true;
}

void saveSettingsFile(std::filesystem::path const& path, Settings const& settings) {
    std::error_code error;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
    if (error) throw std::runtime_error(error.message());

    nlohmann::ordered_json const json{
        {"version", 13},
        {"lastStructurePath", settings.lastStructurePath},
        {"language", settings.language},
        {"uiScale", settings.uiScale},
        {"opacity", settings.opacity},
        {"correctionFillOpacity", settings.correctionFillOpacity},
        {"correctionOutlineOpacity", settings.correctionOutlineOpacity},
        {"structureBoundsEnabled", settings.structureBoundsEnabled},
        {"correctionSeeThrough", settings.correctionSeeThrough},
        {"missingSeeThrough", settings.missingSeeThrough},
        {"experimentalConsent", settings.experimentalConsent},
        {"materialHudEnabled", settings.materialHudEnabled},
        {"materialHudPosition", settings.materialHudPosition},
        {"manualPlacementAllowedItems", place::detail::normalizeManualPlacementAllowedItems(
            settings.manualPlacementAllowedItems).value_or(std::vector<std::string>{})},
        {"placementRadius", settings.placementRadius},
        {"autoPlacementBreakCooldownSeconds", settings.autoPlacementBreakCooldownSeconds},
        {"hudEnabled", settings.hudEnabled},
        {"hudShowFileName", settings.hudShowFileName},
        {"hudShowLayer", settings.hudShowLayer},
        {"hudShowOverallProgress", settings.hudShowOverallProgress},
        {"hudShowProgress", settings.hudShowProgress},
        {"hudShowWrongState", settings.hudShowWrongState},
        {"hudShowWrongType", settings.hudShowWrongType},
        {"hudShowExtraBlocks", settings.hudShowExtraBlocks},
        {"hudShowProjectedBlockName", settings.hudShowProjectedBlockName},
        {"hudPosition", settings.hudPosition},
        {"guiHotkey", settings.guiHotkey},
        {"guiHotkeyModifiers", settings.guiHotkeyModifiers},
        {"layerIncreaseHotkey", settings.layerIncreaseHotkey},
        {"layerDecreaseHotkey", settings.layerDecreaseHotkey},
        {"layerIncreaseHotkeyModifiers", settings.layerIncreaseHotkeyModifiers},
        {"layerDecreaseHotkeyModifiers", settings.layerDecreaseHotkeyModifiers},
        {"loadProjectionHotkey", settings.loadProjectionHotkey},
        {"loadProjectionHotkeyModifiers", settings.loadProjectionHotkeyModifiers},
        {"closeProjectionHotkey", settings.closeProjectionHotkey},
        {"closeProjectionHotkeyModifiers", settings.closeProjectionHotkeyModifiers},
        {"toggleManualHotkey", settings.toggleManualHotkey},
        {"toggleManualHotkeyModifiers", settings.toggleManualHotkeyModifiers},
        {"altWheelOffsetEnabled", settings.altWheelOffsetEnabled},
        {"moveLeftHotkey", settings.moveHotkeys[0]},
        {"moveRightHotkey", settings.moveHotkeys[1]},
        {"moveForwardHotkey", settings.moveHotkeys[2]},
        {"moveBackwardHotkey", settings.moveHotkeys[3]},
        {"moveUpHotkey", settings.moveHotkeys[4]},
        {"moveDownHotkey", settings.moveHotkeys[5]},
        {"moveLeftHotkeyModifiers", settings.moveHotkeyModifiers[0]},
        {"moveRightHotkeyModifiers", settings.moveHotkeyModifiers[1]},
        {"moveForwardHotkeyModifiers", settings.moveHotkeyModifiers[2]},
        {"moveBackwardHotkeyModifiers", settings.moveHotkeyModifiers[3]},
        {"moveUpHotkeyModifiers", settings.moveHotkeyModifiers[4]},
        {"moveDownHotkeyModifiers", settings.moveHotkeyModifiers[5]},
        {"hasSavedProjection", settings.hasSavedProjection},
        {"savedAnchorX", settings.savedAnchorX},
        {"savedAnchorY", settings.savedAnchorY},
        {"savedAnchorZ", settings.savedAnchorZ},
        {"savedStructurePath", settings.savedStructurePath},
        {"savedRotation", settings.savedRotation},
        {"savedMirror", settings.savedMirror},
        {"savedVisible", settings.savedVisible}, {"savedCountExtras", settings.savedCountExtras},
        {"savedOffsetX", settings.savedOffsetX},
        {"savedOffsetY", settings.savedOffsetY},
        {"savedOffsetZ", settings.savedOffsetZ},
        {"savedLayerDisplayMode", settings.savedLayerDisplayMode},
        {"savedDisplayLayer", settings.savedDisplayLayer},
        {"savedLayerAxis", settings.savedLayerAxis}
    };
    // Serialize before touching the destination: invalid UTF-8/allocations can
    // throw. Replacement occurs only after a complete, flushed write succeeds.
    writeAtomically(path, json.dump(2));
}

} // namespace lholo::settings
