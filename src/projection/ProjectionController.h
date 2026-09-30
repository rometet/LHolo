// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Projection lifecycle controller. Frame orchestration and session state stay
// in runtime/; this type only centralizes hook install/rollback and the
// projection disable entry point used by the app kernel.

#pragma once

namespace lholo::projection::detail {

class ProjectionController {
public:
    bool installHooks();
    bool uninstallHooks();
    void disableProjection();

private:
    ProjectionController() = default;
    friend ProjectionController& projectionController();
};

ProjectionController& projectionController();

} // namespace lholo::projection::detail
