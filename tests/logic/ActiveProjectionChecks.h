#pragma once
#include "structure/ActiveProjection.h"

namespace lholo::tests {
template<class Check> void runActiveProjectionChecks(Check check) {
    using namespace structure;
    ActiveProjectionControl control;
    auto request=control.begin(7,12);
    SavedPlacement placed;placed.dimension=2;placed.origin={4,5,6};placed.rotation=1;placed.mirror=2;
    ActiveProjectionEvent event{request,7,19,21,2,"original.mcstructure",placed};
    check(control.current(request));
    for(int field=0;field<4;++field){auto stale=request;
        if(field==0)++stale.serial;if(field==1)++stale.selectionSerial;
        if(field==2)++stale.worldEpoch;if(field==3)++stale.placementId;
        check(!control.current(stale));
    }
    for(int field=0;field<4;++field){auto bad=event;
        if(field==0)bad.worldEpoch=0;if(field==1)bad.loadedGeneration=0;
        if(field==2)bad.activationGeneration=0;if(field==3)++bad.worldEpoch;
        check(!control.publish(bad));
    }
    check(control.publish(event));check(!control.publish(event));
    check(control.pending()==event);check(control.pendingCurrent(event));
    auto newer=event;++newer.activationGeneration;check(control.publish(newer));
    check(!control.publish(event));check(!control.pendingCurrent(event));check(control.pendingCurrent(newer));check(!control.bind(event,12));
    check(!control.bind(newer,0));check(control.bind(newer,12));
    check(!control.pending());check(control.active()->placementId==12);
    check(!control.publish(newer));
    auto failed=control.begin(7);control.fail(failed);
    check(!control.current(failed));check(control.active()->placementId==12);
    check(!control.synchronizeWorld(7,2)); // A failure does not suppress saved restoration.
    auto preWorld=control.begin(0);control.fail(preWorld);
    check(!control.synchronizeWorld(8,2));check(!control.active());
    request=control.begin(7,12);event.request=request;newer.request=request;
    check(control.publish(newer));check(control.bind(newer,12));
    auto another=control.begin(7); // Failed new load keeps old successful selection.
    check(control.active()->placementId==12);check(!control.current(request));
    check(!control.publish(event));check(control.current(another));
    control.fail(request);check(control.current(another));check(control.active()->placementId==12);
    auto second=newer;second.request=another;second.loadedGeneration=20;++second.activationGeneration;
    check(control.publish(second));check(control.synchronizeWorld(7,2));
    control.invalidate(true,true); // An unrelated deletion retires events, not active native state.
    check(!control.pending());check(control.active()->placementId==12);check(!control.publish(second));
    control.invalidate(true);check(!control.active());check(!control.publish(second));
    auto fresh=control.begin(8,14);second.request=fresh;second.worldEpoch=8;second.dimension=3;
    check(control.publish(second));check(control.synchronizeWorld(8,3));check(control.pending()==second);
    check(!control.synchronizeWorld(9,3));check(!control.pending());check(!control.active());
    auto unbound=control.begin(0);second.request=unbound;second.worldEpoch=10;
    check(control.synchronizeWorld(10,3));check(control.publish(second));check(control.bind(second,14));
    auto dimensionRequest=control.begin(10);second.request=dimensionRequest;check(control.publish(second));
    check(control.synchronizeWorld(10,4));check(!control.active());check(!control.pending());
    check(projectionEventCurrent(event,request,7,2,19,"original.mcstructure"));
    check(!projectionEventCurrent(event,request,8,2,19,"original.mcstructure"));
    check(!projectionEventCurrent(event,request,7,3,19,"original.mcstructure"));
    check(!projectionEventCurrent(event,request,7,2,20,"original.mcstructure"));
    check(!projectionEventCurrent(event,request,7,2,19,"other.mcstructure"));
    check(!projectionEventCurrent(event,another,7,2,19,"original.mcstructure"));
    auto a=placed;a.id=3;a.file="same.mcstructure";a.name="A";
    auto b=a;b.id=4;b.name="B";
    PlacementDocument document{{a,b},4};
    check(projectedPlacementId(document,a.file,placed,3,4)==3);
    check(projectedPlacementId(document,a.file,placed,0,3)==4); // Preserve valid manual selection.
    document.selected=0;check(projectedPlacementId(document,a.file,placed,0,4)==4);
    check(projectedPlacementId(document,a.file,placed,0,0)==0); // No vector-order guess.
    check(projectedPlacementId(document,a.file,placed,99,0)==0);
    document.placements.erase(document.placements.begin());
    check(projectedPlacementId(document,a.file,placed,0,0)==4);
    check(projectedPlacementId(document,"other.mcstructure",placed,0,4)==0);
    for(int field=0;field<10;++field){auto moved=placed;
        if(field==0)++moved.dimension;if(field==1)++moved.origin.x;if(field==2)++moved.origin.y;
        if(field==3)++moved.origin.z;if(field==4)++moved.rotation;if(field==5)moved.mirror=0;
        if(field==6)moved.visible=false;if(field==7)moved.countExtras=false;
        if(field==8)moved.layerMode=LayerDisplayMode::Single;if(field==9)++moved.layer;
        check(!sameProjectionPlacement(placed,moved));check(projectedPlacementId(document,a.file,moved,0,4)==0);
    }
    auto layer=placed;layer.layerAxis=LayerAxis::NorthToSouth;check(!sameProjectionPlacement(placed,layer));
    check(sameProjectionPlacement(placed,a)); // ID/name/path are not transform fields.
}
} // namespace lholo::tests
