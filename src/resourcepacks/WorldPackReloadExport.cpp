#include "resourcepacks/WorldPackReloadState.h"
extern "C" __declspec(dllexport) int lholo_resource_reload_v4(
    praxis::companion::packs::WorldPackReloadCommand command,
    praxis::companion::packs::WorldPackReloadRequest const* request,
    praxis::companion::packs::WorldPackReloadReply* reply
) noexcept {
    try {return lholo::resourcepacks::state().call(command,request,reply);}
    catch(...) {if(reply && reply->bytes==sizeof(*reply)){*reply={};reply->error=6;}return 0;}
}

// Older coordinators must not interpret NativeManaged as a v3 fence receipt.
extern "C" __declspec(dllexport) int lholo_resource_reload_v3(
    praxis::companion::packs::WorldPackReloadCommand,
    praxis::companion::packs::WorldPackReloadRequest const*,
    praxis::companion::packs::WorldPackReloadReply* reply) noexcept {
    if(reply && reply->bytes==sizeof(*reply)){*reply={};reply->error=1;}
    return 0;
}
