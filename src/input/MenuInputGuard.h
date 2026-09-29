[Reading 26 lines from start (total: 26 lines, 0 remaining)]

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

[executed on device: ちひろのPC (a22d5426-96cc-488b-9398-cec6fdb0f382)]