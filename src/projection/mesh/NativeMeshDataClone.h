// LHolo - lossless native MeshData field clone for 26.60 client.
// The 26.60 SDK does not publish MeshData's copy constructor or move assignment.
// Construct a valid native destination through an exported Tessellator constructor,
// copy every documented field, then use the exported move constructor.
// Never byte-copy or zero-initialize engine-owned vector objects.
#pragma once

#include <algorithm>
#include <memory>
#include <utility>

#include "mc/client/renderer/Tessellator.h"
#include "mc/deps/minecraft_renderer/renderer/MeshData.h"

namespace lholo::projection::detail {

inline void copyNativeMeshDataFields(mce::MeshData& to, mce::MeshData const& from) {
    to.mMode = from.mMode;
    to.mPositions.get() = from.mPositions.get();
    to.mNormals.get() = from.mNormals.get();
    to.mTangents.get() = from.mTangents.get();
    to.mIndices.get() = from.mIndices.get();
    to.mColors.get() = from.mColors.get();
    to.mBoneId0s.get() = from.mBoneId0s.get();
    for (std::size_t i = 0; i < 3; ++i)
        to.mTextureUVs[i].get() = from.mTextureUVs[i].get();
    to.mPBRTextureIndices.get() = from.mPBRTextureIndices.get();
    to.mMERS.get() = from.mMERS.get();
    to.mGeoType.get() = from.mGeoType.get();
    to.mAABB.get() = from.mAABB.get();
    to.mUVAABB.get() = from.mUVAABB.get();
    to.mFieldEnabled.get() = from.mFieldEnabled.get();
}

// Only operate on two properly constructed MeshData owners. Swapping the typed
// fields avoids the unpublished move assignment, retains every vertex stream
// and does not change either object's native lifetime.
inline void swapNativeMeshDataFields(mce::MeshData& a, mce::MeshData& b) noexcept {
    using std::swap;
    swap(a.mMode, b.mMode);
    a.mPositions.get().swap(b.mPositions.get());
    a.mNormals.get().swap(b.mNormals.get());
    a.mTangents.get().swap(b.mTangents.get());
    a.mIndices.get().swap(b.mIndices.get());
    a.mColors.get().swap(b.mColors.get());
    a.mBoneId0s.get().swap(b.mBoneId0s.get());
    for (std::size_t i = 0; i < 3; ++i)
        a.mTextureUVs[i].get().swap(b.mTextureUVs[i].get());
    a.mPBRTextureIndices.get().swap(b.mPBRTextureIndices.get());
    a.mMERS.get().swap(b.mMERS.get());
    a.mGeoType.get().swap(b.mGeoType.get());
    swap(a.mAABB.get(), b.mAABB.get());
    swap(a.mUVAABB.get(), b.mUVAABB.get());
    swap(a.mFieldEnabled.get(), b.mFieldEnabled.get());
}

inline std::unique_ptr<mce::MeshData> cloneNativeMeshData(mce::MeshData const& from) {
    Tessellator staging{std::weak_ptr<mce::BufferResourceService>{}};
    copyNativeMeshDataFields(staging.mMeshData.get(), from);
    return std::make_unique<mce::MeshData>(std::move(staging.mMeshData.get()));
}

} // namespace lholo::projection::detail
