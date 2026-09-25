// LHolo - Per-installation manual-placement allow-list persistence
// SPDX-License-Identifier: GPL-3.0-or-later
#include "place/ManualPlacementPolicy.h"

#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <mutex>
#include <system_error>
#include <stdexcept>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace lholo::place {
namespace {

using AllowedItems = std::vector<std::string>;
std::atomic<std::shared_ptr<AllowedItems const>> gAllowedItems{std::make_shared<AllowedItems const>()};
// Disk operations serialize separately from hook readers; a slow save must not
// stall placement ticks on a filesystem mutex.
std::mutex gPersistenceMutex;
std::filesystem::path gPath;
ManualPlacementPolicyError gLastError{ManualPlacementPolicyError::NotInitialized};

void publish(std::shared_ptr<AllowedItems const> items) {
    gAllowedItems.store(std::move(items), std::memory_order_release);
}

ManualPlacementPolicyError reloadLocked() {
    if (gPath.empty()) return gLastError = ManualPlacementPolicyError::NotInitialized;
    try {
        if (!std::filesystem::exists(gPath)) {
            publish(std::make_shared<AllowedItems const>());
            return gLastError = ManualPlacementPolicyError::None;
        }
        std::ifstream input(gPath, std::ios::binary);
        if (!input) return gLastError = ManualPlacementPolicyError::ReadFailed;
        // Bound the actual read as well as parsing; do not trust file_size in
        // case another process changes the file between the two operations.
        std::array<char, kManualAllowlistMaxTextLength + 1> buffer{};
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        auto const count = static_cast<std::size_t>(input.gcount());
        if (input.bad() || count > kManualAllowlistMaxTextLength) {
            return gLastError = ManualPlacementPolicyError::ReadFailed;
        }
        auto parsed = parseManualAllowedItems(std::string_view{buffer.data(), count});
        if (!parsed.valid()) return gLastError = ManualPlacementPolicyError::InvalidInput;
        publish(std::make_shared<AllowedItems const>(std::move(parsed.items)));
        return gLastError = ManualPlacementPolicyError::None;
    } catch (...) {
        // Keep the last good snapshot; first initialization starts with none.
        return gLastError = ManualPlacementPolicyError::ReadFailed;
    }
}

} // namespace

ManualPlacementPolicyError initializeManualPlacementPolicy(std::filesystem::path const& path) {
    std::scoped_lock lock(gPersistenceMutex);
    if (gPath != path) {
        publish(std::make_shared<AllowedItems const>());
        gPath = path;
    }
    return reloadLocked();
}

ManualPlacementPolicyError reloadManualPlacementPolicy() {
    std::scoped_lock lock(gPersistenceMutex);
    return reloadLocked();
}

ManualPlacementPolicyError saveManualPlacementPolicy(std::string_view text) {
    std::scoped_lock lock(gPersistenceMutex);
    if (gPath.empty()) return gLastError = ManualPlacementPolicyError::NotInitialized;
    std::filesystem::path temporary;
    try {
        auto parsed = parseManualAllowedItems(text);
        if (!parsed.valid()) return gLastError = ManualPlacementPolicyError::InvalidInput;
        auto const normalized = manualAllowedItemsText(parsed.items);
        // Allocate the new runtime value BEFORE replacing the saved file.
        auto next = std::make_shared<AllowedItems const>(std::move(parsed.items));
        if (!gPath.parent_path().empty()) std::filesystem::create_directories(gPath.parent_path());
        temporary = gPath;
        temporary += ".tmp." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("Cannot create allow-list temporary file");
            output.write(normalized.data(), static_cast<std::streamsize>(normalized.size()));
            output.flush();
            if (!output) throw std::runtime_error("Cannot write allow-list temporary file");
            output.close();
            if (!output) throw std::runtime_error("Cannot close allow-list temporary file");
        }
        // Never remove the old file first. If replacement fails, the existing
        // preferences and the runtime snapshot remain unchanged.
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), gPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
        }
#else
        std::filesystem::rename(temporary, gPath);
#endif
        publish(std::move(next));
        return gLastError = ManualPlacementPolicyError::None;
    } catch (...) {
        if (!temporary.empty()) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        }
        return gLastError = ManualPlacementPolicyError::WriteFailed;
    }
}

ManualPlacementPolicySnapshot manualPlacementPolicySnapshot() {
    std::scoped_lock lock(gPersistenceMutex);
    auto const items = gAllowedItems.load(std::memory_order_acquire);
    return {*items, gLastError};
}

bool manualPlacementAllows(std::string_view itemId) {
    auto const items = gAllowedItems.load(std::memory_order_acquire);
    return manualAllowedItemContains(*items, itemId);
}

} // namespace lholo::place
