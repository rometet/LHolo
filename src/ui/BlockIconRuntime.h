#pragma once
#include "ui/BlockIconStore.h"
namespace lholo::ui::icons {
bool installHooks();
bool uninstallHooks();
// Invoked only by the admitted local-player tick; native references stay on
// that stack, and all published values are independently owned CPU copies.
void tick();
void closeSession();
// UI-owner handoff after native settlement and owned GPU completion.
void startResourceReload(std::uint64_t epoch,std::uint64_t serial,std::vector<Key> keys);
bool resourceReloadReady(std::uint64_t serial);
}
