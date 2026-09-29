[Reading 14 lines from start (total: 14 lines, 0 remaining)]

// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Stateful LevelRendererPlayer render hooks: hit-select suppression and the
// projection frame entry after vanilla block entities are submitted.

#pragma once

namespace lholo::projection::detail {

bool installProjectionRenderHooks();
bool uninstallProjectionRenderHooks();

} // namespace lholo::projection::detail

[executed on device: ちひろのPC (a22d5426-96cc-488b-9398-cec6fdb0f382)]