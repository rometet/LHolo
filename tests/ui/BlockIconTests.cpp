#include "ui/BlockIconCatalog.h"
#include "overlay/BlockIconGpu.h"
#include <atomic>
#include <cstdio>
#include <thread>
#include <filesystem>
#include <fstream>
using namespace lholo;
using namespace ui::icons;
int checks{},failures{};
void check(bool value,char const* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
int main(int argc,char** argv){
 Catalog catalog{R"({"stone":{"textures":"stone"},"test:panel":{"textures":{"side":"panel","up":"top"}},"oak_slab":{"textures":"oak"},"broken":{"textures":7}})",
 R"({"texture_data":{"stone":{"textures":"textures/blocks/stone"},"panel":{"textures":{"path":"textures/custom/panel"}},"oak":{"textures":["textures/blocks/planks_oak"]},"escape":{"textures":"textures/../bad"}}})","{}"};
 check(catalog.blockPath("minecraft:stone")=="textures/blocks/stone","native vanilla namespace");
 check(catalog.blockPath("test:panel")=="textures/custom/panel","custom exact namespace");
 check(catalog.blockPath("another:stone").empty(),"never alias foreign namespace");
 check(catalog.blockPath("minecraft:oak_slab")=="textures/blocks/planks_oak","actual metadata alias");
 check(catalog.blockPath("minecraft:unknown").empty(),"missing mapping fallback");
 check(catalog.blockPath("minecraft:broken").empty(),"malformed texture fallback");
 Catalog malformed{"{broken","{}","[]"};check(malformed.blockPath("minecraft:stone").empty(),"malformed catalog fallback");
 std::vector<MetadataLayer> layers{
  {R"({"piston":{"textures":"piston_side"},"redstone_block":{"textures":"redstone_block"},"observer":{"textures":{"north":"observer_front"}}})",
   R"({"texture_data":{"piston_side":{"textures":"textures/blocks/piston_side"},"redstone_block":{"textures":"textures/blocks/redstone_block"},"observer_front":{"textures":["textures/blocks/observer_front"]}}})"},
  {R"({"honey_block":{"textures":{"north":"honey_side"}},"white_stained_glass":{"textures":"white_stained_glass"}})",
   R"({"texture_data":{"honey_side":{"textures":"textures/blocks/honey_side"},"white_stained_glass":{"textures":"textures/blocks/glass_white"}}})"},
  {R"({"new_block":{"textures":"new_block"}})",R"({"texture_data":{"new_block":{"textures":"textures/blocks/new_block"}}})"}
 };
 auto merged=Catalog::fromLayers(layers,layers.back().blocks,layers.back().terrain);
 check(merged && merged->order()=="last","native winning file establishes last-first pack priority");
 for(auto name:{"piston","redstone_block","observer","honey_block","white_stained_glass","new_block"})
  check(merged && !merged->blockPath(std::string{"minecraft:"}+name).empty(),"partial vanilla slices retain ordinary definitions");
 std::reverse(layers.begin(),layers.end());
 auto reversed=Catalog::fromLayers(layers,layers.front().blocks,layers.front().terrain);
 check(reversed && reversed->order()=="first" && reversed->blockPath("minecraft:piston")==merged->blockPath("minecraft:piston"),"native winner supports reversed engine vector order");
 auto conflicts=layers;
 conflicts.front().blocks=R"({"minecraft:piston":{"textures":"custom_piston"}})";
 conflicts.front().terrain=R"({"texture_data":{"custom_piston":{"textures":"textures/custom/piston"}}})";
 auto overridden=Catalog::fromLayers(conflicts,conflicts.front().blocks,conflicts.front().terrain);
 check(overridden && overridden->blockPath("minecraft:piston")=="textures/custom/piston","high namespace definition overrides lower legacy block key");
 check(!Catalog::fromLayers(conflicts,conflicts.front().blocks,conflicts.back().terrain),"contradicting native metadata winners fail closed");
 check(!Catalog::fromLayers(conflicts,conflicts[1].blocks,conflicts[1].terrain),"middle-only native winner fails closed");
 conflicts.front().blocks=R"({"piston":{"textures":7}})";
 auto badEntry=Catalog::fromLayers(conflicts,conflicts.front().blocks,conflicts.front().terrain);
 check(badEntry && badEntry->blockPath("minecraft:piston").empty(),"malformed upper entry never substitutes lower art");
 conflicts.front().blocks="{bad";
 check(!Catalog::fromLayers(conflicts,conflicts.front().blocks,conflicts.front().terrain),"malformed upper metadata fails closed");
 std::vector<MetadataLayer> single{layers.front()};
 auto one=Catalog::fromLayers(single,single.front().blocks,single.front().terrain);
 check(one && one->order()=="equivalent","single-layer priority does not require direction guess");
 std::vector<MetadataLayer> empty;
 check(!Catalog::fromLayers(empty,"{}","{}"),"missing active stack is not guessed from disk");
 auto sameLayer=layers.front();sameLayer.blocks=R"({"piston":{"textures":"legacy"},"minecraft:piston":{"textures":"qualified"}})";
 sameLayer.terrain=R"({"texture_data":{"legacy":{"textures":"textures/test/legacy"},"qualified":{"textures":"textures/test/qualified"}}})";
 auto exact=Catalog::fromLayers(std::span{&sameLayer,1},sameLayer.blocks,sameLayer.terrain);
 check(exact && exact->blockPath("minecraft:piston")=="textures/test/qualified","same-layer explicit namespace retains exact-lookup precedence");
 std::vector<MetadataLayer> symmetric{layers.front(),layers.back(),layers.front()};
 auto equivalent=Catalog::fromLayers(symmetric,symmetric.front().blocks,symmetric.front().terrain);
 check(equivalent && equivalent->order()=="equivalent" && !equivalent->blockPath("minecraft:piston").empty(),"ambiguous direction accepted only when both merges agree");
 auto ambiguous=symmetric;ambiguous.insert(ambiguous.begin()+2,sameLayer);
 check(!Catalog::fromLayers(ambiguous,ambiguous.front().blocks,ambiguous.front().terrain),"ambiguous priority with different merged results fails closed");
 check(!Catalog::fromLayers(layers,{},layers.front().terrain),"native missing blocks winner never uses hidden per-pack definitions");
 check(!Catalog::fromLayers(layers,layers.front().blocks,{}),"native missing terrain winner fails closed");
 std::vector<MetadataLayer> excess(513,layers.front());
 check(!Catalog::fromLayers(excess,excess.front().blocks,excess.front().terrain),"metadata layer count bounded");
 auto oversized=layers.front();oversized.blocks=std::string(8*1024*1024+1,' ')+"{}";
 check(!Catalog::fromLayers(std::span{&oversized,1},oversized.blocks,oversized.terrain),"oversized metadata rejected before parsing");
 auto padded=layers;padded.insert(padded.begin(),MetadataLayer{});padded.push_back({});
 auto edge=Catalog::fromLayers(padded,layers.front().blocks,layers.front().terrain);
 check(edge && edge->order()=="first","empty end layers do not hide true resource priority endpoints");
 auto onePath=layers;for(auto& layer:onePath)layer.blocks.clear();onePath[1].blocks=layers.front().blocks;
 auto mixed=Catalog::fromLayers(onePath,onePath[1].blocks,onePath.back().terrain);
 check(mixed && mixed->order()=="last","one metadata path direction follows other native winner");
 if(argc==3 && std::string_view(argv[1])=="--installed-metadata") {
  // Optional read-only local integration input. No game resources or artwork
  // are stored in the repository or delivered archives.
  std::vector<MetadataLayer> installed;
  auto read=[](std::filesystem::path const& path) {
   std::ifstream stream(path,std::ios::binary);
   return std::string{std::istreambuf_iterator<char>{stream},std::istreambuf_iterator<char>{}};
  };
  for(auto pack:{"vanilla","vanilla_1.14","vanilla_1.16","vanilla_1.20.20","vanilla_1.26.30","vanilla_1.26.50"}) {
   auto base=std::filesystem::path{argv[2]}/pack;
   installed.push_back({read(base/"blocks.json"),read(base/"textures/terrain_texture.json")});
  }
  auto installedCatalog=Catalog::fromLayers(installed,installed.back().blocks,installed.back().terrain);
  check(installedCatalog.has_value(),"real installed vanilla partial metadata integrates");
  for(auto name:{"piston","redstone_block","honey_block","observer","white_stained_glass"}) {
   auto path=installedCatalog?installedCatalog->blockPath(std::string{"minecraft:"}+name):std::string{};
   check(!path.empty(),"reported ordinary block resolves against real installed metadata");
   std::printf("Installed metadata %s -> %s\n",name,path.c_str());
  }
 }
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
 check(local.retry(*request),"busy native stack read requeues owned request");
 auto retryGeneration=request->generation;
 check(!local.retry(*request),"same native request cannot be retried twice");
 request=local.take();check(request && request->key==key && request->generation==retryGeneration,"retry preserves exact request key and generation");
 local.beginChange();check(!local.publish(*request,pixels),"stale result rejected before mutation");
 check(!local.retry(*request),"reload never requeues an old-generation native request");
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
