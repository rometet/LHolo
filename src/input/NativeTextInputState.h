#pragma once
#include "input/MenuRoute.h"
#include <atomic>
namespace lholo::input {
class NativeTextInputState {
public:
    std::uint64_t token(NativeTextInputFlag flag) const { return mState[index(flag)].load(std::memory_order_acquire); }
    void gain(NativeTextInputFlag flag) {
        auto& word=mState[index(flag)];auto value=word.load(std::memory_order_acquire);
        while(!word.compare_exchange_weak(value,(value+2)|1,std::memory_order_acq_rel)){}
    }
    bool clearIfCurrent(NativeTextInputFlag flag,std::uint64_t token) {
        return mState[index(flag)].compare_exchange_strong(token,(token+2)&~std::uint64_t{1},std::memory_order_acq_rel);
    }
    bool blocked() const {
        for(auto const& word:mState)if(word.load(std::memory_order_acquire)&1)return true;
        return false;
    }
    void reset() {
        for(auto& word:mState){auto value=word.load(std::memory_order_acquire);
            while(!word.compare_exchange_weak(value,(value+2)&~std::uint64_t{1},std::memory_order_acq_rel)){} }
    }
private:
    static constexpr std::size_t index(NativeTextInputFlag flag) {
        return flag==NativeTextInputFlag::Focus?0:flag==NativeTextInputFlag::Keyboard?1:2;
    }
    std::array<std::atomic_uint64_t,3> mState{};
};
} // namespace lholo::input
