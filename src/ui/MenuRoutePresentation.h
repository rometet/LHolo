#pragma once
#include "ui/LHoloMenu.h"
namespace lholo::ui {
constexpr MenuPage menuPageForRoute(input::MenuRoute route,MenuPage current) {
    switch(route){
    case input::MenuRoute::Placed:
    case input::MenuRoute::Files:return MenuPage::Schematics;
    case input::MenuRoute::Verification:return MenuPage::Verification;
    case input::MenuRoute::Materials:return MenuPage::Projection;
    case input::MenuRoute::None:return current;
    }
    return current;
}
inline void applyMenuRoutePresentation(MenuModel& model,input::MenuRoute route) {
    model.page=menuPageForRoute(route,model.page);
    model.directMenuRoute=route;
    if(route==input::MenuRoute::Materials)model.materialPopupRequested=true;
}
} // namespace lholo::ui
