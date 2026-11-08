#pragma once
// Render-thread-only transparent GPU draw groups. Per-section original meshes
// are retained and remain the fallback on source/layout/revision changes.
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"

namespace lholo::projection::detail {
inline constexpr std::size_t kTransparentBatchGroupSize=8;
inline constexpr std::size_t kTransparentBatchMaxVertices=65536;
inline constexpr std::size_t kTransparentBatchMaxResidentVertices=1048576;
struct ProjectionTransparentBatchGroup {
    std::unique_ptr<mce::Mesh> mesh;
    std::vector<std::uint64_t> sortGenerations;
    std::size_t vertexCount{};
    bool unsupported{};
};
struct ProjectionTransparentBatchCache {
    std::vector<std::size_t> visible;
    std::vector<ProjectionTransparentBatchGroup> groups;
    std::uint64_t activation{},meshRevision{},visibilityRevision{};
    std::size_t storedVertices{};
    std::chrono::steady_clock::time_point lastChange{};
    void clear() {
        visible.clear();groups.clear();
        activation=meshRevision=visibilityRevision=0;
        storedVertices=0;lastChange={};
    }
    template<class DrawRecords>
    void observe(DrawRecords const& sequence,
                 std::uint64_t a,std::uint64_t m,std::uint64_t v) {
        if(activation==a && meshRevision==m && visibilityRevision==v
           && visible.size()==sequence.size()
           && std::equal(visible.begin(),visible.end(),sequence.begin(),
                 [](std::size_t index,auto const& record){return index==record.section;}))
            return;
        clear();
        activation=a;meshRevision=m;visibilityRevision=v;
        visible.reserve(sequence.size());
        for(auto const& record:sequence)visible.push_back(record.section);
        groups.resize((visible.size()+kTransparentBatchGroupSize-1)/kTransparentBatchGroupSize);
        lastChange=std::chrono::steady_clock::now();
    }    std::size_t groupStart(std::size_t index) const noexcept {
        return index*kTransparentBatchGroupSize;
    }
    std::size_t groupEnd(std::size_t index) const noexcept {
        return std::min(visible.size(),groupStart(index)+kTransparentBatchGroupSize);
    }
    void retire(std::size_t index) {
        if(index>=groups.size())return;
        auto& group=groups[index];
        if(group.mesh) {
            storedVertices=group.vertexCount>storedVertices ? 0 : storedVertices-group.vertexCount;
        }
        group.mesh.reset();group.vertexCount=0;group.sortGenerations.clear();
    }
    bool readyToBuild() const noexcept {
        return lastChange!=std::chrono::steady_clock::time_point{}
            && std::chrono::steady_clock::now()-lastChange>=std::chrono::milliseconds(250);
    }
};
}