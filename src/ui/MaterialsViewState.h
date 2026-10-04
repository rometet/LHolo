#pragma once
#include "structure/MaterialList.h"
#include <memory>
namespace lholo::ui {
struct MaterialsViewState {
    structure::detail::MaterialFilter filter{};
    std::shared_ptr<structure::detail::MaterialListSnapshot const> cached;
    structure::detail::MaterialFilter cachedFilter{};
    std::vector<std::size_t> visible;
    structure::detail::MaterialListSummary summary;
    std::size_t rebuilds{}, drawnRows{};
    void update(std::shared_ptr<structure::detail::MaterialListSnapshot const> const& snapshot) {
        if (cached==snapshot && cachedFilter==filter) return;
        if (cached && (!snapshot || cached->scope!=snapshot->scope)) filter=structure::detail::MaterialFilter::All;
        cached=snapshot;cachedFilter=filter;visible.clear();summary={};++rebuilds;
        if (!snapshot) return;
        summary=structure::detail::summarizeMaterials(*snapshot);
        for(std::size_t i=0;i<snapshot->requirements.size();++i) if(snapshot->matches(i,filter))visible.push_back(i);
    }
};
}
