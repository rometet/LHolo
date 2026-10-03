#pragma once

#include "overlay/CompanionCallbackStore.h"
#include "overlay/SharedFontPreparation.h"

#include <atomic>
#include <thread>

namespace lholo::tests {
template <class Check>
void runCompanionCallbackChecks(Check check) {
    using namespace overlay::companion::detail;
    CallbackStore store;
    int owner{}, nextOwner{};
    Registration first;
    first.owner = &owner;
    first.hotkey = 121;
    Registration next = first;
    next.owner = &nextOwner;
    check(store.publish(first));
    check(store.publish(first)); // Exact repeated registration is idempotent.
    check(!store.publish(next));
    auto changed = first;
    changed.hotkey = 122;
    check(!store.publish(changed)); // Never replace a live callback generation.
    auto const initializeFonts = +[](void*) noexcept {};
    check(!store.setFontInitializer(&nextOwner, initializeFonts));
    check(!store.setFontInitializer(&owner, nullptr));
    check(store.setFontInitializer(&owner, initializeFonts));
    check(store.publish(first)); // Optional fonts preserve the old v2 entry point.
    std::uint64_t fontGeneration{};
    {
        auto lease = store.acquire();
        check(lease.registration.fontInitializer == initializeFonts);
        fontGeneration = lease.registration.fontGeneration;
    }
    check(store.setFontInitializer(&owner, initializeFonts));
    {
        auto lease = store.acquire();
        check(lease.registration.fontGeneration == fontGeneration);
    }
    SharedFontPreparation fonts;
    int context{};
    check(!fonts.needsPreparation(nullptr, &owner, fontGeneration));
    check(fonts.needsPreparation(&context, &owner, fontGeneration));
    fonts.prepared(&context, &owner, fontGeneration);
    check(!fonts.needsPreparation(&context, &owner, fontGeneration));
    check(fonts.needsPreparation(&context, &owner, fontGeneration + 1));
    check(fonts.needsPreparation(&context, &nextOwner, fontGeneration));
    fonts.reset();
    check(fonts.needsPreparation(&context, &owner, fontGeneration));
    {
        auto lease = store.acquire();
        check(lease.active());
        check(!store.beginRetirement(&owner)); // Reentrant teardown fails safely.
    }
    {
        auto lease = store.changeVisibility(true);
        check(lease.active() && lease.changed);
    }
    check(store.visible());
    check(!store.beginRetirement(&nextOwner));

    std::atomic_bool entered{}, release{};
    bool leaseAcquired{};
    std::thread reader([&] {
        auto lease = store.acquire();
        leaseAcquired = lease.active();
        entered.store(true, std::memory_order_release);
        entered.notify_all();
        release.wait(false, std::memory_order_acquire);
    });
    entered.wait(false, std::memory_order_acquire);
    auto retiring = store.beginRetirement(&owner);
    check(retiring && retiring->registration.owner == &owner && retiring->wasVisible);
    check(!store.acquire().active());
    check(!store.setFontInitializer(&owner, initializeFonts));
    check(!store.publish(next));
    check(!store.publish(first));
    check(!store.beginRetirement(&owner));
    check(!store.beginSession());
    release.store(true, std::memory_order_release);
    release.notify_all();
    store.waitForReaders();
    reader.join();
    check(leaseAcquired);
    // Final provider notifications still belong to retirement: no registration
    // can sneak in after the reader drain and before graphics reset completes.
    check(!store.publish(next));
    store.finishRetirement();
    check(store.publish(next));
    {
        auto lease = store.acquire();
        check(!lease.registration.fontInitializer);
        check(lease.registration.fontGeneration > fontGeneration);
    }
    check(store.registered(&nextOwner));
    auto shutdown = store.beginRetirement(nullptr, true);
    check(shutdown && shutdown->registration.owner == &nextOwner);
    store.waitForReaders();
    store.finishRetirement();
    check(!store.publish(first));
    check(store.beginSession());
    check(store.publish(first));
    retiring = store.beginRetirement(&owner);
    store.waitForReaders();
    store.finishRetirement();
    check(!store.registered());
}
} // namespace lholo::tests
