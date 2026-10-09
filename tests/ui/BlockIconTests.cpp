#include "ui/BlockIconCatalog.h"
#include "overlay/BlockIconGpu.h"
#include <atomic>
#include <cstdio>
#include <thread>
using namespace lholo;
using namespace ui::icons;
int checks{},failures{};
void check(bool value,char const* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
int main(){
 Catalog catalog{R"({"stone":{"textures":"stone"},"test:panel":{"textures":{"side":"panel","up":"top"}},"oak_slab":{"textures":"oak"},"broken":{"textures":7}})",
 R"({"texture_data":{"stone":{"textures":"textures/blocks/stone"},"panel":{"textures":{"path":"textures/custom/panel"}},"oak":{"textures":["textures/blocks/planks_oak"]},"escape":{"textures":"textures/../bad"}}})","{}"};
 check(catalog.blockPath("minecraft:stone")=="textures/blocks/stone","native vanilla namespace");
 check(catalog.blockPath("test:panel")=="textures/custom/panel","custom exact namespace");
 check(catalog.blockPath("another:stone").empty(),"never alias foreign namespace");
 check(catalog.blockPath("minecraft:oak_slab")=="textures/blocks/planks_oak","actual metadata alias");
 check(catalog.blockPath("minecraft:unknown").empty(),"missing mapping fallback");
 check(catalog.blockPath("minecraft:broken").empty(),"malformed texture fallback");
 Catalog malformed{"{broken","{}","[]"};check(malformed.blockPath("minecraft:stone").empty(),"malformed catalog fallback");
 for(auto path:{"textures/../secret","C:/secret","textures\\bad","textures/:bad","file://secret"})check(!safeTexturePath(path),"unsafe path rejected");
 check(keyFor("minecraft:stone [a=1]",{}).block=="minecraft:stone","state retained in text, representative ID normalized");
 std::vector<std::uint8_t> rgb(16*16*3,42),rgba(16*16*4,255);
 auto pixels=copyPixels(16,16,1,3,rgb);check(pixels && pixels->rgba.size()==1024 && pixels->rgba[3]==255,"RGB converted with alpha");
 check(!copyPixels(16,16,1,4,rgb),"short buffer rejected");check(!copyPixels(16,16,2,4,rgba),"array/depth rejected");
 check(!copyPixels(16,16,1,2,rgba),"unknown format rejected");check(!copyPixels(0,0,1,4,rgba),"zero size rejected");
 check(!copyPixels(5000,5000,1,4,rgba),"excessive image rejected");
 std::vector<std::uint8_t> strip(16*64*4,255);check(!copyPixels(16,64,1,4,strip),"animation strip without metadata fallback");
 std::vector<std::uint8_t> large(128*128*4,99);auto thumb=copyPixels(128,128,1,4,large);
 check(thumb && thumb->width==64 && thumb->height==64 && thumb->rgba[0]==99,"bounded nearest thumbnail");
 std::vector<std::uint8_t> wide(32*16*4,250);auto aspect=copyPixels(32,16,1,4,wide);
 check(aspect && aspect->width==32 && aspect->height==16,"static aspect preserved");
 Store local;auto key=keyFor("minecraft:stone",{});
 check(local.request(key).pending,"visible request queued");auto request=local.take();check(request.has_value(),"tick consumes request");
 for(int i=0;i<50;++i)local.request(key);check(!local.take(),"in-flight request deduplicated");
 local.beginChange();check(!local.publish(*request,pixels),"stale result rejected before mutation");
 check(!local.request(key).pixels && !local.request(key).pending,"reload blocks reads");local.beginChange();local.endChange();check(!local.readable(),"nested reload stays closed");local.endChange();
 local.request(key);request=local.take();check(request && local.publish(*request,pixels),"new generation publishes");check(local.request(key).pixels==pixels,"owned pixels published");
 local.reset(false);check(!local.request(key).pending && !local.take(),"closed session admits nothing");local.reset(true);
 for(unsigned i=0;i<Store::PendingCapacity+100;++i)local.request(keyFor("minecraft:fixture_"+std::to_string(i),{}));
 unsigned pending{};while(local.take())++pending;check(pending==Store::PendingCapacity,"bounded request capacity");
 local.reset(true);std::atomic_bool run{true};std::thread publisher([&]{while(run){local.request(key);if(auto request=local.take())local.publish(*request,pixels);}});
 for(int i=0;i<1000;++i){local.beginChange();check(!local.request(key).pixels,"parallel reload exposes no stale pixels");local.endChange();}run=false;publisher.join();
 Microsoft::WRL::ComPtr<ID3D11Device> device;Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
 auto hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);check(SUCCEEDED(hr),"WARP device");
 if(SUCCEEDED(hr)){
  overlay::BlockIconGpu gpu;auto& shared=store();shared.reset(true);
  gpu.beginFrame(device.Get());check(gpu.lookup("minecraft:air",{}).status==ui::BlockIconStatus::Air,"air distinct from unknown");
  check(gpu.lookup("minecraft:stone",{}).status==ui::BlockIconStatus::Pending,"no native assets gives pending fallback");
  auto req=shared.take();check(req && shared.publish(*req,pixels),"fixture CPU publication");
  auto first=gpu.lookup("minecraft:stone",{});check(first.texture && first.status==ui::BlockIconStatus::Ready,"private SRV ready");
  check(gpu.cached()==1 && gpu.pinned()==1,"texture pinned to frame");
  shared.beginChange();shared.endChange();check(!gpu.lookup("minecraft:stone",{}).texture,"stale cache never returned");
  req=shared.take();check(req && shared.publish(*req,aspect),"replacement pixels");auto second=gpu.lookup("minecraft:stone",{});
  check(second.texture && second.texture!=first.texture && second.aspect==2.f,"same-frame replacement preserves aspect");
  check(gpu.pinned()==2,"previous draw texture remains pinned");
  auto* srv=reinterpret_cast<ID3D11ShaderResourceView*>(first.texture);Microsoft::WRL::ComPtr<ID3D11Resource> resource;srv->GetResource(&resource);check(resource!=nullptr,"old SRV alive after generation replacement");
  for(unsigned i=0;i<Store::Capacity+2;++i) {
   auto const name="minecraft:zz_fixture_"+std::to_string(i);
   shared.request(keyFor(name,{}));auto queued=shared.take();
   if(queued)shared.publish(*queued,pixels);
   gpu.lookup(name,{});
  }
  check(gpu.cached()==Store::Capacity,"GPU cache remains bounded after eviction");
  check(gpu.pinned()>Store::Capacity,"eviction does not free submitted-frame references");
  resource.Reset();srv->GetResource(&resource);check(resource!=nullptr,"old image alive after cache eviction");
  shared.request(keyFor("minecraft:stone",{}));auto restored=shared.take();
  check(restored && shared.publish(*restored,aspect),"restore CPU pixels after bounded cache eviction");
  gpu.endFrame();check(gpu.pinned()==0,"pins released after submission");gpu.reset();check(gpu.cached()==0,"graphics reset clears private resources");
  gpu.beginFrame(device.Get());check(gpu.lookup("minecraft:stone",{}).texture!=0,"resize reuploads owned pixels");gpu.endFrame();
  Microsoft::WRL::ComPtr<ID3D11Device> other;Microsoft::WRL::ComPtr<ID3D11DeviceContext> otherContext;
  check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&other,nullptr,&otherContext)),"replacement device");
  gpu.beginFrame(other.Get());check(gpu.cached()==0,"device change drops incompatible cached views");
  auto replaced=gpu.lookup("minecraft:stone",{});check(replaced.texture!=0,"owned pixels reupload on replacement device");
  Microsoft::WRL::ComPtr<ID3D11Device> owner;reinterpret_cast<ID3D11ShaderResourceView*>(replaced.texture)->GetDevice(&owner);
  check(owner==other,"new SRV belongs to current device");gpu.endFrame();gpu.reset();shared.reset(false);
 }
 std::printf("Block icons: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
