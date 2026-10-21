#pragma once
#include <cstddef>
#include <cstdint>
namespace lholo::projection::detail {
// A copied cancellation fact, never a mesh/atlas/native result publication.
struct CancelledSectionBuild {
    std::uint64_t workerGeneration{},structureGeneration{},revision{};
    std::size_t section{};
};
template<class Sections> bool recoverCancelledSection(CancelledSectionBuild const& fact,
    std::uint64_t workerGeneration,std::uint64_t structureGeneration,Sections& sections) {
    if(fact.workerGeneration!=workerGeneration || fact.structureGeneration!=structureGeneration
        || fact.section>=sections.size() || fact.revision>sections[fact.section].requestedRevision)return false;
    auto& section=sections[fact.section];section.buildInFlight=false;section.dirty=true;section.incrementalDirty=true;
    return true;
}
}
