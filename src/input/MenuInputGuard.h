// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#pragma once

namespace lholo::input {

struct MenuInputGuardStatus {
    bool mouseInputHookInstalled{};
    bool keyDownInputHookInstalled{};
    bool keyUpInputHookInstalled{};
};

class MenuInputHandoffScope final {
public:
    MenuInputHandoffScope();
    ~MenuInputHandoffScope();

    MenuInputHandoffScope(MenuInputHandoffScope const&) = delete;
    MenuInputHandoffScope& operator=(MenuInputHandoffScope const&) = delete;
};

MenuInputGuardStatus installMenuInputGuard();
bool uninstallMenuInputGuard();

} // namespace lholo::input
