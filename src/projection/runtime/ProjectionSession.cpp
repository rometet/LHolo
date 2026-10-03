// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/runtime/ProjectionSession.h"

#include <algorithm>

namespace lholo::projection::detail {

ProjectionSession& ProjectionSession::getInstance() {
    static ProjectionSession instance;
    return instance;
}

float ProjectionSession::opacity() const {
    return mOpacity.load(std::memory_order_relaxed);
}

void ProjectionSession::setOpacity(float opacity) {
    mOpacity.store(std::clamp(opacity, 0.0f, 1.0f), std::memory_order_relaxed);
}

float ProjectionSession::correctionFillOpacity() const {
    return mCorrectionFillOpacity.load(std::memory_order_relaxed);
}

void ProjectionSession::setCorrectionFillOpacity(float opacity) {
    mCorrectionFillOpacity.store(std::clamp(opacity, 0.0f, 1.0f), std::memory_order_relaxed);
}

float ProjectionSession::correctionOutlineOpacity() const {
    return mCorrectionOutlineOpacity.load(std::memory_order_relaxed);
}

void ProjectionSession::setCorrectionOutlineOpacity(float opacity) {
    mCorrectionOutlineOpacity.store(std::clamp(opacity, 0.0f, 1.0f), std::memory_order_relaxed);
}

float ProjectionSession::comparisonStrength() const {
    return mComparisonPreferences.strength();
}

void ProjectionSession::setComparisonStrength(float strength) {
    mComparisonPreferences.setStrength(strength);
}

float ProjectionSession::correctionOutlineWidth() const {
    return mComparisonPreferences.outlineWidth();
}

void ProjectionSession::setCorrectionOutlineWidth(float width) {
    mComparisonPreferences.setOutlineWidth(width);
}

bool ProjectionSession::structureBoundsEnabled() const {
    return mStructureBoundsEnabled.load(std::memory_order_relaxed);
}

void ProjectionSession::setStructureBoundsEnabled(bool enabled) {
    mStructureBoundsEnabled.store(enabled, std::memory_order_relaxed);
}

bool ProjectionSession::correctionSeeThrough() const {
    return mCorrectionSeeThrough.load(std::memory_order_relaxed);
}

void ProjectionSession::setCorrectionSeeThrough(bool enabled) {
    mCorrectionSeeThrough.store(enabled, std::memory_order_relaxed);
}

bool ProjectionSession::missingSeeThrough() const {
    return mMissingSeeThrough.load(std::memory_order_relaxed);
}

void ProjectionSession::setMissingSeeThrough(bool enabled) {
    mMissingSeeThrough.store(enabled, std::memory_order_relaxed);
}

std::optional<ProjectionAnchor> ProjectionSession::consumeAnchor() {
    return mActivationRequests.consumeAnchor();
}

void ProjectionSession::requestAnchor(int x, int y, int z) {
    mActivationRequests.requestAnchor(x, y, z);
}

void ProjectionSession::cancelAnchorRequest() {
    mActivationRequests.cancelAnchorRequest();
}

void ProjectionSession::suspendForDimension(
    std::uint64_t structureGeneration,
    int dimensionId,
    ProjectionAnchor anchor
) {
    mActivationRequests.suspendForDimension(structureGeneration, dimensionId, anchor);
}

DimensionActivationStatus ProjectionSession::prepareDimensionActivation(
    std::uint64_t structureGeneration,
    int           dimensionId
) {
    return mActivationRequests.prepareDimensionActivation(structureGeneration, dimensionId);
}

bool ProjectionSession::dimensionSuspended() const {
    return mActivationRequests.dimensionSuspended();
}

void ProjectionSession::cancelDimensionSuspension() {
    mActivationRequests.cancelDimensionSuspension();
}

} // namespace lholo::projection::detail
