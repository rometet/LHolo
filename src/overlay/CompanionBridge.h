#pragma once

#include <cstddef>
#include <cstdint>

namespace lholo::overlay::companion {

using DrawFn = void (*)(void*) noexcept;
using StateFn = void (*)(bool) noexcept;
using HudNeededFn = bool (*)() noexcept;
using RenderV3Fn = void (*)(void* device, void* deviceContext, void* window, bool guiVisible) noexcept;
using WindowMessageV3Fn = void (*)(void* window, unsigned message, std::uintptr_t wParam, std::intptr_t lParam) noexcept;
using GraphicsResetV3Fn = void (*)() noexcept;

bool isRegistered() noexcept;
bool isVisible() noexcept;
bool hudNeeded() noexcept;
bool inputCaptured() noexcept;
bool usesIndependentRenderer() noexcept;

bool handleHotkeyKeyDown(unsigned int key, bool repeated) noexcept;
bool handleHotkeyKeyUp(unsigned int key) noexcept;
void close() noexcept;

void drawGui(void* imguiContext) noexcept;
void drawHud(void* imguiContext) noexcept;
void renderIndependent(void* device, void* deviceContext, void* window, bool guiVisible) noexcept;
void forwardWindowMessage(void* window, unsigned message, std::uintptr_t wParam, std::intptr_t lParam) noexcept;
void resetGraphics() noexcept;
bool beginSession() noexcept;
bool shutdown() noexcept;

} // namespace lholo::overlay::companion
