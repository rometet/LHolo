#pragma once

#include "app/ScopeExit.h"

namespace lholo::projection::detail {
// Both the global neighbor queries and the section mesh must see the same cell.
template <class Set, class Key>
void registerExtraCell(Set& allCells, Set& sectionCells, Key const& key) {
    auto const [entry, inserted] = allCells.insert(key);
    bool committed{};
    app::ScopeExit rollback([&]() noexcept {
        if (inserted && !committed) allCells.erase(entry);
    });
    sectionCells.insert(key);
    committed = true;
}
} // namespace lholo::projection::detail
