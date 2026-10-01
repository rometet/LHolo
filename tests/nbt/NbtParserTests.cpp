#include "structure/formats/JavaNbtReader.h"
#include "projection/runtime/CoalescedEventQueue.h"
#include "structure/formats/McstructureIndices.h"
#include "structure/formats/LitematicFormatRules.h"
#include "app/RetainedObject.h"
#include "app/FutureResult.h"
#include "app/ListenerRetirement.h"
#include "app/InitializationRetention.h"
#include "projection/correction/ExtraCellRegistration.h"
#include "projection/correction/CorrectionUpdate.h"
#include "projection/runtime/EpochFailure.h"
#include "projection/runtime/WorldEventInterest.h"
#include "projection/mesh/SectionBuildCommit.h"
#include "projection/mesh/DirtySectionSelection.h"
#include "structure/ClientViewState.h"
#include "place/VoxelRayAxis.h"
#include "place/PlacementQuantization.h"
#include "NbtTypeChecks.h"
#include "LiquidBoundaryCacheChecks.h"
#include "ui/FileDialog.h"
#include "block/MaterialKeyCache.h"
#include "overlay/OverlayResourceGate.h"

#include <cstdio>
#include <cstdlib>
#include <new>
#include <memory>
#include <set>
#include <string>
#include <future>
#include <thread>

namespace {
thread_local std::size_t gRejectAllocationAbove{};
thread_local int gAllocationCallsBeforeFailure{-1};
int gChecks{}, gFailures{};
void check(bool ok, char const* name) {
    ++gChecks;
    if (!ok) { ++gFailures; std::fprintf(stderr, "FAIL %s\n", name); }
}
void testOverlayResourceGate() {
    using lholo::overlay::detail::acquireLiveOverlayResources;
    using lholo::overlay::detail::snapshotAfterOverlayInitialization;
    std::mutex resources;
    std::atomic_bool stopping{};
    {
        auto lock = acquireLiveOverlayResources(resources, stopping);
        check(lock.owns_lock(), "live overlay admits resource initialization");
    }
    check(snapshotAfterOverlayInitialization(resources, [] { return 7; }) == 7,
        "overlay shutdown snapshot returns initialized value");

    // Present passed its entry check, but is blocked behind a previous owner.
    // Teardown publishes stop before that callback can acquire resources.
    std::unique_lock owner(resources);
    std::promise<void> entered;
    auto entry = entered.get_future();
    bool enteredWhileRunning{}, lateAdmitted{}, lateWindowInstalled{};
    std::thread latePresent([&] {
        enteredWhileRunning = !stopping.load(std::memory_order_acquire);
        entered.set_value();
        auto lock = acquireLiveOverlayResources(resources, stopping);
        lateAdmitted = lock.owns_lock();
        if (lateAdmitted) lateWindowInstalled = true;
    });
    entry.wait();
    stopping.store(true, std::memory_order_release);
    owner.unlock();
    latePresent.join();
    check(enteredWhileRunning && !lateAdmitted, "already-entered Present rechecks stop after waiting for resources");
    check(!lateWindowInstalled, "shutdown cannot be followed by a late window subclass installation");
    {
        auto lock = acquireLiveOverlayResources(resources, stopping);
        check(!lock.owns_lock(), "new initialization after shutdown never acquires live resources");
    }
    bool const released = resources.try_lock();
    check(released, "rejected admission releases the resource mutex");
    if (released) resources.unlock();

    for (unsigned i = 0; i < 20; ++i) {
        stopping.store(false, std::memory_order_release);
        std::atomic_bool installed{};
        std::promise<void> started, finish;
        auto startedFuture = started.get_future();
        auto finishFuture = finish.get_future();
        std::thread initializer([&] {
            auto lock = acquireLiveOverlayResources(resources, stopping);
            started.set_value();
            finishFuture.wait();
            if (lock.owns_lock()) installed.store(true, std::memory_order_release);
        });
        startedFuture.wait();
        stopping.store(true, std::memory_order_release);
        auto snapshot = std::async(std::launch::async, [&] {
            return snapshotAfterOverlayInitialization(resources, [&] {
                return installed.load(std::memory_order_acquire);
            });
        });
        check(snapshot.wait_for(std::chrono::seconds(0)) == std::future_status::timeout,
            "shutdown snapshot waits for admitted native initializer");
        finish.set_value();
        initializer.join();
        check(snapshot.get(), "shutdown sees the WndProc installed by its admitted initializer");
    }
}
void testInitializationRetention() {
    using lholo::app::initializeWithRetainedRollback;
    struct ModuleOwner {
        int& destroyed;
        explicit ModuleOwner(int& value) : destroyed(value) {}
        ~ModuleOwner() { ++destroyed; }
    };
    int destroyed{}, rollbacks{}, failures{};
    auto owner = std::make_shared<ModuleOwner>(destroyed);
    std::weak_ptr<ModuleOwner> observer = owner;
    std::shared_ptr<ModuleOwner> retained;
    auto failure = [&](char const*) noexcept { ++failures; };
    check(initializeWithRetainedRollback(owner, retained, [] { return true; }, [&]() noexcept {
        ++rollbacks;
        return false;
    }, failure), "successful initialization is reported normally");
    check(!retained && rollbacks == 0, "successful initialization does not retain or roll back its module");
    check(!initializeWithRetainedRollback(owner, retained, [] { return false; }, [&]() noexcept {
        ++rollbacks;
        return true;
    }, failure), "failed initialization with complete cleanup remains a failure");
    owner.reset(); // The loader discards an unregistered module after failure.
    check(observer.expired() && destroyed == 1 && !retained,
        "complete rollback allows the loader to destroy its module owner");

    owner = std::make_shared<ModuleOwner>(destroyed);
    observer = owner;
    auto const before = rollbacks;
    gAllocationCallsBeforeFailure = 0;
    auto const initialized = initializeWithRetainedRollback(owner, retained, [] { return false; }, [&]() noexcept {
        ++rollbacks;
        return false;
    }, failure);
    gAllocationCallsBeforeFailure = -1;
    check(!initialized && rollbacks == before + 1, "incomplete rollback runs once without new allocations");
    check(retained == owner, "incomplete rollback retains the actual loader module owner");
    owner.reset();
    check(!observer.expired() && destroyed == 1,
        "discarding the loader owner cannot destroy a module with surviving callbacks");
    check(failures == 0, "explicit initialization failure does not manufacture an exception diagnostic");
    // Only this test's external owner releases after simulated callbacks stop.
    retained.reset();
    check(observer.expired() && destroyed == 2, "retained ownership has no duplicate destruction");

    owner = std::make_shared<ModuleOwner>(destroyed);
    observer = owner;
    check(!initializeWithRetainedRollback(owner, retained, []() -> bool { throw 42; }, []() noexcept {
        return false;
    }, failure), "initialization exception still requires rollback residency");
    owner.reset();
    check(!observer.expired() && failures == 1, "exceptional initialization retains ownership and reports the failure");
    retained.reset();

    owner = std::make_shared<ModuleOwner>(destroyed);
    observer = owner;
    check(!initializeWithRetainedRollback(owner, retained, [] { return false; }, [&]() noexcept {
        return lholo::app::invokeNativeCallback([] { throw 42; }, failure);
    }, failure), "rollback boundary failure cannot report safe initialization");
    owner.reset();
    check(!observer.expired() && failures == 2, "caught cleanup exception retains the module owner");
    retained.reset();
}
void testListenerRetirement() {
    using lholo::app::detachAndRetireListener;
    struct Owner { bool registered{true}; };
    Owner oldOwner;
    Owner* binding = &oldOwner;
    std::array<char, 2> order{};
    std::size_t steps{};
    bool trackedDuringDetach{}, detachedBeforeRetirement{};
    detachAndRetireListener(binding, [&](Owner& owner) {
        trackedDuringDetach = binding == &owner && owner.registered;
        owner.registered = false;
        order[steps++] = 'D';
    }, [&] {
        detachedBeforeRetirement = !oldOwner.registered;
        binding = nullptr;
        order[steps++] = 'R';
    });
    check(trackedDuringDetach, "listener stays tracked during detach");
    check(detachedBeforeRetirement, "listener borrow retires after successful detach");
    check(order == (std::array<char, 2>{'D', 'R'}), "listener detach precedes borrow retirement");
    check(!binding, "removed listener does not retain its old owner");

    oldOwner.registered = true;
    binding = &oldOwner;
    int retirements{};
    bool rejected{};
    try {
        detachAndRetireListener(binding, [](Owner&) { throw std::bad_alloc{}; }, [&] {
            ++retirements;
            binding = nullptr;
        });
    } catch (std::bad_alloc const&) { rejected = true; }
    check(rejected && binding == &oldOwner && oldOwner.registered,
        "failed detach retains the still-registered owner for retry");
    check(retirements == 0, "failed detach cannot retire registration tracking");

    int removeAttempts{};
    auto removeForShutdown = [&](bool reject) {
        detachAndRetireListener(binding, [&](Owner& owner) {
            ++removeAttempts;
            if (reject) throw std::bad_alloc{};
            owner.registered = false;
        }, [&] { binding = nullptr; });
    };
    try { removeForShutdown(true); } catch (std::bad_alloc const&) {}
    check(binding == &oldOwner && oldOwner.registered,
        "interrupted shutdown retains the registration to be removed on retry");
    removeForShutdown(false);
    check(!binding && !oldOwner.registered && removeAttempts == 2,
        "retry after failed removal completes the remaining registration cleanup");
    removeForShutdown(false);
    check(removeAttempts == 2, "successful repeated shutdown does not remove a listener twice");

    auto destroyedOwner = std::make_unique<Owner>();
    binding = destroyedOwner.get();
    bool attachAttempted{};
    rejected = false;
    try {
        detachAndRetireListener(binding, [](Owner& owner) { owner.registered = false; }, [&] {
            binding = nullptr;
        });
        destroyedOwner.reset(); // Removal means no destruction callback will clear this borrow.
        attachAttempted = true;
        throw std::bad_alloc{}; // Simulate failure while registering the next level.
    } catch (std::bad_alloc const&) { rejected = true; }
    check(rejected && attachAttempted && !destroyedOwner, "next attachment failure follows old owner destruction");
    check(!binding, "attachment failure cannot leave an unobserved destroyed owner cached");
    int cleanupCalls{};
    // Do not intentionally dereference a stale borrow in the old-policy run.
    if (!binding) detachAndRetireListener(binding, [&](Owner&) { ++cleanupCalls; }, [&] { ++retirements; });
    check(cleanupCalls == 0, "cleanup after failed attachment does not dereference the old owner");
    Owner nextOwner;
    bool removedReplacement{};
    binding = &nextOwner; // A successful retry publishes only its newly registered owner.
    detachAndRetireListener(binding, [&](Owner& owner) {
        removedReplacement = &owner == &nextOwner;
        ++cleanupCalls;
    }, [&] { binding = nullptr; });
    check(removedReplacement, "listener retry removes only the replacement owner");
    check(!binding && cleanupCalls == 1, "listener retry cleanup remains usable after attachment failure");

    retirements = 0;
    detachAndRetireListener(static_cast<Owner*>(nullptr), [&](Owner&) { ++cleanupCalls; }, [&] { ++retirements; });
    check(cleanupCalls == 1 && retirements == 0, "initial unbound attachment has no previous registration to retire");
}
void testMaterialKeyCache() {
    using lholo::block::detail::cachedMaterialKey;
    std::unordered_map<int, std::string> cache;
    int factories{};
    auto factory = [&] { ++factories; return std::string{"material identity which exceeds small string storage"}; };
    auto const& first = cachedMaterialKey(cache, 1, factory);
    auto const* text = first.c_str();
    for (int repeat = 0; repeat < 1000; ++repeat) (void)cachedMaterialKey(cache, 1, factory);
    check(factories == 1, "cached material factory runs once per palette identity");
    check(first.c_str() == text, "cache hit preserves retained string storage");
    bool hitRejected{};
    try { (void)cachedMaterialKey(cache, 1, []() -> std::string { throw std::bad_alloc{}; }); }
    catch (std::bad_alloc const&) { hitRejected = true; }
    check(!hitRejected && cache.size() == 1, "cache hit does not allocate or run a failing factory");
    bool allocationRejected{};
    gAllocationCallsBeforeFailure = 0;
    try { (void)cachedMaterialKey(cache, 1, [] { return std::string{"unused string requiring heap allocation"}; }); }
    catch (std::bad_alloc const&) { allocationRejected = true; }
    gAllocationCallsBeforeFailure = -1;
    check(!allocationRejected, "cached material lookup succeeds when new allocations are unavailable");
    bool missRejected{};
    try { (void)cachedMaterialKey(cache, 2, []() -> std::string { throw std::bad_alloc{}; }); }
    catch (std::bad_alloc const&) { missRejected = true; }
    check(missRejected && cache.size() == 1, "failed cache miss leaves published entries intact");
    (void)cachedMaterialKey(cache, 2, factory);
    for (int identity = 3; identity < 1000; ++identity) (void)cachedMaterialKey(cache, identity, factory);
    check(first.c_str() == text && first == "material identity which exceeds small string storage",
        "material string reference survives palette-cache rehash");
}
void testExtraCellRegistration() {
    using lholo::projection::detail::registerExtraCell;
    using Key = std::tuple<int, int, int>;
    std::set<Key> allCells, sectionCells;
    Key const existing{-16, -64, 17}, next{16, -1, -17};
    registerExtraCell(allCells, sectionCells, existing);
    check(allCells.contains(existing) && sectionCells.contains(existing), "extra cell is published to both mesh indices");
    bool rejected{};
    gAllocationCallsBeforeFailure = 1; // Global set node succeeds; section node fails.
    try { registerExtraCell(allCells, sectionCells, next); }
    catch (std::bad_alloc const&) { rejected = true; }
    gAllocationCallsBeforeFailure = -1;
    check(rejected, "extra section registration failure reaches the error boundary");
    check(!allCells.contains(next) && !sectionCells.contains(next), "failed extra cell registration cannot leave a half-published cell");
    check(allCells.size() == 1 && sectionCells.size() == 1 && allCells.contains(existing),
        "failed extra cell registration preserves earlier cells");
    registerExtraCell(allCells, sectionCells, next);
    check(allCells.contains(next) && sectionCells.contains(next), "extra registration remains usable after failure");
    rejected = false;
    gAllocationCallsBeforeFailure = 0;
    try { registerExtraCell(allCells, sectionCells, Key{0, 0, 0}); }
    catch (std::bad_alloc const&) { rejected = true; }
    gAllocationCallsBeforeFailure = -1;
    check(rejected && allCells.size() == 2 && sectionCells.size() == 2, "first extra index allocation failure changes neither index");
    sectionCells.erase(next);
    rejected = false;
    gAllocationCallsBeforeFailure = 0;
    try { registerExtraCell(allCells, sectionCells, next); }
    catch (std::bad_alloc const&) { rejected = true; }
    gAllocationCallsBeforeFailure = -1;
    check(rejected && allCells.contains(next) && !sectionCells.contains(next),
        "registration rollback never removes a preexisting global cell");
    registerExtraCell(allCells, sectionCells, next);
    check(allCells == sectionCells, "a repeated registration repairs a missing section entry");
}
void testCorrectionFailureBoundary() {
    using lholo::projection::detail::runCorrectionUpdate;
    lholo::projection::detail::EpochFailure failure;
    auto const epoch = failure.token();
    int failureCalls{};
    auto fail = [&]() noexcept { ++failureCalls; failure.mark(epoch); };
    auto const result = runCorrectionUpdate([] { return 42; }, fail);
    check(result == 42 && !failure.failed() && failureCalls == 0,
        "successful correction preserves its result and active session");
    bool rejected{}, partialUpdate{};
    try {
        runCorrectionUpdate([&] {
            partialUpdate = true; // Earlier cells/notifications can already have been committed.
            throw std::bad_alloc{};
        }, fail);
    } catch (std::bad_alloc const&) { rejected = true; }
    check(rejected && partialUpdate, "correction failure preserves the original exception for diagnostics");
    check(failure.failed() && failureCalls == 1, "interrupted correction marks the partial session unusable");
    failure.advance();
    check(!failure.failed(), "explicit session replacement permits correction again");
    try { runCorrectionUpdate([] { throw 7; }, fail); } catch (int) {}
    check(!failure.failed() && failureCalls == 2, "late correction failure cannot poison a replacement epoch");
}
void appendI32(std::string& bytes, std::int32_t value) {
    auto const bits = static_cast<std::uint32_t>(value);
    for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(static_cast<char>(bits >> shift));
}

void testSectionBuildCommit() {
    using lholo::projection::detail::completeSynchronousSectionBuild;
    struct State {
        bool dirty{true}, incrementalDirty{true};
        std::uint64_t requestedRevision{5}, uploadedRevision{4};
    } section;
    bool failed{};
    try { completeSynchronousSectionBuild(section, [] { throw std::bad_alloc{}; }); }
    catch (std::bad_alloc const&) { failed = true; }
    check(failed && section.dirty, "failed synchronous mesh remains eligible for retry");
    check(section.uploadedRevision == 4 && section.incrementalDirty, "failed mesh does not commit metadata");
    completeSynchronousSectionBuild(section, [] {});
    check(!section.dirty && !section.incrementalDirty && section.uploadedRevision == 5, "retry commits only after success");
    section.dirty = section.incrementalDirty = true;
    completeSynchronousSectionBuild(section, [&] { ++section.requestedRevision; });
    check(section.uploadedRevision == 5 && section.requestedRevision == 6, "nested invalidation cannot claim unbuilt revision");
    check(section.dirty && section.incrementalDirty, "newer requested mesh remains pending");
}

void testPendingStructureOwner() {
    struct Pending { std::string path; std::future<int> future; };
    std::promise<int> first;
    first.set_exception(std::make_exception_ptr(std::bad_alloc{}));
    std::optional<Pending> pending{Pending{"first.mcstructure", first.get_future()}};
    auto read = [&] {
        auto completed = lholo::app::takePendingValue(pending);
        return completed.future.get();
    };
    bool rejected{};
    try { (void)read(); } catch (std::bad_alloc const&) { rejected = true; }
    check(rejected && !pending, "failed structure future retires pending owner before error processing");
    std::promise<int> second;
    second.set_value(42);
    pending.emplace(Pending{"second.mcstructure", second.get_future()});
    check(read() == 42 && !pending, "next structure job remains consumable after failure");
}

void testDirtySectionSelection() {
    using lholo::projection::detail::selectDirtySection;
    struct Candidate { float center; bool dirty, incrementalDirty, buildInFlight; };
    std::array<Candidate, 4> sections{{{0, true, false, false}, {10, true, false, false},
        {100, true, true, false}, {0, true, true, true}}};
    float camera{};
    auto select = [&] { return selectDirtySection(std::span<Candidate const>{sections},
        [&](auto const& section) { auto const delta = section.center - camera; return delta * delta; }); };
    check(select() == 2, "incremental dirty section has priority and in flight section is excluded");
    sections[2].dirty = false;
    check(select() == 0, "nearest initial dirty section selected");
    camera = 9;
    check(select() == 1, "new camera affects next admission");
    camera = 5;
    check(select() == 0, "equal distance uses deterministic section order");
    sections[0].dirty = sections[1].dirty = false;
    check(!select(), "no runnable dirty section yields no task");
}

void testClientViewState() {
    lholo::structure::detail::ClientViewState state;
    int level{}, dimension{}, otherDimension{};
    check(!state.snapshot(), "native view unavailable before player tick");
    state.publish(&level, &dimension, 90, {1, 0, 0});
    auto const first = state.snapshot();
    check(first && first->yaw == 90 && first->forward == (std::array<float, 3>{1, 0, 0}), "view is a copied tick value");
    state.publish(&level, &dimension, 180, {0, 0, 1});
    check(state.snapshot()->worldEpoch == first->worldEpoch && first->yaw == 90, "camera motion does not cancel world requests or mutate old snapshot");
    state.publish(&level, &otherDimension, 0, {0, 1, 0});
    check(state.snapshot()->worldEpoch != first->worldEpoch, "dimension replacement invalidates old world request");
    auto const dimensionEpoch = state.snapshot()->worldEpoch;
    state.invalidate();
    check(!state.snapshot(), "world destruction retires cached input view");
    state.publish(&level, &otherDimension, 0, {0, 1, 0});
    check(state.snapshot()->worldEpoch != dimensionEpoch, "same pointer addresses after rejoin have a new epoch");
}

void testVoxelRayAxis() {
    using lholo::place::detail::voxelRayAxis;
    auto const x = voxelRayAxis(0.5f, 0, 1);
    auto const y = voxelRayAxis(64, 64, 0);
    auto const z = voxelRayAxis(0.5f, 0, 0);
    check(y.step == 0 && std::isinf(y.nextBoundary) && std::isinf(y.delta), "zero direction on integer boundary never produces NaN or steps");
    check(x.nextBoundary < y.nextBoundary && x.nextBoundary < z.nextBoundary, "axis aligned ray enters X neighbor instead of stopping on inactive axis");
    auto const negativeZero = voxelRayAxis(-10, -10, -0.0f);
    check(negativeZero.step == 0 && std::isinf(negativeZero.nextBoundary), "signed zero axis stays inactive");
    auto const west = voxelRayAxis(-1.25f, -2, -1);
    check(west.step == -1 && west.nextBoundary == 0.75 && west.delta == 1, "negative coordinate boundary distance");
    auto const eastFar = voxelRayAxis(16777216.0f, 16777216, 1);
    check(eastFar.nextBoundary == 1, "large positive cell retains one block to next boundary");
}

void testLitematicRules() {
    using namespace lholo::structure::detail;
    auto const signedWord = std::bit_cast<std::int64_t>(0xd000000000000000ull);
    std::vector<std::int64_t> crossing{signedWord, 1};
    check(packedPaletteIndex(crossing, 12, 5) == 29, "five bit palette cell crosses signed long boundary");
    check(packedPaletteIndex(crossing, 13, 5) == 0, "following cell has independent bits");
    std::vector<std::int64_t> allSet{-1, -1};
    for (unsigned bits = 1; bits <= 32; ++bits) {
        auto const expected = static_cast<std::uint32_t>((1ull << bits) - 1);
        check(packedPaletteIndex(allSet, 0, bits) == expected, "all palette bit widths preserve unsigned mask");
    }
    check(packedPaletteIndex(allSet, 1, 32) == 0xffffffffu, "32 bit second palette cell");
    auto rejected = [](auto const& words, std::uint64_t index, unsigned bits) {
        try { (void)packedPaletteIndex(words, index, bits); }
        catch (std::runtime_error const&) { return true; }
        return false;
    };
    check(rejected(std::vector<std::int64_t>{signedWord}, 12, 5), "partial crossing word rejected");
    check(rejected(std::vector<std::int64_t>{}, 0, 2), "empty palette words rejected");
    check(rejected(allSet, 64, 2), "palette cell beyond words rejected");
    check(rejected(allSet, 0, 0) && rejected(allSet, 0, 33), "invalid palette bit widths rejected");
    check(rejected(allSet, UINT64_MAX, 32), "palette bit offset overflow rejected");
    JavaNbtTag::Compound forward, reverse;
    for (auto const name : {"alpha", "middle", "zeta"}) forward.emplace(name, JavaNbtTag{std::string{name}});
    for (auto const name : {"zeta", "middle", "alpha"}) reverse.emplace(name, JavaNbtTag{std::string{name}});
    reverse.rehash(97);
    auto names = [](auto const& compound) {
        std::vector<std::string> result;
        for (auto const* entry : namedEntriesInOrder(compound)) result.push_back(entry->first);
        return result;
    };
    check(names(forward) == (std::vector<std::string>{"alpha", "middle", "zeta"}), "region canonical precedence");
    check(names(reverse) == names(forward), "region precedence ignores NBT insertion order and hash buckets");
    auto const order = namedEntriesInOrder(reverse);
    check(std::get<std::string>(order.back()->second.value) == "zeta", "ordered region references retain correct payload");
}

void testJavaTextEncoding() {
    auto readText = [](std::string_view encoded) {
        std::string bytes("\x0a\0\0\x08\0\x01x", 7);
        bytes.push_back(static_cast<char>(encoded.size() >> 8));
        bytes.push_back(static_cast<char>(encoded.size()));
        bytes += encoded; bytes.push_back(0);
        auto root = lholo::structure::detail::JavaNbtReader(bytes).readRoot();
        return std::get<std::string>(root.at("x").value);
    };
    check(readText("ASCII") == "ASCII", "Java ASCII string");
    check(readText("\xE6\x97\xA5\xE6\x9C\xAC") == "\xE6\x97\xA5\xE6\x9C\xAC", "Java BMP UTF-8");
    check(readText("\xED\xA0\xBD\xED\xB8\x80") == "\xF0\x9F\x98\x80", "Java surrogate pair becomes UTF-8");
    check(readText(std::string("A\xC0\x80" "B", 4)) == std::string("A\0B", 3), "Java modified NUL");
    check(readText("\xF0\x9F\x98\x80") == "\xF0\x9F\x98\x80", "standard UTF-8 compatibility");
    for (auto const encoded : {"\xC2", "\xE6\x97", "\x80", "\xC0\x81", "\xF4\x90\x80\x80"}) {
        bool rejected{};
        try { (void)readText(encoded); } catch (...) { rejected = true; }
        check(rejected, "invalid UTF encoding rejected");
    }
}
std::string rootField(unsigned char type, std::string payload) {
    std::string bytes{"\x0a\0\0", 3};
    bytes.push_back(static_cast<char>(type));
    bytes.append("\0\1x", 3);
    bytes += payload;
    bytes.push_back('\0');
    return bytes;
}
bool rejects(std::string const& bytes) {
    try { (void)lholo::structure::detail::JavaNbtReader{bytes}.readRoot(); }
    catch (std::runtime_error const&) { return true; }
    return false;
}
void appendLE32(std::string& bytes, std::int32_t value) {
    auto const bits = static_cast<std::uint32_t>(value);
    for (int shift = 0; shift <= 24; shift += 8) bytes.push_back(static_cast<char>(bits >> shift));
}
std::string namedLE(unsigned char type, std::string_view name) {
    std::string bytes(1, static_cast<char>(type));
    bytes.push_back(static_cast<char>(name.size()));
    bytes.push_back(static_cast<char>(name.size() >> 8));
    bytes += name;
    return bytes;
}
std::string indicesLE(int value) {
    auto bytes = namedLE(9, "block_indices");
    bytes.push_back('\x0b'); // List<IntArray>
    appendLE32(bytes, 1);    // one layer
    appendLE32(bytes, 1);    // one cell
    appendLE32(bytes, value);
    return bytes;
}
void testBedrockIndices() {
    using namespace lholo::structure::detail;
    auto const decoy = indicesLE(7);
    auto bytes = namedLE(10, "");
    bytes += namedLE(7, "metadata");
    appendLE32(bytes, static_cast<std::int32_t>(decoy.size()));
    bytes += decoy;
    auto const metadataPrefix = bytes;
    bytes += namedLE(10, "structure");
    bytes += indicesLE(1);
    bytes.append(2, '\0');
    auto const stripped = stripMcstructureBlockIndices(bytes);
    check(stripped && stripped->layers.size() == 1 && stripped->layers[0] == std::vector<std::int32_t>{1},
          "only root.structure.block_indices is extracted");
    check(stripped && stripped->parseBytes.starts_with(metadataPrefix), "metadata bytes remain unchanged");
    auto decoyOnly = metadataPrefix; decoyOnly.push_back('\0');
    check(!stripMcstructureBlockIndices(decoyOnly), "metadata needle is not a block index tag");
    auto rejectsLE = [](std::string_view input) {
        try { (void)BedrockNbtScanner{input}.scan(); }
        catch (std::runtime_error const&) { return true; }
        return false;
    };
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        check(rejectsLE(std::string_view{bytes}.substr(0, length)), "every truncated Bedrock prefix rejected");
    }
    check(rejectsLE(bytes + "garbage"), "Bedrock trailing payload rejected");
    auto duplicate = namedLE(10, "") + namedLE(3, "x");
    appendLE32(duplicate, 1);
    duplicate += namedLE(3, "x"); appendLE32(duplicate, 2); duplicate.push_back('\0');
    check(rejectsLE(duplicate), "Bedrock duplicate key rejected");
    auto list = namedLE(10, "") + namedLE(10, "structure") + namedLE(9, "block_indices");
    list.push_back('\x09'); appendLE32(list, 2);
    for (auto const value : {0, -1}) {
        list.push_back('\x03'); appendLE32(list, 1); appendLE32(list, value);
    }
    list.append(2, '\0');
    auto const legacy = stripMcstructureBlockIndices(list);
    check(legacy && legacy->layers == (std::vector<std::vector<std::int32_t>>{{0}, {-1}}),
          "legacy List<List<Int>> layers preserve indices");
    auto unknown = namedLE(10, "") + namedLE(9, "x");
    unknown.push_back('\x0d'); appendLE32(unknown, 0); unknown.push_back('\0');
    check(rejectsLE(unknown), "Bedrock unknown empty list type rejected");
    auto metadataList = namedLE(10, "") + namedLE(9, "x");
    metadataList.push_back('\1'); appendLE32(metadataList, 1000); metadataList.append(1001, '\0');
    bool budgetRejected{};
    try { (void)BedrockNbtScanner{metadataList, 64, 4096}.scan(); }
    catch (std::runtime_error const&) { budgetRejected = true; }
    check(budgetRejected, "Bedrock generic list decoded storage budget");
    std::uint64_t occupied{};
    std::vector<std::uint8_t> candidates(3), air{1, 0};
    check(collectRenderableCandidates(std::vector<std::int32_t>{-1, 0, 1}, 3, occupied, air, candidates)
              && occupied == 2 && candidates == (std::vector<std::uint8_t>{0, 0, 1}),
          "empty/air/body layers preserve occupancy and candidates");
    check(!collectRenderableCandidates(std::vector<std::int32_t>{-2, 0, 1}, 3, occupied, air, candidates),
          "only -1 denotes an empty palette cell");
    check(!collectRenderableCandidates(std::vector<std::int32_t>{2, 0, 1}, 3, occupied, air, candidates),
          "palette overflow is rejected, not silently dropped");
    check(!collectRenderableCandidates(std::vector<std::int32_t>{0}, 3, occupied, air, candidates),
          "layer volume mismatch rejected");
}
void truncatedAllocationCheck(unsigned char type, bool list) {
    std::string payload;
    if (list) payload.push_back('\1');
    appendI32(payload, 65536);
    auto const bytes = rootField(type, std::move(payload));
    bool rejected{}, allocatedBeforeValidation{};
    gRejectAllocationAbove = 65536;
    try { (void)lholo::structure::detail::JavaNbtReader{bytes}.readRoot(); }
    catch (std::bad_alloc const&) { allocatedBeforeValidation = true; }
    catch (std::runtime_error const&) { rejected = true; }
    gRejectAllocationAbove = 0;
    check(rejected && !allocatedBeforeValidation, "truncated payload rejected before large allocation");
}
} // namespace

// This harness refuses large allocations only during adversarial parsing.
// It proves the parser validates payload bounds before allocating, without
// allocating gigabytes to reproduce a file-controlled memory exhaustion.
void* operator new(std::size_t size) {
    if (gAllocationCallsBeforeFailure == 0) {
        gAllocationCallsBeforeFailure = -1;
        throw std::bad_alloc{};
    }
    if (gAllocationCallsBeforeFailure > 0) --gAllocationCallsBeforeFailure;
    if (gRejectAllocationAbove && size > gRejectAllocationAbove) throw std::bad_alloc{};
    if (auto* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc{};
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

int main() {
    testOverlayResourceGate();
    testInitializationRetention();
    testListenerRetirement();
    testExtraCellRegistration();
    testCorrectionFailureBoundary();
    testMaterialKeyCache();
    {
        std::string selected = "C:\\structure-test\\";
        for (int component = 0; component < 4; ++component) {
            for (int character = 0; character < 200; ++character) selected += "\xe3\x81\x82";
            selected += '\\';
        }
        selected += "model.mcstructure";
        std::array<char, lholo::ui::StructurePathUtf8Capacity> buffer{};
        std::snprintf(buffer.data(), buffer.size(), "%s", selected.c_str());
        check(std::string{buffer.data()} == selected, "dialog selected UTF8 path preserved without truncation");
    }
    lholo::tests::runNbtTypeChecks(check);
    lholo::tests::runLiquidBoundaryCacheChecks(check);
    {
        using namespace lholo::projection::detail;
        LiquidBoundaryMaskCache cache;
        std::array<LiquidSectionQuadCount, 2> const counts{{{10, 1}, {20, 1}}};
        NativeLiquidFaceCullMask const mask{true, {1, 1}, 1};
        check(cache.remember(counts, mask), "cache allocation failure fixture admitted");
        bool rejected{};
        gAllocationCallsBeforeFailure = 0;
        try { (void)cache.remember(counts, mask); }
        catch (std::bad_alloc const&) { rejected = true; }
        gAllocationCallsBeforeFailure = -1;
        check(rejected && cache.forOrder(counts)->removeQuads == mask.removeQuads,
            "cache allocation failure preserves preceding mask transaction");
    }
    testSectionBuildCommit();
    testPendingStructureOwner();
    testDirtySectionSelection();
    testClientViewState();
    testVoxelRayAxis();
    {
        using lholo::place::detail::quantizePlacementEye;
        check(quantizePlacementEye(1.375f) == 6 && quantizePlacementEye(-1.375f) == -6,
            "eye quantization retains quarter cell half away rounding");
        check(quantizePlacementEye(536870912.f) == INT64_C(2147483648), "eye quarter cells exceed Windows long maximum");
        check(quantizePlacementEye(-536871040.f) == INT64_C(-2147484160), "negative eye quarter cells exceed Windows long minimum");
        check(quantizePlacementEye(2147483008.f) == INT64_C(8589932032), "validated positive ray coordinate retains wide eye key");
        check(quantizePlacementEye(-2147483008.f) == INT64_C(-8589932032), "validated negative ray coordinate retains wide eye key");
    }
    {
        using Interest = lholo::projection::detail::WorldEventInterest;
        Interest bounds(std::vector<Interest::Box>{{{-16, -16, -16}, {0, 0, 0}}, {{16, 16, 16}, {17, 17, 17}}});
        check(bounds.contains({-16, -16, -16}) && bounds.contains({-1, -1, -1}), "negative region includes both endpoint cells");
        check(!bounds.contains({0, 0, 0}) && !bounds.contains({15, 15, 15}), "region gaps and exclusive boundary excluded");
        check(bounds.contains({16, 16, 16}) && !bounds.contains({17, 16, 16}), "single air-capable region cell retained");
        check(bounds.intersectsSubChunk({-1, -1, -1}) && bounds.intersectsSubChunk({1, 1, 1}), "negative and positive intersecting subchunks");
        check(!bounds.intersectsSubChunk({0, 0, 0}), "touching subchunk with no shared cell excluded");
        check(!bounds.intersectsSubChunk({INT32_MAX, INT32_MIN, INT32_MAX}), "far subchunk multiplication remains wide");
        check(!Interest{}.contains({0, 0, 0}) && !Interest{}.intersectsSubChunk({0, 0, 0}), "retired interest accepts no facts");
    }
    {
        struct Object {
            int& destructions;
            int value;
            ~Object() { ++destructions; }
        };
        int destructions{};
        std::unordered_map<int, std::shared_ptr<Object>> owners;
        auto const first = lholo::app::retainOwnedObject(owners, 1, std::make_shared<Object>(destructions, 42));
        check(first.second && first.first->value == 42 && destructions == 0, "first retained object is map-owned");
        auto const duplicate = lholo::app::retainOwnedObject(owners, 1, std::make_shared<Object>(destructions, 99));
        check(!duplicate.second && duplicate.first == first.first && duplicate.first->value == 42
                  && destructions == 1, "duplicate returns existing owner after candidate destruction");
        auto candidate = std::make_shared<Object>(destructions, 7);
        bool rejected{};
        gAllocationCallsBeforeFailure = 0;
        try { (void)lholo::app::retainOwnedObject(owners, 2, std::move(candidate)); }
        catch (std::bad_alloc const&) { rejected = true; }
        gAllocationCallsBeforeFailure = -1;
        check(rejected && owners.size() == 1 && owners.at(1).get() == first.first && destructions == 2,
              "retention failure leaves existing object alive and destroys candidate");
        owners.clear();
        check(destructions == 3, "each owned candidate destroyed once");
    }
    {
        using Queue = lholo::projection::detail::CoalescedEventQueue<int, std::uint64_t>;
        static_assert(!std::is_copy_constructible_v<Queue> && !std::is_move_constructible_v<Queue>);
        Queue events;
        auto merge = [](auto& previous, auto const& latest) noexcept { previous = (std::max)(previous, latest); };
        for (std::uint64_t time = 1; time <= 10000; ++time) events.push(1, time, merge);
        check(events.size() == 1, "repeated pending facts coalesce before consumption");
        events.push(2, 9, merge); events.push(1, 1, merge); events.push(3, 7, merge);
        check(events.take(0).empty() && events.size() == 3, "zero consumption preserves pending facts");
        check(events.take(2) == (std::vector<std::uint64_t>{10000, 9}), "FIFO order and latest destruction retained");
        check(events.take(10) == (std::vector<std::uint64_t>{7}) && events.size() == 0, "bounded drain consumes each distinct key once");
        events.push(1, 4, merge); events.clear();
        check(events.size() == 0 && events.take(1).empty(), "session clear retires queue and index together");
        Queue failed;
        failed.push(0, 0, merge); (void)failed.take(1); // Initialize index before injecting admission failure.
        bool rejected{};
        // Allow the FIFO node allocation, then fail index admission. This
        // exercises rollback independently of allocator node/bucket sizes.
        gAllocationCallsBeforeFailure = 1;
        try { failed.push(1, 1, merge); }
        catch (std::bad_alloc const&) { rejected = true; }
        gAllocationCallsBeforeFailure = -1;
        check(rejected && failed.size() == 0, "failed index allocation rolls back FIFO admission");
        failed.push(1, 2, merge);
        rejected = false;
        gAllocationCallsBeforeFailure = 0;
        try { (void)failed.take(1); } catch (std::bad_alloc const&) { rejected = true; }
        gAllocationCallsBeforeFailure = -1;
        check(rejected && failed.size() == 1, "failed drain allocation preserves pending fact");
        check(failed.take(1) == (std::vector<std::uint64_t>{2}), "queue remains reusable after failed admission");
    }
    testJavaTextEncoding();
    testLitematicRules();
    using namespace lholo::structure::detail;
    testBedrockIndices();
    truncatedAllocationCheck(11, false);
    truncatedAllocationCheck(12, false);
    truncatedAllocationCheck(9, true);
    std::string scalar; appendI32(scalar, -1234567);
    auto const valid = rootField(3, scalar);
    auto const parsed = JavaNbtReader{valid}.readRoot();
    check(javaValue<std::int32_t>(parsed, "x") && *javaValue<std::int32_t>(parsed, "x") == -1234567,
          "big endian signed scalar");
    std::string emptyList(1, '\0'); appendI32(emptyList, 0);
    check(!rejects(rootField(9, emptyList)), "TAG_End empty list accepted");
    emptyList[0] = 13;
    check(rejects(rootField(9, emptyList)), "unknown empty list type rejected");
    std::string negative; appendI32(negative, -1);
    check(rejects(rootField(11, negative)), "negative array length rejected");
    auto duplicate = valid;
    duplicate.pop_back();
    duplicate += valid.substr(3);
    check(rejects(duplicate), "duplicate compound key rejected");
    check(rejects(valid + "garbage"), "trailing payload rejected");
    std::string deep{"\x0a\0\0", 3};
    for (int level = 0; level < 130; ++level) deep.append("\x0a\0\1x", 4);
    deep.append(131, '\0');
    check(rejects(deep), "nested compound depth rejected");
    bool budgetRejected{};
    try { (void)JavaNbtReader{valid, 16}.readRoot(); }
    catch (std::runtime_error const&) { budgetRejected = true; }
    check(budgetRejected, "decoded memory budget applies to compound nodes");
    std::printf("LHoloNbtTests: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
