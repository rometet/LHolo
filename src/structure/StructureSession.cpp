// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "structure/StructureSession.h"

#include "structure/PlacementTransform.h"
#include <algorithm>
#include <limits>

namespace lholo::structure::detail {
namespace {

bool exchangeIfChanged(std::atomic_int& target, int value) {
    return target.exchange(value, std::memory_order_relaxed) != value;
}

int addClamped(int value, int delta) {
    auto const next = std::clamp(
        static_cast<long long>(value) + static_cast<long long>(delta),
        static_cast<long long>(std::numeric_limits<int>::min()),
        static_cast<long long>(std::numeric_limits<int>::max())
    );
    return static_cast<int>(next);
}

} // namespace

StructureSession& StructureSession::getInstance() {
    static StructureSession instance;
    return instance;
}

StructureTransformSnapshot StructureSession::transformRelaxed() const {
    return {
        mRotationQuarterTurns.load(std::memory_order_relaxed),
        mMirrorMode.load(std::memory_order_relaxed),
        mOffsetX.load(std::memory_order_relaxed),
        mOffsetY.load(std::memory_order_relaxed),
        mOffsetZ.load(std::memory_order_relaxed),
        layerDisplayModeFromInt(mLayerDisplayMode.load(std::memory_order_relaxed)),
        mDisplayLayer.load(std::memory_order_relaxed),
        layerAxisFromInt(mLayerAxis.load(std::memory_order_relaxed)),
        mVisible.load(std::memory_order_relaxed), mCountExtras.load(std::memory_order_relaxed)
    };
}

SavedProjectionSnapshot StructureSession::savedProjectionLocked() const {
    return {
        mHasSavedProjection.load(std::memory_order_acquire),
        mSavedAnchorX.load(std::memory_order_relaxed),
        mSavedAnchorY.load(std::memory_order_relaxed),
        mSavedAnchorZ.load(std::memory_order_relaxed),
        {
            mSavedRotation.load(std::memory_order_relaxed),
            mSavedMirror.load(std::memory_order_relaxed),
            mSavedOffsetX.load(std::memory_order_relaxed),
            mSavedOffsetY.load(std::memory_order_relaxed),
            mSavedOffsetZ.load(std::memory_order_relaxed),
            layerDisplayModeFromInt(mSavedLayerDisplayMode.load(std::memory_order_relaxed)),
            mSavedDisplayLayer.load(std::memory_order_relaxed),
            layerAxisFromInt(mSavedLayerAxis.load(std::memory_order_relaxed)),
            mSavedVisible.load(std::memory_order_relaxed), mSavedCountExtras.load(std::memory_order_relaxed)
        },
        mSavedStructurePath
    };
}

StructureSessionSnapshot StructureSession::snapshot() const {
    std::lock_guard lock(mMutex);
    StructureSessionSnapshot result;
    result.loaded    = mLoaded;
    result.status    = i18n::format(mStatus);
    result.lastPath  = mLastPath;
    result.transform = transformRelaxed();
    result.saved     = savedProjectionLocked();
    if (mLoaded) {
        result.maxLayerY = maxLayerFor(*mLoaded, LayerAxis::Y);
        result.maxLayerX = maxLayerFor(*mLoaded, LayerAxis::X);
    }
    return result;
}

std::shared_ptr<LoadedStructure const> StructureSession::loaded() const {
    std::lock_guard lock(mMutex);
    return mLoaded;
}

bool StructureSession::hasLoaded() const {
    std::lock_guard lock(mMutex);
    return static_cast<bool>(mLoaded);
}

std::string StructureSession::lastPath() const {
    std::lock_guard lock(mMutex);
    return mLastPath;
}

void StructureSession::setStatus(i18n::Message status) {
    std::lock_guard lock(mMutex);
    mStatus = std::move(status);
}

void StructureSession::setLastPath(std::string path) {
    std::lock_guard lock(mMutex);
    mLastPath = std::move(path);
}

void StructureSession::replaceLoaded(
    std::shared_ptr<LoadedStructure> loaded,
    std::string                      path,
    i18n::Message                    status
) {
    std::lock_guard lock(mMutex);
    mLastPath = std::move(path);
    mStatus   = std::move(status);
    mLoaded   = std::move(loaded);
}

void StructureSession::clearLoaded(i18n::Message status) {
    std::lock_guard lock(mMutex);
    // Freeze the last active transform before dropping the structure. Once
    // mLoaded is empty, menu models legitimately clamp their current layer to
    // zero; that transient empty-session value must not replace the restore
    // snapshot.
    refreshSavedTransformLocked();
    mLoaded.reset();
    mStatus = std::move(status);
}

StructureTransformSnapshot StructureSession::transform() const {
    std::lock_guard lock(mMutex);
    return transformRelaxed();
}
bool StructureSession::applyTransform(StructureTransformSnapshot const& value) {
    std::lock_guard lock(mMutex);
    auto next=value;next.rotation&=3;next.mirror=std::clamp(next.mirror,0,2);
    next.layerAxis=layerAxisFromInt(toInt(next.layerAxis));
    next.layerDisplayMode=layerDisplayModeFromInt(toInt(next.layerDisplayMode));
    next.displayLayer=std::max(0,next.displayLayer);
    if(transformRelaxed()==next)return false;
    mRotationQuarterTurns.store(next.rotation,std::memory_order_relaxed);
    mMirrorMode.store(next.mirror,std::memory_order_relaxed);
    mOffsetX.store(next.offsetX,std::memory_order_relaxed);
    mOffsetY.store(next.offsetY,std::memory_order_relaxed);
    mOffsetZ.store(next.offsetZ,std::memory_order_relaxed);
    mLayerDisplayMode.store(toInt(next.layerDisplayMode),std::memory_order_relaxed);
    mDisplayLayer.store(next.displayLayer,std::memory_order_relaxed);
    mLayerAxis.store(toInt(next.layerAxis),std::memory_order_relaxed);
    mVisible.store(next.visible,std::memory_order_relaxed);
    mCountExtras.store(next.countExtras,std::memory_order_relaxed);
    return true;
}

bool StructureSession::layerDisplayEnabled() const {
    return layerDisplayModeFromInt(mLayerDisplayMode.load(std::memory_order_acquire))
        != LayerDisplayMode::All;
}

void StructureSession::resetTransform() {
    std::lock_guard lock(mMutex);
    mVisible.store(true, std::memory_order_relaxed);
    mCountExtras.store(true, std::memory_order_relaxed);
    mRotationQuarterTurns.store(0, std::memory_order_relaxed);
    mMirrorMode.store(0, std::memory_order_relaxed);
    mOffsetX.store(0, std::memory_order_relaxed);
    mOffsetY.store(0, std::memory_order_relaxed);
    mOffsetZ.store(0, std::memory_order_relaxed);
    mLayerDisplayMode.store(0, std::memory_order_relaxed);
    mDisplayLayer.store(0, std::memory_order_relaxed);
    mLayerAxis.store(0, std::memory_order_relaxed);
}

bool StructureSession::setVisible(bool value) { std::lock_guard lock(mMutex); return mVisible.exchange(value) != value; }
bool StructureSession::setCountExtras(bool value) { std::lock_guard lock(mMutex); return mCountExtras.exchange(value) != value; }

bool StructureSession::setRotation(int value) { std::lock_guard lock(mMutex); return exchangeIfChanged(mRotationQuarterTurns, value & 3); }
bool StructureSession::setMirror(int value) { std::lock_guard lock(mMutex); return exchangeIfChanged(mMirrorMode, std::clamp(value, 0, 2)); }
bool StructureSession::setOffsetX(int value) { std::lock_guard lock(mMutex); return exchangeIfChanged(mOffsetX, value); }
bool StructureSession::setOffsetY(int value) { std::lock_guard lock(mMutex); return exchangeIfChanged(mOffsetY, value); }
bool StructureSession::setOffsetZ(int value) { std::lock_guard lock(mMutex); return exchangeIfChanged(mOffsetZ, value); }
bool StructureSession::setLayerDisplayMode(LayerDisplayMode value) {
    std::lock_guard lock(mMutex);
    return exchangeIfChanged(mLayerDisplayMode, toInt(value));
}
bool StructureSession::setDisplayLayer(int value) { std::lock_guard lock(mMutex); return exchangeIfChanged(mDisplayLayer, value); }
bool StructureSession::setLayerAxis(LayerAxis value) {
    std::lock_guard lock(mMutex);
    return exchangeIfChanged(mLayerAxis, toInt(value));
}

void StructureSession::adjustOffsets(int deltaX, int deltaY, int deltaZ) {
    std::lock_guard lock(mMutex);
    if (deltaX != 0) mOffsetX.store(addClamped(mOffsetX.load(std::memory_order_relaxed), deltaX), std::memory_order_relaxed);
    if (deltaY != 0) mOffsetY.store(addClamped(mOffsetY.load(std::memory_order_relaxed), deltaY), std::memory_order_relaxed);
    if (deltaZ != 0) mOffsetZ.store(addClamped(mOffsetZ.load(std::memory_order_relaxed), deltaZ), std::memory_order_relaxed);
}

bool StructureSession::adjustDisplayLayer(int delta) {
    std::lock_guard lock(mMutex);
    if (delta == 0 || layerDisplayModeFromInt(mLayerDisplayMode.load(std::memory_order_relaxed))
        == LayerDisplayMode::All) return false;
    auto const axis = layerAxisFromInt(mLayerAxis.load(std::memory_order_relaxed));
    auto       maxLayer = 0;
    {
        if (mLoaded) {
            maxLayer = axis == LayerAxis::Material
                ? static_cast<int>(std::min<std::uint64_t>(
                      mLoaded->materialCount,
                      static_cast<std::uint64_t>(std::numeric_limits<int>::max())
                  ))
                : maxLayerFor(*mLoaded, axis, mRotationQuarterTurns.load(std::memory_order_relaxed));
            if (axis == LayerAxis::Material && maxLayer > 0) --maxLayer;
        }
    }
    auto const current = static_cast<long long>(mDisplayLayer.load(std::memory_order_relaxed));
    auto const next = std::clamp(current + static_cast<long long>(delta), 0LL, static_cast<long long>(maxLayer));
    mDisplayLayer.store(static_cast<int>(next), std::memory_order_relaxed);
    return true;
}

SavedProjectionSnapshot StructureSession::savedProjection() const {
    std::lock_guard lock(mMutex);
    return savedProjectionLocked();
}

void StructureSession::setSavedProjection(SavedProjectionSnapshot const& saved) {
    std::lock_guard lock(mMutex);
    mHasSavedProjection.store(false, std::memory_order_relaxed);
    mSavedAnchorX.store(saved.anchorX, std::memory_order_relaxed);
    mSavedAnchorY.store(saved.anchorY, std::memory_order_relaxed);
    mSavedAnchorZ.store(saved.anchorZ, std::memory_order_relaxed);
    mSavedRotation.store(saved.transform.rotation & 3, std::memory_order_relaxed);
    mSavedMirror.store(std::clamp(saved.transform.mirror, 0, 2), std::memory_order_relaxed);
    mSavedVisible.store(saved.transform.visible); mSavedCountExtras.store(saved.transform.countExtras);
    mSavedOffsetX.store(saved.transform.offsetX, std::memory_order_relaxed);
    mSavedOffsetY.store(saved.transform.offsetY, std::memory_order_relaxed);
    mSavedOffsetZ.store(saved.transform.offsetZ, std::memory_order_relaxed);
    mSavedLayerDisplayMode.store(toInt(saved.transform.layerDisplayMode), std::memory_order_relaxed);
    mSavedDisplayLayer.store(saved.transform.displayLayer, std::memory_order_relaxed);
    mSavedLayerAxis.store(toInt(saved.transform.layerAxis), std::memory_order_relaxed);
    mSavedStructurePath = saved.structurePath;
    mHasSavedProjection.store(saved.available, std::memory_order_release);
}

void StructureSession::refreshSavedTransformIfActive() {
    std::lock_guard lock(mMutex);
    refreshSavedTransformLocked();
}

void StructureSession::refreshSavedTransformLocked() {
    if (!mLoaded || !mHasSavedProjection.load(std::memory_order_acquire)) return;
    auto const current = transformRelaxed();
    mSavedRotation.store(current.rotation, std::memory_order_relaxed);
    mSavedMirror.store(current.mirror, std::memory_order_relaxed);
    mSavedVisible.store(current.visible); mSavedCountExtras.store(current.countExtras);
    mSavedOffsetX.store(current.offsetX, std::memory_order_relaxed);
    mSavedOffsetY.store(current.offsetY, std::memory_order_relaxed);
    mSavedOffsetZ.store(current.offsetZ, std::memory_order_relaxed);
    mSavedLayerDisplayMode.store(toInt(current.layerDisplayMode), std::memory_order_relaxed);
    mSavedDisplayLayer.store(current.displayLayer, std::memory_order_relaxed);
    mSavedLayerAxis.store(toInt(current.layerAxis), std::memory_order_relaxed);
}

void StructureSession::recordProjectionAnchor(int x, int y, int z) {
    std::lock_guard lock(mMutex);
    mHasSavedProjection.store(false, std::memory_order_relaxed);
    auto const current = transformRelaxed();
    mSavedAnchorX.store(x, std::memory_order_relaxed);
    mSavedAnchorY.store(y, std::memory_order_relaxed);
    mSavedAnchorZ.store(z, std::memory_order_relaxed);
    mSavedRotation.store(current.rotation, std::memory_order_relaxed);
    mSavedMirror.store(current.mirror, std::memory_order_relaxed);
    mSavedVisible.store(current.visible); mSavedCountExtras.store(current.countExtras);
    mSavedOffsetX.store(current.offsetX, std::memory_order_relaxed);
    mSavedOffsetY.store(current.offsetY, std::memory_order_relaxed);
    mSavedOffsetZ.store(current.offsetZ, std::memory_order_relaxed);
    mSavedLayerDisplayMode.store(toInt(current.layerDisplayMode), std::memory_order_relaxed);
    mSavedDisplayLayer.store(current.displayLayer, std::memory_order_relaxed);
    mSavedLayerAxis.store(toInt(current.layerAxis), std::memory_order_relaxed);
    mSavedStructurePath = mLastPath;
    mHasSavedProjection.store(true, std::memory_order_release);
}

int maxLayerFor(LoadedStructure const& structure, LayerAxis axis, int rotation) {
    auto const size = PlacementTransform{{structure.sizeX,structure.sizeY,structure.sizeZ},{},rotation,0}.placedSize();
    return std::max(0, (axis == LayerAxis::X ? structure.sizeX : layerCount(size,axis)) - 1);
}

} // namespace lholo::structure::detail
