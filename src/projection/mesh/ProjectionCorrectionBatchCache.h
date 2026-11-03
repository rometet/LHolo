#pragma once

// Render-thread-only, section-revision-aware batching of the two high-volume
// Missing correction overlays. The original per-section GPU meshes always
// remain available for frustum-edge, resource-failure and dynamic fallbacks.
#include "projection/mesh/ProjectionCorrectionBatchPolicy.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "mc/deps/minecraft_renderer/renderer/MeshData.h"
#include "mc/deps/minecraft_renderer/resources/BufferResourceService.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace lholo::projection::detail {

constexpr std::size_t kCorrectionBatchMaxVertices = 400000;
constexpr std::size_t kCorrectionBatchMaxStoredVertices = 3500000;

class ProjectionCorrectionBatchCache {
public:
    enum Kind : std::size_t { MissingFill, MissingOutline, KindCount };
    enum class Status : unsigned char { Unbuilt, Ready, Unsupported };

    void clear() {
        mSources.clear();
        mGroups.clear();
        mLastUpload = {};
        mBuildCursor = 0;
        mBuilt = 0;
        mStoredVertices = 0;
    }

    // Called only after a worker result's matching section/revision is
    // accepted. Copy the CPU stream before the existing GPU upload consumes it.
    void noteUploaded(
        std::size_t section, std::size_t sectionCount, Kind kind,
        std::unique_ptr<mce::MeshData> cpuData
    ) {
        if (section >= sectionCount || kind >= KindCount) return;
        if (mSources.size() != sectionCount) {
            mSources.resize(sectionCount);
            mGroups.resize((sectionCount + kCorrectionBatchSize - 1) / kCorrectionBatchSize);
            mBuildCursor = 0;
        }
        auto& slot = mSources[section][kind];
        if (slot.source) mStoredVertices -= slot.source->mPositions.get().size();
        slot.source.reset();
        slot.known = true;
        if (cpuData && cpuData->size() != 0) {
            auto const& data = *cpuData;
            // Avoid retaining arbitrarily large CPU copies on extreme
            // schematics. This group will stay on original section rendering.
            auto const vertexCount = data.mPositions.get().size();
            if (vertexCount > kCorrectionBatchMaxVertices
                || vertexCount > kCorrectionBatchMaxStoredVertices - mStoredVertices) {
                slot.known = false;
            } else {
                slot.source = std::move(cpuData);
                mStoredVertices += vertexCount;
            }
        }
        auto const group = section / kCorrectionBatchSize;
        auto& batch = mGroups[group][kind];
        batch.mesh.reset();
        batch.status = Status::Unbuilt;
        mBuildCursor = std::min(mBuildCursor, group);
        mLastUpload = std::chrono::steady_clock::now();
    }

    // Rebuild at most one group per call, after uploads settle. Nothing is
    // drawn from an incomplete batch. Section originals are never changed.
    bool buildNext(
        std::shared_ptr<mce::BufferResourceService> service
    ) {
        if (!service || mSources.size() < 32 || mGroups.empty()) return false;
        if (std::chrono::steady_clock::now() - mLastUpload
            < std::chrono::milliseconds(500)) return false;
        for (auto g = mBuildCursor; g < mGroups.size(); ++g) {
            auto const first = g * kCorrectionBatchSize;
            auto const last = std::min(first + kCorrectionBatchSize, mSources.size());
            for (std::size_t kind = 0; kind < KindCount; ++kind) {
                auto& batch = mGroups[g][kind];
                if (batch.status != Status::Unbuilt) continue;
                std::vector<mce::MeshData const*> parts;
                parts.reserve(kCorrectionBatchSize);
                bool allKnown = true;
                std::size_t vertices = 0;
                for (auto i = first; i < last; ++i) {
                    auto const& slot = mSources[i][kind];
                    if (!slot.known) { allKnown = false; break; }
                    if (!slot.source) continue;
                    auto const count = slot.source->mPositions.get().size();
                    if (count > kCorrectionBatchMaxVertices - vertices) {
                        allKnown = false; break;
                    }
                    vertices += count;
                    parts.push_back(slot.source.get());
                }
                if (!allKnown) continue; // late/ineligible original section
                if (parts.size() < 2) {
                    batch.status = Status::Unsupported;
                    continue;
                }
                auto combined = combine(parts, vertices);
                if (!combined) {
                    batch.status = Status::Unsupported;
                    continue;
                }
                auto mesh = std::make_unique<mce::Mesh>(
                    service, std::move(*combined), false,
                    kind == MissingFill ? "LHoloBatchedMissingFill" : "LHoloBatchedMissingOutline"
                );
                if (!mesh->isValid()) {
                    batch.status = Status::Unsupported;
                    continue;
                }
                batch.mesh = std::move(mesh);
                batch.status = Status::Ready;
                ++mBuilt;
                mBuildCursor = g;
                return true;
            }
            mBuildCursor = g + 1;
        }
        return false;
    }

    [[nodiscard]] mce::Mesh const* readyMesh(
        std::size_t section, Kind kind, std::size_t sectionCount
    ) const {
        if (sectionCount != mSources.size() || kind >= KindCount) return nullptr;
        auto const group = section / kCorrectionBatchSize;
        if (group >= mGroups.size()) return nullptr;
        auto const& slot = mGroups[group][kind];
        return slot.status == Status::Ready ? slot.mesh.get() : nullptr;
    }

    [[nodiscard]] std::uint64_t builtGroups() const { return mBuilt; }

private:
    struct Source {
        std::unique_ptr<mce::MeshData> source;
        bool known{};
    };
    struct Batch {
        std::unique_ptr<mce::Mesh> mesh;
        Status status{Status::Unbuilt};
    };
    std::vector<std::array<Source, KindCount>> mSources;
    std::vector<std::array<Batch, KindCount>> mGroups;
    std::chrono::steady_clock::time_point mLastUpload{};
    std::size_t mBuildCursor{};
    std::uint64_t mBuilt{};
    std::size_t mStoredVertices{};

    template <class T>
    static bool appendVertexField(
        std::vector<T>& target, std::vector<T> const& source,
        std::size_t oldSize, std::size_t newSize
    ) {
        if (target.empty() && source.empty()) return true;
        if (target.size() != oldSize || source.size() != newSize)
            return false;
        target.insert(target.end(), source.begin(), source.end());
        return true;
    }

    static std::unique_ptr<mce::MeshData> combine(
        std::vector<mce::MeshData const*> const& parts, std::size_t totalVertices
    ) {
        if (parts.size() < 2 || totalVertices > kCorrectionBatchMaxVertices)
            return nullptr;
        auto const& first = *parts.front();
        if (!first.mIndices.get().empty()) return nullptr;
        auto result = std::make_unique<mce::MeshData>(first);
        auto& target = *result;
        for (std::size_t i = 1; i < parts.size(); ++i) {
            auto const& source = *parts[i];
            auto const previous = target.mPositions.get().size();
            auto const count = source.mPositions.get().size();
            if (source.mMode != target.mMode
                || source.mFieldEnabled.get() != target.mFieldEnabled.get()
                || !source.mIndices.get().empty()
                || !appendVertexField(target.mNormals.get(), source.mNormals.get(), previous, count)
                || !appendVertexField(target.mTangents.get(), source.mTangents.get(), previous, count)
                || !appendVertexField(target.mColors.get(), source.mColors.get(), previous, count)
                || !appendVertexField(target.mBoneId0s.get(), source.mBoneId0s.get(), previous, count)
                || !appendVertexField(target.mTextureUVs[0].get(), source.mTextureUVs[0].get(), previous, count)
                || !appendVertexField(target.mTextureUVs[1].get(), source.mTextureUVs[1].get(), previous, count)
                || !appendVertexField(target.mTextureUVs[2].get(), source.mTextureUVs[2].get(), previous, count)
                || !appendVertexField(target.mPBRTextureIndices.get(), source.mPBRTextureIndices.get(), previous, count)
                || !appendVertexField(target.mMERS.get(), source.mMERS.get(), previous, count)
                || !appendVertexField(target.mGeoType.get(), source.mGeoType.get(), previous, count)) {
                return nullptr; // exact original individual draws stay active
            }
            target.mPositions.get().insert(
                target.mPositions.get().end(),
                source.mPositions.get().begin(), source.mPositions.get().end());
            auto& bounds = target.mAABB.get();
            auto const& next = source.mAABB.get();
            for (int axis = 0; axis < 3; ++axis) {
                bounds.first[axis] = std::min(bounds.first[axis], next.first[axis]);
                bounds.second[axis] = std::max(bounds.second[axis], next.second[axis]);
            }
            auto& uvBounds = target.mUVAABB.get();
            auto const& uvNext = source.mUVAABB.get();
            for (int axis = 0; axis < 2; ++axis) {
                uvBounds.first[axis] = std::min(uvBounds.first[axis], uvNext.first[axis]);
                uvBounds.second[axis] = std::max(uvBounds.second[axis], uvNext.second[axis]);
            }
        }
        return target.mPositions.get().size() == totalVertices
            ? std::move(result) : nullptr;
    }
};
} // namespace lholo::projection::detail
