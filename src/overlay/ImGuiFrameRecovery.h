#pragma once

#include <imgui.h>
#include <imgui_internal.h>

namespace lholo::overlay::detail {

// Construct after NewFrame. An exception in the UI must close its window,
// table and style stacks before the next NewFrame. Recovery is provided by the
// pinned ImGui version; suppress its interactive assertions only while unwinding.
class ImGuiFrameRecovery final {
public:
    ImGuiFrameRecovery() : mContext(ImGui::GetCurrentContext()) {
        ImGui::ErrorRecoveryStoreState(&mState);
    }
    ~ImGuiFrameRecovery() noexcept {
        if (!mContext->WithinFrameScope) return; // Render already ended the frame.
        ImGui::SetCurrentContext(mContext);
        auto& io = mContext->IO;
        bool const recovery = io.ConfigErrorRecovery;
        bool const asserts = io.ConfigErrorRecoveryEnableAssert;
        bool const debugLog = io.ConfigErrorRecoveryEnableDebugLog;
        bool const tooltip = io.ConfigErrorRecoveryEnableTooltip;
        io.ConfigErrorRecovery = true;
        io.ConfigErrorRecoveryEnableAssert = false;
        io.ConfigErrorRecoveryEnableDebugLog = false;
        io.ConfigErrorRecoveryEnableTooltip = false;
        ImGui::ErrorRecoveryTryToRecoverState(&mState);
        ImGui::EndFrame();
        io.ConfigErrorRecovery = recovery;
        io.ConfigErrorRecoveryEnableAssert = asserts;
        io.ConfigErrorRecoveryEnableDebugLog = debugLog;
        io.ConfigErrorRecoveryEnableTooltip = tooltip;
    }
    ImGuiFrameRecovery(ImGuiFrameRecovery const&) = delete;
    ImGuiFrameRecovery& operator=(ImGuiFrameRecovery const&) = delete;
private:
    ImGuiContext* mContext;
    ImGuiErrorRecoveryState mState{};
};

} // namespace lholo::overlay::detail
