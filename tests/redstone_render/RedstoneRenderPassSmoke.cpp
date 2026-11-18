#include "projection/mesh/RedstoneNativeRenderPass.h"
#include <cassert>
using namespace lholo::projection::detail;
static_assert(isRedstoneDiodeNativeModel("minecraft:unpowered_repeater"));
static_assert(isRedstoneDiodeNativeModel("minecraft:powered_repeater"));
static_assert(isRedstoneDiodeNativeModel("minecraft:unpowered_comparator"));
static_assert(isRedstoneDiodeNativeModel("minecraft:powered_comparator"));
static_assert(!isRedstoneDiodeNativeModel("minecraft:observer"));
static_assert(!isRedstoneDiodeNativeModel("minecraft:redstone_wire"));
static_assert(!diodeNeedsNativeExtraFlag("minecraft:powered_comparator",false));
static_assert(diodeNeedsNativeExtraFlag("minecraft:powered_comparator",true));
static_assert(!diodeNeedsNativeExtraFlag("minecraft:piston",true));
int main() {
    for (auto name : {"minecraft:unpowered_repeater","minecraft:powered_repeater",
                      "minecraft:unpowered_comparator","minecraft:powered_comparator"}) {
        assert(isRedstoneDiodeNativeModel(name));
        assert(!diodeNeedsNativeExtraFlag(name,false));
        assert(diodeNeedsNativeExtraFlag(name,true));
    }
}
