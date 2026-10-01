#pragma once

#include <cstdint>
#include <mutex>
#include <optional>

namespace lholo::overlay::detail {

// Owned identities only: none of these values is ever dereferenced. A queued
// request belongs to one overlay installation, window, client and local player.
struct MouseHandoffIdentity {
    std::uintptr_t window{}, client{}, player{};
    bool operator==(MouseHandoffIdentity const&) const = default;
};
struct MouseHandoffMessage {
    std::uintptr_t ticket{}, session{};
};

inline bool foregroundMouseHandoffWindow(
    std::uintptr_t window, std::uintptr_t foreground, bool exists, bool visible, bool iconic
) noexcept { return window && window==foreground && exists && visible && !iconic; }
inline bool gameMouseHandoffAllowed(bool foreground, bool menusVisible, bool shuttingDown, bool gameplayScreen) noexcept {
    return foreground && !menusVisible && !shuttingDown && gameplayScreen;
}
inline bool windowThreadMouseHandoffAllowed(std::uint32_t caller, std::uint32_t owner) noexcept {
    return owner && caller==owner;
}

class GameMouseHandoffRequest {
public:
    void resetWindow(std::uintptr_t window, std::uintptr_t session) {
        std::lock_guard lock(mMutex);
        mWindow=window;
        mSession=session;
        mPhase=Phase::Idle;
        mTransitionPending=false;
        ++mCancellationEpoch;
    }
    void cancel() {
        std::lock_guard lock(mMutex);
        mPhase=Phase::Idle;
        mTransitionPending=false;
        ++mCancellationEpoch;
    }
    bool matchesWindow(std::uintptr_t window) const {
        std::lock_guard lock(mMutex);
        return window && window==mWindow && mSession;
    }
    std::uint64_t cancellationEpoch() const {
        std::lock_guard lock(mMutex);
        return mCancellationEpoch;
    }
    bool begin(MouseHandoffIdentity identity, std::uint64_t expectedEpoch) {
        std::lock_guard lock(mMutex);
        // An earlier focus/menu/shutdown cancellation must not be overwritten
        // by a render producer that sampled foreground before that event.
        if (expectedEpoch!=mCancellationEpoch) return false;
        mPhase=Phase::Idle;
        mTransitionPending=false;
        if (!identity.window || identity.window!=mWindow || !mSession
            || !identity.client || !identity.player) return false;
        if (++mTicket==0) ++mTicket;
        mIdentity=identity;
        mPhase=Phase::Transition;
        return true;
    }
    bool transitionCurrent(MouseHandoffIdentity identity) const {
        std::lock_guard lock(mMutex);
        return mPhase==Phase::Transition && identity==mIdentity;
    }
    std::optional<MouseHandoffMessage> queueTransition(MouseHandoffIdentity identity) {
        std::lock_guard lock(mMutex);
        if (mPhase!=Phase::Transition || identity!=mIdentity || mTransitionPending) return std::nullopt;
        mTransitionPending=true;
        return MouseHandoffMessage{mTicket,mSession};
    }
    void transitionDeliveryFailed(MouseHandoffMessage message) {
        std::lock_guard lock(mMutex);
        if (mPhase==Phase::Transition && message.ticket==mTicket && message.session==mSession)
            mTransitionPending=false;
    }
    bool consumeTransition(MouseHandoffMessage message, MouseHandoffIdentity current, bool allowedNow) {
        std::lock_guard lock(mMutex);
        if (mPhase!=Phase::Transition || !mTransitionPending
            || message.ticket!=mTicket || message.session!=mSession) return false;
        mTransitionPending=false;
        if (!allowedNow || current!=mIdentity || current.window!=mWindow) {
            mPhase=Phase::Idle;
            return false;
        }
        return true;
    }
    std::optional<MouseHandoffMessage> queue(MouseHandoffIdentity identity) {
        std::lock_guard lock(mMutex);
        if (mPhase!=Phase::Transition || identity!=mIdentity) return std::nullopt;
        mPhase=Phase::Queued;
        mTransitionPending=false;
        return MouseHandoffMessage{mTicket,mSession};
    }
    void deliveryFailed(MouseHandoffMessage message) {
        std::lock_guard lock(mMutex);
        if (mPhase==Phase::Queued && message.ticket==mTicket && message.session==mSession)
            mPhase=Phase::Transition;
    }
    bool consume(MouseHandoffMessage message, MouseHandoffIdentity current, bool allowedNow) {
        std::lock_guard lock(mMutex);
        if (mPhase!=Phase::Queued || message.ticket!=mTicket || message.session!=mSession)
            return false;
        mPhase=Phase::Idle;
        // Invalid current conditions retire the matching request as well. A
        // focus regain cannot make an old queued request valid a second time.
        return allowedNow && current==mIdentity && current.window==mWindow;
    }
private:
    enum class Phase { Idle, Transition, Queued };
    mutable std::mutex mMutex;
    Phase mPhase{Phase::Idle};
    std::uintptr_t mWindow{}, mSession{}, mTicket{};
    MouseHandoffIdentity mIdentity{};
    bool mTransitionPending{};
    std::uint64_t mCancellationEpoch{};
};
} // namespace lholo::overlay::detail
