#include "app/NativeCallbackBoundary.h"
#include "plugin/LHolo.h"
#include "ll/api/mod/NativeMod.h"
#include <Windows.h>

namespace lholo::app {
void reportNativeCallbackFailure(char const* context, char const* reason) noexcept {
    try {
        LHolo::getInstance().getSelf().getLogger().error("LHolo {} failed: {}", context, reason);
    } catch (...) {
        // Error reporting itself can allocate. Keep this final diagnostic safe
        // even during allocation failure; never unwind through the game ABI.
        OutputDebugStringA("LHolo native callback failed; error logging also failed.\n");
    }
}
} // namespace lholo::app
