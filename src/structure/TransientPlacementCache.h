#pragma once
#include "structure/PlacementSession.h"
#include <map>
#include <optional>

namespace lholo::structure {
// Session-only server dimensions. A world change clears this value cache;
// no temporary engine address is ever written to a placement document.
class TransientPlacementCache {
public:
    void remember(int dimension, PlacementDocument document) {
        if (!mDocuments.contains(dimension) && mDocuments.size() >= 16) mDocuments.erase(mDocuments.begin());
        mDocuments.insert_or_assign(dimension, std::move(document));
    }
    std::optional<PlacementDocument> find(int dimension) const {
        auto const it=mDocuments.find(dimension);
        return it==mDocuments.end()?std::nullopt:std::optional{it->second};
    }
    void clear() noexcept { mDocuments.clear(); }
    std::size_t size() const noexcept { return mDocuments.size(); }
private:
    std::map<int,PlacementDocument> mDocuments;
};
} // namespace lholo::structure
