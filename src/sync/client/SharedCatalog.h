#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "sync/common/ControlMessages.h"
#include "sync/common/Protocol.h"
#include "sync/common/SharedTypes.h"

// Client-side view of the server-authoritative placement catalog (§4.6). The
// server is the source of truth; this tracks the last snapshot sequence and
// applies CATALOG_DELTA events only when they are the exact next sequence.
// Out-of-order/duplicate deltas are ignored; a gap forces a full LIST reload.

namespace lholo::sync {

enum class DeltaResult {
    Applied,
    Ignored,   // stale or duplicate sequence
    Gap,       // sequence skipped: caller must re-request the list
    Invalid,   // malformed action / mismatched payload
};

class SharedCatalog {
public:
    // Replaces the whole catalog from a complete, consistent snapshot.
    void applySnapshot(std::vector<SharedPlacementDto> items, std::uint64_t snapshotSequence);

    [[nodiscard]] DeltaResult applyDelta(control::CatalogDelta const& delta);

    [[nodiscard]] std::vector<SharedPlacementDto> const& items() const { return mItems; }
    [[nodiscard]] std::optional<SharedPlacementDto> find(Uuid const& placementId) const;
    [[nodiscard]] std::uint64_t sequence() const { return mSequence; }
    [[nodiscard]] bool hasSnapshot() const { return mHasSnapshot; }
    [[nodiscard]] std::size_t activeCount() const;

    void clear();

private:
    std::vector<SharedPlacementDto> mItems;
    std::uint64_t mSequence{0};
    bool mHasSnapshot{false};
};

} // namespace lholo::sync
