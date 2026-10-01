#pragma once

#include "overlay/CompanionBridge.h"

#include <atomic>
#include <mutex>
#include <optional>
#include <utility>

namespace lholo::overlay::companion::detail {

struct Registration {
    void* owner{};
    unsigned hotkey{};
    DrawFn drawGui{};
    DrawFn drawHud{};
    HudNeededFn hudNeeded{};
    StateFn stateChanged{};
    RenderV3Fn renderV3{};
    WindowMessageV3Fn windowMessageV3{};
    GraphicsResetV3Fn resetGraphicsV3{};
    bool independentRenderer{};
    bool operator==(Registration const&) const = default;
};

// A lease stays on the acquiring thread. Retirement closes admission before
// draining leases, and keeps registration closed through the final callbacks.
// No provider callback runs while the store mutex is held.
class CallbackStore final {
public:
    class Lease final {
    public:
        Registration registration{};
        bool changed{};
        Lease() = default;
        Lease(Lease const&) = delete;
        Lease& operator=(Lease const&) = delete;
        Lease(Lease&& other) noexcept
            : registration(other.registration), changed(other.changed),
              mStore(std::exchange(other.mStore, nullptr)) {}
        Lease& operator=(Lease&&) = delete;
        ~Lease() {
            if (!mStore) return;
            --sReaderDepth;
            if (mStore->mReaders.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                mStore->mReaders.notify_all();
            }
        }
        bool active() const noexcept { return mStore != nullptr; }
    private:
        friend class CallbackStore;
        Lease(CallbackStore& store, Registration value, bool changedValue = false)
            : registration(value), changed(changedValue), mStore(&store) {
            store.mReaders.fetch_add(1, std::memory_order_acq_rel);
            ++sReaderDepth;
        }
        CallbackStore* mStore{};
    };

    struct Retirement {
        Registration registration;
        bool wasVisible{};
    };

    bool beginSession() {
        std::lock_guard lock(mMutex);
        if (mRetiring) return false;
        mAccepting = true;
        return true;
    }

    bool publish(Registration registration) {
        std::lock_guard lock(mMutex);
        if (!mAccepting || mRetiring) return false;
        if (mRegistered) return mRegistration == registration;
        mRegistration = registration;
        mVisible = false;
        mRegistered = true;
        return true;
    }

    Lease acquire() {
        std::lock_guard lock(mMutex);
        return mRegistered ? Lease{*this, mRegistration} : Lease{};
    }

    Lease changeVisibility(bool visible) {
        std::lock_guard lock(mMutex);
        if (!mRegistered) return {};
        auto const changed = std::exchange(mVisible, visible) != visible;
        return Lease{*this, mRegistration, changed};
    }

    bool registered(void* owner = nullptr) const {
        std::lock_guard lock(mMutex);
        return mRegistered && (!owner || mRegistration.owner == owner);
    }
    bool visible() const {
        std::lock_guard lock(mMutex);
        return mRegistered && mVisible;
    }

    std::optional<Retirement> beginRetirement(void* owner, bool shutdown = false) {
        if (sReaderDepth != 0) return std::nullopt;
        std::lock_guard lock(mMutex);
        if (shutdown) mAccepting = false;
        if (mRetiring) return std::nullopt;
        if (!mRegistered) {
            mRetiring = true;
            return Retirement{};
        }
        if (!shutdown && (!owner || mRegistration.owner != owner)) return std::nullopt;
        mRetiring = true;
        mRegistered = false;
        return Retirement{mRegistration, std::exchange(mVisible, false)};
    }

    void waitForReaders() const {
        for (;;) {
            auto const readers = mReaders.load(std::memory_order_acquire);
            if (readers == 0) return;
            mReaders.wait(readers, std::memory_order_acquire);
        }
    }

    void finishRetirement() {
        std::lock_guard lock(mMutex);
        mRegistration = {};
        mRetiring = false;
    }

private:
    inline static thread_local unsigned sReaderDepth{};
    mutable std::mutex mMutex;
    Registration mRegistration;
    std::atomic_uint mReaders{};
    bool mRegistered{};
    bool mVisible{};
    bool mRetiring{};
    bool mAccepting{true};
};

} // namespace lholo::overlay::companion::detail
