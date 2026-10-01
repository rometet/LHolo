#pragma once

#include <algorithm>
#include <functional>
#include <list>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lholo::projection::detail {
// External synchronization is required. Preserve first-arrival FIFO order,
// merge repeated pending keys, and leave the queue unchanged if admission fails.
template <class Key, class Value, class Hash = std::hash<Key>>
class CoalescedEventQueue {
public:
    CoalescedEventQueue() = default;
    CoalescedEventQueue(CoalescedEventQueue const&) = delete;
    CoalescedEventQueue& operator=(CoalescedEventQueue const&) = delete;
    CoalescedEventQueue(CoalescedEventQueue&&) = delete;
    CoalescedEventQueue& operator=(CoalescedEventQueue&&) = delete;
    template <class Merge>
    void push(Key key, Value value, Merge merge) {
        static_assert(std::is_nothrow_invocable_v<Merge, Value&, Value const&>);
        static_assert(std::is_nothrow_invocable_v<Hash const&, Key const&>);
        if (!mIndex) mIndex.emplace();
        if (auto existing = mIndex->find(key); existing != mIndex->end()) {
            merge(existing->second->second, value);
            return;
        }
        mPending.emplace_back(std::move(key), std::move(value));
        auto const pending = std::prev(mPending.end());
        // Roll back through unwinding, without intercepting/rethrowing the
        // original allocation failure. Commit only after both owners agree.
        struct Admission {
            decltype(mPending)& entries;
            bool committed{};
            ~Admission() { if (!committed) entries.pop_back(); }
        } admission{mPending};
        auto const inserted = mIndex->emplace(pending->first, pending);
        if (inserted.second) {
            admission.committed = true;
        } else {
            merge(inserted.first->second->second, pending->second);
        }
    }
    std::vector<Value> take(std::size_t limit) {
        static_assert(std::is_nothrow_copy_constructible_v<Value>);
        std::vector<Value> values;
        auto const count = (std::min)(limit, mPending.size());
        values.reserve(count); // Allocate before consuming any pending fact.
        for (std::size_t i = 0; i < count; ++i) {
            auto const& pending = mPending.front();
            values.push_back(pending.second);
            mIndex->erase(pending.first);
            mPending.pop_front();
        }
        return values;
    }
    std::size_t size() const noexcept { return mPending.size(); }
    // Destroy the index as well: unordered_map::clear retains peak bucket
    // storage across world/load cycles, while resetting optional allocates none.
    void clear() noexcept { mIndex.reset(); mPending.clear(); }
private:
    using Pending = std::list<std::pair<Key, Value>>;
    Pending mPending;
    std::optional<std::unordered_map<Key, typename Pending::iterator, Hash>> mIndex;
};
} // namespace lholo::projection::detail
