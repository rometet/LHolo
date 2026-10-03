#pragma once

#include "structure/Verification.h"
#include <span>

namespace lholo::structure::schematic {
// Values only: neither the UI nor the renderer retains a native block pointer.
struct ReportStamp {
    std::uint64_t worldEpoch{};
    int dimension{};
    std::uint64_t sessionRevision{}, placementId{}, loadedGeneration{}, reportRevision{};
    Cell placementOrigin;
    int placementRotation{}, placementMirror{};
    LayerAxis layerAxis{LayerAxis::Y};
    LayerDisplayMode layerMode{LayerDisplayMode::All};
    int layer{};
    bool visible{true}, countExtras{true};
    std::uint64_t filterRevision{};
    bool operator==(ReportStamp const&) const = default;
    bool valid() const { return placementId && loadedGeneration && reportRevision; }
    bool sameContext(ReportStamp const& other) const {
        auto a=*this,b=other;
        a.reportRevision=b.reportRevision=0;
        return a==b;
    }
};
struct SelectedMistake {
    ReportStamp stamp;
    std::size_t index{};
    Mismatch mismatch;
};
inline bool sameMismatch(Mismatch const& a,Mismatch const& b) {
    return a.kind==b.kind && a.world==b.world && a.expected==b.expected && a.actual==b.actual
        && a.actualLiquid==b.actualLiquid && a.actualExtra==b.actualExtra;
}

// The same pure policy is used by the production runtime and logic checks.
// Context invalidation is explicit; report replacement never reuses a bare row index.
class MistakeSelection {
    MistakeFilter filter_{MistakeFilter::Mistakes};
    std::optional<SelectedMistake> selected_;
public:
    MistakeFilter filter() const { return filter_; }
    void clear() { selected_.reset(); }
    bool clearIfCurrent(SelectedMistake const& checked) {
        if(!selected_ || selected_->stamp!=checked.stamp || selected_->index!=checked.index
            || !sameMismatch(selected_->mismatch,checked.mismatch))return false;
        clear();return true;
    }
    bool setFilter(MistakeFilter filter) {
        if(!validMistakeFilter(filter))return false;
        if(filter_!=filter){filter_=filter;clear();}
        return true;
    }
    bool select(ReportStamp const& requested,ReportStamp const& current,bool running,
        std::span<Mismatch const> rows,std::size_t index) {
        if(running || !current.valid() || requested!=current || index>=rows.size()
            || !matchesFilter(rows[index].kind,filter_))return false;
        selected_=SelectedMistake{current,index,rows[index]};
        return true;
    }
    std::optional<SelectedMistake> target(ReportStamp const& context) const {
        if(!selected_ || !context.valid() || !selected_->stamp.sameContext(context))return {};
        return selected_;
    }
    void reconcile(ReportStamp const& current,std::span<Mismatch const> rows) {
        if(!selected_)return;
        if(!current.valid() || !selected_->stamp.sameContext(current)){clear();return;}
        for(std::size_t i=0;i<rows.size();++i){
            if(sameMismatch(selected_->mismatch,rows[i]) && matchesFilter(rows[i].kind,filter_)){
                selected_=SelectedMistake{current,i,rows[i]};return;
            }
        }
        clear(); // Resolved, changed or no longer in the bounded current result.
    }
    bool cycle(ReportStamp const& current,bool running,std::span<Mismatch const> rows,MistakeFilter filter) {
        if(!setFilter(filter) || running || !current.valid())return false;
        std::optional<std::size_t> previous;
        if(selected_ && selected_->stamp==current)previous=selected_->index;
        auto const next=nextMistake(rows,filter_,previous);
        if(!next){clear();return false;}
        return select(current,current,false,rows,*next);
    }
};
} // namespace lholo::structure::schematic
