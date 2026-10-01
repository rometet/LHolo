// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Menu model build/apply. The controller maps session/UI state into the pure
// menu model and writes applied values back through concrete state operations.

#pragma once

#include "ui/LHoloMenu.h"

namespace lholo::ui {

struct MenuModel;
struct MenuActions;

MenuModel buildStructureMenuModel(float effectiveUiScale);

void applyStructureMenuModel(MenuModel const& model, float effectiveUiScale);

MenuActions buildStructureMenuActions(bool& refreshModel, std::uint64_t captureRevision);

void renderStructureMenu();

} // namespace lholo::ui
