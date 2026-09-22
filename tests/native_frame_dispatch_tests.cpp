#include "native_graphics/native_frame_dispatch.h"
#include "native_graphics/native_scene_tree.h"
#include "native_graphics/native_scene_tree_publication.h"
#include <array>
#include <iostream>
#include <map>
#include <vector>
using namespace edf::native;
namespace {
struct Reader {
  mutable std::map<uint32_t,uint32_t> words;
  std::map<uint32_t,uint8_t> bytes;
  uint32_t Add(uint32_t at,uint32_t offset) const { return at+offset; }
  uint32_t Word(uint32_t at) const { return words.at(at); }
  void StoreWord(uint32_t at,uint32_t value) const { words[at]=value; }
  const uint8_t* Bytes(uint32_t at,uint32_t) const { return &bytes.at(at); }
};
void Require(bool condition,const char* text) { if(!condition) throw std::runtime_error(text); }
struct BinaryReader {
  mutable std::array<uint8_t,65536> memory{};
  uint32_t Add(uint32_t at,size_t bytes) const {
    if(bytes>memory.size() || at>memory.size()-bytes) throw std::runtime_error("range");
    return at+uint32_t(bytes);
  }
  const uint8_t* Bytes(uint32_t at,size_t bytes) const { Add(at,bytes); return memory.data()+at; }
  uint32_t Word(uint32_t at) const { return GuestBlockWord(Bytes(at,4)); }
  void StoreWord(uint32_t at,uint32_t value) const {
    Add(at,4); for(uint32_t i=0;i<4;++i) memory[at+i]=uint8_t(value>>(24-i*8));
  }
};
void TestTreePublication() {
  BinaryReader r;
  constexpr uint32_t owner=0x1000,levels=0x2000,node=0x3000;
  r.StoreWord(owner+52,levels); r.StoreWord(owner+56,levels+32);
  r.StoreWord(levels+20,node); r.StoreWord(levels+24,node+144);
  r.StoreWord(node+116,1); r.StoreWord(node+32,17);
  NativeSceneTreePublications publications;
  Require(publications.Publish(r,owner),"tree publication missing");
  const auto first=publications.Acquire(owner);
  const auto all=publications.AcquireAll();
  Require(all.size()==1 && all.at(owner)==first,"scene tree set omitted its retained hierarchy");
  Require(first && first->nodes==1,"tree publication node count");
  NativeSceneTreeReader owned(r,publications,first,false);
  r.StoreWord(node+32,19);
  Require(owned.Word(node+32)==17 && owned.owned_reads==1,"tree image borrowed mutable memory");
  {
    // Region hits answer exactly as the image's Find: inside a cached region,
    // a range running past its end still reads live.
    NativeSceneTreeReader cached(r,publications,first,false);
    Require(cached.Word(node+116)==1 && cached.Word(node+32)==17 && cached.Word(node+116)==1 &&
      cached.Word(node+64)==0 && cached.owned_reads==4 && !cached.live_reads,"tree region cache missed or changed a read");
    cached.Bytes(node+64,8);
    Require(cached.live_reads==1 && first->Find(node+64,8)==nullptr,"tree region cache served a range past its region");
  }
  NativeSceneTreeReader audited(r,publications,first,true);
  bool mismatch=false;
  try { audited.Word(node+32); } catch(const std::exception&) { mismatch=true; }
  Require(mismatch,"untracked hierarchy change escaped audit");
  publications.Invalidate();
  Require(publications.AcquireAll().empty() && all.at(owner)==first,
    "invalidating hierarchy changed a retained set or published stale current images");
  Require(!publications.Acquire(owner) && owned.Word(node+32)==19 && owned.live_reads==1,"stale tree did not fall back");
  Require(publications.Publish(r,owner),"tree republication failed");
  Require(GuestBlockWord(first->Find(node+32,4))==17,"old tree publication changed");
  Require(publications.Acquire(owner)->epoch!=first->epoch,"tree epoch reused");
  publications.Retire(owner);
  Require(publications.AcquireAll().empty() && GuestBlockWord(all.at(owner)->Find(node+32,4))==17,
    "tree retirement invalidated the retained scene hierarchy");
  Require(!publications.Acquire(owner) && publications.Owners().empty(),"tree retirement retained owner");
  r.StoreWord(node+84,node);
  bool cycle=false;
  try { CaptureNativeSceneTree(r,owner); } catch(const std::exception&) { cycle=true; }
  Require(cycle,"cyclic tree publication accepted");
}
void Run() {
  TestTreePublication();
  const std::array<float,4> nan_transform{std::bit_cast<float>(0xffc00000u),1,2,3};
  Require(NativeVisibilityBitsEqual(nan_transform,nan_transform),"identical NaN register audit failed");
  auto changed_nan=nan_transform; changed_nan[0]=std::bit_cast<float>(0xffc00001u);
  Require(!NativeVisibilityBitsEqual(nan_transform,changed_nan),"different NaN payload escaped audit");
  changed_nan=nan_transform; changed_nan[1]=2;
  Require(!NativeVisibilityBitsEqual(nan_transform,changed_nan),"different finite value escaped audit");
  Reader r;
  constexpr uint32_t owner=0x1000,context=0x4000,manager=0x5000,view=0x6000,world=0x7000,overlay=0x8000;
  r.bytes={{owner+2261,0},{owner+2262,0},{owner+2216,1},{owner+2217,0}};
  r.words={{owner,0x9000},{owner+12,0x9010},{0x9008,view},{0x9000,0x9010},
    {owner+44,0x9100},{owner+56,0x9120},{0x9108,world},{0x9100,0x9110},
    {owner+132,manager},{owner+136,7},{owner+140,2},{owner+144,4},
    {owner+2232,0x9200},{0x9200,0x9210},{0x9210,0x9200},{0x921c,overlay},
    {manager,0xa000},{view,0xa100},{world,0xa200},{overlay,0xa300},
    {0xa004,1},{0xa008,2},{0xa00c,3},{0xa010,4},{0xa110,5},{0xa208,6},{0xa30c,7},{0xa310,8},
    {view+400,0x3f000000},{0x820009a4,0},{0x820008cc,0x3f800000},
    {0x8201711c,0},{0x82017120,0x3f800000}};
  std::vector<uint32_t> order,phases;
  DispatchNativeFrame(r,owner,context,[&](uint32_t fn,uint32_t object,uint32_t arg,uint32_t index,uint32_t) {
    order.push_back(fn);
    if(fn==1) Require(object==manager && arg==view && index==0,"begin view arguments");
    if(fn==6) {
      Require(object==world && arg==context && r.Word(context+16)==view,"world view input");
      Require(r.Word(owner+136)==8 && r.Word(owner+168+511*4)==0,"serial/buckets not ready");
      // A callback removes the next world node. Traversal must reread the link.
      r.StoreWord(0x9100,0x9120);
    }
    if(fn==8) phases.push_back(arg);
  });
  Require(order==std::vector<uint32_t>({1,6,0x821A3BA0,7,5,2,3,4,8,8}),"frame phase order");
  Require(phases==std::vector<uint32_t>({2,3}),"post-frame phase range");
  Require(r.Word(context+8)==0x3f000000 && r.Word(context+12)==7 && r.Word(context+44)==0x3f800000,"context initialization");
  r.bytes[owner+2261]=1;
  order.clear();
  DispatchNativeFrame(r,owner,context,[&](uint32_t fn,uint32_t,uint32_t,uint32_t,uint32_t){order.push_back(fn);});
  Require(order==std::vector<uint32_t>({3,4,8,8}) && r.Word(owner+136)==8,"disabled views must retain final phases");
  // Multiple source lists targeting one bucket must preserve reversed insertion
  // order, descending priority, and the intentionally excluded zero bucket.
  for(uint32_t i=0;i<512;++i) r.StoreWord(owner+168+i*4,0);
  const std::array<uint32_t,4> items{0xb000,0xb100,0xb200,0xb300};
  for(uint32_t i=0;i<items.size();++i) {
    const auto item=items[i];
    r.StoreWord(item,0xa300); r.StoreWord(item+44,100+i); r.StoreWord(item+60,0);
    r.bytes[item+41]=i<2?9:(i==2?255:0);
  }
  r.StoreWord(owner+168,items[0]); r.StoreWord(items[0]+60,items[1]);
  r.StoreWord(owner+172,items[2]); r.StoreWord(owner+176,items[3]);
  std::vector<uint32_t> draws;
  DispatchNativeFrameBuckets(r,owner,context,[&](uint32_t fn,uint32_t item,uint32_t input,uint32_t,uint32_t lr) {
    Require(fn==8 && input==context && lr==0x821A3C54,"bucket callback ABI");
    Require(r.Word(context+40)==r.Word(item+44),"bucket depth input");
    draws.push_back(item);
  });
  Require(draws==std::vector<uint32_t>({items[2],items[1],items[0]}),"bucket priority/insertion order");
  Require(r.Word(owner+1192)==items[3],"zero bucket must remain linked but unrendered");
  NativeSceneVisibilityView view_data;
  view_data.matrix={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  view_data.frustum[24]=1; view_data.frustum[25]=10;
  view_data.frustum[8]=1; view_data.frustum[10]=-1;
  view_data.frustum[12]=-1; view_data.frustum[14]=-1;
  view_data.frustum[17]=1; view_data.frustum[18]=-1;
  view_data.frustum[21]=-1; view_data.frustum[22]=-1;
  Require(NativeVisibilityAabb(view_data,{0,0,5,1},{0.5f,0.5f,0.5f,0})==1,"contained tree bound");
  Require(NativeVisibilityAabb(view_data,{10,0,5,1},{0.5f,0.5f,0.5f,0})==0,"excluded tree bound");
  Require(NativeVisibilityAabb(view_data,{5,0,5,1},{1,1,1,0})==2,"intersecting tree bound");
  Require(NativeVisibilityAabb(view_data,{0,0,0,1},{1,1,1,0})==2,"near-plane touching bound");
  constexpr uint32_t tree=0xd000,levels=0xe000,root=0xf000,children=0x10000;
  r.StoreWord(tree+52,levels); r.StoreWord(tree+56,levels+32);
  r.StoreWord(levels+20,root); r.StoreWord(levels+24,root+288);
  r.StoreWord(root+116,1); r.StoreWord(root+144+116,1); r.StoreWord(root+144+84,0);
  for(uint32_t i=0;i<8;++i) {
    const auto child=children+i*144;
    r.StoreWord(root+84+i*4,child); r.StoreWord(child+116,i==0 || i==2); r.StoreWord(child+84,0);
  }
  std::vector<uint32_t> classified,leaves;
  TraverseNativeSceneTree(r,tree,[&](uint32_t node) {
    classified.push_back(node); return node==root?1u:2u;
  },[&](uint32_t list) { leaves.push_back(list); });
  Require(classified==std::vector<uint32_t>({root,root+144}),"accepted subtree reclassified children");
  Require(leaves==std::vector<uint32_t>({children+120,children+288+120,root+144+120}),"tree leaf order/empty-node filtering");
  Require(r.Word(tree+100)==2 && r.Word(tree+104)==0,"tree culling counters");
  bool invalidated=false;
  try {
    TraverseNativeSceneTree(r,tree,[](uint32_t){return 1u;},[&](uint32_t){r.StoreWord(levels+24,root);});
  } catch(const std::exception&) { invalidated=true; }
  Require(invalidated,"root storage invalidation not detected after callback");
  constexpr uint32_t group_list=0x12000,group_end=0x12100,first_group=0x12200;
  r.StoreWord(group_list+4,group_end); r.StoreWord(group_end,first_group);
  r.StoreWord(first_group+8,42); r.StoreWord(first_group,first_group+16);
  std::vector<uint32_t> groups;
  DispatchNativeSceneGroups(r,group_list,[&](uint32_t group) {
    groups.push_back(group); r.StoreWord(first_group,group_end);
  });
  Require(groups==std::vector<uint32_t>({42}),"group traversal retained stale post-callback link");
  r.StoreWord(group_end,group_end);
  DispatchNativeSceneGroups(r,group_list,[](uint32_t){throw std::runtime_error("empty group list submitted");});
}
}
int main() { try { Run(); std::cout<<"Native frame dispatch checks passed\n"; }
catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
