// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Minecraft interface hooks that do not depend on active ProjectionState:
// tessellation virtual-world queries and the client-side /lholo command.

#pragma once

namespace lholo::projection::detail {

bool installProjectionGameHooks();
bool uninstallProjectionGameHooks();

} // namespace lholo::projection::detail
