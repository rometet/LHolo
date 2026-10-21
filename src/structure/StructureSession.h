// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Structure-session ownership and synchronization. Callers receive snapshots
// or use concrete operations; the underlying mutex and mutable storage never
// escape this module.

#pragma once

#include "i18n/Message.h"
#include "structure/LayerDisplayTypes.h"
#include "structure/StructureLoader.h"
#include "structure/ActiveProjection.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace lholo::structure::detail {

struct StructureTransformSnapshot {
    int rotation{};
    int mirror{};
    int offsetX{};
    int offsetY{};
    int offsetZ{};
    LayerDisplayMode layerDisplayMode{LayerDisplayMode::All};
    int displayLayer{};
    LayerAxis layerAxis{LayerAxis::Y};
    bool visible{true};
    bool countExtras{true};
    bool operator==(StructureTransformSnapshot const&) const = default;
};

struct SavedProjectionSnapshot {
    bool available{};
    int  anchorX{};
    int  anchorY{};
    int  anchorZ{};
    StructureTransformSnapshot transform;
    std::string                structurePath;
};

struct StructureSessionSnapshot {
    std::shared_ptr<LoadedStructure const> loaded;
    std::string                            status;
    std::string                            lastPath;
    int                                    maxLayerY{};
    int                                    maxLayerX{};
    StructureTransformSnapshot             transform;
    SavedProjectionSnapshot                saved;
    ProjectionRequestStamp                 request;
};

class StructureSession {
public:
    static StructureSession& getInstance();

    StructureSession(StructureSession const&)            = delete;
    StructureSession(StructureSession&&)                 = delete;
    StructureSession& operator=(StructureSession const&) = delete;
    StructureSession& operator=(StructureSession&&)      = delete;

    [[nodiscard]] StructureSessionSnapshot snapshot() const;
    // Verification publishes value-only results while this generation and
    // placement are still current. Lock order is StructureSession -> schematic;
    // the callback must not re-enter this owner or perform native work.
    template<class Publish>
    bool publishVerificationIfCurrent(StructureSessionSnapshot const& expected,Publish&& publish) const {
        std::lock_guard lock(mMutex);
        if(!verificationContextCurrentLocked(expected))return false;
        return publish();
    }
    [[nodiscard]] std::shared_ptr<LoadedStructure const> loaded() const;
    [[nodiscard]] bool hasLoaded() const;
    [[nodiscard]] std::string lastPath() const;

    // Status is stored as a key plus arguments, never as rendered text: the
    // menu renders it every frame, so a language switch applies to messages
    // that were produced before the switch.
    void setStatus(i18n::Message status);
    void setLastPath(std::string path);
    void replaceLoaded(
        std::shared_ptr<LoadedStructure> loaded,
        std::string                      path,
        i18n::Message                    status,
        ProjectionRequestStamp          request = {}
    );
    void clearLoaded(i18n::Message status);

    [[nodiscard]] StructureTransformSnapshot transform() const;
    // Publish one domain placement value; readers cannot see mixed XYZ/layers.
    bool applyTransform(StructureTransformSnapshot const& value);
    [[nodiscard]] bool layerDisplayEnabled() const;
    void resetTransform();
    bool setRotation(int value);
    bool setMirror(int value);
    bool setVisible(bool value);
    bool setCountExtras(bool value);
    bool visible() const { return mVisible.load(std::memory_order_acquire); }
    bool countExtras() const { return mCountExtras.load(std::memory_order_acquire); }
    bool setOffsetX(int value);
    bool setOffsetY(int value);
    bool setOffsetZ(int value);
    bool setLayerDisplayMode(LayerDisplayMode value);
    bool setDisplayLayer(int value);
    bool setLayerAxis(LayerAxis value);
    void adjustOffsets(int deltaX, int deltaY, int deltaZ);
    bool adjustDisplayLayer(int delta);

    [[nodiscard]] SavedProjectionSnapshot savedProjection() const;
    void setSavedProjection(SavedProjectionSnapshot const& saved);
    void refreshSavedTransformIfActive();
    void recordProjectionAnchor(int x, int y, int z);
    bool recordProjectionAnchor(std::shared_ptr<LoadedStructure const> const& expected,std::uint64_t generation,int x,int y,int z);

private:
    StructureSession() = default;

    [[nodiscard]] StructureTransformSnapshot transformRelaxed() const;
    [[nodiscard]] SavedProjectionSnapshot savedProjectionLocked() const;
    void refreshSavedTransformLocked();
    void recordProjectionAnchorLocked(int x,int y,int z);
    [[nodiscard]] bool verificationContextCurrentLocked(StructureSessionSnapshot const& expected) const;

    mutable std::mutex              mMutex;
    std::shared_ptr<LoadedStructure> mLoaded;
    ProjectionRequestStamp          mRequest;
    std::string                      mSavedStructurePath;
    std::string                      mLastPath;
    i18n::Message                    mStatus{i18n::TextKey::StatusNotLoaded};

    std::atomic_bool mVisible{true}, mCountExtras{true};
    std::atomic_bool mSavedVisible{true}, mSavedCountExtras{true};
    std::atomic_int mRotationQuarterTurns{0};
    std::atomic_int mMirrorMode{0};
    std::atomic_int mOffsetX{0};
    std::atomic_int mOffsetY{0};
    std::atomic_int mOffsetZ{0};
    std::atomic_int mLayerDisplayMode{0};
    std::atomic_int mDisplayLayer{0};
    std::atomic_int mLayerAxis{0};

    std::atomic_bool mHasSavedProjection{false};
    std::atomic_int  mSavedAnchorX{0};
    std::atomic_int  mSavedAnchorY{0};
    std::atomic_int  mSavedAnchorZ{0};
    std::atomic_int  mSavedRotation{0};
    std::atomic_int  mSavedMirror{0};
    std::atomic_int  mSavedOffsetX{0};
    std::atomic_int  mSavedOffsetY{0};
    std::atomic_int  mSavedOffsetZ{0};
    std::atomic_int  mSavedLayerDisplayMode{0};
    std::atomic_int  mSavedDisplayLayer{0};
    std::atomic_int  mSavedLayerAxis{0};
};

int maxLayerFor(LoadedStructure const& structure, LayerAxis axis, int rotation = 0);

} // namespace lholo::structure::detail
