// LHolo logic tests: pure projection rules and progress publication.
// Run with: xmake r LHoloLogicTests

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <fstream>
#include <future>
#include <limits>
#include <span>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

#include "app/HookLifecycle.h"
#include "app/NativeCallbackBoundary.h"
#include "app/ScopeExit.h"
#include "app/InitializationTransaction.h"
#include "app/FutureResult.h"
#include "overlay/ImGuiFrameRecovery.h"
#include "block/BlockPlacementRules.h"
#include "ManualPlacementChecks.h"
#include "CompanionCallbackChecks.h"
#include "ComparisonStyleChecks.h"
#include "VerifierHighlightChecks.h"
#include "i18n/Message.h"
#include "i18n/Translator.h"
#include "input/ViewMoveBasis.h"
#include "place/PlacementState.h"
#include "projection/core/ProjectionLiquidCompatColor.h"
#include "projection/core/ProjectionLiquidFaceCull.h"
#include "projection/core/ProjectionLiquidUv.h"
#include "projection/core/LiquidReplayRules.h"
#include "projection/core/ProjectionRules.h"
#include "projection/core/ProjectionCoordinateBounds.h"
#include "render/RenderCameraPosition.h"
#include "projection/runtime/ProjectionProgress.h"
#include "projection/runtime/ProjectionActivationRequests.h"
#include "projection/mesh/WorkerTaskBoundary.h"
#include "projection/mesh/SingleTaskWorker.h"
#include "projection/mesh/SectionBlockSnapshot.h"
#include "projection/mesh/TransparentQuadSort.h"
#include "projection/runtime/MeshDiagnosticGate.h"
#include "projection/runtime/EpochFailure.h"
#include "settings/SettingsStore.h"
#include "structure/StructureSession.h"
#include "structure/LoadIntent.h"
#include "structure/capture/CaptureBounds.h"
#include "structure/capture/CaptureRequests.h"
#include "structure/StructureUiState.h"
#include "structure/java_to_bedrock/JavaBlockEntityToBedrock.h"
#include "ui/HotkeyFormat.h"
#include "HudControlChecks.h"

#include <Windows.h>
#include "SchematicChecks.h"
#include "io/AtomicOutput.h"

namespace {

int gChecks = 0;
int gFailures = 0;

#define LHOLO_CHECK(cond)                                     \
    do {                                                      \
        ++gChecks;                                            \
        if (!(cond)) {                                        \
            ++gFailures;                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n",          \
                __FILE__, __LINE__, #cond);                   \
        }                                                     \
    } while (false)

using namespace lholo::projection::detail;
using lholo::structure::LoadedStructure;

bool expectBlockPos(BlockPos const& pos, int x, int y, int z) {
    return pos.x == x && pos.y == y && pos.z == z;
}

void testTransparentQuadSort() {
    auto const keyA = transparentSortKey(glm::vec3{1.24f, -0.01f, 2.0f});
    auto const keyB = transparentSortKey(glm::vec3{1.25f, -0.01f, 2.0f});
    LHOLO_CHECK(keyA && (*keyA)[0] == 4 && (*keyA)[1] == -1 && (*keyA)[2] == 8);
    LHOLO_CHECK(keyB && (*keyB)[0] == 5);

    std::vector<glm::vec3> positions;
    for (float z : {1.0f, 5.0f, 3.0f}) {
        positions.push_back({0, 0, z}); positions.push_back({1, 0, z});
        positions.push_back({1, 1, z}); positions.push_back({0, 1, z});
    }
    auto const order = transparentQuadOrder(positions, glm::vec3{0, 0, 0});
    LHOLO_CHECK(order && order->size() == 3);
    LHOLO_CHECK(order && (*order)[0] == 1 && (*order)[1] == 2 && (*order)[2] == 0);
    LHOLO_CHECK(order && transparentQuadOrderChanged(*order));

    std::vector<int> values{0,1,2,3, 10,11,12,13, 20,21,22,23};
    LHOLO_CHECK(order && reorderQuadVertexField(values, *order));
    LHOLO_CHECK(values == std::vector<int>({10,11,12,13, 20,21,22,23, 0,1,2,3}));
    std::vector<glm::vec3> invalid(5);
    LHOLO_CHECK(!transparentQuadOrder(invalid, glm::vec3{0, 0, 0}));
}

void testLiquidReplayRules() {
    using Candidate = PraxisLiquidMaterialCandidate;
    // Exact Replay candidate B only needs its own native material. A missing
    // diagnostic/fallback material must not hide a successfully built liquid.
    LHOLO_CHECK(liquidReplayMaterialReady(Candidate::BlendBlock, false, true));
    LHOLO_CHECK(liquidReplayMaterialReady(Candidate::BlendBlock, true, true));
    LHOLO_CHECK(!liquidReplayMaterialReady(Candidate::BlendBlock, true, false));
    LHOLO_CHECK(!liquidReplayMaterialReady(Candidate::BlendBlock, false, false));
    LHOLO_CHECK(liquidReplayMaterialReady(Candidate::SignText, true, false));
    LHOLO_CHECK(!liquidReplayMaterialReady(Candidate::SignText, false, true));
    LHOLO_CHECK(retainedLiquidMaterial(true, true) == Candidate::SignText);
    LHOLO_CHECK(retainedLiquidMaterial(true, false) == Candidate::SignText);
    LHOLO_CHECK(retainedLiquidMaterial(false, true) == Candidate::BlendBlock);
    LHOLO_CHECK(!retainedLiquidMaterial(false, false));
    auto const intMaximum = static_cast<std::size_t>((std::numeric_limits<int>::max)());
    LHOLO_CHECK(replayVertexCountFits(intMaximum));
    LHOLO_CHECK(!replayVertexCountFits(intMaximum + 1));
    auto const combined = combineReplayCounts(8, 12, 16, 32);
    LHOLO_CHECK(combined && combined->vertices == 20 && combined->capacity == 48);
    auto const minimumCapacity = combineReplayCounts(8, 12, 0, 0);
    LHOLO_CHECK(minimumCapacity && minimumCapacity->capacity == 20);
    LHOLO_CHECK(combineReplayCounts(intMaximum - 4, 4, 0, 0).has_value());
    LHOLO_CHECK(!combineReplayCounts(intMaximum - 4, 8, 0, 0));
    LHOLO_CHECK(!combineReplayCounts((std::numeric_limits<std::size_t>::max)(), 4, 0, 0));
    LHOLO_CHECK(!combineReplayCounts(4, 4, (std::numeric_limits<std::uint32_t>::max)(), 1));
}

void testProjectionCoordinateBounds() {
    auto const maximum = (std::numeric_limits<int>::max)();
    auto const minimum = (std::numeric_limits<int>::min)();
    LHOLO_CHECK((checkedRelativeBlockCell({10, -10, 30}, {1, 2, 3}, {4, 5, 6})
        == std::array<int, 3>{5, -17, 21}));
    LHOLO_CHECK(!checkedRelativeBlockCell({maximum, 0, 0}, {minimum + 16, 0, 0}, {0, 0, 0}));
    LHOLO_CHECK(!checkedRelativeBlockCell({minimum, 0, 0}, {maximum - 16, 0, 0}, {0, 0, 0}));
    LHOLO_CHECK((checkedRelativeBlockCell({maximum, 0, 0}, {maximum - 16, 0, 0}, {1, 0, 0})
        == std::array<int, 3>{15, 0, 0}));
    auto const upperChunk = checkedSubChunkBlockBounds({maximum / 16, 0, 0});
    LHOLO_CHECK(upperChunk && upperChunk->min[0] == maximum - 15
        && upperChunk->max[0] == static_cast<std::int64_t>(maximum) + 1);
    auto const lowerChunk = checkedSubChunkBlockBounds({minimum / 16, 0, 0});
    LHOLO_CHECK(lowerChunk && lowerChunk->min[0] == minimum);
    LHOLO_CHECK(!checkedSubChunkBlockBounds({maximum / 16 + 1, 0, 0}));
    LHOLO_CHECK(!checkedSubChunkBlockBounds({minimum / 16 - 1, 0, 0}));
    auto const grid = makeSectionGrid((std::numeric_limits<int>::max)(), 17, 16);
    LHOLO_CHECK(grid.has_value());
    if (grid) {
        LHOLO_CHECK(grid->x == 134217728);
        LHOLO_CHECK(grid->y == 2 && grid->z == 1);
        LHOLO_CHECK(!grid->denseCount(1U << 20));
    }
    auto const small = makeSectionGrid(17, 32, 1);
    LHOLO_CHECK(small && small->denseCount(4) == 4);
    LHOLO_CHECK(small && !small->denseCount(3));
    LHOLO_CHECK(!makeSectionGrid(0, 1, 1));
    LHOLO_CHECK(!makeSectionGrid(-1, 1, 1));
    auto const huge = makeSectionGrid((std::numeric_limits<int>::max)(),
        (std::numeric_limits<int>::max)(), (std::numeric_limits<int>::max)());
    LHOLO_CHECK(huge && !huge->denseCount((std::numeric_limits<std::uint64_t>::max)()));
    auto const ordinary = projectionRangeBox({-1.25f, 15.5f, 0}, 2.5f);
    LHOLO_CHECK(ordinary && ordinary->min == (std::array<std::int64_t, 3>{-5, 12, -3}));
    LHOLO_CHECK(ordinary && ordinary->max == (std::array<std::int64_t, 3>{1, 18, 3}));
    LHOLO_CHECK(!projectionRangeBox({(std::numeric_limits<float>::max)(), 0, 0}, 1));
    LHOLO_CHECK(!projectionRangeBox({2147483648.0f, 0, 0}, 1));
    auto const edge = projectionRangeBox({-2147483648.0f, 0, 0}, 64);
    LHOLO_CHECK(edge && edge->min[0] == (std::numeric_limits<int>::min)());
    LHOLO_CHECK(edge && edge->max[0] == -2147483584LL);
    LHOLO_CHECK(!projectionRangeBox({0, 0, 0}, 65));
    LHOLO_CHECK(!projectionRangeBox({0, 0, 0}, -1));
    LHOLO_CHECK(!projectionRangeBox({0, std::numeric_limits<float>::quiet_NaN(), 0}, 1));
    LHOLO_CHECK(checkedProjectionOrigin({-20, 0, 30}, {10, -10, -20}, {16, 16, 16}, 0)
        == (std::array<int, 3>{-10, -10, 10}));
    LHOLO_CHECK(!checkedProjectionOrigin({(std::numeric_limits<int>::max)(), 0, 0}, {1, 0, 0}, {1, 1, 1}, 0));
    LHOLO_CHECK(!checkedProjectionOrigin({(std::numeric_limits<int>::min)(), 0, 0}, {-1, 0, 0}, {1, 1, 1}, 0));
    LHOLO_CHECK(!checkedProjectionOrigin({0, 0, 0}, {0, 0, 0}, {1, 0, 1}, 0));
    auto const nearEdge = (std::numeric_limits<int>::max)() - 100;
    LHOLO_CHECK(checkedProjectionOrigin({nearEdge, 0, 0}, {}, {1, 1, 100}, 0));
    LHOLO_CHECK(!checkedProjectionOrigin({nearEdge, 0, 0}, {}, {1, 1, 100}, 1));
    LHOLO_CHECK(checkedBlockCell({-0.5f, 1.9f, -2.1f}) == (std::array<int, 3>{-1, 1, -3}));
    LHOLO_CHECK(!checkedBlockCell({2147483648.0f, 0, 0}));
    LHOLO_CHECK(!checkedBlockCell({-2147483648.0f, 0, 0}, 1));
    LHOLO_CHECK(!checkedBlockCell({0, std::numeric_limits<float>::quiet_NaN(), 0}));
    LHOLO_CHECK(checkedVoxelRayOrigin({0, 0, 0}, {0, 0, -1}, 6).has_value());
    LHOLO_CHECK(!checkedVoxelRayOrigin({0, 0, 0}, {}, 6));
    LHOLO_CHECK(!checkedVoxelRayOrigin({0, 0, 0}, {0, 0, 1}, -1));
    LHOLO_CHECK(!checkedVoxelRayOrigin({0, 0, 0}, {0, 0, 1}, std::numeric_limits<float>::infinity()));
    LHOLO_CHECK(!checkedVoxelRayOrigin({0, 0, 0}, {0, std::numeric_limits<float>::quiet_NaN(), 1}, 6));
    LoadedStructure hugeStructure;
    hugeStructure.sizeX = (std::numeric_limits<int>::max)();
    hugeStructure.sizeY = 1; hugeStructure.sizeZ = (std::numeric_limits<int>::max)();
    LHOLO_CHECK(expectBlockPos(inverseTransformStructurePosition(BlockPos{-2, 0, 0}, hugeStructure, 0, 1),
        0, 0, (std::numeric_limits<int>::max)()));
    LHOLO_CHECK(expectBlockPos(transformStructurePosition(BlockPos{0, 0, -2}, hugeStructure, 0, 1),
        (std::numeric_limits<int>::max)(), 0, 0));
}

void testLoadIntent() {
    lholo::structure::detail::LoadIntent intent;
    auto const oldAsync = intent.begin();
    auto const newSync = intent.begin();
    int published{};
    LHOLO_CHECK(intent.applyIfCurrent(newSync, [&] { published = 2; }));
    LHOLO_CHECK(!intent.applyIfCurrent(oldAsync, [&] { published = 1; }));
    LHOLO_CHECK(published == 2);
    intent.invalidateAndApply([&] { published = 0; });
    LHOLO_CHECK(!intent.applyIfCurrent(newSync, [&] { published = 2; }));
    LHOLO_CHECK(published == 0);

    // Hold a commit while another thread withdraws the structure. The final
    // state must be cleared even when invalidation races an admitted commit.
    auto const ticket = intent.begin();
    std::atomic_bool entered{}, release{};
    std::thread publisher([&] {
        intent.applyIfCurrent(ticket, [&] {
            entered.store(true, std::memory_order_release);
            entered.notify_all();
            release.wait(false, std::memory_order_acquire);
            published = 3;
        });
    });
    entered.wait(false, std::memory_order_acquire);
    std::thread withdrawer([&] { intent.invalidateAndApply([&] { published = 0; }); });
    release.store(true, std::memory_order_release);
    release.notify_all();
    publisher.join();
    withdrawer.join();
    LHOLO_CHECK(published == 0);
    LHOLO_CHECK(!intent.current(ticket));
}

void testStructureTransformConcurrency() {
    auto& session = lholo::structure::detail::StructureSession::getInstance();
    session.resetTransform();
    session.setRotation(-1);
    session.setMirror(100);
    LHOLO_CHECK(session.transform().rotation == 3 && session.transform().mirror == 2);
    session.resetTransform();
    std::atomic_int producers{2};
    std::atomic_bool coherent{true};
    auto writer = [&] {
        for (int count = 0; count < 10000; ++count) session.adjustOffsets(1, 1, 1);
        producers.fetch_sub(1, std::memory_order_release);
    };
    std::thread first(writer), second(writer);
    std::thread reader([&] {
        while (producers.load(std::memory_order_acquire)) {
            auto const transform = session.transform();
            if (transform.offsetX != transform.offsetY || transform.offsetY != transform.offsetZ) coherent = false;
        }
    });
    first.join(); second.join(); reader.join();
    LHOLO_CHECK(coherent.load());
    auto const final = session.transform();
    LHOLO_CHECK(final.offsetX == 20000 && final.offsetY == 20000 && final.offsetZ == 20000);
    session.resetTransform();
}

void testRenderCameraRead() {
    using lholo::render::readRenderCameraPosition;
    LHOLO_CHECK(!readRenderCameraPosition(nullptr));
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    auto* allocation = static_cast<unsigned char*>(VirtualAlloc(nullptr, system.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    LHOLO_CHECK(allocation != nullptr);
    if (!allocation) return;
    struct ReleasePage {
        void* page;
        ~ReleasePage() { VirtualFree(page, 0, MEM_RELEASE); }
    } release{allocation};
    std::array<float, 3> expected{-1.5f, 20.25f, 1000.5f};
    std::memcpy(allocation + 40, expected.data(), sizeof(expected));
    auto const camera = readRenderCameraPosition(allocation);
    LHOLO_CHECK(camera && camera->x == expected[0] && camera->y == expected[1] && camera->z == expected[2]);
    LHOLO_CHECK(!readRenderCameraPosition(allocation + system.dwPageSize - 12));
    DWORD previous{};
    LHOLO_CHECK(VirtualProtect(allocation, system.dwPageSize, PAGE_READONLY, &previous));
    LHOLO_CHECK(readRenderCameraPosition(allocation).has_value());
    LHOLO_CHECK(VirtualProtect(allocation, system.dwPageSize, PAGE_EXECUTE, &previous));
    LHOLO_CHECK(!readRenderCameraPosition(allocation));
    LHOLO_CHECK(VirtualProtect(allocation, system.dwPageSize, PAGE_NOACCESS, &previous));
    LHOLO_CHECK(!readRenderCameraPosition(allocation));
    LHOLO_CHECK(VirtualProtect(allocation, system.dwPageSize, PAGE_READWRITE, &previous));
    expected[1] = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(allocation + 40, expected.data(), sizeof(expected));
    LHOLO_CHECK(!readRenderCameraPosition(allocation));
}

void testProjectionActivationRequests() {
    ProjectionActivationRequests requests;
    LHOLO_CHECK(!requests.consumeAnchor());
    requests.requestAnchor(1, 2, 3);
    requests.requestAnchor(-1, -2, -3);
    auto anchor = requests.consumeAnchor();
    LHOLO_CHECK(anchor && anchor->x == -1 && anchor->y == -2 && anchor->z == -3);
    LHOLO_CHECK(!requests.consumeAnchor());
    requests.requestAnchor(1, 2, 3);
    requests.cancelAnchorRequest();
    LHOLO_CHECK(!requests.consumeAnchor());
    requests.suspendForDimension(10, 1, {11, 12, 13});
    LHOLO_CHECK(requests.dimensionSuspended());
    LHOLO_CHECK(requests.prepareDimensionActivation(10, 2) == DimensionActivationStatus::Deferred);
    LHOLO_CHECK(!requests.consumeAnchor());
    LHOLO_CHECK(requests.prepareDimensionActivation(10, 1) == DimensionActivationStatus::Resuming);
    anchor = requests.consumeAnchor();
    LHOLO_CHECK(anchor && anchor->x == 11 && anchor->y == 12 && anchor->z == 13);
    LHOLO_CHECK(requests.dimensionSuspended());
    requests.requestAnchor(21, 22, 23);
    LHOLO_CHECK(requests.prepareDimensionActivation(20, 1) == DimensionActivationStatus::Ready);
    LHOLO_CHECK(!requests.dimensionSuspended());
    anchor = requests.consumeAnchor();
    LHOLO_CHECK(anchor && anchor->x == 21 && anchor->y == 22 && anchor->z == 23);

    std::atomic_int writers{2};
    std::atomic_bool coherent{true};
    auto writer = [&](int base) {
        for (int index = 1; index <= 10000; ++index) {
            auto const x = base + index;
            requests.requestAnchor(x, x * 2, -x);
        }
        writers.fetch_sub(1, std::memory_order_release);
    };
    std::thread first(writer, 0), second(writer, 10000);
    std::thread reader([&] {
        while (writers.load(std::memory_order_acquire)) {
            if (auto const value = requests.consumeAnchor(); value && (value->y != value->x * 2 || value->z != -value->x)) coherent = false;
        }
    });
    first.join(); second.join(); reader.join();
    LHOLO_CHECK(coherent.load());
    requests.cancelDimensionSuspension();
    requests.cancelAnchorRequest();
}

void testMeshDiagnosticGate() {
    MeshDiagnosticGate gate;
    LHOLO_CHECK(!gate.inspect(false, 0));
    int snapshots{};
    for (std::uint64_t frame = 0; frame < 600; ++frame) snapshots += gate.inspect(true, frame * 16);
    LHOLO_CHECK(snapshots == 10); // Previously 600 O(S) scans / 2400 log lines.
    LHOLO_CHECK(!gate.inspect(true, 9599));
    LHOLO_CHECK(gate.inspect(true, 10600));
    LHOLO_CHECK(!gate.inspect(false, 20000));
}

void testSectionBlockSnapshot() {
    std::vector<std::uint8_t> corrections(8193), actors(8193);
    for (std::size_t i = 0; i < corrections.size(); ++i) { corrections[i] = i % 5; actors[i] = i % 2; }
    SectionBlockSnapshot snapshot;
    snapshot.capture({8192, 4096, 0, 4095, 4096, 4097}, corrections, actors);
    LHOLO_CHECK(snapshot.size() == 5);
    // Includes both sides of a section boundary and a sparse distant cell.
    for (auto const i : {0U, 4095U, 4096U, 4097U, 8192U}) {
        auto const* value = snapshot.find(i);
        LHOLO_CHECK(value && value->correction == corrections[i] && value->actorRenderer == actors[i]);
    }
    LHOLO_CHECK(!snapshot.find(1) && !snapshot.find(8193));
    corrections[4096] = 4; actors[4096] = 1;
    LHOLO_CHECK(snapshot.find(4096)->correction == 1 && snapshot.find(4096)->actorRenderer == 0);
    snapshot.capture({1}, corrections, actors);
    LHOLO_CHECK(snapshot.size() == 1 && !snapshot.find(4096) && snapshot.find(1));
    bool rejected{};
    try { snapshot.capture({8193}, corrections, actors); } catch (std::out_of_range const&) { rejected = true; }
    LHOLO_CHECK(rejected);
}

void testSingleTaskWorker() {
    SingleTaskWorker worker;
    std::atomic_bool started{}, release{}, stopped{};
    std::atomic_int calls{};
    LHOLO_CHECK(worker.submit([&]() noexcept {
        started.store(true, std::memory_order_release);
        started.notify_all();
        release.wait(false, std::memory_order_acquire);
        ++calls;
    }));
    started.wait(false, std::memory_order_acquire);
    LHOLO_CHECK(!worker.submit([&]() noexcept { calls += 100; }));
    std::thread stopper([&] { worker.stop(); stopped = true; });
    LHOLO_CHECK(!stopped.load()); // Accepted work still holds native references.
    release.store(true, std::memory_order_release);
    release.notify_all();
    stopper.join();
    LHOLO_CHECK(stopped.load() && calls.load() == 1);
    LHOLO_CHECK(!worker.submit([&]() noexcept { ++calls; }));
    worker.stop();
    LHOLO_CHECK(calls.load() == 1);
    // Stop immediately after admission: pending work must also drain exactly once.
    SingleTaskWorker pending;
    LHOLO_CHECK(pending.submit([&]() noexcept { ++calls; }));
    pending.stop();
    LHOLO_CHECK(calls.load() == 2);
}

void testWorkerTaskBoundary() {
    std::atomic_bool busy{true};
    int published{}, failures{};
    auto failure = [&]() noexcept { ++failures; };
    runWorkerTaskBoundary(busy, [] { return 42; }, [&](int value) { published = value; }, failure);
    LHOLO_CHECK(!busy && published == 42 && failures == 0);
    busy = true;
    runWorkerTaskBoundary(busy, []() -> int { throw std::runtime_error("task failure"); },
        [&](int value) { published = value; }, failure);
    LHOLO_CHECK(!busy && published == 42 && failures == 1);
    busy = true;
    runWorkerTaskBoundary(busy, [] { return 43; },
        [](int) { throw std::bad_alloc{}; }, failure);
    LHOLO_CHECK(!busy && published == 42 && failures == 2);
}

void testNativeCallbackBoundary() {
    int calls{}, failures{};
    char const* reported{};
    auto failure = [&](char const* reason) noexcept { ++failures; reported = reason; };
    LHOLO_CHECK(lholo::app::invokeNativeCallback([&] { ++calls; }, failure));
    LHOLO_CHECK(calls == 1 && failures == 0);
    std::string reason;
    auto stringFailure = [&](char const* message) noexcept {
        ++failures;
        // Test storage is reserved before entering the noexcept reporter.
        reason.assign(message);
    };
    reason.reserve(128);
    LHOLO_CHECK(!lholo::app::invokeNativeCallback([] { throw std::runtime_error("UI failure"); }, stringFailure));
    LHOLO_CHECK(reason == "UI failure" && failures == 1);
    LHOLO_CHECK(!lholo::app::invokeNativeCallback([] { throw 7; }, failure));
    LHOLO_CHECK(std::string_view{reported} == "unknown C++ exception" && failures == 2);
    std::array<int, 3> order{};
    int count{};
    LHOLO_CHECK(!lholo::app::invokeNativeCallback([&] {
        lholo::app::ScopeExit outer([&]() noexcept { order[count++] = 2; });
        lholo::app::ScopeExit inner([&]() noexcept { order[count++] = 1; });
        throw std::bad_alloc{};
    }, [&](char const*) noexcept { order[count++] = 3; }));
    LHOLO_CHECK((order == std::array<int, 3>{1, 2, 3}));
}

void testWorldEventInterest() {
    using namespace lholo::projection::detail;
    lholo::structure::LoadedStructure loaded;
    loaded.sizeX = 7; loaded.sizeY = 2; loaded.sizeZ = 5;
    loaded.regions = {{0, 0, 0, 3, 2, 5}, {5, 0, 1, 2, 1, 2}};
    std::array<int, 3> const origin{-33, -17, 47};
    for (int mirror = 0; mirror < 3; ++mirror) for (int rotation = 0; rotation < 4; ++rotation) {
        auto const interest = makeProjectionWorldEventInterest(loaded, origin, mirror, rotation);
        for (int x = -1; x <= 7; ++x) for (int y = -1; y <= 2; ++y) for (int z = -1; z <= 5; ++z) {
            bool const covered = y >= 0 && y < 2 && ((x >= 0 && x < 3 && z >= 0 && z < 5)
                || (x >= 5 && x < 7 && y == 0 && z >= 1 && z < 3));
            auto const transformed = transformStructurePosition(BlockPos{x, y, z}, loaded, mirror, rotation);
            std::array<int, 3> const world{origin[0] + transformed.x, origin[1] + transformed.y, origin[2] + transformed.z};
            LHOLO_CHECK(interest.contains(world) == covered);
            if (covered) {
                auto floorChunk = [](int cell) { return cell >= 0 ? cell / 16 : -((-cell + 15) / 16); };
                LHOLO_CHECK(interest.intersectsSubChunk({floorChunk(world[0]), floorChunk(world[1]), floorChunk(world[2])}));
            }
        }
        LHOLO_CHECK(!interest.contains({1000, 1000, 1000}));
        LHOLO_CHECK(!interest.intersectsSubChunk({INT_MAX, INT_MIN, INT_MAX}));
    }
}

void testInitializationTransaction() {
    int rollbacks{}, reports{};
    auto rollback = [&]() noexcept { ++rollbacks; };
    auto failure = [&](char const*) noexcept { ++reports; };
    LHOLO_CHECK(lholo::app::initializeWithRollback([] { return true; }, rollback, failure));
    LHOLO_CHECK(rollbacks == 0 && reports == 0);
    LHOLO_CHECK(!lholo::app::initializeWithRollback([] { return false; }, rollback, failure));
    LHOLO_CHECK(rollbacks == 1 && reports == 0);
    LHOLO_CHECK(!lholo::app::initializeWithRollback([]() -> bool {
        throw std::runtime_error("hook install failed");
    }, rollback, failure));
    LHOLO_CHECK(rollbacks == 2 && reports == 1);
    LHOLO_CHECK(!lholo::app::initializeWithRollback([]() -> bool { throw 42; }, rollback, failure));
    LHOLO_CHECK(rollbacks == 3 && reports == 2);
    LHOLO_CHECK(lholo::app::initializeWithRollback([] { return true; }, rollback, failure));
    LHOLO_CHECK(rollbacks == 3 && reports == 2);
}

void testFutureResult() {
    std::promise<int> failed;
    std::optional<std::future<int>> inFlight{failed.get_future()};
    failed.set_exception(std::make_exception_ptr(std::runtime_error("worker allocation failed")));
    bool reported{};
    try { (void)lholo::app::takeFutureResult(inFlight); }
    catch (std::runtime_error const& error) { reported = std::string_view{error.what()} == "worker allocation failed"; }
    LHOLO_CHECK(reported);
    LHOLO_CHECK(!inFlight); // A consumed failing future must not poison later ticks.
    std::promise<int> next;
    inFlight.emplace(next.get_future());
    next.set_value(42);
    LHOLO_CHECK(lholo::app::takeFutureResult(inFlight) == 42 && !inFlight);
}

void testMaterialHudAvailabilityRevision() {
    using namespace lholo::structure::detail;
    auto& state = StructureUiState::getInstance();
    state.replaceMaterialHudSnapshot(std::vector<MaterialRequirement>(1), {10});
    auto old = state.materialHudSnapshot();
    LHOLO_CHECK(state.setMaterialHudAvailability(old.revision, {20}));
    LHOLO_CHECK(state.materialHudSnapshot().available == std::vector<int>{20});
    state.clearMaterialHud();
    LHOLO_CHECK(!state.setMaterialHudAvailability(old.revision, {99}));
    LHOLO_CHECK(state.materialHudSnapshot().available.empty());
    state.replaceMaterialHudSnapshot(std::vector<MaterialRequirement>(1), {30});
    old = state.materialHudSnapshot();
    state.replaceMaterialHudSnapshot(std::vector<MaterialRequirement>(1), {40});
    LHOLO_CHECK(!state.setMaterialHudAvailability(old.revision, {99}));
    LHOLO_CHECK(state.materialHudSnapshot().available == std::vector<int>{40});
    auto current = state.materialHudSnapshot();
    LHOLO_CHECK(!state.setMaterialHudAvailability(current.revision, {1, 2}));
    LHOLO_CHECK(state.materialHudSnapshot().available == std::vector<int>{40});
    state.clearMaterialHud();
}

void testMaterialHudPublicationRetirement() {
    using namespace lholo::structure::detail;
    auto& state = StructureUiState::getInstance();
    MaterialRequirement oldMaterial{};
    oldMaterial.displayName = "Old stone";
    oldMaterial.typeName = "minecraft:stone";
    oldMaterial.count = 5;
    MaterialRequirement newMaterial{};
    newMaterial.displayName = "New glass";
    newMaterial.typeName = "minecraft:glass";
    newMaterial.count = 7;
    for (int mode = 0; mode < 4; ++mode) {
        state.clearMaterials();
        auto const revision = state.materialHudRevision();
        std::promise<void> resume;
        auto go = resume.get_future();
        bool committed{};
        std::thread oldResult([&] {
            go.wait();
            committed = state.replaceMaterialHudSnapshot({oldMaterial}, {1}, revision);
        });
        switch (mode) {
        case 0: state.clearMaterialHud(); break;
        case 1: state.clearMaterials(); break;
        case 2: state.resetWorldSession(); break;
        case 3: state.replaceMaterialHudSnapshot({newMaterial}, {2}); break;
        }
        auto const current = state.materialHudView();
        resume.set_value();
        oldResult.join();
        LHOLO_CHECK(!committed);
        LHOLO_CHECK(state.materialHudView() == current);
        LHOLO_CHECK(state.replaceMaterialHudSnapshot({newMaterial}, {2}, state.materialHudRevision()));
        auto const fresh = state.materialHudView();
        LHOLO_CHECK(fresh && fresh->requirements[0].typeName == "minecraft:glass"
            && fresh->available == std::vector<int>{2});
    }
    state.clearMaterials();
}

void testMaterialHudImmutableView() {
    using namespace lholo::structure::detail;
    auto& state = StructureUiState::getInstance();
    state.clearMaterials();
    LHOLO_CHECK(!state.materialHudView());
    MaterialRequirement material{"Owned material name", {}, "minecraft:stone", "minecraft:stone", 100, 64};
    state.replaceMaterialHudSnapshot({material}, {10});
    auto original = state.materialHudView();
    LHOLO_CHECK(original && original == state.materialHudView() && original->ready);
    auto const* borrowedName = original->requirements[0].displayName.c_str();
    LHOLO_CHECK(state.setMaterialHudAvailability(original->revision, {20}));
    auto updated = state.materialHudView();
    LHOLO_CHECK(updated != original && updated->revision == original->revision);
    LHOLO_CHECK(original->available == std::vector<int>{10} && updated->available == std::vector<int>{20});
    auto copied = state.materialHudSnapshot();
    copied.requirements[0].displayName = "Independent value copy";
    LHOLO_CHECK(updated->requirements[0].displayName == "Owned material name");
    state.clearMaterials();
    LHOLO_CHECK(!state.materialHudView() && !state.setMaterialHudAvailability(original->revision, {30}));
    LHOLO_CHECK(std::string_view{borrowedName} == "Owned material name"
        && original->available == std::vector<int>{10});

    std::atomic_bool done{};
    std::atomic_bool consistent{true};
    std::atomic_size_t observations{};
    std::thread reader([&] {
        while (!done.load(std::memory_order_acquire)) {
            auto const view = state.materialHudView();
            if (!view) continue;
            if (!view->ready || view->requirements.size() != 1 || view->available.size() != 1
                || view->requirements[0].count != static_cast<std::uint64_t>(view->available[0])
                || view->requirements[0].displayName != std::to_string(view->available[0])) {
                consistent.store(false, std::memory_order_release);
            }
            observations.fetch_add(1, std::memory_order_release);
        }
    });
    for (int index = 1; index <= 1000; ++index) {
        material.displayName = std::to_string(index);
        material.count = static_cast<std::uint64_t>(index);
        state.replaceMaterialHudSnapshot({material}, {index});
        if (index == 1) {
            while (observations.load(std::memory_order_acquire) == 0) std::this_thread::yield();
        }
        if (index % 7 == 0) state.clearMaterialHud();
    }
    done.store(true, std::memory_order_release);
    reader.join();
    LHOLO_CHECK(observations.load() > 0 && consistent.load());
    LHOLO_CHECK(std::string_view{borrowedName} == "Owned material name");
    state.clearMaterials();
}

void testCaptureRequestsAndBounds() {
    using namespace lholo::structure::capture;
    using namespace lholo::structure::capture::detail;
    auto minInt = (std::numeric_limits<int>::min)();
    auto maxInt = (std::numeric_limits<int>::max)();
    LHOLO_CHECK((captureSize({3, 6, 9}, {-3, -6, -9}) == std::array<std::uint64_t, 3>{7, 13, 19}));
    LHOLO_CHECK(captureVolume({0, 0, 0}, {15, 15, 15}) == 4096);
    LHOLO_CHECK(captureBoundsSupported({-16, -64, -16}, {16, 100, 16}));
    LHOLO_CHECK(!captureBoundsSupported({minInt, 0, 0}, {minInt, 0, 0}));
    LHOLO_CHECK(!captureBoundsSupported({maxInt, 0, 0}, {maxInt, 0, 0}));
    LHOLO_CHECK(!captureVolume({minInt, minInt, minInt}, {maxInt, maxInt, maxInt}));
    LHOLO_CHECK(!captureBoundsSupported({minInt + 1, 0, 0}, {maxInt - 1, 0, 0}));
    LHOLO_CHECK(captureBoundsSupported({0, 0, 0}, {4095, 255, 62}));
    LHOLO_CHECK(!captureBoundsSupported({0, 0, 0}, {4095, 255, 63}));
    CaptureRequests requests;
    requests.requestPoint(PointSlot::First);
    requests.requestPoint(PointSlot::First);
    requests.requestPoint(PointSlot::Second);
    LHOLO_CHECK(requests.takePoints() == 3 && requests.takePoints() == 0);
    Draft first; first.first = Point{1, 2, 3}; first.second = Point{4, 5, 6};
    auto second = first; second.includeEntities = true;
    requests.requestExport(first, "first.mcstructure");
    requests.requestExport(second, "second.mcstructure");
    auto request = requests.takeExport();
    LHOLO_CHECK(request && request->draft == second && request->output == "second.mcstructure");
    LHOLO_CHECK(!requests.takeExport());
    requests.requestExport(first, "old-world.mcstructure");
    requests.requestPoint(PointSlot::First);
    auto const previous = requests.session();
    requests.resetSession();
    LHOLO_CHECK(requests.session() != previous && !requests.takeExport() && requests.takePoints() == 0);
    requests.requestExport(second, "new-world.mcstructure");
    request = requests.takeExport();
    LHOLO_CHECK(request && request->session == requests.session());
}

void testEpochFailure() {
    EpochFailure failure;
    auto const old = failure.token();
    LHOLO_CHECK(!failure.failed());
    failure.mark(old);
    LHOLO_CHECK(failure.failed());
    failure.advance();
    LHOLO_CHECK(!failure.failed());
    failure.mark(old);
    LHOLO_CHECK(!failure.failed());
    auto const current = failure.token();
    failure.mark(current);
    failure.mark(old);
    LHOLO_CHECK(failure.failed());
    failure.advance();
    auto const concurrent = failure.token();
    std::thread stale([&] { for (int i = 0; i < 10000; ++i) failure.mark(old); });
    std::thread latest([&] { for (int i = 0; i < 10000; ++i) failure.mark(concurrent); });
    stale.join(); latest.join();
    LHOLO_CHECK(failure.failed());
    failure.advance();
    LHOLO_CHECK(!failure.failed());
}

void testImGuiFrameRecovery() {
    auto* context = ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800.0f, 600.0f};
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels{};
    int width{}, height{};
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    int failures{};
    for (int cycle = 0; cycle < 10; ++cycle) {
        ImGui::NewFrame();
        LHOLO_CHECK(!lholo::app::invokeNativeCallback([&] {
            lholo::overlay::detail::ImGuiFrameRecovery recovery;
            ImGui::Begin("Aborted UI");
            ImGui::PushID(cycle);
            ImGui::PushStyleColor(ImGuiCol_Text, {1, 0, 0, 1});
            ImGui::BeginGroup();
            ImGui::BeginChild("child", {200, 200});
            if (ImGui::BeginTable("table", 2)) {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted("before failure");
            }
            throw std::runtime_error("draw failed");
        }, [&](char const*) noexcept { ++failures; }));
        LHOLO_CHECK(!context->WithinFrameScope && context->CurrentWindowStack.Size == 0);
        LHOLO_CHECK(context->ColorStack.Size == 0 && context->GroupStack.Size == 0);
        LHOLO_CHECK(io.ConfigErrorRecoveryEnableAssert && io.ConfigErrorRecoveryEnableDebugLog
            && io.ConfigErrorRecoveryEnableTooltip);
        // The next real frame must be usable after each aborted draw.
        ImGui::NewFrame();
        {
            lholo::overlay::detail::ImGuiFrameRecovery recovery;
            ImGui::Begin("Next UI");
            ImGui::TextUnformatted("recovered");
            ImGui::End();
            ImGui::Render();
        }
        LHOLO_CHECK(ImGui::GetDrawData() && ImGui::GetDrawData()->Valid);
    }
    LHOLO_CHECK(failures == 10);
    ImGui::DestroyContext(context);
}

struct TestUv {
    float x{};
    float y{};
};

struct TestPosition {
    float x{};
    float y{};
    float z{};
};

bool nearlyEqual(float lhs, float rhs) {
    return std::abs(lhs - rhs) < 0.00001f;
}

void testNativeLiquidUvRemap() {
    NativeLiquidAtlasRect const atlas{0.25f, 0.5f, 0.5f, 0.75f};
    auto checkUv = [](TestUv const& uv, float u, float v) {
        LHOLO_CHECK(nearlyEqual(uv.x, u));
        LHOLO_CHECK(nearlyEqual(uv.y, v));
    };

    std::array<TestUv, 4> normal{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
    LHOLO_CHECK(remapNativeLiquidUvToAtlas(std::span{normal}, atlas));
    checkUv(normal[0], 0.25f, 0.5f);
    checkUv(normal[1], 0.5f, 0.5f);
    checkUv(normal[2], 0.5f, 0.75f);
    checkUv(normal[3], 0.25f, 0.75f);

    std::array<TestUv, 4> reversedU{{{1, 0}, {0, 0}, {0, 1}, {1, 1}}};
    LHOLO_CHECK(remapNativeLiquidUvToAtlas(std::span{reversedU}, atlas));
    checkUv(reversedU[0], 0.5f, 0.5f);
    checkUv(reversedU[1], 0.25f, 0.5f);
    checkUv(reversedU[2], 0.25f, 0.75f);
    checkUv(reversedU[3], 0.5f, 0.75f);

    std::array<TestUv, 4> reversedV{{{0, 1}, {1, 1}, {1, 0}, {0, 0}}};
    LHOLO_CHECK(remapNativeLiquidUvToAtlas(std::span{reversedV}, atlas));
    checkUv(reversedV[0], 0.25f, 0.75f);
    checkUv(reversedV[1], 0.5f, 0.75f);
    checkUv(reversedV[2], 0.5f, 0.5f);
    checkUv(reversedV[3], 0.25f, 0.5f);

    std::array<TestUv, 4> arbitrary{{{-2, 10}, {6, 10}, {6, 14}, {-2, 14}}};
    LHOLO_CHECK(remapNativeLiquidUvToAtlas(std::span{arbitrary}, atlas));
    checkUv(arbitrary[0], 0.25f, 0.5f);
    checkUv(arbitrary[2], 0.5f, 0.75f);

    std::array<TestUv, 4> degenerate{{{4, 4}, {4, 4}, {4, 4}, {4, 4}}};
    LHOLO_CHECK(remapNativeLiquidUvToAtlas(std::span{degenerate}, atlas));
    checkUv(degenerate[0], 0.25f, 0.5f);
    checkUv(degenerate[1], 0.5f, 0.5f);
    checkUv(degenerate[2], 0.5f, 0.75f);
    checkUv(degenerate[3], 0.25f, 0.75f);

    auto const extreme = (std::numeric_limits<float>::max)();
    std::array<TestUv, 4> wideSource{{{-extreme, -extreme}, {extreme, -extreme},
        {extreme, extreme}, {-extreme, extreme}}};
    LHOLO_CHECK(remapNativeLiquidUvToAtlas(std::span{wideSource}, atlas));
    checkUv(wideSource[0], 0.25f, 0.5f);
    checkUv(wideSource[1], 0.5f, 0.5f);
    checkUv(wideSource[2], 0.5f, 0.75f);
    checkUv(wideSource[3], 0.25f, 0.75f);
    std::array<TestUv, 4> wideDestination{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
    LHOLO_CHECK(remapNativeLiquidUvToAtlas(std::span{wideDestination},
        NativeLiquidAtlasRect{-extreme, -extreme, extreme, extreme}));
    checkUv(wideDestination[0], -extreme, -extreme);
    checkUv(wideDestination[1], extreme, -extreme);
    checkUv(wideDestination[2], extreme, extreme);
    checkUv(wideDestination[3], -extreme, extreme);

    auto invalidAtlasUvs = normal;
    LHOLO_CHECK(!remapNativeLiquidUvToAtlas(
        std::span{invalidAtlasUvs}, NativeLiquidAtlasRect{0.5f, 0.5f, 0.5f, 0.75f}
    ));
    auto nonFinite = normal;
    nonFinite[2].x = std::numeric_limits<float>::infinity();
    LHOLO_CHECK(!remapNativeLiquidUvToAtlas(std::span{nonFinite}, atlas));
    std::array<TestUv, 3> incompleteQuad{{{0, 0}, {1, 0}, {1, 1}}};
    LHOLO_CHECK(!remapNativeLiquidUvToAtlas(std::span{incompleteQuad}, atlas));
}

void testPraxisCompatLiquidColor() {
    auto const expect = [](std::uint32_t source, PraxisCompatRgba8 expected) {
        auto const derived = applyPraxisCompatMissingAbgr(source);
        LHOLO_CHECK(unpackAbgr(derived) == expected);
        LHOLO_CHECK(unpackAbgr(derived).alpha == 0xFFU);
    };

    // ExistingCurrent first normalizes native RGB by max intensity, then mixes
    // the Missing tint. White, gray, black, and the <=1/255 threshold all
    // intentionally converge to the same opaque candidate.
    expect(packAbgr({255, 255, 255, 17}), {197, 234, 255, 255});
    expect(packAbgr({128, 128, 128, 64}), {197, 234, 255, 255});
    expect(packAbgr({0, 0, 0, 0}), {197, 234, 255, 255});
    expect(packAbgr({1, 1, 1, 1}), {197, 234, 255, 255});
    expect(packAbgr({255, 0, 0, 128}), {197, 111, 133, 255});
    expect(packAbgr({0, 255, 0, 128}), {74, 234, 133, 255});
    expect(packAbgr({0, 0, 255, 128}), {74, 111, 255, 255});
    expect(packAbgr({2, 1, 0, 9}), {197, 173, 133, 255});

    auto const nativeWhite = packAbgr({255, 255, 255, 255});
    auto const waterSeed = selectPraxisCompatLiquidColorSeed(nativeWhite, true);
    LHOLO_CHECK(waterSeed.waterSeedApplied);
    LHOLO_CHECK(unpackAbgr(waterSeed.packed) == PraxisWaterColorSeed);
    LHOLO_CHECK(
        unpackAbgr(applyPraxisCompatMissingAbgr(waterSeed.packed))
        == (PraxisCompatRgba8{108, 175, 255, 255})
    );
    auto const waterFinal = applyPraxisCompatLiquidAlpha(
        applyPraxisCompatMissingAbgr(waterSeed.packed),
        true
    );
    LHOLO_CHECK(
        unpackAbgr(waterFinal) == (PraxisCompatRgba8{108, 175, 255, 160})
    );

    // The one-byte white tolerance accepts native rounding noise. Water that
    // already carries meaningful RGB and every lava vertex stay canonical.
    auto const nearWhite = selectPraxisCompatLiquidColorSeed(
        packAbgr({254, 255, 254, 17}),
        true
    );
    LHOLO_CHECK(nearWhite.waterSeedApplied);
    auto const tintedWater = packAbgr({253, 255, 255, 99});
    auto const tintedResult = selectPraxisCompatLiquidColorSeed(tintedWater, true);
    LHOLO_CHECK(!tintedResult.waterSeedApplied);
    LHOLO_CHECK(tintedResult.packed == tintedWater);
    auto const lavaResult = selectPraxisCompatLiquidColorSeed(nativeWhite, false);
    LHOLO_CHECK(!lavaResult.waterSeedApplied);
    LHOLO_CHECK(lavaResult.packed == nativeWhite);
    auto const lavaDerived = applyPraxisCompatMissingAbgr(lavaResult.packed);
    LHOLO_CHECK(applyPraxisCompatLiquidAlpha(lavaDerived, false) == lavaDerived);
}

void testNativeLiquidInternalFaceCull() {
    using Quad = std::array<TestPosition, 4>;
    auto const append = [](std::vector<TestPosition>& positions, Quad const& quad) {
        positions.insert(positions.end(), quad.begin(), quad.end());
    };
    auto const positiveX = Quad{{
        {1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}
    }};
    auto const negativeX = Quad{{
        {1, 0, 0}, {1, 0, 1}, {1, 1, 1}, {1, 1, 0}
    }};

    // 1. A unique pair on the same plane with opposite winding is removed.
    std::vector<TestPosition> opposite;
    append(opposite, positiveX);
    append(opposite, negativeX);
    auto result = buildNativeLiquidInternalFaceCullMask(
        std::span<TestPosition const>{opposite}
    );
    LHOLO_CHECK(result.valid);
    LHOLO_CHECK(result.facePairs == 1U);
    LHOLO_CHECK(result.removedVertices() == 8U);
    LHOLO_CHECK(result.removeQuads[0] == 1U && result.removeQuads[1] == 1U);

    std::array<std::uint8_t, 8> sameLiquid{};
    result = buildNativeLiquidInternalFaceCullMask(std::span<TestPosition const>{opposite},
        NativeLiquidFullFaceTolerance, std::span<std::uint8_t const>{sameLiquid});
    LHOLO_CHECK(result.valid && result.facePairs == 1);
    std::array<std::uint8_t, 8> waterLava{0, 0, 0, 0, 1, 1, 1, 1};
    result = buildNativeLiquidInternalFaceCullMask(std::span<TestPosition const>{opposite},
        NativeLiquidFullFaceTolerance, std::span<std::uint8_t const>{waterLava});
    LHOLO_CHECK(result.valid && result.removedVertices() == 0);
    sameLiquid[3] = 1; // Ambiguous mixed-kind quad retains its native geometry.
    result = buildNativeLiquidInternalFaceCullMask(std::span<TestPosition const>{opposite},
        NativeLiquidFullFaceTolerance, std::span<std::uint8_t const>{sameLiquid});
    LHOLO_CHECK(result.valid && result.removedVertices() == 0);
    result = buildNativeLiquidInternalFaceCullMask(std::span<TestPosition const>{opposite},
        NativeLiquidFullFaceTolerance, std::span<std::uint8_t const>{waterLava}.first(7));
    LHOLO_CHECK(!result.valid);
    auto incomplete = positiveX;
    incomplete[3] = incomplete[2]; // Bounds still cover a unit face, but one corner is absent.
    std::vector<TestPosition> incompletePair;
    append(incompletePair, incomplete);
    std::swap(incomplete[1], incomplete[2]);
    append(incompletePair, incomplete);
    result = buildNativeLiquidInternalFaceCullMask(std::span<TestPosition const>{incompletePair});
    LHOLO_CHECK(result.valid && result.removedVertices() == 0);
    auto crossed = positiveX;
    std::swap(crossed[2], crossed[3]);
    std::vector<TestPosition> crossedPair;
    append(crossedPair, crossed);
    std::swap(crossed[1], crossed[2]);
    append(crossedPair, crossed);
    result = buildNativeLiquidInternalFaceCullMask(std::span<TestPosition const>{crossedPair});
    LHOLO_CHECK(result.valid && result.removedVertices() == 0);
    auto farPositive = positiveX, farNegative = negativeX;
    for (auto& point : farPositive) point.x = 1.0e20f;
    for (auto& point : farNegative) point.x = 2.0e20f;
    std::vector<TestPosition> farPair;
    append(farPair, farPositive); append(farPair, farNegative);
    result = buildNativeLiquidInternalFaceCullMask(std::span<TestPosition const>{farPair});
    LHOLO_CHECK(result.valid && result.removedVertices() == 0);

    // 2. Same-facing duplicates may be intentional overlays and remain.
    std::vector<TestPosition> sameFacing;
    append(sameFacing, positiveX);
    append(sameFacing, positiveX);
    result = buildNativeLiquidInternalFaceCullMask(std::span<TestPosition const>{sameFacing});
    LHOLO_CHECK(result.valid && result.facePairs == 0U);

    // 3. A single exposed face remains. Add a non-candidate so the aggregate
    // still satisfies the minimum two-quad contract.
    auto const partialX = Quad{{
        {1, 0, 0}, {1, 0.5F, 0}, {1, 0.5F, 1}, {1, 0, 1}
    }};
    std::vector<TestPosition> exposed;
    append(exposed, positiveX);
    append(exposed, partialX);
    result = buildNativeLiquidInternalFaceCullMask(std::span<TestPosition const>{exposed});
    LHOLO_CHECK(result.valid && result.facePairs == 0U);

    // 4. Partial-height liquid sides are never unit full-face candidates.
    std::vector<TestPosition> partial;
    append(partial, partialX);
    auto reversedPartial = partialX;
    std::reverse(reversedPartial.begin(), reversedPartial.end());
    append(partial, reversedPartial);
    result = buildNativeLiquidInternalFaceCullMask(std::span<TestPosition const>{partial});
    LHOLO_CHECK(result.valid && result.facePairs == 0U);

    // 5. Sloped liquid geometry is not axis-aligned and remains.
    auto const sloped = Quad{{
        {1, 0, 0}, {1.1F, 1, 0}, {1.1F, 1, 1}, {1, 0, 1}
    }};
    std::vector<TestPosition> slopes;
    append(slopes, sloped);
    auto reversedSlope = sloped;
    std::reverse(reversedSlope.begin(), reversedSlope.end());
    append(slopes, reversedSlope);
    result = buildNativeLiquidInternalFaceCullMask(std::span<TestPosition const>{slopes});
    LHOLO_CHECK(result.valid && result.facePairs == 0U);

    // 6. Matching unit faces emitted by adjacent cells are removed on another
    // axis and at a non-origin block boundary.
    auto const positiveZ = Quad{{
        {3, 4, 7}, {4, 4, 7}, {4, 5, 7}, {3, 5, 7}
    }};
    auto const negativeZ = Quad{{
        {3, 4, 7}, {3, 5, 7}, {4, 5, 7}, {4, 4, 7}
    }};
    std::vector<TestPosition> adjacent;
    append(adjacent, positiveZ);
    append(adjacent, negativeZ);
    result = buildNativeLiquidInternalFaceCullMask(std::span<TestPosition const>{adjacent});
    LHOLO_CHECK(result.valid && result.facePairs == 1U);

    // The same typed matcher operates after aggregate assembly, so a pair on
    // the canonical 16-block section boundary is eligible as one global pair.
    auto const sectionBoundaryPositive = Quad{{
        {16, 2, 3}, {16, 3, 3}, {16, 3, 4}, {16, 2, 4}
    }};
    auto const sectionBoundaryNegative = Quad{{
        {16, 2, 3}, {16, 2, 4}, {16, 3, 4}, {16, 3, 3}
    }};
    std::vector<TestPosition> sectionBoundary;
    append(sectionBoundary, sectionBoundaryPositive);
    append(sectionBoundary, sectionBoundaryNegative);
    result = buildNativeLiquidInternalFaceCullMask(
        std::span<TestPosition const>{sectionBoundary}
    );
    LHOLO_CHECK(result.valid && result.facePairs == 1U);

    // 7. Normal tessellation noise inside the Praxis tolerance still pairs.
    auto withinPositive = positiveX;
    auto withinNegative = negativeX;
    for (auto& vertex : withinPositive) {
        vertex.x += 0.001F;
        vertex.y -= 0.001F;
        vertex.z += 0.001F;
    }
    for (auto& vertex : withinNegative) {
        vertex.x += 0.001F;
        vertex.y -= 0.001F;
        vertex.z += 0.001F;
    }
    std::vector<TestPosition> withinTolerance;
    append(withinTolerance, withinPositive);
    append(withinTolerance, withinNegative);
    result = buildNativeLiquidInternalFaceCullMask(
        std::span<TestPosition const>{withinTolerance}
    );
    LHOLO_CHECK(result.valid && result.facePairs == 1U);

    // 8. A plane beyond tolerance from an integer boundary is retained.
    auto outsidePositive = positiveX;
    auto outsideNegative = negativeX;
    for (auto& vertex : outsidePositive) vertex.x += 0.004F;
    for (auto& vertex : outsideNegative) vertex.x += 0.004F;
    std::vector<TestPosition> outsideTolerance;
    append(outsideTolerance, outsidePositive);
    append(outsideTolerance, outsideNegative);
    result = buildNativeLiquidInternalFaceCullMask(
        std::span<TestPosition const>{outsideTolerance}
    );
    LHOLO_CHECK(result.valid && result.facePairs == 0U);

    // 9. A malformed non-quad stream fails closed.
    std::array<TestPosition, 7> malformed{};
    result = buildNativeLiquidInternalFaceCullMask(std::span<TestPosition const>{malformed});
    LHOLO_CHECK(!result.valid && result.removeQuads.empty());

    // 10. Every enabled typed stream must match the position vertex count.
    std::array<std::size_t, 5> matchingFields{8U, 8U, 0U, 8U, 0U};
    std::array<std::size_t, 5> mismatchedFields{8U, 8U, 7U, 8U, 0U};
    LHOLO_CHECK(nativeLiquidPerVertexFieldCountsMatch(8U, matchingFields));
    LHOLO_CHECK(!nativeLiquidPerVertexFieldCountsMatch(8U, mismatchedFields));
}

void testLayoutRules() {
    LHOLO_CHECK(isVanillaSaplingType("minecraft:oak_sapling"));
    LHOLO_CHECK(isVanillaSaplingType("minecraft:dark_oak_sapling"));
    LHOLO_CHECK(isVanillaSaplingType("minecraft:bamboo_sapling"));
    LHOLO_CHECK(!isVanillaSaplingType("minecraft:oak_log"));
    LHOLO_CHECK(!isVanillaSaplingType("minecraft:flower_pot"));
    LHOLO_CHECK(!isVanillaSaplingType("example:oak_sapling"));

    LHOLO_CHECK(getProjectionMirror(0) == Mirror::None);
    LHOLO_CHECK(getProjectionMirror(1) == Mirror::Z);
    LHOLO_CHECK(getProjectionMirror(2) == Mirror::X);
    LHOLO_CHECK(getProjectionMirror(9) == Mirror::None);

    LHOLO_CHECK(getProjectionRotation(0) == Rotation::None);
    LHOLO_CHECK(getProjectionRotation(1) == Rotation::Clockwise90);
    LHOLO_CHECK(getProjectionRotation(2) == Rotation::Clockwise180);
    LHOLO_CHECK(getProjectionRotation(3) == Rotation::CounterClockwise90);
    LHOLO_CHECK(getProjectionRotation(4) == Rotation::None);
    LHOLO_CHECK(getProjectionRotation(5) == Rotation::Clockwise90);
    LHOLO_CHECK(getProjectionRotation(-1) == Rotation::CounterClockwise90);

    LoadedStructure loaded;
    loaded.sizeX = 4;
    loaded.sizeY = 3;
    loaded.sizeZ = 5;
    LoadedStructure::RenderBlock const entry{1, 2, 3, nullptr, nullptr, nullptr};

    LHOLO_CHECK(expectBlockPos(transformStructurePosition(entry, loaded, 0, 0), 1, 2, 3));
    LHOLO_CHECK(expectBlockPos(transformStructurePosition(entry, loaded, 1, 0), 2, 2, 3));
    LHOLO_CHECK(expectBlockPos(transformStructurePosition(entry, loaded, 2, 0), 1, 2, 1));
    LHOLO_CHECK(expectBlockPos(transformStructurePosition(entry, loaded, 0, 1), 1, 2, 1));
    LHOLO_CHECK(expectBlockPos(transformStructurePosition(entry, loaded, 0, 2), 2, 2, 1));
    LHOLO_CHECK(expectBlockPos(transformStructurePosition(entry, loaded, 0, 3), 3, 2, 2));
    LHOLO_CHECK(expectBlockPos(transformStructurePosition(entry, loaded, 1, 1), 1, 2, 2));

    for (int mirror = 0; mirror <= 2; ++mirror) {
        for (int rotation = 0; rotation < 4; ++rotation) {
            for (int x = 0; x < loaded.sizeX; ++x) {
                for (int z = 0; z < loaded.sizeZ; ++z) {
                    BlockPos const local{x, 1, z};
                    auto const transformed = transformStructurePosition(
                        local, loaded, mirror, rotation
                    );
                    auto const restored = inverseTransformStructurePosition(
                        transformed, loaded, mirror, rotation
                    );
                    LHOLO_CHECK(expectBlockPos(restored, x, 1, z));
                }
            }
        }
    }

    loaded.regions = {
        {0, 0, 0, 2, 3, 2},
        {3, 0, 3, 1, 1, 2},
    };
    LHOLO_CHECK(isStructureCellCovered(loaded, BlockPos{1, 2, 1}));
    LHOLO_CHECK(isStructureCellCovered(loaded, BlockPos{3, 0, 4}));
    LHOLO_CHECK(!isStructureCellCovered(loaded, BlockPos{2, 0, 2}));
    LHOLO_CHECK(!isStructureCellCovered(loaded, BlockPos{4, 0, 4}));

    using lholo::structure::LayerAxis;
    using lholo::structure::LayerDisplayMode;
    LHOLO_CHECK(lholo::structure::layerAxisFromInt(-1) == LayerAxis::Y);
    LHOLO_CHECK(lholo::structure::layerAxisFromInt(99) == LayerAxis::Material);
    LHOLO_CHECK(lholo::structure::layerDisplayModeFromInt(-1) == LayerDisplayMode::All);
    LHOLO_CHECK(lholo::structure::layerDisplayModeFromInt(99) == LayerDisplayMode::FromCurrent);
    LHOLO_CHECK(lholo::structure::toInt(LayerAxis::Material) == 2);
    LHOLO_CHECK(lholo::structure::toInt(LayerDisplayMode::FromCurrent) == 3);
    LHOLO_CHECK(isLayerVisible(3, LayerDisplayMode::All, 0));
    LHOLO_CHECK(!isLayerVisible(3, LayerDisplayMode::Single, 2));
    LHOLO_CHECK(isLayerVisible(2, LayerDisplayMode::Single, 2));
    LHOLO_CHECK(isLayerVisible(2, LayerDisplayMode::UpToCurrent, 3));
    LHOLO_CHECK(!isLayerVisible(4, LayerDisplayMode::UpToCurrent, 3));
    LHOLO_CHECK(isLayerVisible(4, LayerDisplayMode::FromCurrent, 3));
    LHOLO_CHECK(!isLayerVisible(2, LayerDisplayMode::FromCurrent, 3));
    LHOLO_CHECK(isLayerVisible(0, LayerDisplayMode::All, 0, 2, -1, LayerAxis::Material));
    LHOLO_CHECK(isLayerVisible(0, LayerDisplayMode::Single, 2, 2, -1, LayerAxis::Material));
    LHOLO_CHECK(!isLayerVisible(0, LayerDisplayMode::Single, 1, 2, -1, LayerAxis::Material));
    LHOLO_CHECK(isLayerVisible(0, LayerDisplayMode::UpToCurrent, 2, 1, 3, LayerAxis::Material));
    LHOLO_CHECK(!isLayerVisible(0, LayerDisplayMode::UpToCurrent, 2, 3, 4, LayerAxis::Material));
    LHOLO_CHECK(isLayerVisible(0, LayerDisplayMode::FromCurrent, 3, -1, 3, LayerAxis::Material));
    LHOLO_CHECK(!isLayerVisible(0, LayerDisplayMode::FromCurrent, 2, 1, 1, LayerAxis::Material));
    LHOLO_CHECK(isLayerVisible(0, LayerDisplayMode::All, 0, -1, -1, LayerAxis::Material));
    LHOLO_CHECK(!isLayerVisible(0, LayerDisplayMode::Single, 0, -1, -1, LayerAxis::Material));

    LHOLO_CHECK(renderBucketFor(BlockRenderLayer::RenderlayerOpaque) == RenderBucket::Opaque);
    LHOLO_CHECK(renderBucketFor(BlockRenderLayer::RenderlayerSeasonsOpaque) == RenderBucket::Opaque);
    LHOLO_CHECK(renderBucketFor(BlockRenderLayer::RenderlayerBlend) == RenderBucket::Blend);
    LHOLO_CHECK(renderBucketFor(BlockRenderLayer::RenderlayerBlendToOpaque) == RenderBucket::Blend);
    LHOLO_CHECK(renderBucketFor(BlockRenderLayer::RenderlayerAlphatestSingleSide) == RenderBucket::AlphaOneSided);
    LHOLO_CHECK(renderBucketFor(BlockRenderLayer::RenderlayerAlphatest) == RenderBucket::Alpha);
    LHOLO_CHECK(renderBucketFor(BlockRenderLayer::RenderlayerDoubleSided) == RenderBucket::Alpha);

    // Praxis appearance contract: preserve native RGB/AO, multiply native
    // alpha, and leave alpha zero transparent.
    LHOLO_CHECK(applyGhostAppearanceAbgr(0xFF563412U, 0.5f) == 0x7F563412U);
    LHOLO_CHECK(applyGhostAppearanceAbgr(0x80563412U, 0.5f) == 0x40563412U);
    LHOLO_CHECK(applyGhostAppearanceAbgr(0x00563412U, 0.5f) == 0x00563412U);
    LHOLO_CHECK(applyGhostAppearanceAbgr(0xFF563412U, 0.0f) == 0x00563412U);
    LHOLO_CHECK(applyGhostAppearanceAbgr(0xFF563412U, 1.0f) == 0xFF563412U);
    LHOLO_CHECK(applyGhostAppearanceAbgr(0xFF804020U, 1.0f, 1.0f) == 0xFF803618U);
}

void testProgress() {
    initializePublishedBuildProgress(100);
    auto progress = getPublishedBuildProgress();
    LHOLO_CHECK(progress.total == 100);
    LHOLO_CHECK(progress.visibleTotal == 100);
    LHOLO_CHECK(progress.placed == 0);

    publishPlacedProgress(120);
    progress = getPublishedBuildProgress();
    LHOLO_CHECK(progress.placed == 100);

    publishVisibleProgress(60, 80);
    publishErrorProgress(130, 5, 140);
    progress = getPublishedBuildProgress();
    LHOLO_CHECK(progress.visiblePlaced == 60);
    LHOLO_CHECK(progress.visibleTotal == 80);
    LHOLO_CHECK(progress.wrongType == 100);
    LHOLO_CHECK(progress.wrongState == 5);
    LHOLO_CHECK(progress.extra == 140);

    resetPublishedBuildProgress();
    progress = getPublishedBuildProgress();
    LHOLO_CHECK(progress.total == 0);
    LHOLO_CHECK(progress.placed == 0);
    LHOLO_CHECK(progress.visibleTotal == 0);

    initializePublishedBuildProgress(50);
    publishVisibleProgress(40, 30);
    progress = getPublishedBuildProgress();
    LHOLO_CHECK(progress.visiblePlaced == 30);
    LHOLO_CHECK(progress.total == 50);

    resetPublishedBuildProgressCounts();
    progress = getPublishedBuildProgress();
    LHOLO_CHECK(progress.total == 50);
    LHOLO_CHECK(progress.placed == 0);
    LHOLO_CHECK(progress.wrongType == 0);
    LHOLO_CHECK(progress.wrongState == 0);
    LHOLO_CHECK(progress.extra == 0);
}

void testAtomicOutput() {
    wchar_t uniquePath[MAX_PATH]{};
    auto const directory = std::filesystem::temp_directory_path();
    auto const created = GetTempFileNameW(directory.c_str(), L"LHO", 0, uniquePath);
    LHOLO_CHECK(created != 0);
    if (!created) return;
    std::filesystem::path const destination{uniquePath};
    lholo::app::ScopeExit cleanup([&]() noexcept { DeleteFileW(destination.c_str()); });
    { std::ofstream file(destination, std::ios::binary); file << "original"; }
    auto read = [&] {
        std::ifstream file(destination, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>{file}, {});
    };
    std::filesystem::path staging;
    LHOLO_CHECK(!lholo::io::writeOutputAtomically(destination, [&](auto const& temporary) {
        staging = temporary;
        std::ofstream file(temporary, std::ios::binary); file << "partial";
        return false;
    }));
    LHOLO_CHECK(read() == "original" && !std::filesystem::exists(staging.parent_path()));
    bool rejected{};
    try {
        (void)lholo::io::writeOutputAtomically(destination, [&](auto const& temporary) -> bool {
            staging = temporary;
            std::ofstream file(temporary, std::ios::binary); file << "partial";
            throw std::runtime_error("native writer failed");
        });
    } catch (std::runtime_error const&) { rejected = true; }
    LHOLO_CHECK(rejected && read() == "original" && !std::filesystem::exists(staging.parent_path()));
    auto const lock = CreateFileW(destination.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    LHOLO_CHECK(lock != INVALID_HANDLE_VALUE);
    if (lock != INVALID_HANDLE_VALUE) {
        rejected = false;
        try {
            (void)lholo::io::writeOutputAtomically(destination, [&](auto const& temporary) {
                staging = temporary;
                std::ofstream file(temporary, std::ios::binary); file << "replacement"; file.close();
                return static_cast<bool>(file);
            });
        } catch (std::system_error const&) { rejected = true; }
        CloseHandle(lock);
        LHOLO_CHECK(rejected && read() == "original" && !std::filesystem::exists(staging.parent_path()));
    }
    LHOLO_CHECK(lholo::io::writeOutputAtomically(destination, [&](auto const& temporary) {
        staging = temporary;
        std::ofstream file(temporary, std::ios::binary); file << "complete"; file.close();
        return static_cast<bool>(file);
    }));
    LHOLO_CHECK(read() == "complete" && !std::filesystem::exists(staging.parent_path()));
}

void testComparisonSettings() {
    wchar_t owned[MAX_PATH]{};
    LHOLO_CHECK(GetTempFileNameW(std::filesystem::temp_directory_path().c_str(), L"LHC", 0, owned) != 0);
    if (!owned[0]) return;
    std::filesystem::path const path{owned};
    lholo::settings::Settings settings;
    auto const inf = std::numeric_limits<float>::infinity();
    for (float bad : {std::numeric_limits<float>::quiet_NaN(), inf, -inf}) {
        settings.comparisonStrength = bad;
        settings.correctionOutlineWidth = bad;
        lholo::settings::saveSettingsFile(path, settings);
        lholo::settings::Settings loaded;
        LHOLO_CHECK(lholo::settings::loadSettingsFile(path, loaded));
        LHOLO_CHECK(loaded.comparisonStrength == 1.f && loaded.correctionOutlineWidth == 1.f);
    }
    for (auto const& values : {std::pair{-10.f,-10.f}, std::pair{10.f,10.f}, std::pair{1.7f,6.25f}}) {
        settings.comparisonStrength = values.first;
        settings.correctionOutlineWidth = values.second;
        lholo::settings::saveSettingsFile(path, settings);
        lholo::settings::Settings loaded;
        LHOLO_CHECK(lholo::settings::loadSettingsFile(path, loaded));
        LHOLO_CHECK(loaded.comparisonStrength == normalizeComparisonStrength(values.first));
        LHOLO_CHECK(loaded.correctionOutlineWidth == normalizeCorrectionOutlineWidth(values.second));
    }
    for (auto const json : {
        R"({"version":13,"comparisonStrength":null,"correctionOutlineWidth":"invalid","lastStructurePath":"preserved"})",
        R"({"version":13,"comparisonStrength":1e100,"correctionOutlineWidth":-1e100,"lastStructurePath":"preserved"})",
        R"({"version":13,"lastStructurePath":"preserved"})"}) {
        { std::ofstream output(path, std::ios::trunc); output << json; }
        lholo::settings::Settings loaded;
        LHOLO_CHECK(lholo::settings::loadSettingsFile(path, loaded));
        LHOLO_CHECK(loaded.comparisonStrength == 1.f && loaded.correctionOutlineWidth == 1.f);
        LHOLO_CHECK(loaded.lastStructurePath == "preserved");
    }
    std::error_code error;
    std::filesystem::remove(path, error);
}

void testSettingsStore() {
    wchar_t uniquePath[MAX_PATH]{};
    auto const tempDirectory = std::filesystem::temp_directory_path();
    auto const created = GetTempFileNameW(tempDirectory.c_str(), L"LHT", 0, uniquePath);
    LHOLO_CHECK(created != 0);
    if (!created) return;
    std::filesystem::path const path{uniquePath}; // Only this test owns this file.
    std::error_code error;
    std::filesystem::remove(path, error);

    lholo::settings::Settings settings;
    LHOLO_CHECK(settings.language == "ja_JP");
    LHOLO_CHECK(settings.guiHotkey == VK_INSERT);
    LHOLO_CHECK(settings.guiHotkeyModifiers == 0);
    LHOLO_CHECK(settings.toggleManualHotkey == 0);
    LHOLO_CHECK(settings.toggleManualHotkeyModifiers == 0);
    settings.language = "en_US";
    settings.uiScale = 1.25f;
    LHOLO_CHECK(settings.comparisonStrength == 1.f && settings.correctionOutlineWidth == 1.f);
    settings.comparisonStrength = 1.6f;
    settings.correctionOutlineWidth = 4.5f;
    settings.guiHotkey = 'L';
    settings.guiHotkeyModifiers = 1;
    settings.toggleManualHotkey = 'R';
    settings.toggleManualHotkeyModifiers = 0;
    settings.hudShowProjectedBlockName = false;
    settings.hudShowExtraBlocks = false;
    settings.autoPlacementBreakCooldownSeconds = 27;
    LHOLO_CHECK(settings.manualPlacementAllowedItems.empty());
    settings.manualPlacementAllowedItems = {" dirt ", "minecraft:scaffolding", "minecraft:dirt"};
    settings.correctionSeeThrough = true;
    settings.materialHudEnabled = true;
    settings.materialHudPosition = 3;
    settings.altWheelOffsetEnabled = false;
    settings.moveHotkeys[4] = 0x57; // W
    settings.hasSavedProjection = true;
    settings.savedAnchorX = 12;
    settings.savedAnchorZ = -34;
    lholo::settings::saveSettingsFile(path, settings);
    {
        std::ifstream saved(path);
        std::ostringstream contents;
        contents << saved.rdbuf();
        LHOLO_CHECK(contents.str().find("\"version\": 13") != std::string::npos);
        LHOLO_CHECK(contents.str().find("\"language\": \"en_US\"") != std::string::npos);
        LHOLO_CHECK(contents.str().find("\"altWheelOffsetEnabled\": false") != std::string::npos);
        LHOLO_CHECK(contents.str().find("\"moveUpHotkey\": 87") != std::string::npos);
        // The axis-era move key names are gone from new files; they are only
        // read as a fallback (see the legacy config below).
        LHOLO_CHECK(contents.str().find("moveXMinusHotkey") == std::string::npos);
        LHOLO_CHECK(contents.str().find("moveYPlusHotkey") == std::string::npos);
        LHOLO_CHECK(contents.str().find("\"toggleManualHotkey\": 82") != std::string::npos);
        LHOLO_CHECK(contents.str().find("\"toggleManualHotkeyModifiers\": 0") != std::string::npos);
        LHOLO_CHECK(contents.str().find("toggleEasyHotkey") == std::string::npos);
        LHOLO_CHECK(contents.str().find("toggleRangeHotkey") == std::string::npos);
    }

    lholo::settings::Settings loaded;
    LHOLO_CHECK(lholo::settings::loadSettingsFile(path, loaded));
    LHOLO_CHECK(loaded.language == "en_US");
    LHOLO_CHECK(loaded.uiScale == 1.25f);
    LHOLO_CHECK(loaded.comparisonStrength == 1.6f && loaded.correctionOutlineWidth == 4.5f);
    LHOLO_CHECK(loaded.guiHotkey == 'L');
    LHOLO_CHECK(loaded.guiHotkeyModifiers == 1);
    LHOLO_CHECK(loaded.toggleManualHotkey == 'R');
    LHOLO_CHECK(loaded.toggleManualHotkeyModifiers == 0);
    LHOLO_CHECK(!loaded.hudShowProjectedBlockName);
    LHOLO_CHECK(!loaded.hudShowExtraBlocks);
    LHOLO_CHECK(loaded.autoPlacementBreakCooldownSeconds == 27);
    LHOLO_CHECK(loaded.manualPlacementAllowedItems
        == (std::vector<std::string>{"minecraft:dirt", "minecraft:scaffolding"}));
    LHOLO_CHECK(loaded.correctionSeeThrough);
    LHOLO_CHECK(loaded.materialHudEnabled);
    LHOLO_CHECK(loaded.materialHudPosition == 3);
    LHOLO_CHECK(!loaded.altWheelOffsetEnabled);
    LHOLO_CHECK(loaded.moveHotkeys[4] == 0x57);
    LHOLO_CHECK(loaded.hasSavedProjection);
    LHOLO_CHECK(loaded.savedAnchorX == 12);
    LHOLO_CHECK(loaded.savedAnchorZ == -34);

    auto readBytes = [](std::filesystem::path const& source) {
        std::ifstream input(source, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), {});
    };
    auto const original = readBytes(path);
    auto badEncoding = settings;
    badEncoding.lastStructurePath = std::string(1, static_cast<char>(0xFF));
    bool rejectedEncoding{};
    try { lholo::settings::saveSettingsFile(path, badEncoding); }
    catch (...) { rejectedEncoding = true; }
    LHOLO_CHECK(rejectedEncoding);
    LHOLO_CHECK(readBytes(path) == original);

    // A late type error must not publish the early fields of a parsed file.
    {
        std::ofstream malformed(path, std::ios::trunc);
        malformed << R"({"version":13,"lastStructurePath":"changed","savedStructurePath":false})";
    }
    auto preserved = settings;
    bool rejectedParse{};
    try { (void)lholo::settings::loadSettingsFile(path, preserved); }
    catch (...) { rejectedParse = true; }
    LHOLO_CHECK(rejectedParse);
    LHOLO_CHECK(preserved.lastStructurePath == settings.lastStructurePath);
    LHOLO_CHECK(preserved.language == settings.language && preserved.uiScale == settings.uiScale);

    lholo::settings::saveSettingsFile(path, settings);
    auto const locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    LHOLO_CHECK(locked != INVALID_HANDLE_VALUE);
    if (locked != INVALID_HANDLE_VALUE) {
        bool rejectedWrite{};
        try { lholo::settings::saveSettingsFile(path, loaded); }
        catch (...) { rejectedWrite = true; }
        LHOLO_CHECK(rejectedWrite);
        CloseHandle(locked);
        LHOLO_CHECK(readBytes(path) == original);
    }

    wchar_t relativePath[MAX_PATH]{};
    auto const currentDirectory = std::filesystem::current_path();
    auto const relativeCreated = GetTempFileNameW(currentDirectory.c_str(), L"LHT", 0, relativePath);
    LHOLO_CHECK(relativeCreated != 0);
    if (relativeCreated) {
        std::filesystem::path const ownedRelative{relativePath};
        bool savedRelative{};
        try { lholo::settings::saveSettingsFile(ownedRelative.filename(), settings); savedRelative = true; }
        catch (...) {}
        LHOLO_CHECK(savedRelative && readBytes(ownedRelative) == original);
        std::filesystem::remove(ownedRelative, error);
    }

    // Existing configs keep their preference when the old, narrower
    // block-entity label migrates to the projected-block label.
    {
        std::ofstream legacy(path, std::ios::trunc);
        legacy << R"({"version":12,"language":"zh_CN","guiHotkey":77,"guiHotkeyModifiers":2,"hudShowBlockEntity":false,"toggleManualHotkey":82,"toggleEasyHotkey":70,"toggleRangeHotkey":89,"moveXMinusHotkey":65,"moveXMinusHotkeyModifiers":2,"moveYPlusHotkey":87})";
    }
    lholo::settings::Settings migrated;
    LHOLO_CHECK(lholo::settings::loadSettingsFile(path, migrated));
    LHOLO_CHECK(!migrated.hudShowProjectedBlockName);
    LHOLO_CHECK(migrated.hudShowExtraBlocks);
    LHOLO_CHECK(migrated.autoPlacementBreakCooldownSeconds == 10);
    LHOLO_CHECK(migrated.manualPlacementAllowedItems.empty());
    LHOLO_CHECK(!migrated.correctionSeeThrough);
    LHOLO_CHECK(!migrated.materialHudEnabled);
    LHOLO_CHECK(migrated.materialHudPosition == 3);
    // The upstream schema-12 defaults migrate to this fork's Japanese/Insert defaults.
    LHOLO_CHECK(migrated.language == "ja_JP");
    LHOLO_CHECK(migrated.guiHotkey == VK_INSERT);
    LHOLO_CHECK(migrated.guiHotkeyModifiers == 0);
    // The historic manual-placement binding is accepted again when it is still
    // present in an older config; easy/range bindings remain retired.
    LHOLO_CHECK(migrated.toggleManualHotkey == 'R');
    LHOLO_CHECK(migrated.toggleManualHotkeyModifiers == 0);
    // Likewise, a config written before the Alt+wheel switch existed keeps the
    // gesture enabled, so upgrading never silently changes input behavior.
    LHOLO_CHECK(migrated.altWheelOffsetEnabled);
    // Move bindings survive the rename of the move slots from world axes to
    // view-relative directions: the old key names are still read as a fallback.
    LHOLO_CHECK(migrated.moveHotkeys[0] == 65);
    LHOLO_CHECK(migrated.moveHotkeyModifiers[0] == 2);
    LHOLO_CHECK(migrated.moveHotkeys[4] == 87);

    {
        std::ofstream invalidLanguage(path, std::ios::trunc);
        invalidLanguage << R"({"language":1})";
    }
    lholo::settings::Settings invalid;
    LHOLO_CHECK(lholo::settings::loadSettingsFile(path, invalid));
    LHOLO_CHECK(invalid.language == "ja_JP");

    // An invalid exception list never grants a partial exemption and does not
    // prevent unrelated, valid preferences from loading.
    for (auto const* badList : {"true", "\"minecraft:dirt\"", "[\"dirt\",4]", "[\"dirt\",\"*\"]"}) {
        {
            std::ofstream bad(path, std::ios::trunc);
            bad << "{\"placementRadius\":2,\"manualPlacementAllowedItems\":" << badList << "}";
        }
        lholo::settings::Settings rejected;
        rejected.manualPlacementAllowedItems = {"minecraft:scaffolding"};
        LHOLO_CHECK(lholo::settings::loadSettingsFile(path, rejected));
        LHOLO_CHECK(rejected.manualPlacementAllowedItems.empty());
        LHOLO_CHECK(rejected.placementRadius == 2);
    }

    lholo::settings::Settings missing;
    std::filesystem::remove(path, error);
    LHOLO_CHECK(!lholo::settings::loadSettingsFile(path, missing));
    std::filesystem::remove(path, error);
}

void testSettingsCurrentKeyPriority() {
    wchar_t uniquePath[MAX_PATH]{};
    auto const tempDirectory = std::filesystem::temp_directory_path();
    auto const created = GetTempFileNameW(tempDirectory.c_str(), L"LHP", 0, uniquePath);
    LHOLO_CHECK(created != 0);
    if (!created) return;
    std::filesystem::path const path{uniquePath};
    lholo::app::ScopeExit cleanup([&]() noexcept {
        std::error_code error;
        std::filesystem::remove(path, error);
    });
    auto load = [&](std::string const& json, lholo::settings::Settings& settings) {
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file << json;
            if (!file) throw std::runtime_error("Could not write owned settings fixture");
        }
        try { return lholo::settings::loadSettingsFile(path, settings); }
        catch (std::exception const&) { return false; }
    };
    lholo::settings::Settings hud;
    LHOLO_CHECK(load(R"({"version":13,"hudShowProjectedBlockName":false,"hudShowBlockEntity":"unused"})", hud));
    LHOLO_CHECK(!hud.hudShowProjectedBlockName);
    constexpr std::array currentKeys{
        "moveLeftHotkey", "moveRightHotkey", "moveForwardHotkey", "moveBackwardHotkey", "moveUpHotkey", "moveDownHotkey"
    };
    constexpr std::array legacyKeys{
        "moveXMinusHotkey", "moveXPlusHotkey", "moveZMinusHotkey", "moveZPlusHotkey", "moveYPlusHotkey", "moveYMinusHotkey"
    };
    for (std::size_t index = 0; index < currentKeys.size(); ++index) {
        for (bool modifiers : {false, true}) {
            auto const suffix = modifiers ? "Modifiers" : "";
            auto const value = modifiers ? 5 : 76;
            auto const fixture = std::string{"{\"version\":13,\""} + currentKeys[index] + suffix
                + "\":" + std::to_string(value) + ",\"" + legacyKeys[index] + suffix + "\":{}}";
            lholo::settings::Settings settings;
            LHOLO_CHECK(load(fixture, settings));
            LHOLO_CHECK((modifiers ? settings.moveHotkeyModifiers[index] : settings.moveHotkeys[index]) == value);
        }
    }
    // The selected current field still fails closed on a type error. A valid
    // legacy value cannot hide malformed current data or publish early fields.
    for (auto const* fixture : {
        R"({"version":13,"lastStructurePath":"changed","hudShowProjectedBlockName":{},"hudShowBlockEntity":false})",
        R"({"version":13,"lastStructurePath":"changed","moveLeftHotkey":{},"moveXMinusHotkey":76})",
        R"({"version":13,"lastStructurePath":"changed","moveLeftHotkeyModifiers":{},"moveXMinusHotkeyModifiers":5})"
    }) {
        lholo::settings::Settings settings;
        settings.lastStructurePath = "preserved";
        LHOLO_CHECK(!load(fixture, settings));
        LHOLO_CHECK(settings.lastStructurePath == "preserved");
    }
}

void testStructureSession() {
    using lholo::structure::LayerAxis;
    using lholo::structure::LayerDisplayMode;
    using lholo::structure::detail::SavedProjectionSnapshot;
    using lholo::structure::detail::StructureSession;

    auto& session = StructureSession::getInstance();
    session.clearLoaded(lholo::i18n::Message{lholo::i18n::TextKey::StatusNotLoaded});
    session.resetTransform();
    session.setLastPath("initial.mcstructure");
    session.setSavedProjection(SavedProjectionSnapshot{});

    auto snapshot = session.snapshot();
    LHOLO_CHECK(!snapshot.loaded);
    // The snapshot renders the stored message in the selected language.
    LHOLO_CHECK(snapshot.status == lholo::i18n::tr(lholo::i18n::TextKey::StatusNotLoaded));
    LHOLO_CHECK(snapshot.lastPath == "initial.mcstructure");
    LHOLO_CHECK(snapshot.transform.rotation == 0);
    LHOLO_CHECK(!snapshot.saved.available);

    auto loaded = std::make_shared<LoadedStructure>();
    loaded->sizeX = 7;
    loaded->sizeY = 5;
    loaded->sizeZ = 3;
    session.replaceLoaded(
        loaded,
        "active.mcstructure",
        lholo::i18n::Message{lholo::i18n::TextKey::StatusRestoredPending}
    );
    LHOLO_CHECK(session.setRotation(2));
    LHOLO_CHECK(!session.setRotation(2));
    LHOLO_CHECK(session.setMirror(1));
    LHOLO_CHECK(session.setOffsetX(12));
    LHOLO_CHECK(session.setOffsetY(-4));
    LHOLO_CHECK(session.setLayerDisplayMode(LayerDisplayMode::Single));
    LHOLO_CHECK(session.setDisplayLayer(4));
    LHOLO_CHECK(session.setLayerAxis(LayerAxis::X));

    snapshot = session.snapshot();
    LHOLO_CHECK(snapshot.loaded == loaded);
    LHOLO_CHECK(snapshot.maxLayerY == 4);
    LHOLO_CHECK(snapshot.maxLayerX == 6);
    LHOLO_CHECK(snapshot.transform.offsetX == 12);
    LHOLO_CHECK(snapshot.transform.offsetY == -4);

    // A coordinate layer is already a maximum index, not a count.
    session.setDisplayLayer(5);
    LHOLO_CHECK(session.adjustDisplayLayer(1));
    LHOLO_CHECK(session.transform().displayLayer == 6);
    session.setLayerAxis(LayerAxis::Y);
    session.setDisplayLayer(3);
    LHOLO_CHECK(session.adjustDisplayLayer(1));
    LHOLO_CHECK(session.transform().displayLayer == 4);
    LHOLO_CHECK(session.adjustDisplayLayer((std::numeric_limits<int>::max)()));
    LHOLO_CHECK(session.transform().displayLayer == 4);
    session.setLayerAxis(LayerAxis::X);
    session.setDisplayLayer(4);

    session.recordProjectionAnchor(10, 20, 30);
    auto const saved = session.savedProjection();
    LHOLO_CHECK(saved.available);
    LHOLO_CHECK(saved.anchorX == 10);
    LHOLO_CHECK(saved.anchorY == 20);
    LHOLO_CHECK(saved.anchorZ == 30);
    LHOLO_CHECK(saved.transform.rotation == 2);
    LHOLO_CHECK(saved.transform.mirror == 1);
    LHOLO_CHECK(saved.structurePath == "active.mcstructure");

    auto layered = std::make_shared<LoadedStructure>();
    layered->sizeX = 3;
    layered->sizeY = 8;
    layered->sizeZ = 4;
    session.replaceLoaded(
        layered,
        "layered.mcstructure",
        lholo::i18n::Message{lholo::i18n::TextKey::StatusRestoredPending}
    );
    session.setLayerDisplayMode(LayerDisplayMode::UpToCurrent);
    session.setDisplayLayer(7);
    session.setLayerAxis(LayerAxis::Y);
    session.recordProjectionAnchor(40, 50, 60);
    session.clearLoaded(lholo::i18n::Message{lholo::i18n::TextKey::StatusProjectionClosed});
    session.setDisplayLayer(0); // Empty-menu clamping must not alter the saved layer.
    auto const layeredSaved = session.savedProjection();
    LHOLO_CHECK(layeredSaved.transform.layerDisplayMode == LayerDisplayMode::UpToCurrent);
    LHOLO_CHECK(layeredSaved.transform.displayLayer == 7);
    LHOLO_CHECK(layeredSaved.transform.layerAxis == LayerAxis::Y);

    session.setLayerDisplayMode(layeredSaved.transform.layerDisplayMode);
    session.setDisplayLayer(layeredSaved.transform.displayLayer);
    session.setLayerAxis(layeredSaved.transform.layerAxis);
    session.replaceLoaded(
        layered,
        layeredSaved.structurePath,
        lholo::i18n::Message{lholo::i18n::TextKey::StatusRestoredPending}
    );
    snapshot = session.snapshot();
    LHOLO_CHECK(snapshot.loaded == layered);
    LHOLO_CHECK(snapshot.maxLayerY == 7);
    LHOLO_CHECK(snapshot.transform.displayLayer == 7);

    session.clearLoaded(lholo::i18n::Message{lholo::i18n::TextKey::StatusProjectionClosed});
}

void testPlacementState() {
    using lholo::place::detail::FailedPlanKey;
    using lholo::place::detail::PlacementState;

    auto& state = PlacementState::getInstance();
    state.setEnabled(true);
    state.setRangeEnabled(true);
    state.setManualMode(true);
    state.setRadius(3);
    state.setAutoPlacementBreakCooldownSeconds(12);
    LHOLO_CHECK(state.beginManualPress(100, state.manualInputEpoch()));
    LHOLO_CHECK(!state.beginManualPress(120, state.manualInputEpoch()));
    state.setLastManualPlaceAt(80);
    state.setNextPlaceAt(140);
    state.setNextSwapAt(150);

    LHOLO_CHECK(state.enabled());
    LHOLO_CHECK(state.rangeEnabled());
    LHOLO_CHECK(state.manualMode());
    LHOLO_CHECK(state.radius() == 3);
    LHOLO_CHECK(state.autoPlacementBreakCooldownSeconds() == 12);
    LHOLO_CHECK(state.manualPressAt() == 100);
    LHOLO_CHECK(state.lastManualPlaceAt() == 80);
    LHOLO_CHECK(state.manualPlaceRequested());
    LHOLO_CHECK(state.manualHeld());
    LHOLO_CHECK(state.nextPlaceAt() == 140);
    LHOLO_CHECK(state.nextSwapAt() == 150);
    state.releaseManualPress();
    LHOLO_CHECK(!state.manualHeld());
    LHOLO_CHECK(state.manualPlaceRequested());
    state.cancelManualPress();
    LHOLO_CHECK(!state.manualPlaceRequested());

    constexpr std::int64_t recentCell = 0x123456789LL;
    state.recordRecentPlacement(recentCell, 100, 150);
    LHOLO_CHECK(state.recentPlacementActive(recentCell, 149));
    LHOLO_CHECK(!state.recentPlacementActive(recentCell, 150));

    constexpr std::int64_t suppressedCell = 0x23456789ALL;
    LHOLO_CHECK(!state.autoPlacementSuppressionsActive(100));
    state.suppressAutoPlacement(suppressedCell, 200);
    LHOLO_CHECK(state.autoPlacementSuppressionsActive(100));
    LHOLO_CHECK(state.autoPlacementSuppressed(suppressedCell, 199));
    LHOLO_CHECK(!state.autoPlacementSuppressionsActive(200));
    LHOLO_CHECK(!state.autoPlacementSuppressed(suppressedCell, 200));

    // Extending the current earliest entry may leave the cheap expiry hint at
    // its old value, but the first boundary refresh must retain the live entry.
    state.suppressAutoPlacement(suppressedCell, 300);
    state.suppressAutoPlacement(suppressedCell, 350);
    LHOLO_CHECK(state.autoPlacementSuppressionsActive(300));
    LHOLO_CHECK(state.autoPlacementSuppressed(suppressedCell, 349));
    LHOLO_CHECK(!state.autoPlacementSuppressionsActive(350));

    FailedPlanKey const failedKey{recentCell, 42, 7, 1, 2, 3, 4, 5, 6};
    state.cacheFailedPlan(failedKey, 200, 250);
    LHOLO_CHECK(state.failedPlanCached(failedKey, 249));
    LHOLO_CHECK(!state.failedPlanCached(failedKey, 250));

    state.setAimedProjectedBlockName("Test projected block");
    LHOLO_CHECK(state.aimedProjectedBlockName() == "Test projected block");

    state.recordRecentPlacement(recentCell, 400, 500);
    state.suppressAutoPlacement(suppressedCell, 500);
    state.cacheFailedPlan(failedKey, 400, 500);
    state.resetDimensionSession();
    LHOLO_CHECK(state.enabled());
    LHOLO_CHECK(state.rangeEnabled());
    LHOLO_CHECK(state.manualMode());
    LHOLO_CHECK(!state.manualHeld());
    LHOLO_CHECK(!state.manualPlaceRequested());
    LHOLO_CHECK(state.manualPressAt() == 0);
    LHOLO_CHECK(state.lastManualPlaceAt() == 0);
    LHOLO_CHECK(state.nextPlaceAt() == 0);
    LHOLO_CHECK(state.nextSwapAt() == 0);
    LHOLO_CHECK(!state.recentPlacementActive(recentCell, 400));
    LHOLO_CHECK(!state.autoPlacementSuppressionsActive(400));
    LHOLO_CHECK(!state.autoPlacementSuppressed(suppressedCell, 400));
    LHOLO_CHECK(!state.failedPlanCached(failedKey, 400));
    LHOLO_CHECK(state.aimedProjectedBlockName().empty());
    LHOLO_CHECK(state.radius() == 3);
    LHOLO_CHECK(state.autoPlacementBreakCooldownSeconds() == 12);

    LHOLO_CHECK(state.beginManualPress(450, state.manualInputEpoch()));
    state.setAimedProjectedBlockName("World projected block");
    state.suppressAutoPlacement(suppressedCell, 500);
    state.resetWorldSession();
    LHOLO_CHECK(!state.enabled());
    LHOLO_CHECK(!state.rangeEnabled());
    LHOLO_CHECK(!state.manualMode());
    LHOLO_CHECK(!state.manualHeld());
    LHOLO_CHECK(!state.manualPlaceRequested());
    LHOLO_CHECK(state.manualPressAt() == 0);
    LHOLO_CHECK(state.lastManualPlaceAt() == 0);
    LHOLO_CHECK(state.nextPlaceAt() == 0);
    LHOLO_CHECK(state.nextSwapAt() == 0);
    LHOLO_CHECK(!state.recentPlacementActive(recentCell, 0));
    LHOLO_CHECK(!state.autoPlacementSuppressionsActive(0));
    LHOLO_CHECK(!state.autoPlacementSuppressed(suppressedCell, 0));
    LHOLO_CHECK(!state.failedPlanCached(failedKey, 0));
    LHOLO_CHECK(state.aimedProjectedBlockName().empty());
    // User configuration survives a world transition.
    LHOLO_CHECK(state.radius() == 3);
    LHOLO_CHECK(state.autoPlacementBreakCooldownSeconds() == 12);

    state.setRadius(4);
    state.setAutoPlacementBreakCooldownSeconds(10);
}

void testStructureUiState() {
    using lholo::structure::detail::HudStateSnapshot;
    using lholo::structure::detail::StructureUiState;

    {
        std::array const fields{&HudStateSnapshot::showFileName, &HudStateSnapshot::showLayer,
            &HudStateSnapshot::showOverallProgress, &HudStateSnapshot::showProgress,
            &HudStateSnapshot::showWrongState, &HudStateSnapshot::showWrongType,
            &HudStateSnapshot::showExtraBlocks, &HudStateSnapshot::showProjectedBlockName};
        HudStateSnapshot hud;
        for (auto field : fields) hud.*field = false;
        LHOLO_CHECK(!hud.hasVisibleFields());
        for (auto field : fields) {
            hud.*field = true;
            LHOLO_CHECK(hud.hasVisibleFields());
            hud.enabled = false;
            LHOLO_CHECK(!hud.hasVisibleFields());
            hud.enabled = true;
            hud.*field = false;
        }
    }

    auto& state = StructureUiState::getInstance();
    state.resetHotkeys();
    state.resetHotkeyState();
    state.stopHotkeyCapture();
    (void)state.consumePendingHotkeyActions();
    state.clearMaterials();
    LHOLO_CHECK(state.altWheelOffsetEnabled());
    LHOLO_CHECK(state.setAltWheelOffsetEnabled(false));
    LHOLO_CHECK(!state.setAltWheelOffsetEnabled(false));
    LHOLO_CHECK(state.setAltWheelOffsetEnabled(true));
    LHOLO_CHECK(state.altWheelOffsetEnabled());

    auto hud = state.hud();
    hud.enabled = false;
    hud.showLayer = false;
    hud.showProjectedBlockName = false;
    hud.position = 3;
    hud.uiScale = 1.5f;
    LHOLO_CHECK(state.applyHud(hud));
    LHOLO_CHECK(!state.applyHud(hud));
    auto const appliedHud = state.hud();
    LHOLO_CHECK(!appliedHud.enabled);
    LHOLO_CHECK(!appliedHud.showLayer);
    LHOLO_CHECK(!appliedHud.showProjectedBlockName);
    LHOLO_CHECK(appliedHud.position == 3);
    LHOLO_CHECK(appliedHud.uiScale == 1.5f);

    state.resetHotkeys();
    auto const guiSlot = lholo::input::hotkeyIndex(lholo::input::HotkeyId::Gui);
    auto const moveLeftSlot = lholo::input::hotkeyIndex(lholo::input::HotkeyId::MoveLeft);
    auto const moveRightSlot = lholo::input::hotkeyIndex(lholo::input::HotkeyId::MoveRight);
    auto const layerIncreaseSlot = lholo::input::hotkeyIndex(lholo::input::HotkeyId::LayerIncrease);
    auto const loadProjectionSlot = lholo::input::hotkeyIndex(lholo::input::HotkeyId::LoadProjection);
    auto const closeProjectionSlot = lholo::input::hotkeyIndex(lholo::input::HotkeyId::CloseProjection);
    auto const toggleManualSlot = lholo::input::hotkeyIndex(lholo::input::HotkeyId::ToggleManualPlacement);
    LHOLO_CHECK(state.hotkey(guiSlot).key == VK_INSERT);
    LHOLO_CHECK(state.hotkey(guiSlot).modifiers == 0);
    LHOLO_CHECK(state.hotkey(moveLeftSlot).key == VK_LEFT);
    LHOLO_CHECK(state.hotkey(layerIncreaseSlot).key == VK_UP);
    LHOLO_CHECK(state.hotkey(loadProjectionSlot).key == 0);
    LHOLO_CHECK(state.hotkey(closeProjectionSlot).key == 0);
    LHOLO_CHECK(state.hotkey(toggleManualSlot).key == 0);

    state.beginHotkeyCapture(moveLeftSlot);
    LHOLO_CHECK(state.capturingHotkey() == moveLeftSlot);
    state.setHotkey(moveRightSlot, 'K', lholo::ui::kHotkeyModifierControl);
    state.bindCapturedHotkey(moveLeftSlot, 'K', lholo::ui::kHotkeyModifierControl);
    LHOLO_CHECK(state.hotkey(moveLeftSlot).key == 'K');
    LHOLO_CHECK(state.hotkey(moveRightSlot).key == 0);
    LHOLO_CHECK(!state.capturingHotkey());

    state.setControlHeld(true);
    state.setShiftHeld(true);
    LHOLO_CHECK(
        state.currentHotkeyModifiers()
        == (lholo::ui::kHotkeyModifierControl | lholo::ui::kHotkeyModifierShift)
    );
    state.setControlHeld(false);
    state.setShiftHeld(false);

    state.resetHotkeys();
    LHOLO_CHECK(state.tryPressHotkey(guiSlot));
    LHOLO_CHECK(!state.tryPressHotkey(guiSlot));
    LHOLO_CHECK(state.releaseHotkeysForKey(VK_INSERT, 100));
    LHOLO_CHECK(state.releaseHotkeysForKey(VK_INSERT, 150));
    LHOLO_CHECK(!state.releaseHotkeysForKey(VK_INSERT, 201));

    // The state only accumulates a delta now; which world direction a move
    // hotkey produces is resolved in ViewMoveBasis from the player's facing.
    state.queueOffsetDelta(-1, 1, 0);
    state.queueLayerDelta(-1);
    state.queueLoadProjection();
    state.queueCloseProjection();
    state.queueToggleManualPlacement();
    state.requestSettingsSave();
    auto const pending = state.consumePendingHotkeyActions();
    LHOLO_CHECK(pending.offsetX == -1);
    LHOLO_CHECK(pending.offsetY == 1);
    LHOLO_CHECK(pending.offsetZ == 0);
    LHOLO_CHECK(pending.layerDelta == -1);
    LHOLO_CHECK(pending.loadProjection);
    LHOLO_CHECK(pending.closeProjection);
    LHOLO_CHECK(pending.toggleManualPlacement);
    LHOLO_CHECK(pending.settingsSave);

    state.clearMaterials();
    LHOLO_CHECK(!state.materialListReady());
    state.requestMaterialList();
    LHOLO_CHECK(state.consumeMaterialListRequest());
    LHOLO_CHECK(!state.consumeMaterialListRequest());
    state.replaceMaterialRequirements({
        {.displayName = "Stone", .typeName = "minecraft:stone",
         .itemId = "minecraft:stone", .count = 12}
    });
    LHOLO_CHECK(state.materialListReady());
    // Reopening a completed list must not queue another full structure scan.
    state.requestMaterialList();
    LHOLO_CHECK(!state.consumeMaterialListRequest());
    auto const materials = state.materialRequirements();
    LHOLO_CHECK(materials.size() == 1);
    LHOLO_CHECK(materials[0].typeName == "minecraft:stone");
    LHOLO_CHECK(materials[0].itemId == "minecraft:stone");
    LHOLO_CHECK(materials[0].count == 12);

    auto hudMaterials = state.materialHudSnapshot();
    LHOLO_CHECK(!hudMaterials.ready);
    state.replaceMaterialHudSnapshot(
        {{.displayName = "Glass", .typeName = "minecraft:glass",
          .itemId = "minecraft:glass", .count = 5}},
        {2}
    );
    hudMaterials = state.materialHudSnapshot();
    LHOLO_CHECK(hudMaterials.ready);
    LHOLO_CHECK(hudMaterials.requirements.size() == 1);
    LHOLO_CHECK(hudMaterials.requirements[0].count == 5);
    LHOLO_CHECK(hudMaterials.available.size() == 1);
    LHOLO_CHECK(hudMaterials.available[0] == 2);
    // Updating the current-layer HUD must not replace the whole-structure list.
    LHOLO_CHECK(state.materialRequirements()[0].typeName == "minecraft:stone");
    state.clearMaterialHud();
    LHOLO_CHECK(!state.materialHudSnapshot().ready);

    state.setExperimentalConsentGiven(true);
    state.setMaterialHudEnabled(true);
    state.setMaterialHudPosition(3);
    state.setActionHint(lholo::i18n::Message{lholo::i18n::TextKey::StatusWorldExited}, 1234);
    LHOLO_CHECK(state.experimentalConsentGiven());
    LHOLO_CHECK(state.materialHudEnabled());
    LHOLO_CHECK(state.materialHudPosition() == 3);
    auto const hint = state.actionHint();
    LHOLO_CHECK(hint.text == lholo::i18n::tr(lholo::i18n::TextKey::StatusWorldExited));
    LHOLO_CHECK(hint.expiry == 1234);

    state.setGuiVisible(false);
    LHOLO_CHECK(state.toggleGuiVisible());
    LHOLO_CHECK(state.guiVisible());
    state.setOpeningInputBlockFrames(1);
    LHOLO_CHECK(state.openingInputBlocked());
    state.consumeOpeningInputBlockFrame();
    LHOLO_CHECK(!state.openingInputBlocked());

    state.setGuiVisible(true);
    state.setOpeningInputBlockFrames(3);
    state.setBlockGameInputUntil(900);
    state.beginHotkeyCapture(moveLeftSlot);
    state.setControlHeld(true);
    state.queueOffsetDelta(1, 0, 0);
    state.queueLayerDelta(1);
    state.queueLoadProjection();
    state.queueCloseProjection();
    state.requestSettingsSave();
    state.setAltWheelOffsetEnabled(false);
    state.replaceMaterialRequirements({
        {.displayName = "Stone", .typeName = "minecraft:stone",
         .itemId = "minecraft:stone", .count = 4}
    });
    state.replaceMaterialHudSnapshot(
        {{.displayName = "Glass", .typeName = "minecraft:glass",
          .itemId = "minecraft:glass", .count = 2}},
        {1}
    );
    state.setActionHint(lholo::i18n::Message{lholo::i18n::TextKey::StatusWorldExited}, 9999);
    state.resetWorldSession();
    LHOLO_CHECK(!state.guiVisible());
    LHOLO_CHECK(!state.openingInputBlocked());
    LHOLO_CHECK(state.blockGameInputUntil() == 0);
    LHOLO_CHECK(!state.capturingHotkey());
    LHOLO_CHECK(state.currentHotkeyModifiers() == 0);
    LHOLO_CHECK(!state.materialListReady());
    LHOLO_CHECK(!state.materialHudSnapshot().ready);
    LHOLO_CHECK(state.actionHint().text.empty());
    LHOLO_CHECK(state.actionHint().expiry == 0);
    auto const afterWorldExit = state.consumePendingHotkeyActions();
    LHOLO_CHECK(afterWorldExit.offsetX == 0);
    LHOLO_CHECK(afterWorldExit.offsetY == 0);
    LHOLO_CHECK(afterWorldExit.offsetZ == 0);
    LHOLO_CHECK(afterWorldExit.layerDelta == 0);
    LHOLO_CHECK(!afterWorldExit.loadProjection);
    LHOLO_CHECK(!afterWorldExit.closeProjection);
    // A pending settings write is not world-owned and must still complete.
    LHOLO_CHECK(afterWorldExit.settingsSave);
    LHOLO_CHECK(state.experimentalConsentGiven());
    LHOLO_CHECK(state.materialHudEnabled());
    LHOLO_CHECK(state.materialHudPosition() == 3);
    // The fixed-gesture switch is a user preference, so leaving a world keeps
    // it whereas the transient flags above are cleared.
    LHOLO_CHECK(!state.altWheelOffsetEnabled());

    state.setGuiVisible(false);
    state.resetHotkeys();
    // "Reset all hotkeys" also restores the fixed-gesture switch.
    LHOLO_CHECK(state.altWheelOffsetEnabled());
    state.resetHotkeyState();
    state.clearMaterials();
    LHOLO_CHECK(!state.materialListReady());
    state.setExperimentalConsentGiven(false);
    state.setMaterialHudEnabled(false);
    state.setMaterialHudPosition(3);
    state.setActionHint({}, 0);
    state.applyHud(HudStateSnapshot{});
}

void testHotkeyFormat() {
    LHOLO_CHECK(lholo::ui::isModifierKey(VK_CONTROL));
    LHOLO_CHECK(lholo::ui::isModifierKey(VK_MENU));
    LHOLO_CHECK(lholo::ui::isModifierKey(VK_LWIN));
    LHOLO_CHECK(!lholo::ui::isModifierKey('A'));
    // Compare against the table rather than against literals: these assertions
    // cover name formatting, while the wording follows the selected language.
    using lholo::i18n::TextKey;
    LHOLO_CHECK(lholo::ui::hotkeyName(0) == lholo::i18n::tr(TextKey::KeyNotSet));
    LHOLO_CHECK(lholo::ui::hotkeyName(VK_MBUTTON) == lholo::i18n::tr(TextKey::KeyMouseMiddle));
    LHOLO_CHECK(lholo::ui::hotkeyName(VK_XBUTTON1) == lholo::i18n::tr(TextKey::KeyMouseSide1));
    LHOLO_CHECK(lholo::ui::hotkeyName(VK_XBUTTON2) == lholo::i18n::tr(TextKey::KeyMouseSide2));
    LHOLO_CHECK(lholo::ui::hotkeyChordName(0, 0) == lholo::i18n::tr(TextKey::KeyNotSet));
    auto const chord = lholo::ui::hotkeyChordName(lholo::ui::kHotkeyModifierControl, 'M');
    LHOLO_CHECK(chord.rfind("Ctrl + ", 0) == 0);
    LHOLO_CHECK(chord.size() > 7);
    // Match the native keyboard-layout name for extended navigation/numpad
    // keys. Literals would incorrectly tie this check to one system language.
    for (auto const key : {VK_PRIOR, VK_NEXT, VK_END, VK_HOME, VK_INSERT, VK_DIVIDE, VK_NUMLOCK}) {
        auto const scan = MapVirtualKeyW(static_cast<unsigned int>(key), MAPVK_VK_TO_VSC);
        wchar_t expectedWide[128]{};
        auto const length = GetKeyNameTextW(static_cast<LONG>((scan << 16) | (1U << 24)),
            expectedWide, static_cast<int>(std::size(expectedWide)));
        LHOLO_CHECK(length > 0);
        auto const bytes = WideCharToMultiByte(CP_UTF8, 0, expectedWide, length, nullptr, 0, nullptr, nullptr);
        std::string expected(static_cast<std::size_t>((std::max)(0, bytes)), '\0');
        if (bytes > 0) WideCharToMultiByte(CP_UTF8, 0, expectedWide, length, expected.data(), bytes, nullptr, nullptr);
        auto const actual = lholo::ui::hotkeyName(static_cast<unsigned int>(key));
        if (actual != expected) std::fprintf(stderr, "extended key 0x%02X actual='%s' expected='%s'\n",
            key, actual.c_str(), expected.c_str());
        LHOLO_CHECK(bytes > 0 && actual == expected);
    }
}

void testViewMoveBasis() {
    using lholo::input::HotkeyId;
    using lholo::input::viewForwardStep;
    using lholo::input::viewRelativeMoveStep;

    // Facing follows the game's yaw convention: 0 = south (+Z), 90 = west (-X),
    // 180 = north (-Z), -90 = east (+X). Left and right are the facing turned a
    // quarter turn, so facing north puts east on the right and facing east puts
    // south on the right.
    auto const south = viewRelativeMoveStep(HotkeyId::MoveForward, 0.0f);
    LHOLO_CHECK(south.valid && south.dx == 0 && south.dy == 0 && south.dz == 1);
    auto const southBackward = viewRelativeMoveStep(HotkeyId::MoveBackward, 0.0f);
    LHOLO_CHECK(southBackward.valid && southBackward.dz == -1);
    auto const southRight = viewRelativeMoveStep(HotkeyId::MoveRight, 0.0f);
    LHOLO_CHECK(southRight.valid && southRight.dx == -1 && southRight.dz == 0);
    auto const southLeft = viewRelativeMoveStep(HotkeyId::MoveLeft, 0.0f);
    LHOLO_CHECK(southLeft.valid && southLeft.dx == 1 && southLeft.dz == 0);

    auto const north = viewRelativeMoveStep(HotkeyId::MoveForward, 180.0f);
    LHOLO_CHECK(north.valid && north.dx == 0 && north.dz == -1);
    auto const northRight = viewRelativeMoveStep(HotkeyId::MoveRight, 180.0f);
    LHOLO_CHECK(northRight.valid && northRight.dx == 1 && northRight.dz == 0);

    auto const east = viewRelativeMoveStep(HotkeyId::MoveForward, -90.0f);
    LHOLO_CHECK(east.valid && east.dx == 1 && east.dz == 0);
    auto const eastRight = viewRelativeMoveStep(HotkeyId::MoveRight, -90.0f);
    LHOLO_CHECK(eastRight.valid && eastRight.dx == 0 && eastRight.dz == 1);

    auto const west = viewRelativeMoveStep(HotkeyId::MoveForward, 90.0f);
    LHOLO_CHECK(west.valid && west.dx == -1 && west.dz == 0);
    auto const westRight = viewRelativeMoveStep(HotkeyId::MoveRight, 90.0f);
    LHOLO_CHECK(westRight.valid && westRight.dx == 0 && westRight.dz == -1);

    // Only the dominant axis steps: 30 degrees still moves along Z, 60 degrees
    // moves along X, and an exactly diagonal facing resolves to X.
    auto const shallow = viewRelativeMoveStep(HotkeyId::MoveForward, 30.0f);
    LHOLO_CHECK(shallow.valid && shallow.dx == 0 && shallow.dz == 1);
    auto const steep = viewRelativeMoveStep(HotkeyId::MoveForward, 60.0f);
    LHOLO_CHECK(steep.valid && steep.dx == -1 && steep.dz == 0);
    auto const diagonal = viewRelativeMoveStep(HotkeyId::MoveForward, 45.0f);
    LHOLO_CHECK(diagonal.valid && diagonal.dx == -1 && diagonal.dz == 0);
    // Facing north-east (-135) and stepping backward faces south-west, which the
    // dominant-axis rule resolves to west on the X axis.
    auto const diagonalBackward = viewRelativeMoveStep(HotkeyId::MoveBackward, -135.0f);
    LHOLO_CHECK(diagonalBackward.valid && diagonalBackward.dx == -1 && diagonalBackward.dz == 0);

    // The vertical slots stay on the world Y axis whatever the facing is.
    auto const up = viewRelativeMoveStep(HotkeyId::MoveUp, 45.0f);
    LHOLO_CHECK(up.valid && up.dx == 0 && up.dy == 1 && up.dz == 0);
    auto const down = viewRelativeMoveStep(HotkeyId::MoveDown, 203.0f);
    LHOLO_CHECK(down.valid && down.dx == 0 && down.dy == -1 && down.dz == 0);

    // Pitch never participates: every facing yields a usable step, the four
    // horizontal slots stay horizontal and change exactly one coordinate.
    float const yaws[]{0.0f, 45.0f, 90.0f, 135.0f, 180.0f, -135.0f, -90.0f, -45.0f, 359.5f};
    for (auto const yaw : yaws) {
        for (auto const move : {HotkeyId::MoveLeft, HotkeyId::MoveRight,
                                HotkeyId::MoveForward, HotkeyId::MoveBackward}) {
            auto const step = viewRelativeMoveStep(move, yaw);
            LHOLO_CHECK(step.valid);
            LHOLO_CHECK(step.dy == 0);
            LHOLO_CHECK((step.dx != 0) != (step.dz != 0));
        }
        LHOLO_CHECK(viewRelativeMoveStep(HotkeyId::MoveUp, yaw).valid);
        LHOLO_CHECK(viewRelativeMoveStep(HotkeyId::MoveDown, yaw).valid);
    }
    // Slots that are not move slots have no direction to produce.
    LHOLO_CHECK(!viewRelativeMoveStep(HotkeyId::Gui, 0.0f).valid);
    LHOLO_CHECK(!viewRelativeMoveStep(HotkeyId::LayerIncrease, 0.0f).valid);

    // The fixed Alt+wheel gesture keeps its own rule: pitch participates and a
    // diagonal view still produces a diagonal step.
    auto const ahead = viewForwardStep(0.0f, 0.0f, -1.0f, 1);
    LHOLO_CHECK(ahead.valid && ahead.dx == 0 && ahead.dy == 0 && ahead.dz == -1);
    auto const aheadAndDown = viewForwardStep(0.0f, -0.5f, -0.5f, 1);
    LHOLO_CHECK(aheadAndDown.valid && aheadAndDown.dy == -1 && aheadAndDown.dz == -1);
    auto const diagonalWheel = viewForwardStep(0.707f, 0.0f, -0.707f, 1);
    LHOLO_CHECK(diagonalWheel.valid && diagonalWheel.dx == 1 && diagonalWheel.dz == -1);
    auto const twoNotches = viewForwardStep(0.0f, 1.0f, 0.0f, 2);
    LHOLO_CHECK(twoNotches.valid && twoNotches.dy == 2);
    LHOLO_CHECK(!viewForwardStep(0.0f, 0.0f, 0.0f, 1).valid);
    LHOLO_CHECK(!viewForwardStep(0.0f, 0.0f, -1.0f, 0).valid);
    for (auto const invalid : {std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
            (std::numeric_limits<float>::max)()}) {
        LHOLO_CHECK(!viewForwardStep(invalid, 0.f, 0.f, 1).valid);
        LHOLO_CHECK(!viewForwardStep(0.f, invalid, 0.f, 1).valid);
        LHOLO_CHECK(!viewForwardStep(0.f, 0.f, invalid, 1).valid);
    }
    for (auto const yaw : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        LHOLO_CHECK(!viewRelativeMoveStep(HotkeyId::MoveForward, yaw).valid);
        LHOLO_CHECK(!viewRelativeMoveStep(HotkeyId::MoveLeft, yaw).valid);
        LHOLO_CHECK(viewRelativeMoveStep(HotkeyId::MoveUp, yaw).dy == 1);
    }
    auto const minimum = (std::numeric_limits<int>::min)();
    auto const maximum = (std::numeric_limits<int>::max)();
    LHOLO_CHECK(viewForwardStep(1.f, 0.f, 0.f, minimum).dx == minimum);
    LHOLO_CHECK(viewForwardStep(-1.f, 0.f, 0.f, maximum).dx == -maximum);
    LHOLO_CHECK(!viewForwardStep(-1.f, 0.f, 0.f, minimum).valid);
    LHOLO_CHECK(!viewForwardStep(2.f, 0.f, 0.f, maximum).valid);
}

void testHookLifecycle() {
    using namespace lholo::app::hook_lifecycle;

    LHOLO_CHECK(state() == State::Disabled);
    LHOLO_CHECK(beginEnable());
    LHOLO_CHECK(isRunning());

    {
        DetourGuard runningGuard;
        LHOLO_CHECK(static_cast<bool>(runningGuard));

        beginQuiesce();
        LHOLO_CHECK(state() == State::Quiescing);
        LHOLO_CHECK(!isRunning());

        // A nested LHolo detour on the already-admitted thread stays inside
        // the outer lease even after quiescing starts; rejecting it mid-stack
        // could leave the outer callback only partially executed.
        DetourGuard nestedGuard;
        LHOLO_CHECK(static_cast<bool>(nestedGuard));
        LHOLO_CHECK(insideDetour());
    }

    // New top-level entries are origin-only once the admitted outer detour exits,
    // but they still hold a DLL-lifetime lease until their origin call returns.
    {
        DetourGuard blockedAfterQuiesce;
        LHOLO_CHECK(!static_cast<bool>(blockedAfterQuiesce));
        LHOLO_CHECK(insideDetour());
        {
            DetourGuard nestedBlockedAfterQuiesce;
            LHOLO_CHECK(!static_cast<bool>(nestedBlockedAfterQuiesce));
        }
        markDisabled();
        LHOLO_CHECK(state() == State::Quiescing);
    }
    LHOLO_CHECK(!insideDetour());

    waitForQuiescence();
    markDisabled();
    LHOLO_CHECK(state() == State::Disabled);

    // Re-enable after a complete teardown must be valid and deterministic.
    LHOLO_CHECK(beginEnable());
    beginQuiesce();
    waitForQuiescence();
    markDisabled();
    LHOLO_CHECK(state() == State::Disabled);

    // Race fresh admissions against closure, then observe the actual bodies
    // after Running drain. Nested queries must inherit the outer decision in
    // both phases, and the next session must start only after every entry ends.
    for (unsigned round = 0; round < 32; ++round) {
        LHOLO_CHECK(beginEnable());
        std::atomic_uint ready{}, liveBodies{}, mismatches{};
        std::atomic_bool release{}, closed{};
        std::array<std::thread, 4> callbacks;
        for (auto& callback : callbacks) {
            callback = std::thread([&] {
                ready.fetch_add(1, std::memory_order_acq_rel);
                ready.notify_all();
                release.wait(false, std::memory_order_acquire);
                for (unsigned visit = 0; visit < 512; ++visit) {
                    bool const alreadyClosed = closed.load(std::memory_order_acquire);
                    DetourGuard outer;
                    bool const running = static_cast<bool>(outer);
                    if (running) liveBodies.fetch_add(1, std::memory_order_acq_rel);
                    DetourGuard nested;
                    if (static_cast<bool>(nested) != running || !insideDetour()
                        || (alreadyClosed && running)) {
                        mismatches.fetch_add(1, std::memory_order_relaxed);
                    }
                    if (running) liveBodies.fetch_sub(1, std::memory_order_acq_rel);
                }
            });
        }
        for (;;) {
            auto const entered = ready.load(std::memory_order_acquire);
            if (entered == callbacks.size()) break;
            ready.wait(entered, std::memory_order_acquire);
        }
        release.store(true, std::memory_order_release);
        release.notify_all();
        beginQuiesce();
        closed.store(true, std::memory_order_release);
        waitForRunningCallbacks();
        LHOLO_CHECK(liveBodies.load(std::memory_order_acquire) == 0);
        for (auto& callback : callbacks) callback.join();
        waitForQuiescence();
        markDisabled();
        LHOLO_CHECK(mismatches.load(std::memory_order_acquire) == 0);
        LHOLO_CHECK(state() == State::Disabled);
    }
}

void testBlockPlacementRules() {
    using lholo::block::placeableBaseName;
    using lholo::block::materialKey;
    LHOLO_CHECK(placeableBaseName("minecraft:lit_redstone_lamp") == "minecraft:redstone_lamp");
    LHOLO_CHECK(placeableBaseName("minecraft:powered_repeater") == "minecraft:unpowered_repeater");
    LHOLO_CHECK(placeableBaseName("minecraft:stone") == "minecraft:stone");
    LHOLO_CHECK(materialKey("minecraft:powered_repeater") == "item:minecraft:repeater");
    LHOLO_CHECK(materialKey("minecraft:flowing_water") == "minecraft:water");
    LHOLO_CHECK(materialKey("minecraft:moving_block").empty());
}

void testJavaTextComponents() {
    using lholo::structure::detail::javaTextComponentToPlainText;
    LHOLO_CHECK(javaTextComponentToPlainText(R"("Launch")") == "Launch");
    LHOLO_CHECK(javaTextComponentToPlainText(R"({"text":"X","extra":[{"text":" count"}]})") == "X count");
    LHOLO_CHECK(javaTextComponentToPlainText(R"(["A",{"text":"B"}])") == "AB");
    LHOLO_CHECK(javaTextComponentToPlainText(R"({"translate":"block.minecraft.oak_sign"})")
                == "block.minecraft.oak_sign");
    LHOLO_CHECK(javaTextComponentToPlainText("not json") == "not json");
    std::string deepArray(30000, '[');
    deepArray += R"("deep")";
    deepArray.append(30000, ']');
    LHOLO_CHECK(deepArray.size() <= 65535);
    LHOLO_CHECK(javaTextComponentToPlainText(deepArray) == "deep");
    std::string deepExtra;
    for (int depth = 0; depth < 2500; ++depth) deepExtra += R"({"text":"a","extra":[)";
    deepExtra += R"("tail")";
    for (int depth = 0; depth < 2500; ++depth) deepExtra += "]}";
    LHOLO_CHECK(deepExtra.size() <= 65535);
    LHOLO_CHECK(javaTextComponentToPlainText(deepExtra) == std::string(2500, 'a') + "tail");
}

void testI18n() {
    using namespace lholo::i18n;

    // Every locale discovered by the generated registry must parse and cover
    // every key: this is the runtime successor of the old compile-time check.
    initLanguageStore();
    auto const available = languages();
    LHOLO_CHECK(available.size() >= 2);

    auto const chinese = languageFromCode("zh_CN");
    auto const japanese = languageFromCode("ja_JP");
    auto const english = languageFromCode("en_US");
    LHOLO_CHECK(chinese != kInvalidLanguage);
    LHOLO_CHECK(japanese != kInvalidLanguage);
    LHOLO_CHECK(english != kInvalidLanguage);
    LHOLO_CHECK(defaultLanguage() == japanese);
    LHOLO_CHECK(languageFromCode("missing_LOCALE") == kInvalidLanguage);

    for (std::size_t index = 0; index < available.size(); ++index) {
        auto const candidate = static_cast<Language>(index);
        auto const stats = languageStats(candidate);
        LHOLO_CHECK(stats.parsed);
        LHOLO_CHECK(stats.metadataValid);
        LHOLO_CHECK(stats.missing == 0);
        LHOLO_CHECK(stats.unknown == 0);
        LHOLO_CHECK(stats.nonString == 0);
        LHOLO_CHECK(stats.empty == 0);
        LHOLO_CHECK(!available[index].code.empty());
        LHOLO_CHECK(!available[index].displayName.empty());
    }

    // Every key resolves to text in every language; every key except the "no
    // message" sentinel must carry actual wording.
    for (std::size_t index = 0; index < kTextKeyCount; ++index) {
        auto const key = static_cast<TextKey>(index);
        for (std::size_t languageIndex = 0; languageIndex < available.size(); ++languageIndex) {
            auto const candidate = static_cast<Language>(languageIndex);
            auto const* text = tr(key, candidate);
            LHOLO_CHECK(text != nullptr);
            LHOLO_CHECK(key == TextKey::None ? *text == '\0' : *text != '\0');
        }
    }
    // The sentinel is the platform zero value, so a default-constructed message
    // renders as nothing instead of an unrelated entry declared first.
    LHOLO_CHECK(static_cast<std::uint16_t>(TextKey::None) == 0);
    LHOLO_CHECK(format(Message{}) == std::string{});
    // Out-of-range keys resolve to an empty string instead of null or garbage.
    LHOLO_CHECK(tr(static_cast<TextKey>(kTextKeyCount)) != nullptr);
    LHOLO_CHECK(*tr(static_cast<TextKey>(kTextKeyCount)) == '\0');

    // Every language must declare the same placeholders as the default locale.
    // A translation that drops or adds one would consume arguments that are not
    // there (or silently ignore one that is).
    auto const placeholders = [](std::string_view text) {
        std::size_t count = 0;
        for (std::size_t index = 0; index < text.size(); ++index) {
            if (text[index] != '%') continue;
            auto cursor = index + 1;
            while (cursor < text.size()
                   && (std::isdigit(static_cast<unsigned char>(text[cursor]))
                       || text[cursor] == 'l' || text[cursor] == 'h'
                       || text[cursor] == 'z' || text[cursor] == '.'
                       || text[cursor] == '-')) {
                ++cursor;
            }
            if (cursor < text.size()
                && std::string_view{"diufsgxXc"}.find(text[cursor])
                    != std::string_view::npos) {
                ++count;
                index = cursor;
            }
        }
        return count;
    };
    for (std::size_t index = 0; index < kTextKeyCount; ++index) {
        auto const key = static_cast<TextKey>(index);
        auto const expected = placeholders(tr(key, japanese));
        for (std::size_t languageIndex = 0; languageIndex < available.size(); ++languageIndex) {
            LHOLO_CHECK(
                placeholders(tr(key, static_cast<Language>(languageIndex))) == expected
            );
        }
    }

    // Switching the active language by index or stable code changes lookups and
    // is reversible. An unknown code falls back to Japanese in this fork.
    setLanguage(japanese);
    auto const japaneseClose = std::string{tr(TextKey::MenuClose)};
    setLanguage(chinese);
    auto const chineseClose = std::string{tr(TextKey::MenuClose)};
    setLanguage(english);
    auto const englishClose = std::string{tr(TextKey::MenuClose)};
    LHOLO_CHECK(chineseClose != englishClose);
    LHOLO_CHECK(std::string{tr(TextKey::MenuClose, chinese)} == chineseClose);
    LHOLO_CHECK(std::string{tr(TextKey::MenuClose, english)} == englishClose);
    LHOLO_CHECK(setLanguageByCode("en_US"));
    LHOLO_CHECK(language() == english);
    LHOLO_CHECK(!setLanguageByCode("missing_LOCALE"));
    LHOLO_CHECK(language() == japanese);
    LHOLO_CHECK(std::string{tr(TextKey::MenuClose)} == japaneseClose);

    // Language names are shown in their own language, never translated.
    LHOLO_CHECK(std::string{languageName(english)} == available[english].displayName);
    LHOLO_CHECK(std::string{languageName(chinese)} == available[chinese].displayName);
    LHOLO_CHECK(std::string{languageName(japanese)} == available[japanese].displayName);

    // Messages keep their arguments and follow the active language.
    setLanguage(english);
    auto const englishFailure = format(Message{TextKey::StatusLoadFailed, {"boom"}});
    LHOLO_CHECK(englishFailure.find("boom") != std::string::npos);
    setLanguage(chinese);
    auto const chineseFailure = format(Message{TextKey::StatusLoadFailed, {"boom"}});
    LHOLO_CHECK(chineseFailure != englishFailure);
    LHOLO_CHECK(chineseFailure.find("boom") != std::string::npos);
    // A message without arguments renders its pattern unchanged.
    auto const plain = format(Message{TextKey::StatusProjectionClosed});
    LHOLO_CHECK(!plain.empty());
    LHOLO_CHECK(plain == std::string{tr(TextKey::StatusProjectionClosed)});
    // Extra arguments are ignored and a pattern without placeholders is never
    // interpreted as a printf format string.
    LHOLO_CHECK(format(Message{TextKey::StatusProjectionClosed, {"extra"}}) == plain);
}

} // namespace

int main() {
    try {
    lholo::tests::runSchematicChecks([](bool ok) { LHOLO_CHECK(ok); });
    testTransparentQuadSort();
    testLiquidReplayRules();
    testProjectionCoordinateBounds();
    testLoadIntent();
    testStructureTransformConcurrency();
    testRenderCameraRead();
    testProjectionActivationRequests();
    testWorkerTaskBoundary();
    testNativeCallbackBoundary();
    testInitializationTransaction();
    testWorldEventInterest();
    testFutureResult();
    testMaterialHudAvailabilityRevision();
    testMaterialHudPublicationRetirement();
    testMaterialHudImmutableView();
    testCaptureRequestsAndBounds();
    testEpochFailure();
    testImGuiFrameRecovery();
    testSingleTaskWorker();
    testSectionBlockSnapshot();
    testMeshDiagnosticGate();
    lholo::tests::runCompanionCallbackChecks([](bool ok) { LHOLO_CHECK(ok); });
    lholo::tests::runComparisonStyleChecks([](bool ok) { LHOLO_CHECK(ok); });
    lholo::tests::runVerifierHighlightChecks([](bool ok) { LHOLO_CHECK(ok); });
    lholo::tests::runManualPlacementChecks([](bool ok) { LHOLO_CHECK(ok); });
    testNativeLiquidUvRemap();
    testPraxisCompatLiquidColor();
    testNativeLiquidInternalFaceCull();
    testLayoutRules();
    testProgress();
    testSettingsStore();
    testComparisonSettings();
    testSettingsCurrentKeyPriority();
    testAtomicOutput();
    testStructureSession();
    testPlacementState();
    testStructureUiState();
    testHotkeyFormat();
    testViewMoveBasis();
    testHookLifecycle();
    testBlockPlacementRules();
    testJavaTextComponents();
    testI18n();
    lholo::tests::runHudControlChecks([](bool ok) { LHOLO_CHECK(ok); });
    std::printf("LHoloLogicTests: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
    } catch (std::exception const& exception) {
        std::fprintf(stderr, "LHoloLogicTests unhandled test error after %d checks: %s\n", gChecks, exception.what());
        return 1;
    }
}
