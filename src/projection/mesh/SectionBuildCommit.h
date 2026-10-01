#pragma once

#include <functional>
#include <utility>

namespace lholo::projection::detail {

template <class Section, class Build>
void completeSynchronousSectionBuild(Section& section, Build&& build) {
    auto const revision = section.requestedRevision;
    std::invoke(std::forward<Build>(build));
    section.uploadedRevision = revision;
    section.dirty = section.requestedRevision != revision;
    if (!section.dirty) section.incrementalDirty = false;
}

} // namespace lholo::projection::detail
