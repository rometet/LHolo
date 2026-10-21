#pragma once

#include "structure/PlacementSession.h"
#include <optional>

namespace lholo::structure {
struct ProjectionRequestStamp {
    std::uint64_t serial{}, selectionSerial{}, worldEpoch{}, placementId{};
    bool operator==(ProjectionRequestStamp const&) const = default;
};
struct ActiveProjectionEvent {
    ProjectionRequestStamp request;
    std::uint64_t worldEpoch{}, loadedGeneration{}, activationGeneration{};
    int dimension{};
    std::filesystem::path sourcePath;
    SavedPlacement placement; // Owned transform/origin values, not an engine borrow.
    bool operator==(ActiveProjectionEvent const&) const = default;
};
struct ActiveProjectionBinding {
    ActiveProjectionEvent event;
    std::uint64_t placementId{};
};
inline bool projectionEventCurrent(ActiveProjectionEvent const& event,ProjectionRequestStamp const& request,
    std::uint64_t worldEpoch,int dimension,std::uint64_t generation,std::filesystem::path const& source) {
    return event.request==request && event.worldEpoch==worldEpoch && event.dimension==dimension
        && event.loadedGeneration==generation && event.sourcePath==source;
}
inline bool sameProjectionPlacement(SavedPlacement const& a,SavedPlacement const& b) {
    return a.dimension==b.dimension && a.origin==b.origin && a.rotation==b.rotation && a.mirror==b.mirror
        && a.visible==b.visible && a.countExtras==b.countExtras && a.layerAxis==b.layerAxis
        && a.layerMode==b.layerMode && a.layer==b.layer;
}
inline std::uint64_t projectedPlacementId(PlacementDocument const& document,std::string const& file,
    SavedPlacement const& projected,std::uint64_t requestedId,std::uint64_t lastActiveId) {
    auto matches=[&](SavedPlacement const& p){return p.file==file && sameProjectionPlacement(p,projected);};
    auto byId=[&](std::uint64_t id){for(auto const& p:document.placements)if(p.id==id && matches(p))return id;return std::uint64_t{};};
    if(requestedId)return byId(requestedId);
    if(auto id=byId(document.selected))return id;
    if(auto id=byId(lastActiveId))return id;
    std::uint64_t unique{};
    for(auto const& p:document.placements)if(matches(p)){if(unique)return 0;unique=p.id;}
    return unique; // Ambiguous entries are never selected by vector order/basename.
}
// All operations are called under the schematic value-owner mutex. Publication
// only enqueues one owned value; filesystem/native work is never performed here.
class ActiveProjectionControl {
    std::uint64_t serial_{}, selectionSerial_{};
    ProjectionRequestStamp request_;
    std::optional<ActiveProjectionEvent> pending_;
    std::optional<ActiveProjectionBinding> active_;
    static void advance(std::uint64_t& value) { if(++value==0)++value; }
public:
    ProjectionRequestStamp begin(std::uint64_t worldEpoch,std::uint64_t placementId=0) {
        advance(serial_);pending_.reset(); // Keep the old successful binding until a new commit/unload.
        request_={serial_,selectionSerial_,worldEpoch,placementId};return request_;
    }
    void invalidate(bool explicitSelection=false,bool keepActive=false) {
        advance(serial_);if(explicitSelection)advance(selectionSerial_);
        pending_.reset();if(!keepActive)active_.reset();request_={};
    }
    bool synchronizeWorld(std::uint64_t worldEpoch,int dimension) {
        if(active_ && (active_->event.worldEpoch!=worldEpoch || active_->event.dimension!=dimension))active_.reset();
        if(pending_ && (pending_->worldEpoch!=worldEpoch || pending_->dimension!=dimension))pending_.reset();
        if(request_.worldEpoch && request_.worldEpoch!=worldEpoch){invalidate();return false;}
        return current(request_);
    }
    bool current(ProjectionRequestStamp const& request) const {
        return request.serial && request==request_ && request.serial==serial_ && request.selectionSerial==selectionSerial_;
    }
    bool pendingCurrent(ActiveProjectionEvent const& event) const {
        return current(event.request) && pending_ && *pending_==event;
    }
    void fail(ProjectionRequestStamp const& request) {
        if(current(request))invalidate(false,true); // Retire only the failed request; retain the last success.
    }
    bool publish(ActiveProjectionEvent event) {
        if(!current(event.request) || !event.worldEpoch || !event.loadedGeneration || !event.activationGeneration
            || (event.request.worldEpoch && event.request.worldEpoch!=event.worldEpoch))return false;
        if(active_ && active_->event.request==event.request
            && active_->event.loadedGeneration==event.loadedGeneration)return false;
        if(pending_ && pending_->activationGeneration>=event.activationGeneration)return false;
        pending_=std::move(event);return true;
    }
    std::optional<ActiveProjectionEvent> pending() const { return pending_; }
    std::optional<ActiveProjectionBinding> active() const { return active_; }
    bool bind(ActiveProjectionEvent const& event,std::uint64_t placementId) {
        if(!placementId || !pendingCurrent(event))return false;
        active_=ActiveProjectionBinding{event,placementId};pending_.reset();return true;
    }
    void discard(ActiveProjectionEvent const& event) {
        if(pending_ && *pending_==event)pending_.reset();
    }
};
} // namespace lholo::structure
