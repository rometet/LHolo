#pragma once
#include "structure/PlacementSession.h"
#include "structure/StructureSession.h"

namespace lholo::structure {
// Legacy config has no world identity. Import into the explicitly chosen
// current world only; file copying/relative-path allocation stays in glue.
inline SavedPlacement migrateSavedPlacement(detail::SavedProjectionSnapshot const& legacy,
    std::string file,std::uint64_t id,int dimension) {
    SavedPlacement p;p.id=id;p.file=std::move(file);p.name=p.file.size()<=128?p.file:"Imported projection";p.dimension=dimension;
    p.origin={static_cast<std::int64_t>(legacy.anchorX)+legacy.transform.offsetX,
        static_cast<std::int64_t>(legacy.anchorY)+legacy.transform.offsetY,
        static_cast<std::int64_t>(legacy.anchorZ)+legacy.transform.offsetZ};
    p.rotation=legacy.transform.rotation;p.mirror=legacy.transform.mirror;p.layerAxis=legacy.transform.layerAxis;
    p.layerMode=legacy.transform.layerDisplayMode;p.layer=legacy.transform.displayLayer;
    p.visible=legacy.transform.visible;p.countExtras=legacy.transform.countExtras;
    return p;
}
}
