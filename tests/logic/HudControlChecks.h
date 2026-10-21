#pragma once
#include "ui/HudSummaryPolicy.h"
#include "ui/HudLayout.h"
#include "settings/SettingsStore.h"
#include "structure/StructureUiState.h"
#include <fstream>
#include <limits>
namespace lholo::tests {
template<class Check> void runHudControlChecks(Check check) {
    auto unknown=ui::hudProgress(0,0);check(!unknown.ratio);
    auto progress=ui::hudProgress(3,10);check(progress.ratio&&std::abs(*progress.ratio-.3f)<.00001f&&progress.remaining==7);
    auto completed=ui::hudProgress(12,10);check(completed.ratio&&*completed.ratio==1&&completed.remaining==0);
    ui::MissingTotal total;total.add(12);total.add(30);check(total.exact&&total.value==42);
    total.add((std::numeric_limits<std::uint64_t>::max)());check(!total.exact&&total.value==42);
    auto& state=structure::detail::StructureUiState::getInstance();
    ui::HudLayout custom{true,28,-12,.5f,8};state.setHudLayout(0,custom);check(state.hudLayout(0)==custom);
    auto invalid=custom;invalid.scale=std::numeric_limits<float>::infinity();state.setHudLayout(0,invalid);check(state.hudLayout(0)==custom);
    state.setHudLayout(0,{});check(!state.hudLayout(0).custom);
    auto const path=std::filesystem::temp_directory_path()/"lholo-hud-contract-test.json";
    settings::Settings source;source.hudLayouts[0]=custom;source.hudPosition=2;source.guiHotkey='Q';source.guiHotkeyModifiers=3;
    settings::saveSettingsFile(path,source);
    settings::Settings loaded;check(settings::loadSettingsFile(path,loaded));
    check(loaded.hudLayouts==source.hudLayouts&&loaded.hudPosition==2&&loaded.guiHotkey=='Q'&&loaded.guiHotkeyModifiers==3);
    {std::ofstream out(path);out<<"{\"hudPosition\":3,\"hudLayouts\":[{\"custom\":true,\"scale\":99},{}]}";}
    loaded={};check(settings::loadSettingsFile(path,loaded));check(!loaded.hudLayouts[0].custom&&loaded.hudPosition==3);
    {std::ofstream out(path);out<<"{\"hudPosition\":2,\"guiHotkey\":85}";}
    loaded={};check(settings::loadSettingsFile(path,loaded));check(!loaded.hudLayouts[0].custom&&loaded.hudPosition==2&&loaded.guiHotkey==85);
    std::error_code ignored;std::filesystem::remove(path,ignored);
}
}
