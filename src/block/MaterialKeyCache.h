#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>

namespace lholo::block::detail {
template <class Key, class Factory>
std::string const& cachedMaterialKey(std::unordered_map<Key, std::string>& cache, Key const& key, Factory&& create) {
    if (auto const found = cache.find(key); found != cache.end()) return found->second;
    return cache.try_emplace(key, std::invoke(std::forward<Factory>(create))).first->second;
}
} // namespace lholo::block::detail
