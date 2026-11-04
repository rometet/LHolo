// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#pragma once
#include <cstdint>

namespace lholo::input {

struct MenuInputGuardStatus {
    bool mouseInputHookInstalled{};
    bool keyDownInputHookInstalled{};
    bool keyUpInputHookInstalled{};
    bool textInputHooksInstalled{};
};

class MenuInputHandoffScope final {
public:
    MenuInputHandoffScope();
    ~MenuInputHandoffScope();

    MenuInputHandoffScope(MenuInputHandoffScope const&) = delete;
    MenuInputHandoffScope& operator=(MenuInputHandoffScope const&) = delete;
};

void observeNativeWindowMessage(unsigned int message, std::uintptr_t wParam) noexcept;
MenuInputGuardStatus installMenuInputGuard();
bool uninstallMenuInputGuard();

} // namespace lholo::input
