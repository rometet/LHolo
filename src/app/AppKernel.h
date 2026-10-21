// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Single application entry point. The plugin facade only delegates here;
// AppKernel owns the enable/disable/load orchestration order.

#pragma once

namespace lholo::app {

class AppKernel {
public:
    static AppKernel& getInstance();

    AppKernel(AppKernel const&) = delete;
    AppKernel& operator=(AppKernel const&) = delete;

    bool load();
    bool enable();
    bool disable();
    bool unload();

private:
    AppKernel() = default;
};

} // namespace lholo::app
