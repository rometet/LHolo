#pragma once

// Registers the /lholosync administrator command (§8). Admin-only; diagnostic
// output never leaks file bytes or personal identifiers.

namespace lholo::sync {

[[nodiscard]] bool registerAdminCommands();

} // namespace lholo::sync
