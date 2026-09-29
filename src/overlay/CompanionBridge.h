#pragma once

#include <cstddef>

namespace lholo::overlay::companion {

using DrawFn = void (*)(void*) noexcept;
using StateFn = void (*)(bool) noexcept;
using HudNeededFn = bool (*)() noexcept;

bool isRegistered() noexcept;
bool isVisible() noexcept;
bool hudNeeded() noexcept;
bool inputCaptured() noexcept;

bool handleHotkeyKeyDown(unsigned int key, bool repeated) noexcept;
bool handleHotkeyKeyUp(unsigned int key) noexcept;
void close() noexcept;

void drawGui(void* imguiContext) noexcept;
void drawHud(void* imguiContext) noexcept;
void shutdown() noexcept;

} // namespace lholo::overlay::companion
