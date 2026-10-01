#pragma once

#include <memory>
#include <utility>

namespace lholo::app {
// The returned pointer always belongs to the map, including duplicate keys.
// Never publish a pre-insertion borrow of the candidate being transferred.
template <class Map, class Key, class Owner>
auto retainOwnedObject(Map& owners, Key&& key, Owner candidate) {
    auto const [retained, inserted] = owners.try_emplace(std::forward<Key>(key), std::move(candidate));
    return std::pair{retained->second.get(), inserted};
}
} // namespace lholo::app
