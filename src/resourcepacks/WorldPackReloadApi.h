#pragma once
#include <cstdint>
#include <type_traits>

namespace praxis::companion::packs {
// Prepared Windows participant ABI, not an implemented provider or admission
// grant. Both sides must use this version/layout and retain the enabled module.
inline constexpr std::uint32_t WorldPackReloadAbi=4;
enum class WorldPackReloadCommand : std::uint32_t {
    Query=0, Pause=1, Prepare=2, BeginNative=3, NativeSettled=4, Resume=5,
    CancelBeforeNative=6, ResumeCancelled=7, Stop=8
};
enum class WorldPackReloadPhase : std::uint32_t {
    Unknown=0, Idle, Pausing, Preparing, Prepared, NativeActive, Reacquiring,
    Ready, Cancelled, Invalidated
};
// NativeManaged is a lifetime policy for normal engine service handles, NOT
// GPU completion. Private D3D resources still require an actual fence receipt.
enum class OwnedGpuFence : std::uint32_t {Unknown=0, Pending, Complete, NoOwnedSubmission, NativeManaged};
struct WorldPackReloadRequest {
    std::uint32_t bytes{sizeof(WorldPackReloadRequest)}, abi{WorldPackReloadAbi};
    std::uint64_t token{}, world{}, client{}, renderer{}, oldEpoch{}, nextEpoch{};
};
struct WorldPackReloadReply {
    std::uint32_t bytes{sizeof(WorldPackReloadReply)}, abi{WorldPackReloadAbi};
    std::uint64_t token{}, world{}, client{}, renderer{}, epoch{};
    WorldPackReloadPhase phase{WorldPackReloadPhase::Unknown};
    OwnedGpuFence ownedGpu{OwnedGpuFence::Unknown};
    std::uint32_t cpuReaders{}, asyncJobs{}, nativeBorrowers{}, admissionClosed{}, oldOwnersRetained{}, error{};
    std::uint64_t rejectedOldPublications{};
};
using WorldPackReloadParticipant=int(*)(WorldPackReloadCommand,WorldPackReloadRequest const*,WorldPackReloadReply*) noexcept;
static_assert(std::is_standard_layout_v<WorldPackReloadRequest>&&std::is_trivially_copyable_v<WorldPackReloadRequest>);
static_assert(std::is_standard_layout_v<WorldPackReloadReply>&&std::is_trivially_copyable_v<WorldPackReloadReply>);
// Versioned exports: praxis_resource_reload_v4 / lholo_resource_reload_v4.
// v3 cannot opt into engine-managed lifetime semantics and is rejected.
// Unknown/default replies never mean Prepared/Ready. CPU reader zero is not a
// GPU fence; old native owners are retained through ambiguity and world/stop.
}
