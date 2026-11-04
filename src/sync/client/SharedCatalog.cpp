#include "sync/client/SharedCatalog.h"

#include <algorithm>

namespace lholo::sync {
namespace {

void upsert(std::vector<SharedPlacementDto>& items, SharedPlacementDto const& dto) {
    auto it = std::find_if(items.begin(), items.end(), [&](SharedPlacementDto const& candidate) {
        return candidate.placementId == dto.placementId;
    });
    if (it == items.end()) {
        items.push_back(dto);
    } else {
        *it = dto;
    }
}

void erase(std::vector<SharedPlacementDto>& items, Uuid const& placementId) {
    std::erase_if(items, [&](SharedPlacementDto const& candidate) {
        return candidate.placementId == placementId;
    });
}

} // namespace

void SharedCatalog::applySnapshot(std::vector<SharedPlacementDto> items, std::uint64_t snapshotSequence) {
    mItems = std::move(items);
    // Tombstones are not part of an active listing.
    std::erase_if(mItems, [](SharedPlacementDto const& dto) { return dto.deleted; });
    mSequence = snapshotSequence;
    mHasSnapshot = true;
}

DeltaResult SharedCatalog::applyDelta(control::CatalogDelta const& delta) {
    if (!mHasSnapshot) {
        return DeltaResult::Gap; // no baseline yet
    }
    if (delta.action != "upsert" && delta.action != "delete") {
        return DeltaResult::Invalid;
    }
    if (delta.sequence == mSequence) {
        return DeltaResult::Ignored; // duplicate
    }
    if (delta.sequence < mSequence) {
        return DeltaResult::Ignored; // stale / replayed
    }
    if (delta.sequence != mSequence + 1) {
        return DeltaResult::Gap; // missed events: reload the list
    }

    if (delta.action == "delete") {
        erase(mItems, delta.placement.placementId);
    } else if (delta.placement.deleted) {
        // A tombstone carried as an upsert still removes the entry.
        erase(mItems, delta.placement.placementId);
    } else {
        upsert(mItems, delta.placement);
    }
    mSequence = delta.sequence;
    return DeltaResult::Applied;
}

std::optional<SharedPlacementDto> SharedCatalog::find(Uuid const& placementId) const {
    auto it = std::find_if(mItems.begin(), mItems.end(), [&](SharedPlacementDto const& candidate) {
        return candidate.placementId == placementId;
    });
    if (it == mItems.end()) {
        return std::nullopt;
    }
    return *it;
}

std::size_t SharedCatalog::activeCount() const {
    return mItems.size();
}

void SharedCatalog::clear() {
    mItems.clear();
    mSequence = 0;
    mHasSnapshot = false;
}

} // namespace lholo::sync
