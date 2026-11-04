#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Client-side transport adapter: registers the shared SyncFramePacket handler,
// wires client world/tick events to the SyncClient session machine, and provides
// the single send entry point. All sends happen on the game thread.

namespace lholo::sync {

[[nodiscard]] bool installClientPacketAdapter();
void uninstallClientPacketAdapter() noexcept;

// Sends one already-encoded LHS1 frame to the server. Returns false if the
// transport is unavailable.
[[nodiscard]] bool sendFrameToServer(std::vector<std::uint8_t> const& frame);

// Logging helpers bound to the LHolo mod logger.
void syncLogInfo(std::string const& message);
void syncLogWarn(std::string const& message);
void syncLogError(std::string const& message);

} // namespace lholo::sync
