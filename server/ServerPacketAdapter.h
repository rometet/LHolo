#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mc/network/NetworkIdentifier.h"

// Server-side transport adapter (§3.2): the receive handler only validates,
// copies and enqueues frames; the SyncService drains them on the game thread.
// All sends to a client go through sendFrameToClient().

namespace lholo::sync {

[[nodiscard]] bool installServerPacketAdapter();
void uninstallServerPacketAdapter() noexcept;

[[nodiscard]] bool sendFrameToClient(NetworkIdentifier const& target, std::vector<std::uint8_t> const& frame);

void serverLogInfo(std::string const& message);
void serverLogWarn(std::string const& message);
void serverLogError(std::string const& message);

} // namespace lholo::sync
