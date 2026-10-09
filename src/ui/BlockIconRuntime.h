#pragma once
namespace lholo::ui::icons {
bool installHooks();
bool uninstallHooks();
// Invoked only by the admitted local-player tick; native references stay on
// that stack, and all published values are independently owned CPU copies.
void tick();
void closeSession();
}
