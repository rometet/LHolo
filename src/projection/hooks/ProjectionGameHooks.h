[Reading 14 lines from start (total: 14 lines, 0 remaining)]

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

[executed on device: ちひろのPC (a22d5426-96cc-488b-9398-cec6fdb0f382)]