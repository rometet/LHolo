#include "structure/capture/McstructureExporter.h"

#include "mc/deps/core/file/Path.h"
#include "mc/world/level/levelgen/structure/StructureManager.h"
#include "mc/world/level/levelgen/structure/StructureTemplate.h"
#include "io/AtomicOutput.h"

namespace lholo::structure::capture {

bool exportMcstructure(StructureTemplate const& structure, std::filesystem::path const& output) {
    return io::writeOutputAtomically(output, [&](auto const& staged) {
        return StructureManager::exportStructure(structure, Core::Path{staged});
    });
}

} // namespace lholo::structure::capture
