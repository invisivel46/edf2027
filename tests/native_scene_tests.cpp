#include "native_graphics/native_scene.h"
#include "native_graphics/native_scene_bindings.h"
#include "native_graphics/native_scene_sources.h"
#include "native_graphics/native_scene_adapter.h"
#include "native_graphics/native_world_publication_mirror.h"
#include "native_graphics/native_scene_tree.h"
#include "native_graphics/native_scene_walk_lock.h"
#include "native_graphics/native_scene_static_walk.h"
#include "native_graphics/native_scene_pass_inputs.h"
#include "native_graphics/native_queued_scene.h"
#include "native_graphics/native_scene_cpu_window.h"
#include "native_graphics/native_scene_membership.h"
#include "native_graphics/native_scene_tree_publication.h"
#include "native_graphics/native_scene_geometry.h"
#include "native_graphics/native_scene_handoff.h"
#include "native_graphics/native_scene_execution.h"
#include "native_graphics/native_scene_geometry_install.h"
#include "native_graphics/native_static_group_eligibility.h"
#include "native_graphics/native_recorded_reads.h"
#include "native_graphics/native_buffer_writes.h"
#include "native_graphics/native_static_world_resolve.h"
#include "native_graphics/native_static_world_pass.h"
#include "native_graphics/native_static_world_cache.h"
#include "native_graphics/native_full_frame_static_world.h"
#include "native_graphics/native_address_filter.h"
#include "native_graphics/native_texture_binding.h"
#include "native_graphics/d3d11_backend.h"
#include "native_graphics/d3d12_backend.h"
#include <bit>
#include <chrono>
#include <cstring>
#include <mutex>
#include <span>
#include <iostream>

using namespace edf::native;
namespace {
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
template<class F> void Reject(F f) { bool rejected=false; try { f(); } catch(const std::exception&) { rejected=true; } Require(rejected,"invalid scene operation accepted"); }
void Word(std::vector<uint8_t>& bytes,size_t offset,uint32_t word) {
  for(size_t i=0;i<4;++i) bytes[offset+i]=uint8_t(word>>(24-i*8));
}
struct GeometryRetryReader {
  std::vector<uint8_t>& bytes;
  uint32_t Add(uint32_t address,uint32_t offset) const { return address+offset; }
  uint32_t Word(uint32_t at) const {
    uint32_t value=0; for(unsigned i=0;i<4;++i) value=(value<<8)|bytes.at(at+i); return value;
  }
  uint64_t DoubleWord(uint32_t at) const { return (uint64_t(Word(at))<<32)|Word(at+4); }
  const uint8_t* Bytes(uint32_t at,size_t size) const {
    if(at>bytes.size() || size>bytes.size()-at) throw std::runtime_error("geometry test range");
    return bytes.data()+at;
  }
  void StoreWord(uint32_t at,uint32_t value) const {
    for(unsigned i=0;i<4;++i) bytes.at(at+i)=uint8_t(value>>(24-i*8));
  }
  void StoreDoubleWord(uint32_t at,uint64_t value) const { StoreWord(at,uint32_t(value>>32)); StoreWord(at+4,uint32_t(value)); }
  void StoreByte(uint32_t at,uint8_t value) const { bytes.at(at)=value; }
};
// Retail image constants (0x82000000+) mapped past the test heap at 0x40000.
struct ImageMappedReader {
  GeometryRetryReader heap;
  static uint32_t Map(uint32_t at) { return at>=0x82000000u?at-0x82000000u+0x40000u:at; }
  uint32_t Add(uint32_t address,uint32_t offset) const { return address+offset; }
  uint32_t Word(uint32_t at) const { return heap.Word(Map(at)); }
  const uint8_t* Bytes(uint32_t at,size_t size) const { return heap.Bytes(Map(at),size); }
  void StoreWord(uint32_t at,uint32_t value) const { heap.StoreWord(Map(at),value); }
};
void TreePublicationReuse() {
  std::vector<uint8_t> memory(0x10000);
  const GeometryRetryReader reader{memory};
  constexpr uint32_t owner=0x1000,levels=0x2000,node=0x3000;
  reader.StoreWord(owner+52,levels); reader.StoreWord(owner+56,levels+32);
  reader.StoreWord(levels+20,node); reader.StoreWord(levels+24,node+288);
  reader.StoreWord(node+116,1); reader.StoreWord(node+32,17);
  NativeSceneTreePublications trees;
  Require(trees.Publish(reader,owner) && trees.captures()==1,"tree publication missing");
  const auto first=trees.Acquire(owner);
  Require(first && first->nodes==2,"tree publication node count");
  Require(trees.Publish(reader,owner) && trees.captures()==1 && trees.Acquire(owner)==first,"unchanged tree was recaptured");
  trees.Invalidate();
  Require(!trees.Acquire(owner),"invalidated tree stayed current");
  Require(trees.Publish(reader,owner) && trees.captures()==1,"invalidation without changed bytes recaptured the tree");
  const auto restamped=trees.Acquire(owner);
  Require(restamped && restamped!=first && restamped->regions==first->regions && !trees.Current(*first),
    "restamp mutated or kept the published image current");
  reader.StoreWord(node+32,19); // Untracked write: the epoch does not move.
  Require(trees.Publish(reader,owner) && trees.captures()==2,"changed tree was not recaptured");
  const auto changed=trees.Acquire(owner);
  Require(changed && GuestBlockWord(changed->Find(node+32,4))==19 && GuestBlockWord(first->Find(node+32,4))==17 &&
    GuestBlockWord(restamped->Find(node+32,4))==17,"recapture missed the change or mutated an old image");
  reader.StoreWord(node+144+116,1); // An empty root becomes occupied and exposes new bounds.
  Require(trees.Publish(reader,owner) && trees.captures()==3 && trees.Acquire(owner)->Find(node+144+32,36),
    "occupancy change was not recaptured");
  Require(trees.Publish(reader,owner) && trees.captures()==3 && trees.reuses()==3,"tree reuse count");
}
void SharedIndex() {
  NativeSharedMap<uint32_t,uint32_t> map;
  for(uint32_t i=0;i<1000;++i) map.Set(i*7919%1000,i);
  const auto frozen=map;
  Require(map.Shares(frozen) && map.size()==1000,"shared map copy was not shared");
  uint32_t expected=0;
  for(const auto& [key,value]:frozen) Require(key==expected++ && value*7919%1000==key,"shared map order");
  for(uint32_t i=0;i<1000;i+=2) Require(map.Erase(i),"shared map erase");
  *map.Mutable(1)=5000; map.Set(1001,1);
  Require(!map.Shares(frozen) && map.size()==501 && frozen.size()==1000 && frozen.at(1)!=5000 && map.at(1)==5000 &&
    frozen.contains(0) && !map.contains(0) && !map.Erase(0) && !map.Mutable(0) && map.contains(1001),
    "shared map write reached a shared copy");
  expected=1;
  for(const auto& [key,value]:map) { Require(key==expected,"shared map order after erase"); expected+=2; }
  Require(expected==1003,"shared map iteration skipped a chunk");
  Require(map.EraseIf([](const auto& entry) { return entry.first>500; })==251 && map.size()==250 && frozen.size()==1000,
    "shared map erase_if");
  NativeSharedVector<int> list{1,2,3};
  Require(list.size()==3 && list[2]==3 && NativeSharedVector<int>{}.empty(),"shared vector sequence");
  // Difference: exactly the keys a brute-force comparison of the two copies
  // finds, in key order, through inserts, splits, erases, overwrites and
  // equal rewrites (a rewritten chunk with equal elements reports nothing).
  // Four-element chunks so every step crosses chunk boundaries.
  NativeSharedMap<uint32_t,uint32_t,4> live;
  std::map<uint32_t,uint32_t> model;
  uint32_t seed=7;
  const auto next=[&] { seed=seed*1664525u+1013904223u; return seed>>8; };
  std::vector<uint32_t> reported;
  const auto differs=[&](const auto& before) {
    reported.clear(); live.Difference(before,[&](uint32_t key) { reported.push_back(key); });
  };
  Require((differs(live),reported.empty()),"a map differs from itself");
  for(uint32_t step=0;step<400;++step) {
    const auto before=live;
    const auto previous=model;
    for(uint32_t edit=next()%12;edit--;) {
      const auto key=next()%160;
      switch(next()%4) {
        case 0: live.Erase(key); model.erase(key); break;
        case 1: if(model.contains(key)) { live.Set(key,model[key]); break; } [[fallthrough]];  // Equal rewrite.
        default: { const auto value=next()%3; live.Set(key,value); model[key]=value; }
      }
    }
    std::vector<uint32_t> expected;
    for(uint32_t key=0;key<160;++key) {
      const auto a=previous.find(key); const auto b=std::as_const(model).find(key);
      if((a==previous.end())!=(b==model.end()) || (a!=previous.end() && a->second!=b->second)) expected.push_back(key);
    }
    differs(before);
    Require(reported==expected,"shared map difference is not the brute-force difference");
    Require(before.size()==previous.size(),"a difference or a write reached the earlier copy");
  }
  // Copies that share nothing still compare exactly.
  NativeSharedMap<uint32_t,uint32_t,4> rebuilt;
  for(const auto& [key,value]:model) rebuilt.Set(key,value);
  differs(rebuilt);
  Require(reported.empty(),"equal unshared maps differ");
  rebuilt.Set(1000,1); differs(rebuilt);
  Require(reported==std::vector<uint32_t>{1000},"a key only the earlier copy holds was not reported");
}
// Page-backed guest arena: the device, its texture objects and the retail
// constant tables at 0x82000000 live far apart.
struct SparseReader {
  std::map<uint32_t,std::array<uint8_t,4096>>& pages;
  uint32_t Add(uint32_t address,uint32_t offset) const { return address+offset; }
  const uint8_t* Bytes(uint32_t at,size_t size) const {
    if((at&4095)+size>4096) throw std::runtime_error("sparse test access crosses a page");
    return pages[at&~4095u].data()+(at&4095);
  }
  uint32_t Word(uint32_t at) const { return GuestBlockWord(Bytes(at,4)); }
  uint64_t DoubleWord(uint32_t at) const { return (uint64_t(Word(at))<<32)|Word(at+4); }
  void StoreWord(uint32_t at,uint32_t value) const {
    auto* bytes=const_cast<uint8_t*>(Bytes(at,4));
    for(unsigned i=0;i<4;++i) bytes[i]=uint8_t(value>>(24-i*8));
  }
  void StoreDoubleWord(uint32_t at,uint64_t value) const { StoreWord(at,uint32_t(value>>32)); StoreWord(at+4,uint32_t(value)); }
  void StoreByte(uint32_t at,uint8_t value) const { const_cast<uint8_t*>(Bytes(at,1))[0]=value; }
  const uint8_t* WritableBytes(uint32_t at,size_t size,size_t) const { return Bytes(at,size); }
};
// The static world pass against sequential guest groups on a CPU arena. Guest
// activation is modelled by the production executors, as ActivateNativeMaterial
// runs them: constant uploads, texture binding with retirement, sampler words,
// state CPU writes. Run once with a live fence (retirement stamps the replaced
// handle's +8) and once without (retirement queues a tagged deferred entry):
// the whole arena, not only the device block, must match sequential groups.
void StaticWorldPass() {
  constexpr uint32_t device=0x10000,owner=0x300,queue=0x30000,common=0x100;
  struct Material {
    uint32_t address;
    std::vector<std::array<uint32_t,2>> states;
    std::vector<NativeMaterialCpuProgram::Constant> constants;
    std::vector<NativeMaterialCpuProgram::Texture> textures;
    NativeSceneMaterialProgram program;
  };
  using Pages=std::map<uint32_t,std::array<uint8_t,4096>>;
  // Published order: E has no selections; G has a guest-queued selection.
  constexpr uint32_t empty=0xa600,guest=0xa300;
  const std::vector<uint32_t> order{0xa000,0xa100,empty,0xa200,guest,0xa400,0xa500};
  const auto run=[&](uint32_t fence) {
    Pages initial;
    const SparseReader setup{initial};
    setup.StoreWord(0x8200964c,0x3b808081u); setup.StoreWord(0x82003198,0x42000000u);
    for(uint32_t i=0;i<6;++i) setup.StoreWord(0x82009608+i*4,i*2);
    setup.StoreWord(device+10780,fence); setup.StoreWord(device+10784,common);
    setup.StoreWord(device+13148,queue); setup.StoreWord(device+13152,queue+8*16);
    setup.StoreWord(device+12184,1); setup.StoreWord(device+12168,1);
    for(uint32_t offset:{10424u,10456u,10460u,10464u}) setup.StoreWord(device+offset,0x10001);
    setup.StoreWord(device+10420,0x70); setup.StoreWord(device+10440,0x5); setup.StoreWord(device+10428,0x80000007);
    setup.StoreWord(device+10332,15); setup.StoreWord(device+11576,0x10001); setup.StoreWord(device+11580,0);
    setup.StoreWord(device+11588,15);
    for(uint32_t slot=0;slot<16;++slot) {
      for(uint32_t i=0;i<6;++i) setup.StoreWord(device+1024+slot*24+i*4,0x9e3779b9u*(slot*6+i+1));
      setup.StoreWord(device+12272+slot*4,0x40000+slot*0x100); setup.StoreWord(0x40000+slot*0x100,common);
      setup.StoreByte(device+11652+slot,uint8_t(slot%5)); setup.StoreByte(device+11678+slot,uint8_t(slot%3));
      setup.StoreByte(device+11704+slot,uint8_t(15-slot%4)); setup.StoreByte(device+11730+slot,uint8_t(slot%2?5:0));
    }
    for(uint32_t i=0;i<256;++i) setup.StoreWord(0x60000+i*4,0x3f800000u+i*0x1001u);
    // A null handle binds nothing: no descriptor words, texture lod or filter bit.
    const auto texture=[&](uint32_t slot,uint32_t handle,std::array<uint32_t,4> settings) {
      NativeMaterialCpuProgram::Texture result{slot,handle,{}};
      result.sampler.slot=slot; result.sampler.settings=settings;
      if(!handle) return result;
      setup.StoreWord(handle,common);
      for(uint32_t i=0;i<6;++i) setup.StoreWord(handle+28+i*4,(0x85ebca6bu*(i+1))^(handle<<3));
      result.sampler.texture_filter_high=setup.Word(handle+40)&0x80000000u;
      result.sampler.texture_lod=setup.Word(handle+44)&0x3fcu;
      return result;
    };
    const auto material=[&](uint32_t address,std::vector<std::array<uint32_t,2>> states,
                            std::vector<NativeMaterialCpuProgram::Constant> constants,
                            std::vector<NativeMaterialCpuProgram::Texture> textures) {
      Material result{address,std::move(states),std::move(constants),std::move(textures),{}};
      result.program.inputs.state_overrides=result.states;
      for(const auto& bound:result.textures) result.program.sampler_operations.push_back(bound.sampler);
      return result;
    };
    const auto half=std::bit_cast<uint32_t>(.5f),quarter=std::bit_cast<uint32_t>(-.25f);
    // M0's states and slot 0 are superseded within its run, and M1's slot 1 is
    // last touched by M2's null bind, so the first handoff replays only M2 and
    // M0/M1 bind: their textures retire at M2's binds, M1's descriptor words
    // stay under the null bind, and vertex c20 (M0) and pixel c5 (M1) are
    // written by no replayed activation. G is a guest group between two runs.
    std::vector<Material> materials;
    materials.push_back(material(0xa000,{{0x3c,1},{0x48,6},{0x98,3}},{{false,20,0x60000,2}},
      {texture(0,0x50000,{half,1,1,1})}));
    materials.push_back(material(0xa100,{{0x44,0x80402010u},{0x28,1}},{{true,5,0x60100,1}},
      {texture(1,0x51000,{quarter,2,0,1})}));
    materials.push_back(material(0xa200,{{0x34,2},{0x4c,7}},{{false,21,0x60200,1}},
      {texture(0,0x52000,{0,1,1,0}),texture(1,0,{half,1,0,1})}));
    materials.push_back(material(0xa300,{{0x38,1},{0xd8,3}},{{false,21,0x60300,1}},{texture(2,0x53000,{half,0,0,0})}));
    materials.push_back(material(0xa400,{{0x60,1},{0x4c,5},{0x40,1},{0x58,4}},{},{texture(0,0x54000,{quarter,1,0,1})}));
    materials.push_back(material(0xa500,{{0x64,0x80},{0x154,0x7f}},{{true,6,0x60340,2}},{texture(3,0x55000,{0,1,1,1})}));
    // states=false is the handoff's bind: the activation without its state operations.
    const auto activate=[&](const SparseReader& reader,const Material& selected,bool states) {
      for(const auto& operation:selected.constants) {
        const auto first=operation.first/4,last=(operation.first+operation.count-1)/4;
        const uint64_t mask=(~uint64_t(0)>>(first))&(~uint64_t(0)<<(63-last));
        Require(UploadNativeMaterialConstant(reader,device,operation,mask),"static world fixture constant aliases");
      }
      for(const auto& bound:selected.textures) {
        const uint64_t mask=uint64_t(1)<<(43-bound.slot);
        SetNativeTextureResource(reader,device,bound.slot,bound.handle,mask,
          []()->uint32_t { throw std::runtime_error("unexpected retirement allocation"); },
          []()->uint32_t { return 0x80000000u; });
        const auto resolved=ApplyNativeMaterialSampler(ReadNativeMaterialSamplerPass(reader,device,bound.slot),bound.sampler);
        reader.StoreWord(device+1036+bound.slot*24,resolved.words[1]);
        reader.StoreWord(device+1040+bound.slot*24,resolved.words[2]);
        reader.StoreDoubleWord(device+16,reader.DoubleWord(device+16)|mask);
      }
      if(states) for(const auto& [offset,value]:selected.states) {
        const auto writes=NativeMaterialStateCpuWrites(ReadNativeMaterialRenderPassMirrors(reader,device),
          offset,value,reader.DoubleWord(device+16),reader.DoubleWord(device+24));
        Require(writes.has_value(),"static world fixture state needs a callback");
        for(const auto& [address,word]:*writes) reader.StoreWord(device+address,word);
      }
    };
    const auto find=[&](uint32_t address) -> const Material& {
      for(const auto& candidate:materials) if(candidate.address==address) return candidate;
      throw std::runtime_error("unknown static world fixture group");
    };
    Pages sequential=initial;
    {
      const SparseReader reader{sequential};
      for(const auto group:order) if(group!=empty) activate(reader,find(group),true);
    }
    Pages native=initial;
    const SparseReader reader{native};
    const auto load=[&] {
      NativeSceneMaterialPassState pass;
      pass.render=ReadNativeMaterialRenderPass(reader,device);
      for(uint32_t slot=0;slot<16;++slot) pass.samplers[slot]=ReadNativeMaterialSamplerPass(reader,device,slot);
      return pass.Inputs();
    };
    auto cursor=load();
    std::vector<const Material*> owed;
    NativeMaterialRenderPass owed_start;
    std::vector<std::string> trace;
    std::vector<uint32_t> replayed,bound;
    WalkNativeStaticWorld(order,[&](uint32_t group) -> std::optional<NativeStaticWorldFallback> {
      if(group==guest) return NativeStaticWorldFallback::GuestQueue;
      if(group==empty) { trace.push_back("empty"); return {}; }
      const auto& selected=find(group);
      if(owed.empty()) owed_start=cursor.render;
      owed.push_back(&selected);
      cursor=cursor.After(selected.program);
      trace.push_back("native "+std::to_string(group));
      return {};
    },[&](uint32_t group,NativeStaticWorldFallback reason) {
      Require(reason==NativeStaticWorldFallback::GuestQueue,"static world fallback reason");
      trace.push_back("guest "+std::to_string(group));
      activate(reader,find(group),true);
      cursor=load();
    },[&] {
      if(owed.empty()) return;
      trace.push_back("handoff");
      std::vector<NativeStaticWorldHandoffGroup> groups;
      for(const auto* selected:owed) {
        uint32_t slots=0;
        for(const auto& operation:selected->program.sampler_operations) slots|=1u<<operation.slot;
        groups.push_back({selected->program.inputs.state_overrides,slots});
      }
      const auto writes=HandOffNativeStaticWorld(reader,device,owed_start,cursor.samplers,groups,[&](size_t index) {
        replayed.push_back(owed[index]->address); activate(reader,*owed[index],true);
      },[&](size_t index) {
        bound.push_back(owed[index]->address); activate(reader,*owed[index],false);
      });
      Require(writes.render==cursor.render,"static world handoff render state diverged from the pass cursor");
      owed.clear();
    });
    Require(trace==std::vector<std::string>{"native 40960","native 41216","empty","native 41472","handoff",
      "guest 41728","native 41984","native 42240","handoff"},"static world walk left published order or skipped a handoff");
    Require(replayed==decltype(replayed){0xa200,0xa400,0xa500},
      "static world handoff replayed other than each last slot binder and the last group");
    Require(bound==decltype(bound){0xa000,0xa100},"static world handoff skipped an unreplayed group's binds");
    // Pass-state chaining equals the mirrors sequential guest groups leave.
    const SparseReader expected{sequential};
    NativeSceneMaterialPassState oracle;
    oracle.render=ReadNativeMaterialRenderPass(expected,device);
    for(uint32_t slot=0;slot<16;++slot) oracle.samplers[slot]=ReadNativeMaterialSamplerPass(expected,device,slot);
    Require(cursor==oracle.Inputs(),"chained static world pass state differs from sequential guest groups");
    // The whole device block: render words, blend factor, alpha reference,
    // constant registers, sampler records, bound textures and dirty masks.
    for(uint32_t offset=0;offset<13520;offset+=4)
      if(reader.Word(device+offset)!=expected.Word(device+offset))
        throw std::runtime_error("static world handoff device word differs at +"+std::to_string(offset));
    // Null slot: M2's null bind leaves M1's descriptor. Words +0/+8 are M1's
    // header outside the preserved bits; +12 is the header's filter bit over
    // sampler bits, because M1's sampler step (texture lod present) cleared
    // its low 19 texture bits after the bind; the pre-pass word had them set.
    const auto slot1=device+1024+24;
    Require(((reader.Word(slot1)^setup.Word(0x51000+28))&~0x3ffc00u)==0 && reader.Word(slot1+8)==setup.Word(0x51000+36) &&
      (reader.Word(slot1+12)&~0x7ff80000u)==(setup.Word(0x51000+40)&0x80000000u) &&
      (setup.Word(slot1+12)&0x7ffffu)!=0 &&
      reader.Word(device+12272+4)==0,"static world null bind kept pre-pass texture words");
    // Registers only a skipped activation wrote hold its values.
    Require(reader.Word(device+(20+112)*16)==expected.Word(0x60000) && reader.Word(device+(5+368)*16)==expected.Word(0x60100),
      "static world handoff lost registers only a skipped activation wrote");
    // Retirement in sequential order: each replaced handle stamped with the
    // fence, or one tagged entry per replaced handle in the deferred queue.
    const std::vector<uint32_t> retired{0x40000,0x40100,0x50000,0x51000,0x40200,0x52000,0x40300};
    if(fence) {
      for(const auto handle:retired) Require(reader.Word(handle+8)==fence,"static world handoff skipped a texture retirement");
      Require(reader.Word(device+13148)==queue,"fenced retirement queued an entry");
    } else {
      Require(reader.Word(device+13148)==queue+8*retired.size(),"deferred retirement entry count");
      for(size_t i=0;i<retired.size();++i)
        Require(reader.DoubleWord(queue+uint32_t(i)*8)==((uint64_t((retired[i]>>2)|0x80000000u)<<32)|0xffffffffu),
          "deferred retirement left sequential order");
    }
    // Everything else the groups touched: handles, queue, constant sources.
    const std::array<uint8_t,4096> zero{};
    for(const auto* pages:{&sequential,&native}) for(const auto& entry:*pages) {
      const auto page=entry.first;
      const auto left=sequential.find(page);
      const auto right=native.find(page);
      if((left==sequential.end()?zero:left->second)!=(right==native.end()?zero:right->second))
        throw std::runtime_error("static world handoff arena differs in page "+std::to_string(page));
    }
  };
  run(200); run(0);
  Require(NativeStaticWorldReplayPlan(std::vector<NativeStaticWorldHandoffGroup>{{{},1},{{},2},{{},1},{{},0}})==
    std::vector<size_t>{1,2,3} && NativeStaticWorldReplayPlan({}).empty(),"static world replay plan");
  // Order selection: anything short of a covering published order runs 821C3BB8.
  NativeSceneAdapter adapter;
  NativeSceneQueues queues;
  queues.Push(0xa000,0x7000); queues.Push(0xa000,0x7100);
  Require(!NativeStaticWorldOrder(nullptr,owner,&queues),"static world pass ran without a publication");
  Require(!NativeStaticWorldOrder(adapter.Publish(1).get(),owner,&queues),"static world pass ran without a group order");
  adapter.PublishGroupOrder(owner,order);
  const auto publication=adapter.Publish(2);
  Require(!NativeStaticWorldOrder(publication.get(),owner,nullptr),"static world pass ran without native queues");
  Require(NativeStaticWorldOrder(publication.get(),owner,&queues)==publication->group_order.at(owner),
    "covering group order was not selected");
  Require(!NativeStaticWorldOrder(publication.get(),owner+4,&queues),"another owner's order was selected");
  queues.Push(0xb000,0x7200);
  Require(!NativeStaticWorldOrder(publication.get(),owner,&queues),"stale order would leave a queued group undrawn");
  // A declined group returns its selections unchanged to the guest callback.
  const auto taken=queues.Take(0xa000);
  for(auto at=taken.rbegin();at!=taken.rend();++at) queues.Push(0xa000,*at);
  Require(queues.Take(0xa000)==taken,"declined static group selections changed order");
  auto disabled=NativeSceneQueues{};
  disabled.Push(0xa000,0x7000);
  Pages lists;
  const SparseReader list_reader{lists};
  disabled.Materialize(list_reader);
  Require(!NativeStaticWorldOrder(publication.get(),owner,&disabled),"static world pass ran over materialized queues");
}
void GeometryPublicationRetry() {
  constexpr uint32_t device=0x1000,old_vertex=0x10000,old_index=0x10100,queue=0x18000;
  const NativeSceneGeometrySource input{0x11000,0x11100,0x12000,28,6,0x13000,0x14000};
  for(bool fence:{false,true}) for(unsigned failed=0;failed<3;++failed) {
    std::vector<uint8_t> memory(0x20000);
    const GeometryRetryReader reader{memory};
    reader.StoreWord(device+12188,old_vertex); reader.StoreWord(device+12164,old_index);
    reader.StoreWord(device+10780,fence?200:0); reader.StoreWord(device+10784,1);
    for(auto resource:{old_vertex,old_index,input.vertex,input.index}) reader.StoreWord(resource,1);
    reader.StoreWord(input.vertex+24,0x15000); reader.StoreWord(input.vertex+28,4096);
    reader.StoreWord(device+13148,queue); reader.StoreWord(device+13152,queue+64);
    NativeSceneGeometryInstallState progress;
    std::array<unsigned,3> publications{};
    unsigned retirements=0;
    bool fail=true;
    const auto install=[&](const NativeSceneGeometrySource& source) {
      InstallNativeSceneGeometry(reader,device,source,
        [](bool)->uint32_t { throw std::runtime_error("unexpected retry allocation"); },
        [](bool) { return 0u; },
        [&](bool,auto,auto,auto,auto) { ++retirements; },
        [&](NativeGeometryBinding binding) {
          const auto index=unsigned(binding); ++publications[index];
          if(index==failed && fail) { fail=false; throw std::runtime_error("publication failed"); }
        },&progress);
    };
    Reject([&] { install(input); });
    Require(progress.step==failed*2+1,"geometry retry lost completed CPU stage");
    auto changed=input; ++changed.vertex;
    Reject([&] { install(changed); });
    install(input); install(input);
    Require(progress.step==6 && retirements==2,"geometry retry repeated a resource retirement");
    for(unsigned i=0;i<3;++i)
      Require(publications[i]==(i==failed?2u:1u),"geometry retry skipped or repeated a completed publication");
    Require(reader.Word(device+12188)==input.vertex && reader.Word(device+12164)==input.index &&
      reader.Word(device+11536)==input.declaration,"geometry retry changed final bindings");
    Require(reader.Word(input.vertex+8)==0 && reader.Word(input.index+8)==0,
      "geometry retry retired the newly installed resource");
    if(fence) Require(reader.Word(old_vertex+8)==200 && reader.Word(old_index+8)==200 &&
      reader.Word(device+13148)==queue,"geometry retry changed fence retirement");
    else Require(reader.Word(device+13148)==queue+16 && reader.Word(queue)==(old_vertex>>2) &&
      reader.Word(queue+8)==(old_index>>2),"geometry retry duplicated or changed deferred retirement records");
  }
}
void StaticGroupEligibility() {
  using Result=NativeStaticGroupEligibility;
  constexpr uint32_t device=0x1000,stack=0x38000,material=0x24000,pass=0x25000,record=0x26000,payload=0x27000;
  const NativeSceneGeometrySource input{0x10000,0x10100,0x12000,28,6,material,0x20000};
  std::vector<uint8_t> memory(0x50000);
  const ImageMappedReader reader{{memory}};
  constexpr uint32_t bias_scale=0x82003198,anisotropy=0x82009608,color_scale=0x8200964c;
  reader.StoreWord(bias_scale,0x42000000); reader.StoreWord(color_scale,0x3b808081);
  reader.StoreWord(device+10424,0x10001);
  reader.StoreWord(material+108,pass);
  reader.StoreWord(pass,pass+32); reader.StoreWord(pass+4,pass+64);
  reader.StoreWord(pass+32,input.shader); reader.StoreWord(pass+68,0x21000);
  reader.StoreWord(material,record); reader.StoreWord(material+8,1);
  reader.StoreWord(record,0); reader.StoreWord(record+4,payload); reader.StoreWord(record+12,1);
  const auto seed=memory;
  const auto check=[&](Result expected) {
    const auto before=memory;
    Require(AssessNativeStaticGroup(reader,device,stack,input)==expected,"static eligibility misclassified input");
    Require(memory==before,"eligibility preflight mutated guest memory");
  };
  check(Result::Supported);
  // Incoming state is carried explicitly; only the resolved pass must decode.
  reader.StoreWord(device+10424,0x07060706); check(Result::Supported); memory=seed;
  reader.StoreWord(device+10424,0x0303); check(Result::PassState); memory=seed;
  reader.StoreWord(device+10420,1); check(Result::PassState); memory=seed;
  reader.StoreWord(device+12188,0x30000);
  check(Result::RetirementMode);
  reader.StoreWord(device+10780,200); check(Result::Supported);
  memory=seed; reader.StoreWord(record+4,device+1792); check(Result::AliasedInput);
  memory=seed; reader.StoreWord(record+4,stack-16); check(Result::AliasedInput);
  memory=seed;
  reader.StoreWord(material,device+1792);
  reader.StoreWord(device+1792+4,payload); reader.StoreWord(device+1792+12,1);
  check(Result::AliasedInput);
  memory=seed; reader.StoreWord(device+10780,200); reader.StoreWord(device+12164,record-8);
  check(Result::AliasedInput); // Old resource fence would overwrite a captured operation.
  // Pass readers' image inputs: a retirement fence store onto any of them aliases.
  memory=seed; reader.StoreWord(device+10780,200); reader.StoreWord(device+12188,bias_scale-8);
  check(Result::AliasedInput); // Sampler bias scale.
  memory=seed; reader.StoreWord(device+10780,200); reader.StoreWord(device+12188,color_scale-8);
  check(Result::AliasedInput); // Render-state color scale.
  memory=seed; reader.StoreWord(device+10780,200); reader.StoreWord(device+12188,anisotropy+20-8);
  check(Result::Supported); // No slot selects anisotropy entry 5.
  memory[device+11652+3]=5; check(Result::AliasedInput); // Slot 3 selects it.
  memory=seed; reader.StoreWord(bias_scale,0); check(Result::TextureOrState);
  memory=seed; reader.StoreWord(color_scale,0); check(Result::PassState);
  // Captured live shape: 3 local + 1 global texture and the same 5 state operations.
  constexpr uint32_t textures=0x29000,globals=0x2a000,global_source=0x2a100,states=0x2b000,objects=0x2c000;
  const auto operations=[&](std::initializer_list<std::array<uint32_t,2>> list) {
    uint32_t i=0;
    for(const auto& [offset,value]:list) {
      reader.StoreWord(states+i*8,offset); reader.StoreWord(states+i*8+4,value);
      reader.StoreWord(device+56+offset,NativeMaterialStateSetter(offset)); ++i;
    }
    reader.StoreWord(material+96,states); reader.StoreWord(material+104,i);
  };
  const auto captured=[&] {
    memory=seed;
    for(uint32_t i=0;i<3;++i) {
      reader.StoreWord(textures+i*28+4,objects+i*64); reader.StoreWord(textures+i*28+8,i);
      reader.StoreWord(objects+i*64+28,0x1000+i);
    }
    reader.StoreWord(material+72,textures); reader.StoreWord(material+80,3);
    reader.StoreWord(globals,global_source); reader.StoreWord(globals+4,3);
    reader.StoreWord(global_source+28,objects+192);
    reader.StoreWord(material+84,globals); reader.StoreWord(material+92,1);
    operations({{0x38,2},{0x30,1},{0x3c,1},{0x48,6},{0x4c,7}});
  };
  captured(); check(Result::Supported);
  captured(); reader.StoreWord(device+10424,0x07060706); check(Result::Supported); // After a blended group.
  captured(); reader.StoreWord(device+56+0x3c,0); check(Result::TextureOrState); // Setter identity changed.
  captured(); reader.StoreWord(textures+28+8,16); check(Result::TextureOrState); // Invalid sampler slot.
  captured(); reader.StoreWord(material+104,6); reader.StoreWord(states+40,0xc8); reader.StoreWord(states+44,1);
  reader.StoreWord(device+56+0xc8,NativeMaterialStateSetter(0xc8)); check(Result::TextureOrState); // Scissor callback.
  captured(); reader.StoreWord(states+32,0x48); reader.StoreWord(states+36,3); check(Result::PassState); // Resolved blend factor.
  // A material's own operations may replace an unrepresentable inherited word.
  memory=seed; reader.StoreWord(device+10424,0x0303); operations({{0x3c,0}}); check(Result::Supported);
  memory=seed; reader.StoreWord(device+10420,1); operations({{0x6c,0}}); check(Result::Supported);
  memory=seed; reader.StoreWord(device+10420,1); operations({{0x6c,1}}); reader.StoreWord(device+12184,1);
  check(Result::PassState);
  // Old texture retirement: fence required, and its write joins the alias check.
  constexpr uint32_t old_texture=0x2d000;
  captured(); reader.StoreWord(device+12272+4,old_texture); check(Result::RetirementMode);
  reader.StoreWord(device+10780,200); check(Result::Supported);
  reader.StoreWord(device+12272+4,textures+28+4-8); check(Result::AliasedInput);
  captured(); reader.StoreWord(device+10780,200); reader.StoreWord(textures+28+8,0); // Slot 0 twice.
  check(Result::Supported);
  reader.StoreWord(textures+4,textures+28+4-8); check(Result::AliasedInput); // Retired by the repeated slot.
  captured(); reader.StoreWord(textures+4,device+1024-28); check(Result::AliasedInput); // Header read in device mirror.
  // Fenced, so the table's zeroed neighbour records fail on aliasing, not retirement.
  captured(); reader.StoreWord(device+10780,200);
  reader.StoreWord(material+72,device+2048); reader.StoreWord(device+2048+4,objects);
  check(Result::AliasedInput); // Texture table inside the device mirror.
  {
    // The world pass records this assessment's bytes and keeps the fence,
    // retirement slots and stack as a witness: those words never enter the
    // record, and over their changes the witness agrees with a fresh assessment.
    captured(); reader.StoreWord(device+10780,200); reader.StoreWord(device+12188,0x30000);
    reader.StoreWord(device+12272+4,old_texture);
    NativeRecordedReads recorded;
    NativeStaticEligibilityWitness witness;
    Require(AssessNativeStaticGroup(NativeRecordingReader(reader,recorded),device,stack,input,&witness)==Result::Supported &&
      !witness.fenced && witness.retirements.size()>4,"witnessed assessment declined or missed a texture retirement slot");
    const auto agrees=[&](uint32_t at,bool expected,const char* what) {
      Require(recorded.Unchanged(reader),"per-frame eligibility words entered the recorded reads");
      Require(witness.Holds(reader,at)==expected &&
        (AssessNativeStaticGroup(reader,device,at,input)==Result::Supported)==expected,what);
    };
    agrees(stack,true,"witness rejected an unchanged frame");
    reader.StoreWord(device+10780,201); agrees(stack,true,"fence advance changed the witness");
    reader.StoreWord(device+12188,0x30100); agrees(stack,true,"moved retired shader changed the witness");
    reader.StoreWord(device+12272+4,old_texture+0x100); agrees(stack,true,"moved retired texture changed the witness");
    reader.StoreWord(device+12272+4,textures+28+4-8); agrees(stack,false,"retired texture over a captured read was witnessed");
    reader.StoreWord(device+12272+4,old_texture);
    agrees(stack-0x1000,true,"another call depth changed the witness");
    agrees(payload+16,false,"stack window over a captured read was witnessed");
    agrees(2048,false,"stack without room for its window was witnessed");
    reader.StoreWord(device+10780,0); agrees(stack,false,"unfenced retirement was witnessed");
    reader.StoreWord(device+12188,0); reader.StoreWord(device+12272+4,0);
    agrees(stack,true,"retirement-free unfenced frame was rejected");
    // The program's own repeated slot needs the fence whatever the device holds.
    captured(); reader.StoreWord(device+10780,200); reader.StoreWord(textures+28+8,0);
    NativeRecordedReads repeated;
    Require(AssessNativeStaticGroup(NativeRecordingReader(reader,repeated),device,stack,input,&witness)==Result::Supported &&
      witness.fenced,"repeated texture slot's retirement was not witnessed");
    reader.StoreWord(device+10780,0);
    Require(repeated.Unchanged(reader) && !witness.Holds(reader,stack) &&
      AssessNativeStaticGroup(reader,device,stack,input)==Result::RetirementMode,"unfenced repeated slot was witnessed");
  }
  memory=seed;
  constexpr uint32_t defaults=0x28000,header=0x20000+872;
  reader.StoreWord(header+20,defaults-header);
  reader.StoreWord(defaults+24,20);
  reader.StoreWord(defaults+36,0xfc000001); reader.StoreWord(defaults+40,0xdeadbeef);
  reader.StoreWord(record+4,0x11000); // Default destination extends beyond the normal device mirror.
  check(Result::AliasedInput);
  // Defaults landing on any traced pass mirror: sampler words, sampler bytes,
  // render-state words outside the decoded six.
  const auto default_word=[&](uint32_t offset) {
    memory=seed;
    reader.StoreWord(header+20,defaults-header);
    reader.StoreWord(defaults+24,20);
    reader.StoreWord(defaults+36,(offset<<16)|1); reader.StoreWord(defaults+40,0xdeadbeef);
  };
  default_word(1024); check(Result::Supported); // Fetch constant beyond the pixel samplers.
  default_word(3*24+12); check(Result::PassState); // Slot 3 sampler word.
  default_word(11652+4-1024); check(Result::PassState); // Anisotropy indices, slots 4-7.
  default_word(11580-1024); check(Result::PassState); // Blend control.
  memory=seed;
  auto invalid=input; invalid.count=0;
  Require(AssessNativeStaticGroup(reader,device,stack,invalid)==Result::Geometry,"zero geometry count admitted");
  Require(AssessNativeStaticGroup(reader,device+1,stack,input)==Result::Alignment,"unaligned device admitted");
}
// The static world pass's cross-frame group cache, driven the way the pass
// drives it: a candidate whose eligibility reads still hold and whose constants
// are current is reused, anything else resolves again and is stored.
// A group's resolve splits into one group step and a step per instance: the
// group step runs once for any number of instances, and any decline ends the
// walk short so that the caller returns the whole group.
void StaticWorldGroupResolve() {
  using D=NativeStaticWorldDecline;
  struct Result { D decline=D::None; uint32_t instance=0; };
  const std::vector<uint32_t> instances{0x100,0x11C,0x138,0x154,0x170,0x18C,0x1A8,0x1C4};
  std::vector<Result> resolved;
  size_t groups=0,visits=0;
  const auto run=[&](D group,uint32_t failing,D instance) {
    groups=visits=0;
    return ResolveNativeStaticGroupInstances(instances,resolved,[&] { ++groups; return group; },
      [&](uint32_t at) { ++visits; return Result{at==failing?instance:D::None,at}; });
  };
  Require(run(D::None,0,D::None)==D::None && groups==1 && visits==instances.size() && resolved.size()==instances.size() &&
    std::ranges::equal(resolved,instances,{},&Result::instance),"static group resolve did not run its group step once for all instances");
  Require(run(D::Revision,0,D::None)==D::Revision && groups==1 && !visits && resolved.empty(),
    "static group decline resolved instances");
  Require(run(D::None,0x154,D::WorldRegisters)==D::WorldRegisters && groups==1 && visits==4 && resolved.size()==3 &&
    resolved.size()!=instances.size(),"static instance decline did not return the group");
  Require(run(D::None,0,D::None)==D::None && resolved.size()==instances.size(),"static group resolve kept a previous walk");
  groups=0;
  Require(ResolveNativeStaticGroupInstances(std::span<const uint32_t>{},resolved,[&] { ++groups; return D::None; },
    [&](uint32_t at) { return Result{D::None,at}; })==D::None && resolved.empty() && !groups,"empty static group ran its group step");
  // The world-only test the per-instance step makes from the source generation.
  NativeSceneSources::Source source; source.world_data=0x1000; source.world_first=16;
  Require(NativeStaticWorldOnlySource(source,16,0x80000000),"world-only source rejected");
  Require(!NativeStaticWorldOnlySource(source,20,0x80000000),"world-only source accepted another register");
  source.world_first.reset(); Require(!NativeStaticWorldOnlySource(source,16,0x80000000),"source without recorded parameters accepted");
  source.world_first=16; source.world_data=0x80000000+1792;
  Require(!NativeStaticWorldOnlySource(source,16,0x80000000),"world data inside the device constants accepted");
  source.world_data=0; Require(!NativeStaticWorldOnlySource(source,16,0x80000000),"source without world data accepted");
}
void StaticWorldGroupCache() {
  struct View { uint32_t viewport=0,targets=0; bool operator==(const View&) const=default; };
  struct Material { std::shared_ptr<const int> identity; };
  using Cache=NativeStaticWorldGroupCache<View,Material>;
  using Constant=NativeSceneMaterialInputs::Constant;
  const auto program=std::make_shared<NativeSceneMaterialProgram>();
  program->inputs.vertex=0x100; program->inputs.pixel=0x200;
  const auto publish=[](std::shared_ptr<const NativeSceneMaterialProgram> with) {
    auto group=std::make_shared<NativeSceneGroupMaterial>();
    group->group=0x5000; group->revision=1; group->program=std::move(with);
    group->constants={{true,"tint",std::vector<uint8_t>(16,1),false},{false,"g_mViewProjection",std::vector<uint8_t>(64,2),true}};
    return std::shared_ptr<const NativeSceneGroupMaterial>(std::move(group));
  };
  const auto material=publish(program);
  Cache::Key key{.group=0x5000,.device=0x10000,.vertex=0x100,.pixel=0x200,
    .program=material->program,.setup={0x1000,0x2000,0x3000,32,6,0x4000,0x100},.geometry=std::make_shared<const int>(7),
    .backend=std::make_shared<const int>(9),.view={1,2},.filtering=-1,.shaders=3};
  key.pass.render.words[0]=0x10001;
  std::vector<uint8_t> memory(256);
  const GeometryRetryReader guest{memory};
  Cache cache;
  size_t resolves=0;
  NativeSceneView camera;
  // The assessment's per-frame words: a retirement slot at 0x60 and the fence
  // at 0x64, neither recorded; the alias-checked read is the recorded 0x80.
  constexpr uint32_t frame_stack=0x70000;
  const NativeStaticEligibilityWitness witness{{{0x80,16}},{0x60},0x64,false};
  const auto lookup=[&](const Cache::Key& inputs,const std::vector<Constant>& constants,uint32_t stack=frame_stack) {
    auto* entry=cache.Candidate(inputs);
    if(entry && entry->reads.Unchanged(guest) && entry->eligibility.Holds(guest,stack) &&
       Cache::Current(*entry,constants,nullptr,camera)) return entry->material.identity;
    ++resolves;
    NativeRecordedReads reads;
    const NativeRecordingReader recorder(guest,reads);
    recorder.Word(0x40); recorder.Bytes(0x80,16);
    return cache.Store(inputs,constants,std::move(reads),witness,{},Material{std::make_shared<const int>(int(resolves))},
      nullptr,camera,{stack,nullptr}).material.identity;
  };
  const auto constants=material->constants;
  const auto identity=lookup(key,constants);
  Require(resolves==1 && cache.size()==1,"first group visit did not resolve and store");
  Require(lookup(key,constants)==identity && resolves==1,"cache hit resolved again or changed the material");
  Require(lookup(key,constants)==identity && resolves==1,"second cache hit resolved again");
  // Every key component: a change is a different group, pass or program, and
  // the miss diagnostic names exactly that component.
  using M=NativeStaticWorldMiss;
  const auto misses=[&](auto&& change,M component,const char* what) {
    auto changed=key; change(changed);
    Require(!cache.Candidate(changed),what);
    Require(NativeStaticWorldKeyDifferences(key,changed)==NativeStaticWorldMissBit(component),"miss diagnostic named another component");
  };
  misses([](Cache::Key& k) { k.group+=4; },M::Absent,"group address change hit");
  misses([](Cache::Key& k) { k.device+=16; },M::Device,"device change hit");
  misses([](Cache::Key& k) { k.vertex^=1; },M::Vertex,"vertex id change hit");
  misses([](Cache::Key& k) { k.pixel^=1; },M::Pixel,"pixel id change hit");
  misses([&](Cache::Key& k) { k.program=std::make_shared<NativeSceneMaterialProgram>(*program); },M::Program,"program change hit");
  misses([](Cache::Key& k) { k.setup.count+=3; },M::Setup,"geometry setup change hit");
  misses([](Cache::Key& k) { k.setup.material+=4; },M::Setup,"guest material change hit");
  misses([](Cache::Key& k) { k.geometry=std::make_shared<const int>(7); },M::Geometry,"retained geometry change hit");
  misses([](Cache::Key& k) { k.backend=std::make_shared<const int>(9); },M::Backend,"backend change hit");
  misses([](Cache::Key& k) { k.pass.render.words[0]^=1; },M::Pass,"incoming render words change hit");
  misses([](Cache::Key& k) { k.pass.samplers[3].words[1]^=1; },M::Pass,"incoming sampler pass change hit");
  misses([](Cache::Key& k) { k.view.viewport^=1; },M::View,"pass viewport change hit");
  misses([](Cache::Key& k) { k.view.targets^=1; },M::View,"pass targets change hit");
  misses([](Cache::Key& k) { k.filtering=16; },M::Filtering,"filtering change hit");
  misses([](Cache::Key& k) { k.shaders+=1; },M::Shaders,"shader registry change hit");
  Require(cache.Candidate(key) && !NativeStaticWorldKeyDifferences(key,key),"unchanged key missed");
  Require(cache.Missed(NativeStaticWorldMissBit(M::Reads)|NativeStaticWorldMissBit(M::Stack))==1 &&
    cache.missed[size_t(M::Reads)]==1 && cache.missed[size_t(M::Stack)]==1 && cache.missed[size_t(M::Pass)]==0 &&
    cache.MissSummary().find("reads=1")!=std::string::npos,"miss diagnostic miscounted its components");
  // The stack is not keyed: another call depth hits while the witness holds,
  // and misses only where the assessment would decline (the 4 KB below it
  // aliasing an alias-checked read, or no room for it).
  Require(lookup(key,constants,frame_stack-0x230)==identity && resolves==1,"stack change alone resolved again");
  Require(cache.Find(0x5000)->observed.stack==frame_stack,"a hit replaced the stored observation");
  Require(lookup(key,constants,0x1000)!=identity && resolves==2,"stack window over a captured read reused eligibility");
  Require(lookup(key,constants,0x800)!=identity && resolves==3,"stack without room for the window reused eligibility");
  auto stacked=lookup(key,constants);
  Require(stacked!=identity && resolves==3 && lookup(key,constants,frame_stack+0x100)==stacked && resolves==3,
    "restored stack did not hit");
  // Retirement slot and fence: per-frame words, checked as the predicate.
  const auto word=[&](uint32_t at,uint32_t value) { guest.StoreWord(at,value); };
  word(0x60,0xa0); // An old handle with no fence: the group would retire it through the queue.
  Require(lookup(key,constants)!=stacked && resolves==4,"unfenced retirement reused eligibility");
  word(0x64,200); stacked=lookup(key,constants);
  Require(resolves==4,"fenced retirement of an unaliased handle resolved again");
  word(0x64,201); word(0x60,0xb0);
  Require(lookup(key,constants)==stacked && resolves==4,"fence advance or moved retired handle resolved again");
  word(0x60,0x78); // Its fence store at 0x80 lands on a captured read.
  Require(lookup(key,constants)!=stacked && resolves==5,"retirement aliasing a captured read reused eligibility");
  word(0x60,0); word(0x64,0);
  stacked=lookup(key,constants);
  Require(resolves==5 && lookup(key,constants)==stacked && resolves==5,"retirement-free frame did not hit");
  // Constants: without a derivable camera every constant is compared.
  auto tinted=constants; tinted[0].registers[3]^=1;
  Require(lookup(key,tinted)!=stacked && resolves==6,"changed material constant reused the material");
  NativeStaticWorldMiss why{};
  size_t differed=0;
  Require(!Cache::Current(*cache.Find(0x5000),constants,nullptr,camera,&why,&differed) && why==M::Constants && differed==0,
    "constant miss diagnostic named another constant");
  auto moved=constants; moved[1].registers[7]^=1;
  Require(lookup(key,moved)!=stacked && resolves==7,"underivable camera change reused the material");
  const auto current=lookup(key,constants);
  Require(resolves==8 && lookup(key,constants)==current && resolves==8,"restored constants did not hit");
  // Eligibility: a changed guest byte the assessment read resolves again.
  memory[0x85]^=1;
  Require(lookup(key,constants)!=current && resolves==9,"changed eligibility input reused the entry");
  Require(cache.Candidate(key) && lookup(key,constants) && resolves==9,"revalidated eligibility did not hit");
  // A republish of the same program and constants (the preload's refresh of
  // a constant the pass replaces or zeroes) is not keyed: it still hits. Its
  // constants are compared, so a changed one misses.
  const auto before=lookup(key,constants);
  const auto republished=publish(program);
  Require(republished!=material && lookup(key,republished->constants)==before && resolves==9,
    "republished group material with an unchanged program and constants resolved again");
  auto retinted=republished->constants; retinted[0].registers[0]^=1;
  Require(lookup(key,retinted)!=before && resolves==10,"republished changed constant reused the old resolve");
  auto reprogrammed=key;
  reprogrammed.program=std::make_shared<NativeSceneMaterialProgram>(*program);
  Require(lookup(reprogrammed,constants) && resolves==11 && !cache.Candidate(key),"republished program reused the old resolve");
  cache.Invalidate(0x5000);
  Require(!cache.Candidate(reprogrammed) && cache.size()==0,"invalidated group survived");
  cache.Clear();
}
// A BasicLockable that records every acquisition, so tests can count them.
// The composed handoff against sequential guest groups that also bind shaders
// (with defaults over registers and a sampler record) and read a global
// constant and a global texture: no group outside the replay plan reaches the
// bind callback (ActivateNativeMaterial in production) unless it has no
// effects, and the device block and every touched page match sequential
// groups, with a live fence and with the deferred queue.
void StaticWorldComposedBinds() {
  constexpr uint32_t device=0x10000,queue=0x30000,common=0x100;
  using Pages=std::map<uint32_t,std::array<uint8_t,4096>>;
  struct Group {
    NativeStaticWorldBindEffects effects;
    std::vector<std::array<uint32_t,2>> states;
    NativeSceneMaterialProgram program;
  };
  const auto run=[&](uint32_t fence,bool fallback) {
    Pages initial;
    const SparseReader setup{initial};
    setup.StoreWord(0x8200964c,0x3b808081u); setup.StoreWord(0x82003198,0x42000000u);
    for(uint32_t i=0;i<6;++i) setup.StoreWord(0x82009608+i*4,i*2);
    setup.StoreWord(device+10780,fence); setup.StoreWord(device+10784,common);
    setup.StoreWord(device+13148,queue); setup.StoreWord(device+13152,queue+8*32);
    setup.StoreWord(device+12184,1); setup.StoreWord(device+12168,1);
    for(uint32_t offset:{10424u,10456u,10460u,10464u}) setup.StoreWord(device+offset,0x10001);
    setup.StoreWord(device+10420,0x70); setup.StoreWord(device+10440,0x5); setup.StoreWord(device+10428,0x80000007);
    setup.StoreWord(device+10332,15); setup.StoreWord(device+11576,0x10001); setup.StoreWord(device+11580,0);
    setup.StoreWord(device+11588,15); setup.StoreByte(device+10810,0xc1);
    for(uint32_t slot=0;slot<16;++slot) {
      for(uint32_t i=0;i<6;++i) setup.StoreWord(device+1024+slot*24+i*4,0x9e3779b9u*(slot*6+i+1));
      setup.StoreWord(device+12272+slot*4,0x40000+slot*0x100); setup.StoreWord(0x40000+slot*0x100,common);
      setup.StoreByte(device+11652+slot,uint8_t(slot%5)); setup.StoreByte(device+11678+slot,uint8_t(slot%3));
      setup.StoreByte(device+11704+slot,uint8_t(15-slot%4)); setup.StoreByte(device+11730+slot,uint8_t(slot%2?5:0));
    }
    for(uint32_t i=0;i<256;++i) setup.StoreWord(0x60000+i*4,0x3f800000u+i*0x1001u);
    for(uint32_t i=0;i<8;++i) setup.StoreWord(device+i*4,0x5a5a5a5au^(i*0x01010101u));
    // Bound shaders 0x49000 (vertex) and 0x48000 (pixel). Vertex A and pixel C
    // carry defaults: A copies vertex c20's first two words and masks slot 1's
    // record word +4; C copies pixel c5. B and D have none.
    constexpr uint32_t A=0x4a000,B=0x4b000,C=0x4c000,D=0x4d000;
    setup.StoreWord(device+12420,0x49000); setup.StoreWord(device+12416,0x48000);
    for(const uint32_t shader:{0x48000u,0x49000u,A,B,C,D}) setup.StoreWord(shader,common);
    const auto defaults=[&](uint32_t shader,bool pixel,uint64_t clear,bool shared,const std::vector<uint32_t>& entries) {
      const auto header=shader+(pixel?40:872),data=header+64;
      setup.StoreWord(header+20,64);
      setup.StoreDoubleWord(data,clear); setup.StoreDoubleWord(data+8,shared?1:0);
      setup.StoreWord(data+24,uint32_t(entries.size()*4));
      for(size_t i=0;i<entries.size();++i) setup.StoreWord(data+32+uint32_t(i)*4,entries[i]);
    };
    defaults(A,false,0xffff000000000000ull,true,{0,(1088u<<16)|2,0x11111111,0x22222222,0,(28u<<16)|2,0xffff0000u,0x1234,0});
    defaults(C,true,0x00000000ffffffffull,false,{0,(4944u<<16)|4,1,2,3,4,0,0});
    const auto half=std::bit_cast<uint32_t>(.5f),quarter=std::bit_cast<uint32_t>(-.25f);
    const auto header=[&](uint32_t handle) {
      setup.StoreWord(handle,common);
      for(uint32_t i=0;i<6;++i) setup.StoreWord(handle+28+i*4,(0x85ebca6bu*(i+1))^(handle<<3));
    };
    // Resolved as the pass cursor sees it: a null handle binds nothing.
    const auto sampler=[&](uint32_t slot,uint32_t handle,std::array<uint32_t,4> settings) {
      NativeMaterialSamplerOperation result; result.slot=slot; result.settings=settings;
      if(!handle) return result;
      header(handle);
      result.texture_filter_high=setup.Word(handle+40)&0x80000000u;
      result.texture_lod=setup.Word(handle+44)&0x3fcu;
      return result;
    };
    const auto local=[&](uint32_t slot,uint32_t handle,std::array<uint32_t,4> settings) {
      NativeStaticWorldBindEffects::Texture result{false,slot,handle,{}};
      result.sampler.slot=slot; result.sampler.settings=settings;
      return result;
    };
    const auto constant=[](bool pixel,uint32_t first,uint32_t count,uint32_t data,bool global=false) {
      const auto low=first/4,high=(first+count-1)/4;
      return NativeStaticWorldBindEffects::Constant{pixel,global,first,count,data,(~uint64_t(0)>>low)&(~uint64_t(0)<<(63-high))};
    };
    // G1's pixel c5 is a global (storage through record 0x5f200) and its slot
    // 2 texture a global (source through record 0x5f000).
    setup.StoreWord(0x5f200,0x60340);
    setup.StoreWord(0x5f000,0x5f100); setup.StoreWord(0x5f100+28,0x52000);
    const std::array<uint32_t,4> global_settings{half,1,0,1};
    for(uint32_t i=0;i<4;++i) setup.StoreWord(0x5f100+32+i*4,global_settings[i]);
    const auto group=[&](uint32_t vertex,uint32_t pixel,std::vector<std::array<uint32_t,2>> states,
                         std::vector<NativeStaticWorldBindEffects::Constant> constants,
                         std::vector<std::pair<NativeStaticWorldBindEffects::Texture,NativeMaterialSamplerOperation>> textures) {
      Group result;
      result.effects.shaders={vertex,pixel};
      result.effects.defaults={ReadNativeShaderDefaults(setup,vertex,false),ReadNativeShaderDefaults(setup,pixel,true)};
      result.effects.constants=std::move(constants);
      result.states=std::move(states);
      result.program.inputs.state_overrides=result.states;
      for(auto& [texture,operation]:textures) {
        result.effects.textures.push_back(texture); result.program.sampler_operations.push_back(operation);
      }
      return result;
    };
    // Only G3, the last group and every slot's last binder, is replayed:
    // slot 0 binds 50000, 53000, 54000; slot 1 51000, 57000, then null.
    std::vector<Group> groups;
    groups.push_back(group(A,C,{{0x3c,1}},{constant(false,20,2,0x60000)},
      {{local(0,0x50000,{half,1,1,1}),sampler(0,0x50000,{half,1,1,1})},{local(1,0x51000,{quarter,2,0,1}),sampler(1,0x51000,{quarter,2,0,1})}}));
    NativeStaticWorldBindEffects::Texture global_texture{true,2,0x5f000,{}};
    global_texture.sampler.slot=2;
    groups.push_back(group(B,C,{{0x48,6}},{constant(true,5,1,0x5f200,true)},
      {{global_texture,sampler(2,0x52000,global_settings)}}));
    groups.push_back(group(A,D,{{0x34,2}},{constant(false,22,1,0x60200)},
      {{local(0,0x53000,{0,1,1,0}),sampler(0,0x53000,{0,1,1,0})},{local(1,0x57000,{half,0,1,1}),sampler(1,0x57000,{half,0,1,1})}}));
    groups.push_back(group(B,D,{{0x4c,7}},{constant(true,6,2,0x60300)},
      {{local(0,0x54000,{quarter,1,0,1}),sampler(0,0x54000,{quarter,1,0,1})},{local(1,0,{half,1,0,1}),sampler(1,0,{half,1,0,1})},
       {local(2,0x55000,{0,1,1,1}),sampler(2,0x55000,{0,1,1,1})}}));
    const auto reserve=[]()->uint32_t { throw std::runtime_error("unexpected retirement allocation"); };
    const auto tag=[]()->uint32_t { return 0x80000000u; };
    // The guest activation: shaders, constants, textures, then states.
    const auto activate=[&](const SparseReader& reader,const Group& selected,bool states) {
      for(const bool pixel:{false,true}) SetNativeShaderResource(reader,device,selected.effects.shaders[pixel],pixel,reserve,tag);
      for(const auto& operation:selected.effects.constants) {
        const NativeMaterialCpuProgram::Constant upload{operation.pixel,operation.first,
          operation.global?reader.Word(operation.data):operation.data,operation.count};
        Require(UploadNativeMaterialConstant(reader,device,upload,operation.mask),"composed fixture constant aliases");
      }
      for(auto bound:selected.effects.textures) {
        if(bound.global) {
          const auto source=reader.Word(bound.handle);
          bound.handle=reader.Word(source+28);
          for(uint32_t i=0;i<4;++i) bound.sampler.settings[i]=reader.Word(source+32+i*4);
        }
        const uint64_t mask=uint64_t(1)<<(43-bound.slot);
        SetNativeTextureResource(reader,device,bound.slot,bound.handle,mask,reserve,tag);
        const auto resolved=ApplyNativeMaterialSampler(ReadNativeMaterialSamplerPass(reader,device,bound.slot),bound.sampler);
        reader.StoreWord(device+1036+bound.slot*24,resolved.words[1]);
        reader.StoreWord(device+1040+bound.slot*24,resolved.words[2]);
        reader.StoreDoubleWord(device+16,reader.DoubleWord(device+16)|mask);
      }
      if(states) for(const auto& [offset,value]:selected.states) {
        const auto writes=NativeMaterialStateCpuWrites(ReadNativeMaterialRenderPassMirrors(reader,device),
          offset,value,reader.DoubleWord(device+16),reader.DoubleWord(device+24));
        Require(writes.has_value(),"composed fixture state needs a callback");
        for(const auto& [address,word]:*writes) reader.StoreWord(device+address,word);
      }
    };
    Pages sequential=initial;
    {
      const SparseReader reader{sequential};
      for(const auto& selected:groups) activate(reader,selected,true);
    }
    Pages native=initial;
    const SparseReader reader{native};
    NativeSceneMaterialPassState pass;
    pass.render=ReadNativeMaterialRenderPass(reader,device);
    for(uint32_t slot=0;slot<16;++slot) pass.samplers[slot]=ReadNativeMaterialSamplerPass(reader,device,slot);
    auto cursor=pass.Inputs();
    const auto start=cursor.render;
    std::vector<NativeStaticWorldHandoffGroup> owed;
    for(size_t index=0;index<groups.size();++index) {
      const auto& selected=groups[index];
      uint32_t slots=0;
      for(const auto& operation:selected.program.sampler_operations) slots|=1u<<operation.slot;
      owed.push_back({selected.states,slots,fallback && index==1?nullptr:&selected.effects});
      cursor=cursor.After(selected.program);
    }
    std::vector<size_t> replayed,bound;
    std::vector<NativeStaticWorldRetirement> tags;
    const auto writes=HandOffNativeStaticWorld(reader,device,start,cursor.samplers,owed,[&](size_t index) {
      replayed.push_back(index); activate(reader,groups[index],true);
    },[&](size_t index) {
      bound.push_back(index); activate(reader,groups[index],false);
    },[](NativeStaticWorldRetirement)->uint32_t { throw std::runtime_error("unexpected composed retirement allocation"); },
      [&](NativeStaticWorldRetirement kind)->uint32_t { tags.push_back(kind); return 0x80000000u; });
    Require(writes.render==cursor.render,"composed handoff render state diverged from the pass cursor");
    Require(replayed==std::vector<size_t>{3},"composed handoff replayed other than the last group");
    Require(bound==(fallback?std::vector<size_t>{1}:std::vector<size_t>{}),
      "composed handoff ran the activation for a group with effects, or skipped one without");
    // The deferred path asks for a tag per composed retirement of a handle
    // with the common bit: two shaders and each replaced texture per group.
    using R=NativeStaticWorldRetirement;
    const std::vector<R> composed_tags=fallback?
      std::vector<R>{R::Vertex,R::Pixel,R::Texture,R::Texture,R::Vertex,R::Pixel,R::Texture,R::Texture}:
      std::vector<R>{R::Vertex,R::Pixel,R::Texture,R::Texture,R::Vertex,R::Pixel,R::Texture,R::Vertex,R::Pixel,R::Texture,R::Texture};
    Require(tags==(fence?std::vector<R>{}:composed_tags),"composed handoff retired other than in guest order");
    const SparseReader expected{sequential};
    for(uint32_t offset=0;offset<13520;offset+=4)
      if(reader.Word(device+offset)!=expected.Word(device+offset))
        throw std::runtime_error("composed handoff device word differs at +"+std::to_string(offset));
    // Registers only composed groups wrote: G0's vertex c21, and c20 under G2's
    // A defaults after G0's upload; G1's global pixel c5 over C's; G2's c22.
    Require(reader.Word(device+(21+112)*16)==expected.Word(0x60010) && reader.Word(device+(20+112)*16)==0x11111111u &&
      reader.Word(device+(20+112)*16+8)==expected.Word(0x60008) && reader.Word(device+(5+368)*16)==expected.Word(0x60340) &&
      reader.Word(device+(22+112)*16)==expected.Word(0x60200),"composed handoff lost registers only a composed group wrote");
    if(fence) for(const uint32_t handle:{0x49000u,0x48000u,A,B,C,0x40000u,0x40100u,0x50000u,0x51000u,0x40200u,0x53000u,0x57000u})
      Require(reader.Word(handle+8)==fence,"composed handoff skipped a retirement");
    const std::array<uint8_t,4096> zero{};
    for(const auto* pages:{&sequential,&native}) for(const auto& entry:*pages) {
      const auto page=entry.first;
      const auto left=sequential.find(page);
      const auto right=native.find(page);
      if((left==sequential.end()?zero:left->second)!=(right==native.end()?zero:right->second))
        throw std::runtime_error("composed handoff arena differs in page "+std::to_string(page));
    }
  };
  run(200,false); run(0,false); run(200,true); run(0,true);
}
struct CountingMutex {
  bool locked=false; uint64_t locks=0;
  void lock() { Require(!locked,"walk lock reacquired a held non-recursive mutex"); locked=true; ++locks; }
  void unlock() { Require(locked,"walk lock released an unheld mutex"); locked=false; }
};
void WalkLock() {
  using Scope=NativeWalkLockScope<CountingMutex>;
  using GuestCall=NativeWalkGuestCall<CountingMutex>;
  CountingMutex mutex;
  NativeSceneQueues queues,reference;
  // Two candidates' parts, gathered as the per-object pushes would be.
  const std::vector<std::pair<uint32_t,uint32_t>> parts{{0x500,0x1000},{0x600,0x1100},{0x500,0x1200},{0x600,0x1300}};
  for(const auto& [group,instance]:parts) reference.Push(group,instance);
  NativeGuestCallCached<uint32_t> view;
  uint32_t camera=7;
  {
    Scope walk(mutex);
    Require(Scope::current==&walk && !mutex.locked,"walk scope locked before first need");
    // One list: membership, sources, candidates and counters share one hold.
    for(const auto& [group,instance]:parts) { walk.Hold(); queues.Push(group,instance); }
    walk.Hold();
    Require(mutex.locks==1 && walk.acquisitions==1,"walk lock taken more than once between guest calls");
    Require(view.Get(Scope::guest_calls,[&] { return camera; })==7 &&
            view.Get(Scope::guest_calls,[&] { return camera+1; })==7 && view.reads==1,"view reread without a guest call");
    {
      GuestCall guest;
      Require(!mutex.locked && !Scope::current,"guest call ran under the walk lock");
      camera=9;
      // A hook re-entered from guest code owns its own scope and leaves the
      // mutex released when it returns to the guest.
      {
        Scope nested(mutex); nested.Hold();
        Require(Scope::current==&nested && mutex.locked,"nested gather did not take the lock");
        { GuestCall inner; Require(!mutex.locked,"nested guest call ran under the lock"); }
        Require(Scope::current==&nested && !mutex.locked,"nested guest call did not restore its scope");
      }
      Require(!Scope::current && !mutex.locked,"nested scope leaked into guest code");
    }
    Require(Scope::current==&walk && !mutex.locked,"guest call did not restore the walk scope");
    Require(view.Get(Scope::guest_calls,[&] { return camera; })==9 && view.reads==2,"view not reread after a guest call");
    walk.Hold(); walk.Hold();
    Require(walk.acquisitions==2 && mutex.locks==3,"walk lock not taken once after the guest call");
  }
  Require(!Scope::current && !mutex.locked,"walk scope did not release the lock");
  {
    // Two leaf lists in one walk: the hold ends at the list boundary, and a
    // native bucket insert is not a guest call.
    mutex.locks=0;
    Scope walk(mutex);
    const auto calls=Scope::guest_calls;
    uint32_t camera_reads=0;
    NativeGuestCallCached<uint32_t> walk_view;
    const auto list=[&](bool native_ok) {
      walk.Hold();
      Require(walk_view.Get(Scope::guest_calls,[&] { ++camera_reads; return camera; })==9,"walk view changed");
      bool native_ran=false,guest_ran=false;
      const bool guest=NativeWalkBucketDispatch<CountingMutex>(true,[&] {
        Require(mutex.locked && Scope::current==&walk,"native bucket insert ran outside the walk hold");
        native_ran=true; return native_ok;
      },[&] { Require(!mutex.locked && !Scope::current,"guest bucket dispatch ran under the walk lock"); guest_ran=true; });
      Require(native_ran && guest==!native_ok && guest_ran==!native_ok,"bucket dispatch route");
      if(native_ok) Require(mutex.locked,"native bucket insert released the walk lock");
      walk.Hold();
      walk.EndList();
      Require(!mutex.locked && Scope::current==&walk,"walk lock held past a list boundary");
    };
    list(true);
    Require(Scope::guest_calls==calls && walk.acquisitions==1 && mutex.locks==1,"native bucket insert counted as a guest call");
    list(true);
    Require(Scope::guest_calls==calls && walk.acquisitions==2 && walk.lists==2 && camera_reads==1,
            "list boundary did not release once, or reread the view");
    list(false);
    Require(Scope::guest_calls==calls+1 && walk.acquisitions==4,"guest bucket dispatch not a guest call");
    Require(walk_view.Get(Scope::guest_calls,[&] { ++camera_reads; return camera; })==9 && camera_reads==2,
            "view not reread after a guest bucket dispatch");
    // A long list releases the hold after the object budget.
    walk.Hold();
    for(uint32_t i=0;i+1<Scope::kObjectBudget;++i) walk.Advance();
    Require(mutex.locked,"walk lock released before the object budget");
    walk.Advance();
    Require(!mutex.locked,"walk lock held past the object budget");
    walk.Advance();
    Require(!mutex.locked && walk.acquisitions==5,"object budget reacquired the lock");
  }
  Require(!Scope::current && !mutex.locked,"list walk scope did not release the lock");
  for(const uint32_t group:{0x500u,0x600u})
    Require(queues.Take(group)==reference.Take(group),"walk-locked queue order differs from per-object pushes");
  // Exceptions unwind through the scope without leaving the mutex held.
  Reject([&] { Scope walk(mutex); walk.Hold(); throw std::runtime_error("walk failed"); });
  Require(!Scope::current && !mutex.locked,"failed walk left the lock held");
}
void StaticWalkPlan() {
  using Route=NativeStaticWalkRoute;
  Require(ClassifyNativeStaticWalk(1,0,true)==Route::Hidden && ClassifyNativeStaticWalk(0,0,true)==Route::Direct &&
    ClassifyNativeStaticWalk(0,0,false)==Route::Virtual && ClassifyNativeStaticWalk(0,1,false)==Route::Bucket &&
    ClassifyNativeStaticWalk(0,2,true)==Route::Bucket && ClassifyNativeStaticWalk(0,3,true)==Route::Unknown &&
    ClassifyNativeStaticWalk(0,0xFFFFFFFFu,true)==Route::Unknown,"static walk route order differs from sub_821C0C00");
  std::vector<uint8_t> memory(0x10000);
  const GeometryRetryReader r{memory};
  constexpr uint32_t owner=0x1000,levels=0x2000,root=0x3000,leaf=root+120,world_list=owner+372;
  constexpr uint32_t first=0x5000,second=0x5010,sentinel=0x5100,a=0x6000,b=0x6100,va=0x7000,vb=0x7100;
  r.StoreWord(owner+52,levels); r.StoreWord(owner+56,levels+32);
  r.StoreWord(levels+20,root); r.StoreWord(levels+24,root+288);
  r.StoreWord(root+116,1);  // Occupied leaf; the second root stays empty.
  r.StoreWord(world_list,world_list+8); r.StoreWord(world_list+12,world_list+8);
  r.StoreWord(leaf,first); r.StoreWord(leaf+12,sentinel);
  r.StoreWord(first,second); r.StoreWord(first+8,a); r.StoreWord(second,sentinel); r.StoreWord(second+8,b);
  r.StoreWord(a,va); r.StoreWord(b,vb); r.StoreWord(b+52,1); r.StoreWord(a+64,0);
  r.StoreWord(va+16,kNativeStaticDirectRender); r.StoreWord(vb+16,0x820B0000);
  std::vector<uint32_t> lists;
  CollectNativeSceneLeafLists(r,owner,lists);
  Require(lists==std::vector<uint32_t>{leaf} && r.Word(owner+100)==0,"leaf collection differs or wrote walk counters");
  NativeSceneSources sources;
  sources.Born(a);
  NativeSceneVisibility visibility; visibility.radius=2; visibility.lod_count=1;
  const auto born=sources.CandidateRevision();
  Require(sources.PublishVisibility(a,visibility) && sources.CandidateRevision()==born+1,"visibility did not move the candidate revision");
  sources.PublishWorld(a,NativeSceneSources::World{});
  Require(sources.CandidateRevision()==born+1 && sources.AcquireSnapshot()->CandidateRevision()==born+1,
    "world registers moved the candidate revision or a snapshot lost it");
  const auto find=[&](uint32_t object) { return sources.FindCandidate(object); };
  NativeStaticWalkPlans plans;
  const auto anchors=std::make_unique<NativeAddressFilter>();
  plans.SetAnchorFilter(anchors.get());
  Require(!anchors->MayContain(leaf) && !anchors->MayContain(first),"empty anchor filter holds a list");
  plans.Publish(r,owner,1,sources.CandidateRevision(),find);
  Require(anchors->MayContain(leaf) && anchors->MayContain(first) && anchors->MayContain(second) &&
    anchors->MayContain(world_list),"a planned list header or member node is missing from the anchor filter");
  const auto built=plans.Acquire(leaf);
  Require(built && plans.Acquire(world_list) && plans.Acquire(world_list)->members.empty() && plans.stats().builds==2,
    "static walk plan did not cover the leaves and world+372");
  Require(built->Head()==first && built->end==sentinel && built->Next(0)==second && built->Next(1)==sentinel &&
    built->members.size()==2 && built->members[0].owner==a && built->members[0].direct && built->members[0].mode==0 &&
    !built->members[1].direct && built->members[1].mode==1 && built->members[0].source.registered &&
    built->members[0].source.visibility && *built->members[0].source.visibility==visibility &&
    !built->members[1].source.registered,"static walk plan members");
  // The vtable slot is trusted only through the vtable it was read with.
  uint32_t slot_reads=0;
  const auto read_slot=[&](uint32_t vtable) { ++slot_reads; return r.Word(vtable+16); };
  Require(PlannedNativeStaticDirect(built->members[0],va,read_slot) && slot_reads==0 &&
    !PlannedNativeStaticDirect(built->members[0],vb,read_slot) && slot_reads==1,"planned vtable slot reuse");
  plans.Publish(r,owner,1,sources.CandidateRevision(),find);
  Require(plans.Acquire(leaf)==built && plans.stats().reuses==2 && plans.stats().collections==1,"unchanged plan was rebuilt");
  // New sources refresh candidates without re-reading membership.
  visibility.radius=3; sources.PublishVisibility(a,visibility);
  const auto touches=plans.Touches();
  plans.Publish(r,owner,1,sources.CandidateRevision(),find);
  const auto refreshed=plans.Acquire(leaf);
  Require(refreshed!=built && plans.stats().refreshes==2 && refreshed->membership==built->membership &&
    plans.Current(leaf,*built) && plans.Touches()==touches && refreshed->members[0].source.visibility->radius==3 &&
    built->members[0].source.visibility->radius==2,"source refresh changed membership or a published plan");
  // Gameplay rewrites mode and hidden: the plan keeps its advisory copy, the
  // audit counts drift and no route mismatch when the live words are used.
  r.StoreWord(a+52,2);
  NativeStaticWalkAudit audit;
  Require(AuditNativeStaticWalkMember(*refreshed,0,first,second,a,audit),"aligned member flagged");
  AuditNativeStaticWalkClassification(refreshed->members[0],va,r.Word(a+52),r.Word(a+64)>>16,true,&refreshed->members[0].source,audit);
  Require(audit.drift==1 && !audit.mismatches(),"live mode drift counted as a mismatch");
  AuditNativeStaticWalkClassification(refreshed->members[0],va,0,0,false,&built->members[0].source,audit);
  Require(audit.routes==1 && audit.sources==1,"planned slot or stale source not counted");
  Require(!AuditNativeStaticWalkMember(*refreshed,1,second,sentinel,a,audit) &&
    !AuditNativeStaticWalkMember(*refreshed,2,sentinel,sentinel,b,audit) && audit.membership==2,"membership mismatch not counted");
  // A member unlink (821A1678 hook) drops the plan by its node index.
  r.StoreWord(first,sentinel);
  Require(plans.Touch(second) && !plans.Acquire(leaf) && plans.Touches()!=touches && !plans.Current(leaf,*refreshed) &&
    !plans.Touch(second),"member touch did not drop the list plan");
  plans.Publish(r,owner,1,sources.CandidateRevision(),find);
  const auto unlinked=plans.Acquire(leaf);
  Require(unlinked && unlinked->members.size()==1 && unlinked->membership!=refreshed->membership && !plans.Touch(second) &&
    plans.nodes()==1,"rebuilt plan kept an unlinked member");
  Require(plans.Touch(leaf) && !plans.Acquire(leaf),"list header touch did not drop the plan");
  plans.Publish(r,owner,1,sources.CandidateRevision(),find);
  // A header rewritten without a hook is caught by the step's header check.
  r.StoreWord(leaf,sentinel);
  plans.Publish(r,owner,1,sources.CandidateRevision(),find);
  Require(plans.Acquire(leaf)->members.empty(),"untracked header change kept the old plan");
  // A tree epoch move recollects the leaf set.
  r.StoreWord(root+144+116,1);
  r.StoreWord(root+144+120,sentinel+16); r.StoreWord(root+144+132,sentinel+16);
  plans.Publish(r,owner,1,sources.CandidateRevision(),find);
  Require(!plans.Acquire(root+144+120),"leaf set recollected without an epoch move");
  plans.Publish(r,owner,2,sources.CandidateRevision(),find);
  Require(plans.stats().collections==2 && plans.Acquire(root+144+120) && plans.lists()==3,"epoch move did not recollect leaves");
  r.StoreWord(root+116,0);
  plans.Publish(r,owner,3,sources.CandidateRevision(),find);
  Require(!plans.Acquire(leaf) && plans.lists()==2,"vacated leaf kept its plan");
  plans.Retire(owner);
  Require(!plans.Acquire(world_list) && !plans.Acquire(root+144+120) && plans.lists()==0 && plans.nodes()==0,
    "retired world kept plans");
  // A torn list is rejected, not planned.
  r.StoreWord(owner+372,0);
  Reject([&] { plans.Publish(r,owner,4,sources.CandidateRevision(),find); });
}
// The full-frame static world from published data only. Octree: root0 an
// inside leaf, root1 a leaf behind the camera, root2 an intersecting internal
// node whose child 0 is inside and child 1 outside. Frustum: |x|<z, |y|<z,
// 1<=z<=1000, identity view, depth = z.
void FullFrameStaticWorld() {
  std::vector<uint8_t> memory(0x10000);
  const GeometryRetryReader r{memory};
  const auto store=[&](uint32_t at,std::initializer_list<float> values) {
    for(const auto value:values) { r.StoreWord(at,std::bit_cast<uint32_t>(value)); at+=4; }
  };
  constexpr uint32_t world=0x1000,levels=0x2000,root0=0x3000,root1=root0+144,root2=root1+144,children=0x4000;
  constexpr uint32_t child_a=children,child_b=children+144;
  r.StoreWord(world+52,levels); r.StoreWord(world+56,levels+32);
  r.StoreWord(levels+20,root0); r.StoreWord(levels+24,root2+144);
  const auto node=[&](uint32_t at,std::array<float,3> center,float extent,float radius) {
    r.StoreWord(at+116,1); store(at+32,{center[0],center[1],center[2],1,extent,extent,extent,0,radius});
  };
  node(root0,{0,0,50},5,5); node(root1,{0,0,-50},5,5); node(root2,{30,0,50},30,30);
  for(uint32_t i=0;i<8;++i) r.StoreWord(root2+84+i*4,children+i*144);
  node(child_a,{0,0,50},1,1); node(child_b,{60,0,20},1,1);
  const std::shared_ptr<const NativeSceneTreeImage> image=CaptureNativeSceneTree(r,world);
  // Objects: A near (LOD 0), B far (LOD 1), C beyond its distance, D outside the
  // frustum, E bucket mode, F hidden, G a second leaf, H only in the culled
  // leaf, I in world+372 with a group outside the order, J without route words.
  constexpr uint32_t A=0x6000,B=0x6100,C=0x6200,D=0x6300,E=0x6400,F=0x6500,G=0x6600,H=0x6700,I=0x6800,J=0x6900;
  constexpr uint32_t G1=0x8100,G2=0x8200,G3=0x8300;
  NativeSceneSources sources;
  // The live route words the frame reads, per owner (J has none: unreadable).
  std::map<uint32_t,NativeFullFrameStaticRoute> live;
  std::vector<uint32_t> route_reads;
  const NativeFullFrameStaticRouteRead routes=[&](uint32_t owner)->std::optional<NativeFullFrameStaticRoute> {
    route_reads.push_back(owner);
    const auto found=live.find(owner);
    if(found==live.end()) return std::nullopt;
    return found->second;
  };
  std::map<uint32_t,NativeSceneVisibility> records;
  // Each owner's world: identity with its own x translation (row-major registers).
  const auto translation=[](uint32_t owner) { return float(owner>>8); };
  const auto object=[&](uint32_t owner,std::array<float,3> center,float distance,uint32_t lods,
      std::vector<NativeSceneSources::Part> parts,std::optional<NativeFullFrameStaticRoute> route=NativeFullFrameStaticRoute{true,0,0}) {
    sources.Born(owner);
    for(auto& part:parts) { part.world_data=owner+0x80; part.world_first=0; }
    Require(sources.Observe(owner,parts),"full-frame fixture parts");
    NativeSceneVisibility visibility;
    visibility.box={center[0],center[1],center[2],1, 1,0,0,0, 0,1,0,0, 0,0,1,0};
    visibility.radius=1.7f; visibility.distance=distance; visibility.lod_count=lods; visibility.lod_thresholds={100,0};
    sources.PublishVisibility(owner,visibility); records[owner]=visibility;
    NativeSceneSources::World registers{};
    auto matrix=kNativeSceneIdentity; matrix[12]=translation(owner);
    for(size_t i=0;i<16;++i) for(size_t byte=0;byte<4;++byte)
      registers[i*4+byte]=uint8_t(std::bit_cast<uint32_t>(matrix[i])>>(24-byte*8));
    sources.PublishWorld(owner,registers);
    if(route) live[owner]=*route;
  };
  using P=NativeSceneSources::Part;
  object(A,{0,0,50},1000,2,{P{0x7000,0,0,0,G1},P{0x7100,1,0,0,G1}});
  object(B,{0,0,300},1000,2,{P{0x7200,0,0,0,G2},P{0x7300,1,0,0,G1}});
  object(C,{0,0,500},400,1,{P{0x7700,0,0,0,G1}});
  object(D,{100,0,50},1000,1,{P{0x7800,0,0,0,G1}});
  object(E,{0,0,50},1000,1,{P{0x7900,0,0,0,G1}},NativeFullFrameStaticRoute{true,1,0});
  object(F,{0,0,50},1000,1,{P{0x7a00,0,0,0,G1}},NativeFullFrameStaticRoute{true,0,1});
  object(G,{0,5,60},1000,1,{P{0x7400,0,0,0,G2},P{0x7500,0,1,0,G1}});
  object(H,{0,0,50},1000,1,{P{0x7b00,0,0,0,G1}});
  object(I,{0,0,40},1000,1,{P{0x7600,0,0,0,G3}});
  object(J,{0,0,50},1000,1,{P{0x7c00,0,0,0,G1}},std::nullopt);
  auto lists=std::make_shared<NativeSceneMembership::Publication>();
  const auto list=[&](uint32_t address,std::vector<uint32_t> owners) {
    auto snapshot=std::make_shared<NativeSceneMembership::Snapshot>(); snapshot->end=address+8;
    for(const auto owner:owners) snapshot->members.push_back({owner+0x10,owner});
    lists->lists.Set(address,std::move(snapshot));
  };
  list(root0+120,{A,B,C,D,E,F}); list(root1+120,{H}); list(child_a+120,{A,G}); list(world+372,{I,J});
  NativeScenePublication publication;
  publication.sources=sources.AcquireSnapshot(); publication.membership=lists;
  publication.trees[world]=image;
  publication.group_order.Set(world,std::make_shared<const NativeSceneGroupOrder>(NativeSceneGroupOrder{G2,G1}));
  NativeFullFrameStaticCamera camera;
  camera.visibility.matrix=kNativeSceneIdentity; camera.visibility.depth_scale=-1;
  auto& f=camera.visibility.frustum;
  f[8]=1; f[10]=-1; f[12]=-1; f[14]=-1; f[17]=1; f[18]=-1; f[21]=-1; f[22]=-1; f[24]=1; f[25]=1000;
  // No guest reads but the route words: the arena is gone before the frame is
  // selected, and a read outside the published image throws instead of
  // reaching memory.
  std::fill(memory.begin(),memory.end(),uint8_t(0xcd));
  const NativeSceneTreeImageReader image_reader(*image);
  Reject([&] { image_reader.Word(root1+120); });
  Reject([&] { image_reader.Word(world+100); });
  // The flat index answers every read as the map does: each captured word of
  // every node, alternating regions, and a read past a region's end throws.
  NativeSceneTreeImageReader::Index index;
  NativeSceneTreeImageReader::Build(*image,index);
  Require(index.size()==image->regions->size(),"tree image index lost a region");
  const NativeSceneTreeImageReader indexed(*image,&index);
  for(const auto at:{root0,root1,root2,child_a,child_b}) for(const auto offset:{116u,32u,84u,64u,48u,88u,36u,116u}) {
    const auto address=at+offset;
    bool plain=true,flat=true;
    uint32_t a=0,b=0;
    try { a=image_reader.Word(address); } catch(const std::exception&) { plain=false; }
    try { b=indexed.Word(address); } catch(const std::exception&) { flat=false; }
    Require(plain==flat && a==b,"indexed tree image read differs from the map's");
  }
  Reject([&] { indexed.Word(root1+120); });
  Reject([&] { indexed.Word(world+100); });
  Reject([&] { indexed.Word(0x10); });
  using Object=NativeFullFrameStaticSelection::Object;
  const auto selection=SelectNativeFullFrameStaticWorld(publication,camera,routes);
  const auto& s=selection.stats;
  Require(s.worlds==1 && s.nodes_classified==5 && s.lists==3 && !s.missing_lists && s.members==10 && s.duplicates==1 &&
    s.unrouted==1 && s.not_direct==2 && s.culled_distance==1 && s.culled_frustum==1 && s.visible==4 && s.selected==4 &&
    s.parts==5 && s.unordered_groups==1 && s.unordered_parts==1 && s.tree_reads>0 && s.route_reads==7,
    "full-frame traversal or culling statistics");
  // Route words are read once per object culling keeps, in walk order: never
  // for C (distance), D (frustum), H (culled leaf) or the duplicate A.
  Require(route_reads==std::vector<uint32_t>{A,B,E,F,G,I,J},"full-frame route words read for culled or duplicate objects");
  // Live semantics: a mode written after the step (no publication) routes E
  // natively on the next frame, and a hidden write drops A.
  live[E].mode=0; live[A].hidden=1;
  const auto rerouted=SelectNativeFullFrameStaticWorld(publication,camera,routes);
  Require(rerouted.objects==std::vector<Object>{{B,1},{E,0},{G,0},{I,0}},"full-frame selection used stale route words");
  live[E].mode=1; live[A].hidden=0;
  Require(selection.objects==std::vector<Object>{{A,0},{B,1},{G,0},{I,0}},"full-frame traversal selected other objects");
  Require(selection.owners.size()==1 && selection.owners[0].owner==world && selection.owners[0].groups.size()==2 &&
    selection.owners[0].groups[0].group==G2 && selection.owners[0].groups[0].instances==std::vector<uint32_t>{0x7400} &&
    selection.owners[0].groups[1].group==G1 && selection.owners[0].groups[1].instances==std::vector<uint32_t>{0x7500,0x7300,0x7000},
    "full-frame selection lost the published group order or the queue order");
  // The LOD is the one the existing selection computes from the view depth.
  for(const auto& chosen:selection.objects) {
    const auto& record=records.at(chosen.owner);
    const auto center=NativeVisibilityTransform({record.box[0],record.box[1],record.box[2],record.box[3]},camera.visibility.matrix);
    Require(chosen.lod==NativeVisibilityLod(record,-float(center[2]*camera.visibility.depth_scale)),"full-frame LOD differs from NativeVisibilityLod");
  }
  // A camera with three times the depth scale: A (150) switches to LOD 1, B
  // (900) stays at LOD 1 inside its distance, the frustum set is unchanged.
  auto near_camera=camera;
  near_camera.visibility.depth_scale=-3;
  const auto moved=SelectNativeFullFrameStaticWorld(publication,near_camera,routes);
  Require(moved.objects==std::vector<Object>{{A,1},{B,1},{G,0},{I,0}} &&
    moved.owners.at(0).groups.at(1).instances==std::vector<uint32_t>{0x7500,0x7300,0x7100},
    "full-frame LOD ignored the camera depth");
  // Draws: one instanced draw per group, in the published order, each group's
  // material resolved once and cached across frames.
  auto program=std::make_shared<NativeSceneMaterialProgram>();
  program->inputs.vertex=0x9100; program->inputs.pixel=0x9200;
  program->inputs.vertex_registers.push_back({"g_mWorld",0,4});
  for(const auto group:{G1,G2,G3}) {
    const auto revision=publication.sources->FindGroup(group)->revision;
    auto material=std::make_shared<NativeSceneGroupMaterial>(); material->group=group; material->revision=revision; material->program=program;
    // Sorted and unique by group, as NativeSceneAdapter publishes them.
    publication.group_materials.Assign(material,NativeSceneGroupKey{});
    auto geometry=std::make_shared<NativeSceneGroupGeometry>(); geometry->group=group; geometry->revision=revision;
    publication.group_geometry.Assign(geometry,NativeSceneGroupKey{});
  }
  NativeFullFrameStaticPass pass;
  pass.targets.dsv_format=DXGI_FORMAT_D24_UNORM_S8_UINT; pass.targets.rtv_format[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
  const auto base=NativeFullFrameStaticBaseState(pass.targets);
  const auto decoded=DecodeNativeRenderState(base.render.words);
  // Reverse depth (GREATER_EQUAL, D3D11 7) and D3DCULL_CW (CCW front), as 8219E3B8 sets them.
  Require(decoded.depth_enable && decoded.depth_write && decoded.depth_func==7 && !decoded.blend_enable &&
    decoded.write_mask==15 && decoded.cull==kNativeCullBack && decoded.front_counter_clockwise && !decoded.scissor,
    "full-frame static base state is not opaque depth-tested GREATER_EQUAL with CW culling");
  std::vector<uint32_t> resolved;
  const auto resolve=[&](const NativeSceneGroupMaterial& group,const auto&,const NativeSceneMaterialPassState& state,auto) {
    Require(state==base,"full-frame material resolved against a chained state");
    resolved.push_back(group.group);
    NativeFullFrameStaticMaterial result; result.render=state.render.words;
    return result;
  };
  NativeFullFrameStaticWorld pass_world;
  const auto frame=pass_world.Build(publication,camera,routes,pass,resolve);
  Require(resolved==std::vector<uint32_t>{G2,G1} && frame.stats.resolves==2 && !frame.stats.cache_hits &&
    frame.draws.size()==2 && frame.stats.instances==4 && frame.stats.fresh_objects==4 && !frame.stats.world_declines,
    "full-frame draws were not one resolve and one draw per group");
  const auto& g2=frame.draws[0]; const auto& g1=frame.draws[1];
  Require(g2.group==G2 && g2.instances.size()==1 && g1.group==G1 && g1.instances.size()==3,"full-frame draws lost the group order");
  const std::array<uint32_t,3> g1_owners{G,B,A};
  for(size_t i=0;i<g1.instances.size();++i) {
    const auto& instance=*g1.instances[i];
    Require(instance.object.geometry==g1.geometry && instance.object.material==g1.material &&
      instance.object.world[12]==translation(g1_owners[i]) && instance.previous==instance.object.world,
      "full-frame instance does not share its group's draw or lost its published world");
  }
  Require(g1.Snapshot().instances.size()==3,"full-frame draw snapshot");
  const auto again=pass_world.Build(publication,near_camera,routes,pass,resolve);
  Require(resolved.size()==2 && again.stats.cache_hits==again.stats.groups-again.stats.missing_group && !again.stats.resolves,
    "a moved camera re-resolved cached group materials");
  const auto same_draws=[](const NativeFullFrameStaticFrame& a,const NativeFullFrameStaticFrame& b) {
    if(a.draws.size()!=b.draws.size()) return false;
    for(size_t i=0;i<a.draws.size();++i) {
      const auto& x=a.draws[i]; const auto& y=b.draws[i];
      if(x.group!=y.group || x.material!=y.material || x.geometry!=y.geometry || x.instances.size()!=y.instances.size()) return false;
      for(size_t j=0;j<x.instances.size();++j) if(x.instances[j]!=y.instances[j]) return false;
    }
    return true;
  };
  // Nothing moved: the previous frame whole.
  const auto still=pass_world.Build(publication,near_camera,routes,pass,resolve);
  Require(still.stats.reused_frame && same_draws(still,again) && resolved.size()==2,
    "an unchanged frame was built again");
  // Only the pass camera moved: each group's accepted constants (no camera
  // constant here) and its instances are reused.
  auto turned_camera=near_camera; turned_camera.pass.view[0]=0x3f800000;
  const auto turned=pass_world.Build(publication,turned_camera,routes,pass,resolve);
  Require(!turned.stats.reused_frame && turned.stats.camera_only==turned.stats.draws &&
    turned.stats.reused_draws==turned.stats.draws && turned.stats.cache_hits==turned.stats.draws && !turned.stats.resolves &&
    turned.stats.instances==again.stats.instances && same_draws(turned,again),"a moved pass camera rebuilt unchanged groups");
  auto other=pass; other.targets.reverse_depth=true;
  pass_world.Build(publication,camera,routes,other,resolve);
  Require(resolved.size()==4,"changed targets reused a cached material");
  // The selection cache changes what a selection costs, never what it is.
  const auto same_selection=[](const NativeFullFrameStaticSelection& a,const NativeFullFrameStaticSelection& b) {
    if(a.objects!=b.objects || a.owners.size()!=b.owners.size() || std::memcmp(&a.stats,&b.stats,sizeof(a.stats))) return false;
    for(size_t i=0;i<a.owners.size();++i) {
      const auto& x=a.owners[i]; const auto& y=b.owners[i];
      if(x.owner!=y.owner || x.groups.size()!=y.groups.size()) return false;
      for(size_t g=0;g<x.groups.size();++g) if(x.groups[g].group!=y.groups[g].group || x.groups[g].instances!=y.groups[g].instances) return false;
    }
    return true;
  };
  NativeFullFrameStaticSelectCache select_cache;
  Require(same_selection(SelectNativeFullFrameStaticWorld(publication,camera,routes,&select_cache),selection),
    "a cached full-frame selection differs");
  const auto builds=select_cache.stats.list_builds;
  for(const auto* view:{&near_camera,&camera,&near_camera})
    Require(same_selection(SelectNativeFullFrameStaticWorld(publication,*view,routes,&select_cache),
      SelectNativeFullFrameStaticWorld(publication,*view,routes)),"a cached full-frame selection differs");
  Require(builds==3 && select_cache.stats.list_builds==builds && select_cache.stats.list_hits>=9 &&
    select_cache.stats.invalidations==1,"the selection cache rebuilt unchanged lists");
  // A world republish: a new sources generation with the same candidates keeps the lists.
  sources.PublishWorld(A,NativeSceneSources::World{});
  auto republished=publication; republished.sources=sources.AcquireSnapshot();
  Require(republished.sources!=publication.sources && republished.sources->SameCandidates(*publication.sources) &&
    same_selection(SelectNativeFullFrameStaticWorld(republished,camera,routes,&select_cache),selection) &&
    select_cache.stats.invalidations==1 && select_cache.stats.list_builds==builds,"a world republish invalidated the selection cache");
  // What moved between the two generations: A's owner entry (its world), no
  // part. A re-observed owner moves its parts too: the dropped, the added
  // and the ones whose record changed (0x7c00's LOD).
  std::vector<uint32_t> moved_owners,moved_parts;
  const auto diff=[&](const NativeSceneSources& after,const NativeSceneSources& before) {
    moved_owners.clear(); moved_parts.clear();
    after.Differences(before,[&](uint32_t owner) { moved_owners.push_back(owner); },
      [&](uint32_t instance) { moved_parts.push_back(instance); });
  };
  diff(*republished.sources,*publication.sources);
  Require(moved_owners==std::vector<uint32_t>{A} && moved_parts.empty(),"a world republish moved other owners or parts");
  diff(*publication.sources,*publication.sources);
  Require(moved_owners.empty() && moved_parts.empty(),"a generation differs from itself");
  {
    NativeSceneSources producer=*republished.sources;  // A new lineage: shares its chunks until written.
    Require(producer.Observe(J,std::vector<NativeSceneSources::Part>{P{0x7c00,1,0,J+0x80,G1,0},P{0x7d00,0,0,J+0x80,G1,0}}),
      "re-observed fixture parts");
    diff(*producer.AcquireSnapshot(),*republished.sources);
    Require(moved_owners==std::vector<uint32_t>{J} && moved_parts==std::vector<uint32_t>{0x7c00,0x7d00},
      "a re-observed owner's parts did not move");
  }
  const NativeSceneSources copied=*republished.sources;
  Require(!copied.SameCandidates(*republished.sources),"a copied source producer kept its lineage");
  // Route words are never cached: a live write routes E natively through a warm cache.
  live[E].mode=0;
  const auto direct=SelectNativeFullFrameStaticWorld(republished,camera,routes,&select_cache);
  Require(same_selection(direct,SelectNativeFullFrameStaticWorld(republished,camera,routes)) &&
    select_cache.stats.list_builds==builds && std::ranges::count(direct.objects,NativeFullFrameStaticSelection::Object{E,0})==1,
    "a cached selection used stale route words");
  live[E].mode=1;
}
// NativeFullFrameStaticWorld across a run of gameplay-like frames: the camera
// moves every frame, some objects publish new worlds every frame (a new
// sources generation each time), the world animation advances and a few
// materials read it, and now and then an owner is born, retired or re-observed
// and a group material is republished. Each frame of one persistent pass
// equals a from-scratch build of the same inputs (groups, order, instances,
// worlds, views), while the caches keep what did not move. With
// --static-world-bench (static_world_bench) it runs 600 frames, checks every
// 50th and prints the persistent pass's mean select and build times.
bool static_world_bench=false;
void FullFrameStaticWorldFrames() {
  std::vector<uint8_t> memory(0x40000);
  const GeometryRetryReader r{memory};
  const auto store=[&](uint32_t at,std::initializer_list<float> values) {
    for(const auto value:values) { r.StoreWord(at,std::bit_cast<uint32_t>(value)); at+=4; }
  };
  // 160 inside-or-culled leaf roots on a 16x10 grid, each with its own list.
  constexpr uint32_t world=0x1000,levels=0x2000,roots=0x10000,kColumns=16,kRows=10,kLeaves=kColumns*kRows;
  r.StoreWord(world+52,levels); r.StoreWord(world+56,levels+32);
  r.StoreWord(levels+20,roots); r.StoreWord(levels+24,roots+kLeaves*144);
  const auto leaf_center=[](uint32_t leaf) {
    return std::array<float,3>{float(int(leaf%kColumns)*40-300),0.f,float(int(leaf/kColumns)*40+20)};
  };
  for(uint32_t leaf=0;leaf<kLeaves;++leaf) {
    const auto at=roots+leaf*144; const auto c=leaf_center(leaf);
    r.StoreWord(at+116,1); store(at+32,{c[0],c[1],c[2],1,20,20,20,0,35});
  }
  const std::shared_ptr<const NativeSceneTreeImage> image=CaptureNativeSceneTree(r,world);
  constexpr uint32_t kOwners=8000,kGroups=300,kOwnerBase=0x01000000,kGroupBase=0x02000000;
  const auto owner_of=[](uint32_t i) { return kOwnerBase+i*0x200; };
  const auto group_of=[](uint32_t i) { return kGroupBase+(i%kGroups)*16; };
  uint32_t seed=12345;
  const auto next=[&] { seed=seed*1664525u+1013904223u; return seed>>8; };
  const auto unit=[&] { return float(next()%20001)/10000.f-1.f; };
  NativeSceneSources sources;
  const auto registers_of=[](float x,float y,float z) {
    auto matrix=kNativeSceneIdentity; matrix[12]=x; matrix[13]=y; matrix[14]=z;
    NativeSceneSources::World registers{};
    for(size_t i=0;i<16;++i) for(size_t byte=0;byte<4;++byte)
      registers[i*4+byte]=uint8_t(std::bit_cast<uint32_t>(matrix[i])>>(24-byte*8));
    return registers;
  };
  std::vector<std::array<float,3>> centers(kOwners);
  const auto parts_of=[&](uint32_t i,uint32_t variant) {
    std::vector<NativeSceneSources::Part> parts;
    const uint32_t count=1+(i%3==0);
    for(uint32_t p=0;p<count;++p) {
      NativeSceneSources::Part part{0x20000000+i*64+p*28,0,p,owner_of(i)+0x80,group_of(i+p*7+variant)};
      part.world_first=0; parts.push_back(part);
    }
    return parts;
  };
  const auto spawn=[&](uint32_t i,uint32_t variant) {
    const auto owner=owner_of(i);
    sources.Born(owner);
    Require(sources.Observe(owner,parts_of(i,variant)),"full-frame frames fixture parts");
    const auto& c=centers[i];
    NativeSceneVisibility visibility;
    visibility.box={c[0],c[1],c[2],1, 1,0,0,0, 0,1,0,0, 0,0,1,0};
    visibility.radius=1.7f; visibility.distance=float(150+i%7*60); visibility.lod_count=1;
    sources.PublishVisibility(owner,visibility);
    sources.PublishWorld(owner,registers_of(c[0],c[1],c[2]));
  };
  auto lists=std::make_shared<NativeSceneMembership::Publication>();
  std::vector<std::vector<uint32_t>> members(kLeaves+1);
  for(uint32_t i=0;i<kOwners;++i) {
    const uint32_t leaf=i%(kLeaves+1);  // The last is world+372.
    const auto c=leaf<kLeaves?leaf_center(leaf):std::array<float,3>{0,0,100};
    centers[i]={c[0]+unit()*18,c[1]+unit()*18,c[2]+unit()*18};
    spawn(i,0);
    members[leaf].push_back(owner_of(i));
  }
  const auto list_address=[&](uint32_t leaf) { return leaf<kLeaves?roots+leaf*144+120:world+372; };
  const auto publish_list=[&](uint32_t leaf) {
    auto snapshot=std::make_shared<NativeSceneMembership::Snapshot>(); snapshot->end=list_address(leaf)+8;
    for(const auto owner:members[leaf]) snapshot->members.push_back({owner+0x10,owner});
    lists->lists.Set(list_address(leaf),std::move(snapshot));
  };
  for(uint32_t leaf=0;leaf<=kLeaves;++leaf) publish_list(leaf);
  NativeScenePublication publication;
  publication.membership=lists; publication.trees[world]=image;
  NativeSceneGroupOrder order;
  for(uint32_t g=0;g<kGroups;++g) order.push_back(kGroupBase+((g*37)%kGroups)*16);
  publication.group_order.Set(world,std::make_shared<const NativeSceneGroupOrder>(order));
  auto program=std::make_shared<NativeSceneMaterialProgram>();
  program->inputs.vertex=0x9100; program->inputs.pixel=0x9200;
  program->inputs.vertex_registers.push_back({"g_mWorld",0,4});
  // Constants: plain ones, the object world, and on every 100th group the
  // world animation's m_WaterTime (a per-frame miss) or g_SignalBrightness.
  const auto constants_of=[&](uint32_t g,uint8_t salt) {
    std::vector<NativeSceneMaterialInputs::Constant> result;
    for(uint32_t c=0;c<12;++c) result.push_back({c%2==1,"c"+std::to_string(c),std::vector<uint8_t>(16,uint8_t(g+c+salt)),false});
    result.push_back({false,"g_mWorld",std::vector<uint8_t>(64,0),false});
    if(g%100==0) result.push_back({false,"m_WaterTime",std::vector<uint8_t>(16,0),true});
    if(g%100==1) result.push_back({true,"g_SignalBrightness",std::vector<uint8_t>(16,0),true});
    return result;
  };
  const auto publish_material=[&](uint32_t g,uint8_t salt) {
    const auto group=kGroupBase+g*16;
    const auto* membership=publication.sources->FindGroup(group);
    auto material=std::make_shared<NativeSceneGroupMaterial>(); material->group=group;
    material->revision=membership?membership->revision:1; material->program=program; material->constants=constants_of(g,salt);
    publication.group_materials.Assign(material,NativeSceneGroupKey{});
    auto geometry=std::make_shared<NativeSceneGroupGeometry>(); geometry->group=group; geometry->revision=material->revision;
    publication.group_geometry.Assign(geometry,NativeSceneGroupKey{});
  };
  const auto republish_all=[&] {
    publication.sources=sources.AcquireSnapshot();
    // Revisions follow the sources' groups, as the adapter republishes them.
    for(uint32_t g=0;g<kGroups;++g) {
      const auto group=kGroupBase+g*16;
      const auto* membership=publication.sources->FindGroup(group);
      const auto* material=publication.group_materials.Find(group,NativeSceneGroupKey{});
      if(!material || (membership && (*material)->revision!=membership->revision)) publish_material(g,0);
    }
  };
  republish_all();
  NativeFullFrameStaticPass pass;
  pass.targets.dsv_format=DXGI_FORMAT_D24_UNORM_S8_UINT; pass.targets.rtv_format[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
  uint64_t resolves=0;
  const auto resolve=[&](const NativeSceneGroupMaterial& group,const auto&,const NativeSceneMaterialPassState& state,
      std::span<const NativeSceneMaterialInputs::Constant> constants) {
    ++resolves;
    NativeFullFrameStaticMaterial result; result.render=state.render.words;
    // A column-major world on odd groups: the decode differs per group.
    result.world_column_major=(group.group>>4)&1;
    (void)constants;
    return result;
  };
  const NativeFullFrameStaticRouteRead routes=[&](uint32_t owner)->std::optional<NativeFullFrameStaticRoute> {
    return NativeFullFrameStaticRoute{true,0,uint16_t((owner>>9)%97==0)};
  };
  NativeFullFrameStaticCamera camera;
  camera.visibility.matrix=kNativeSceneIdentity; camera.visibility.depth_scale=-1;
  auto& f=camera.visibility.frustum;
  f[8]=1; f[10]=-1; f[12]=-1; f[14]=-1; f[17]=1; f[18]=-1; f[21]=-1; f[22]=-1; f[24]=1; f[25]=1000;
  NativeFullFrameStaticWorld cached;
  const auto same_frame=[](const NativeFullFrameStaticFrame& a,const NativeFullFrameStaticFrame& b) {
    if(a.selection.objects!=b.selection.objects || a.draws.size()!=b.draws.size()) return false;
    const auto& x=a.stats; const auto& y=b.stats;
    if(x.groups!=y.groups || x.draws!=y.draws || x.instances!=y.instances || x.world_declines!=y.world_declines ||
       x.missing_group!=y.missing_group || x.missing_material!=y.missing_material || x.declined!=y.declined) return false;
    for(size_t i=0;i<a.draws.size();++i) {
      const auto& p=a.draws[i]; const auto& q=b.draws[i];
      if(p.owner!=q.owner || p.group!=q.group || p.geometry!=q.geometry || p.material!=q.material ||
         p.instances.size()!=q.instances.size() || std::memcmp(&p.view.viewport,&q.view.viewport,sizeof(p.view.viewport)) ||
         p.view.scissor_enabled!=q.view.scissor_enabled ||
         std::memcmp(p.view.view.data(),q.view.view.data(),sizeof(p.view.view))) return false;
      for(size_t j=0;j<p.instances.size();++j) {
        const auto& u=*p.instances[j]; const auto& v=*q.instances[j];
        if(u.object.world!=v.object.world || u.previous!=v.previous || u.object.geometry!=v.object.geometry ||
           u.object.material!=v.object.material) return false;
      }
    }
    return true;
  };
  NativeFullFrameStaticFrame::Stats totals{};
  uint64_t cached_resolves=0;
  const bool bench=static_world_bench;
  const uint32_t frames=bench?600:48;
  double select_ms=0,build_ms=0;
  for(uint32_t frame=0;frame<frames;++frame) {
    // Camera and pass camera move every frame; the animation advances.
    camera.visibility.matrix[12]=float(frame%40)*1.5f-30; camera.visibility.matrix[14]=float(frame%13);
    camera.pass.view[12]=0x3f800000u+frame; camera.pass.view_projection[3]=frame;
    camera.animation=NativeScenePassAnimation{frame,frame/8};
    // A few objects move every frame: a new sources generation.
    for(uint32_t k=0;k<12;++k) {
      const auto i=(frame*131+k*577)%kOwners;
      centers[i][1]+=.25f;
      sources.PublishWorld(owner_of(i),registers_of(centers[i][0],centers[i][1],centers[i][2]));
    }
    // Now and then: a re-observed owner moves between groups, an owner retires
    // and returns, a group material republishes (equal or changed constants).
    if(frame%7==3) { const auto i=(frame*97)%kOwners; Require(sources.Observe(owner_of(i),parts_of(i,frame)),"re-observe"); }
    if(frame%11==5) { const auto i=(frame*53)%kOwners; sources.Retire(owner_of(i)); spawn(i,0); }
    if(frame%5==2) publish_material((frame*17)%kGroups,uint8_t(frame%3==0?0:frame));
    if(frame%9==4) {
      auto& list=members[frame%kLeaves];
      std::rotate(list.begin(),list.begin()+1,list.end()); publish_list(frame%kLeaves);
    }
    republish_all();
    const auto before=resolves;
    const auto t0=std::chrono::steady_clock::now();
    cached.Select(publication,camera,routes);
    const auto t1=std::chrono::steady_clock::now();
    const auto& built=cached.BuildSelected(publication,camera,pass,resolve);
    const auto t2=std::chrono::steady_clock::now();
    cached_resolves+=resolves-before;
    if(frame>=2) {
      select_ms+=std::chrono::duration<double,std::milli>(t1-t0).count();
      build_ms+=std::chrono::duration<double,std::milli>(t2-t1).count();
    }
    const auto& s=built.stats;
    totals.reused_draws+=s.reused_draws; totals.camera_only+=s.camera_only; totals.draws+=s.draws;
    totals.cache_hits+=s.cache_hits; totals.instances+=s.instances; totals.reused_instances+=s.reused_instances;
    totals.reused_moved+=s.reused_moved;
    // Every frame is a new sources generation and a new animation: the
    // groups not reading the animation keep Current's acceptance (another
    // group's resolve does not take it), and the instances whose reads did
    // not move are carried across the generation.
    if(frame>=2) Require(s.camera_only>=s.cache_hits*9/10 && s.reused_instances>=s.instances*8/10 && s.reused_moved &&
      s.moved_owners>=12,"a moved generation, camera or animation rebuilt unchanged groups");
    if(bench && frame%50) continue;
    NativeFullFrameStaticWorld fresh;
    Require(same_frame(built,fresh.Build(publication,camera,routes,pass,resolve)),
      "a cached full-frame static world frame differs from a fresh build");
  }
  Require(totals.draws>frames*100 && totals.instances>frames*1000,"full-frame frames fixture drew too little");
  if(bench)
    std::cout<<"static world frames: select_ms="<<select_ms/(frames-2)<<" build_ms="<<build_ms/(frames-2)
      <<" draws="<<totals.draws/frames<<" instances="<<totals.instances/frames<<" reused_draws="<<totals.reused_draws/frames
      <<" reused_instances="<<totals.reused_instances/frames<<" camera_only="<<totals.camera_only/frames<<" resolves="<<double(cached_resolves)/frames<<"\n";
}
// NativeAddressFilter: every added address answers true; addresses never
// added are almost all false (two bits of a 4 Mi-bit table).
void AddressFilter() {
  const auto filter=std::make_unique<NativeAddressFilter>();
  for(uint32_t i=0;i<4096;++i) Require(!filter->MayContain(0x40000000u+i*16),"empty address filter holds an address");
  for(uint32_t i=0;i<20000;++i) filter->Add(0x40000000u+i*16);
  for(uint32_t i=0;i<20000;++i) Require(filter->MayContain(0x40000000u+i*16),"address filter lost an added address");
  size_t false_positives=0;
  for(uint32_t i=0;i<100000;++i) false_positives+=filter->MayContain(0x50000000u+i*16+4);
  Require(false_positives<100,"address filter answers true for too many absent addresses");
  filter->Add(0);
  Require(filter->MayContain(0),"address zero is an address like any other");
  for(const auto address:{0u,1u,0x82000000u,0xFFFFFFFFu})
    for(const auto bit:NativeAddressFilter::Bits(address)) Require(bit<NativeAddressFilter::kBits,"address filter bit out of range");
}
// NativeFullFrameLiveRoutes: sub_821C0C00's words straight from the header
// (+0 vtable, +52 mode, +64 hidden halfword), the vtable+16 slot read only
// for a render-slot object and once per vtable, and an unreadable header
// unrouted rather than thrown.
void FullFrameLiveRoutes() {
  std::vector<uint8_t> memory(0x2000);
  const GeometryRetryReader r{memory};
  constexpr uint32_t direct_table=0x100,virtual_table=0x200,A=0x1000,B=0x1100,C=0x1200,D=0x1300;
  r.StoreWord(direct_table+16,kNativeStaticDirectRender); r.StoreWord(virtual_table+16,0x82001234);
  const auto header=[&](uint32_t owner,uint32_t vtable,uint32_t mode,uint16_t hidden) {
    r.StoreWord(owner,vtable); r.StoreWord(owner+52,mode); r.StoreWord(owner+64,uint32_t(hidden)<<16|0xBEEF);
  };
  header(A,direct_table,0,0); header(B,virtual_table,0,0); header(C,direct_table,2,0); header(D,direct_table,0,3);
  const NativeFullFrameLiveRoutes routes(r);
  Require(routes(A)==NativeFullFrameStaticRoute{true,0,0} && routes(B)==NativeFullFrameStaticRoute{false,0,0} &&
    routes(C)==NativeFullFrameStaticRoute{false,2,0} && routes(D)==NativeFullFrameStaticRoute{false,0,3},
    "live route words misread");
  Require(routes.reads==4 && routes.slot_reads==2,"vtable slot read for a bucket or hidden object");
  // Live: a changed word is seen on the next read; the vtable slot is cached.
  r.StoreWord(C+52,0); r.StoreWord(direct_table+16,0);
  Require(routes(C)==NativeFullFrameStaticRoute{true,0,0} && routes.slot_reads==2,"live mode change missed or vtable slot re-read");
  // Past the arena (the header straddles its end): unrouted, counted.
  Require(!routes(0x2000-40) && !routes(0x3000) && routes.failures==2 && routes.reads==5,"unreadable header was not unrouted");
  // Through the type-erased reader SelectNativeFullFrameStaticWorld takes.
  const NativeFullFrameStaticRouteRead erased=std::cref(routes);
  Require(erased(B)==NativeFullFrameStaticRoute{false,0,0} && routes.reads==6,"erased route reader");
  // The last vtable's answer is kept beside the map: alternating vtables and
  // a bucket or hidden object between them keep each object's own answer.
  r.StoreWord(C+52,2);
  const NativeFullFrameLiveRoutes fresh(r);
  for(uint32_t pass=0;pass<3;++pass)
    Require(fresh(A)==NativeFullFrameStaticRoute{false,0,0} && fresh(B)==NativeFullFrameStaticRoute{false,0,0} &&
      fresh(C)==NativeFullFrameStaticRoute{false,2,0} && fresh(D)==NativeFullFrameStaticRoute{false,0,3} &&
      fresh(B)==NativeFullFrameStaticRoute{false,0,0},"alternating vtables took another object's slot answer");
  r.StoreWord(direct_table+16,kNativeStaticDirectRender);
  const NativeFullFrameLiveRoutes restored(r);
  Require(restored(A)==NativeFullFrameStaticRoute{true,0,0} && restored(B)==NativeFullFrameStaticRoute{false,0,0} &&
    restored(A)==NativeFullFrameStaticRoute{true,0,0} && restored(A)==NativeFullFrameStaticRoute{true,0,0} &&
    restored.slot_reads==2,"alternating vtables re-read a slot or lost its answer");
}
// clRock (NativeSceneFixedRecord): slot 4 820BAF90 is `addi r3,r3,396; b
// 821BEE68`. Its +396 record is a scene source filed as LOD 0 and found by
// LodParts(owner+396) for a fixed owner only; the live route reads vtable+16
// 820BAF90 as fixed; the full-frame selection takes the one record whatever
// the depth selects, and a route of the other kind than the source is dropped.
void FullFrameFixedRecord() {
  std::vector<uint8_t> memory(0x10000);
  const GeometryRetryReader r{memory};
  const auto store=[&](uint32_t at,std::initializer_list<float> values) {
    for(const auto value:values) { r.StoreWord(at,std::bit_cast<uint32_t>(value)); at+=4; }
  };
  constexpr uint32_t rock=0x2000,lod_owner=0x3000,instances=0x5000,G1=0x8100,G2=0x8200;
  constexpr uint32_t rock_table=0x100,lod_table=0x200;
  // The record at +396: instance vector +400/+404 (two 28-byte instances,
  // group at +8), no parameter at +424.
  r.StoreWord(rock+400,instances); r.StoreWord(rock+404,instances+56);
  r.StoreWord(instances+8,G1); r.StoreWord(instances+28+8,G2);
  // +404 is the vector's end pointer, not an LOD count: the LOD readers reject it.
  Reject([&] { ReadNativeStaticSceneParts(r,rock); });
  Reject([&] { ReadNativeSceneVisibility(r,rock,true); });
  using P=NativeSceneSources::Part;
  const auto parts=ReadNativeFixedSceneParts(r,rock);
  Require(parts==std::vector<P>{{instances,0,0,0,G1},{instances+28,0,1,0,G2}} &&
    ReadNativeSceneOwnerParts(r,rock,true)==parts,"fixed record parts");
  store(rock+288,{0,0,300,1}); r.StoreWord(rock+352,std::bit_cast<uint32_t>(2.0f)); r.StoreWord(rock+76,std::bit_cast<uint32_t>(1000.0f));
  const auto bounds=ReadNativeFixedSceneVisibility(r,rock);
  Require(bounds.lod_count==1 && bounds.box[2]==300 && bounds.radius==2 && bounds.distance==1000 &&
    ReadNativeSceneOwnerVisibility(r,rock,true)==bounds,"fixed record visibility");
  // Sources: the rock's record answers LodParts(rock+396); an LOD owner's
  // +396 and the rock's +408 answer nothing.
  NativeSceneSources sources;
  sources.Born(rock,true); sources.Born(lod_owner);
  Require(sources.Observe(rock,parts) && sources.Observe(lod_owner,std::vector<P>{{0x6000,0,0,0,G1}}),"fixture parts");
  Require(sources.Fixed(rock) && !sources.Fixed(lod_owner) && sources.LodParts(rock+396)->size()==2 &&
    !sources.LodParts(rock+408) && sources.LodParts(lod_owner+408)->size()==1 && !sources.LodParts(lod_owner+396),
    "fixed record lookup");
  Require(sources.FindCandidateView(rock).fixed && sources.FindCandidate(rock).fixed && !sources.FindCandidateView(lod_owner).fixed &&
    !SameNativeSceneCandidate(sources.FindCandidateView(rock),NativeSceneSources::CandidateView{true,false,nullptr,
      sources.FindCandidateView(rock).lods}),"candidate fixed flag");
  // Live routes: 820BAF90 is fixed, 820B2670 direct; each vtable slot read once.
  r.StoreWord(rock_table+16,NativeSceneFixedRecord::render); r.StoreWord(lod_table+16,kNativeStaticDirectRender);
  r.StoreWord(rock,rock_table); r.StoreWord(lod_owner,lod_table);
  const NativeFullFrameLiveRoutes live_routes(r);
  Require(live_routes(rock)==NativeFullFrameStaticRoute{false,0,0,true} && live_routes(lod_owner)==NativeFullFrameStaticRoute{true,0,0,false} &&
    live_routes(rock)==NativeFullFrameStaticRoute{false,0,0,true} && live_routes.slot_reads==2,"fixed live route");
  r.StoreWord(rock+52,1);
  Require(live_routes(rock)==NativeFullFrameStaticRoute{false,1,0,false},"a filed rock took the render slot");
  r.StoreWord(rock+52,0);
  // One inside leaf holding both objects (identity view, depth = z).
  constexpr uint32_t world=0x1000,levels=0x1800,root=0x3800;
  r.StoreWord(world+52,levels); r.StoreWord(world+56,levels+32);
  r.StoreWord(levels+20,root); r.StoreWord(levels+24,root+144);
  r.StoreWord(root+116,1); store(root+32,{0,0,50,1,5,5,5,0,5});
  const std::shared_ptr<const NativeSceneTreeImage> image=CaptureNativeSceneTree(r,world);
  const auto visibility=[&](uint32_t owner,float z,uint32_t lods) {
    NativeSceneVisibility value;
    value.box={0,0,z,1, 1,0,0,0, 0,1,0,0, 0,0,1,0};
    value.radius=1.7f; value.distance=1000; value.lod_count=lods; value.lod_thresholds={100,0};
    sources.PublishVisibility(owner,value);
  };
  // The rock's depth (300) is past the LOD threshold of a two-LOD record: 820BAF90
  // still publishes its one record (LOD 0).
  visibility(rock,300,2); visibility(lod_owner,50,1);
  auto lists=std::make_shared<NativeSceneMembership::Publication>();
  auto snapshot=std::make_shared<NativeSceneMembership::Snapshot>(); snapshot->end=root+120+8;
  for(const auto owner:{lod_owner,rock}) snapshot->members.push_back({owner+0x10,owner});
  lists->lists.Set(root+120,std::move(snapshot));
  NativeScenePublication publication;
  publication.sources=sources.AcquireSnapshot(); publication.membership=lists; publication.trees[world]=image;
  publication.group_order.Set(world,std::make_shared<const NativeSceneGroupOrder>(NativeSceneGroupOrder{G2,G1}));
  NativeFullFrameStaticCamera camera;
  camera.visibility.matrix=kNativeSceneIdentity; camera.visibility.depth_scale=-1;
  auto& f=camera.visibility.frustum;
  f[8]=1; f[10]=-1; f[12]=-1; f[14]=-1; f[17]=1; f[18]=-1; f[21]=-1; f[22]=-1; f[24]=1; f[25]=1000;
  std::map<uint32_t,NativeFullFrameStaticRoute> live{{rock,{false,0,0,true}},{lod_owner,{true,0,0,false}}};
  const NativeFullFrameStaticRouteRead routes=[&](uint32_t owner)->std::optional<NativeFullFrameStaticRoute> { return live.at(owner); };
  using Object=NativeFullFrameStaticSelection::Object;
  const auto selection=SelectNativeFullFrameStaticWorld(publication,camera,routes);
  Require(selection.objects==std::vector<Object>{{lod_owner,0},{rock,0}} && selection.stats.fixed==1 &&
    !selection.stats.route_mismatch && selection.stats.parts==3,"fixed record not selected as its one record");
  Require(selection.owners.size()==1 && selection.owners[0].groups.size()==2 &&
    selection.owners[0].groups[0].group==G2 && selection.owners[0].groups[0].instances==std::vector<uint32_t>{instances+28} &&
    selection.owners[0].groups[1].group==G1 && selection.owners[0].groups[1].instances==std::vector<uint32_t>{instances,0x6000},
    "fixed record parts lost their groups or queue order");
  // Through the live reader: the same selection.
  Require(SelectNativeFullFrameStaticWorld(publication,camera,std::cref(live_routes)).objects==selection.objects,
    "fixed record selection through the live route reader");
  // A route of the other kind than the source (a vtable rewrite the sources
  // never saw) is dropped, not drawn from the wrong parts.
  live[rock]={true,0,0,false}; live[lod_owner]={false,0,0,true};
  const auto mismatched=SelectNativeFullFrameStaticWorld(publication,camera,routes);
  Require(mismatched.objects.empty() && mismatched.stats.route_mismatch==2 && !mismatched.stats.fixed,"route kind mismatch was drawn");
}
// NativeWorldPublicationMirror: the 820B4250 post-hook takes the bridge lock
// only for a value the adapter does not already hold. Driven exactly as the
// hook and the two retire sites drive it, against a real adapter; the model
// is the old path, which always locked and published.
void WorldPublicationMirror() {
  using Order=std::vector<uint32_t>;
  NativeSceneAdapter adapter,always;
  NativeWorldPublicationMirror mirror;
  struct BridgeLock {
    std::mutex mutex; bool held=false;
    void lock() { mutex.lock(); held=true; }
    void unlock() { held=false; mutex.unlock(); }
  } bridge;
  size_t locks=0,publishes=0;
  const auto lock=[&] { ++locks; return std::unique_lock(bridge); };
  const auto order=[&](uint32_t owner,const Order& value) {
    always.PublishGroupOrder(owner,value);
    return mirror.PublishOrder(owner,value,lock,[&](std::span<const uint32_t> published) {
      Require(bridge.held,"group order published without the bridge lock");
      ++publishes; adapter.PublishGroupOrder(owner,published);
    });
  };
  const auto animation=[&](uint32_t owner,NativeScenePassAnimation value) {
    always.PublishWorldAnimation(owner,value);
    return mirror.PublishAnimation(owner,value,lock,[&](const NativeScenePassAnimation& published) {
      Require(bridge.held,"world animation published without the bridge lock");
      ++publishes; adapter.PublishWorldAnimation(owner,published);
    });
  };
  uint64_t tick=0;
  const auto same=[&] {
    ++tick;
    const auto a=adapter.Publish(tick),b=always.Publish(tick);
    if(a->world_animations!=b->world_animations || a->group_order.size()!=b->group_order.size()) return false;
    for(const auto& [owner,value]:b->group_order) {
      const auto* found=a->group_order.Find(owner);
      if(!found || !*found || !value || **found!=*value) return false;
    }
    return true;
  };
  // Retire sites: 820B5FA8 (both) and RetireGroupOrder (orders), under the lock.
  const auto retire=[&](uint32_t owner,bool animations) {
    std::lock_guard guard(bridge);
    adapter.RetireGroupOrder(owner); mirror.RetiredOrder(owner); always.RetireGroupOrder(owner);
    if(animations) { adapter.RetireWorldAnimation(owner); mirror.RetiredAnimation(owner); always.RetireWorldAnimation(owner); }
  };
  Require(order(0x100,{1,2,3}) && locks==1 && same(),"first group order skipped the adapter");
  Require(!order(0x100,{1,2,3}) && locks==1 && same(),"unchanged group order took the bridge lock");
  const auto retained=adapter.GroupOrder(0x100);
  Require(order(0x100,{1,3}) && locks==2 && *adapter.GroupOrder(0x100)==Order{1,3} && *retained==Order{1,2,3} && same(),
    "changed group order was skipped or mutated the retained order");
  Require(order(0x200,{1,3}) && locks==3 && same(),"an equal order of another owner was taken as current");
  Require(order(0x100,{}) && !order(0x100,{}) && locks==4 && same(),"an emptied group order was not published once");
  retire(0x100,false);
  Require(!adapter.GroupOrder(0x100) && order(0x100,{}) && locks==5 && same(),
    "a retired owner's order was proven current without the adapter");
  Require(animation(0x100,{5,1}) && !animation(0x100,{5,1}) && same(),"unchanged world animation took the bridge lock");
  const auto before=adapter.AcquireWorldAnimations();
  Require(animation(0x100,{6,1}) && adapter.AcquireWorldAnimations()!=before && same(),"advanced water time was skipped");
  retire(0x100,true);
  Require(animation(0x100,{6,1}) && same() && adapter.AcquireWorldAnimations()->at(0x100)==NativeScenePassAnimation{6,1},
    "a retired owner's animation was proven current without the adapter");
  // A throwing adapter call leaves the mirror unproven: the next call locks again.
  const auto locked=locks;
  Reject([&] { mirror.PublishOrder(0x300,Order{7},lock,[](std::span<const uint32_t>) { throw std::runtime_error("publish"); }); });
  Require(!mirror.OrderCurrent(0x300,Order{7}) && locks==locked+1 && !bridge.held,"a failed publication was mirrored or kept the lock");
  Require(publishes==locks-1,"a lock was taken without a publication");
}
void GroupOrder() {
  std::vector<uint8_t> memory(0x1000);
  const GeometryRetryReader reader{memory};
  constexpr uint32_t owner=0x100,list=owner+240,sentinel=0x400;
  // 821C3BB8 layout: list+4 is the sentinel; node+0 next, node+4 previous, node+8 group.
  const auto link=[&](std::initializer_list<uint32_t> nodes) {
    reader.StoreWord(list+4,sentinel);
    uint32_t previous=sentinel;
    for(const auto node:nodes) {
      reader.StoreWord(previous,node); reader.StoreWord(node+4,previous); reader.StoreWord(node+8,0x9000+node);
      previous=node;
    }
    reader.StoreWord(previous,sentinel); reader.StoreWord(sentinel+4,previous);
  };
  using Order=std::vector<uint32_t>;
  Order order,live;
  NativeSceneAdapter adapter;
  link({}); CaptureNativeSceneGroupOrder(reader,list,order);
  Require(order.empty(),"empty group list captured a group");
  link({0x500,0x480,0x600}); CaptureNativeSceneGroupOrder(reader,list,order);
  Require(order==Order{0x9500,0x9480,0x9600},"group order capture lost the list walk order");
  Order dispatched;
  DispatchNativeSceneGroups(reader,list,[&](uint32_t group) { dispatched.push_back(group); });
  Require(dispatched==order,"group order capture differs from group dispatch");
  Require(adapter.PublishGroupOrder(owner,order),"first group order was not published");
  const auto first=adapter.GroupOrder(owner);
  const auto first_publication=adapter.Publish(1);
  Require(first_publication->group_order.at(owner)==first,"scene publication omitted the group order");
  CaptureNativeSceneGroupOrder(reader,list,order);
  Require(!adapter.PublishGroupOrder(owner,order) && adapter.GroupOrder(owner)==first &&
    adapter.Publish(2)->group_order.at(owner)==first,"unchanged group order replaced its publication");
  link({0x500,0x700,0x480,0x600}); CaptureNativeSceneGroupOrder(reader,list,order);
  Require(adapter.PublishGroupOrder(owner,order) && *adapter.GroupOrder(owner)==Order{0x9500,0x9700,0x9480,0x9600} &&
    *first==Order{0x9500,0x9480,0x9600},"group insertion was not published or mutated a retained order");
  const auto inserted=adapter.GroupOrder(owner);
  link({0x500,0x600}); CaptureNativeSceneGroupOrder(reader,list,order);
  Require(adapter.PublishGroupOrder(owner,order) && *adapter.GroupOrder(owner)==Order{0x9500,0x9600} &&
    inserted->size()==4 && *first_publication->group_order.at(owner)==*first,
    "group removal was not published or mutated a retained order");
  CaptureNativeSceneGroupOrder(reader,list,live);
  Require(adapter.AuditGroupOrder(owner,live) && adapter.group_order_audit().checks==1 &&
    !adapter.group_order_audit().mismatches,"matching live group walk counted as a mismatch");
  link({0x600,0x500}); CaptureNativeSceneGroupOrder(reader,list,live);
  Require(!adapter.AuditGroupOrder(owner,live) && adapter.group_order_audit().mismatches==1,"reordered live walk was not counted");
  link({0x500}); CaptureNativeSceneGroupOrder(reader,list,live);
  Require(!adapter.AuditGroupOrder(owner,live) && adapter.group_order_audit().mismatches==2,"removed live group was not counted");
  Require(!adapter.AuditGroupOrder(0x200,live) && adapter.group_order_audit().missing==1 &&
    adapter.group_order_audit().mismatches==2 && adapter.group_order_audit().checks==4,"unpublished owner audit miscounted");
  adapter.RetireGroupOrder(owner);
  Require(!adapter.GroupOrder(owner) && !adapter.Publish(3)->group_order.count(owner) &&
    first_publication->group_order.count(owner),"group order retirement changed a retained publication");
  link({0x600,0x500}); reader.StoreWord(0x600,0);
  Reject([&] { CaptureNativeSceneGroupOrder(reader,list,order); });
  reader.StoreWord(0x600,0x600);
  Reject([&] { CaptureNativeSceneGroupOrder(reader,list,order); });
}
void StaticWorldResolve() {
  using D=NativeStaticWorldDecline;
  // The guest-draw path reports these exact strings; the pass shares them.
  const std::pair<D,const char*> reasons[]{
    {D::PendingWrites,"pending resource writes"},{D::Revision,"group membership or preload revision"},
    {D::GeometryPublication,"retained geometry publication"},{D::GeometryCount,"geometry count"},
    {D::DeferredGeometry,"deferred geometry identity"},{D::GeometryBinding,"geometry binding identity"},
    {D::BufferGeneration,"model buffer generation"},{D::VersionsUnavailable,"observed resource versions unavailable"},
    {D::VersionChanged,"observed resource revision changed"},{D::Lifetime,"instance lifetime not in scene publication"},
    {D::ComparisonChanged,"due geometry comparison found a change"}};
  for(const auto& [decline,reason]:reasons) {
    const auto* actual=NativeStaticWorldDeclineReason(decline);
    Require(actual && std::string(actual)==reason,"static world decline reason changed");
  }
  for(auto silent:{D::None,D::ProgramBinding,D::Targets,D::WorldParameter,D::Source,D::WorldRegisters,D::WorldMismatch})
    Require(!NativeStaticWorldDeclineReason(silent),"silent static world decline gained a report");

  struct Stream { uint32_t resource,offset,stride; };
  const NativeSceneGeometrySource source{0x11000,0x11100,0x12000,28,6,0x13000,0x14000};
  Stream stream{source.vertex,0,source.stride};
  uint32_t index=source.index,declaration=source.declaration;
  unsigned reads=0;
  const auto bound=[&] {
    reads=0;
    return NativeBoundGeometryMatches(source,[&]() -> const Stream& { ++reads; return stream; },
      [&] { ++reads; return index; },[&] { ++reads; return declaration; });
  };
  Require(bound() && reads==3,"bound geometry identity rejected");
  stream.offset=4; Require(!bound() && reads==1,"stream offset admitted or later bindings read"); stream.offset=0;
  stream.stride=32; Require(!bound() && reads==1,"stream stride admitted"); stream.stride=source.stride;
  index=0; Require(!bound() && reads==2,"index binding admitted or declaration read"); index=source.index;
  declaration=0; Require(!bound() && reads==3,"declaration binding admitted"); declaration=source.declaration;
  // A missing later binding throws only once every earlier binding matched.
  const auto missing=[&](const Stream& s) {
    return NativeBoundGeometryMatches(source,[&]() -> const Stream& { return s; },
      [&]() -> uint32_t { throw std::out_of_range("no index binding"); },[&] { return declaration; });
  };
  Require(!missing({0,0,source.stride}),"mismatched stream reached a missing index binding");
  Reject([&] { missing(stream); });

  NativeSceneView camera;
  camera.view[3]=5.0f; camera.view_projection=kNativeSceneIdentity;
  NativeViewportState viewport{};
  viewport.viewport={16,32,1280,720,0.25f,1.0f}; viewport.scissor={1,2,3,4};
  for(bool scissor:{false,true}) {
    const auto view=NativeStaticInstanceView(camera,viewport,scissor);
    Require(view.view==camera.view && view.view_projection==camera.view_projection,"static instance view changed the camera");
    Require(view.viewport.x==16 && view.viewport.y==32 && view.viewport.width==1280 && view.viewport.height==720 &&
      view.viewport.min_depth==0.25f && view.viewport.max_depth==1.0f,"static instance viewport");
    Require(view.scissor.left==1 && view.scissor.top==2 && view.scissor.right==3 && view.scissor.bottom==4 &&
      view.scissor_enabled==scissor,"static instance scissor");
  }
}
// A published draw whose sampled comparison falls due runs it rather than
// declining, and keeps the retained geometry only on an exact match.
void StaticGeometryComparison() {
  using C=NativeStaticComparison;
  using Source=NativeBufferWrites::SnapshotSource;
  using View=NativeBufferWrites::SnapshotIdentityView;
  NativeBufferWrites::SnapshotPolicy policy{};
  policy.verify_initial=1; policy.verify_interval=4;
  std::vector<uint8_t> vertex_bytes(64,1),index_bytes(32,2);
  const auto vertex=std::make_shared<const std::vector<uint8_t>>(vertex_bytes);
  const auto index=std::make_shared<const std::vector<uint8_t>>(index_bytes);
  const std::array<Source,2> sources{{{1,0x1000,vertex_bytes,vertex},{2,0x2000,index_bytes,index}}};
  const std::array<View,2> views{{{1,0x1000,64,&vertex},{2,0x2000,32,&index}}};
  const auto load=[&](NativeBufferWrites& writes) {
    writes.Subscribe(1,0x1000,64); writes.Subscribe(2,0x2000,32);
    const auto first=writes.CopyObservedSet(sources,nullptr,policy);
    Require(first && (*first)[0].contents==vertex && (*first)[1].contents==index,"comparison test load copied");
    size_t accepted=0;
    while(writes.TryValidateObservedSet(views,policy)) ++accepted;
    Require(accepted==3,"comparison test did not reach a due comparison");
    return std::array<NativeBufferWrites::ObservedVersion,2>{(*first)[0].version,(*first)[1].version};
  };
  {
    NativeBufferWrites writes;
    const auto loaded=load(writes);
    // Audit and compare-every-observation policies stay with the preload.
    NativeBufferWrites::SnapshotPolicy audit=policy; audit.audit_revisions=true;
    Require(CompareNativeStaticGeometry(writes,sources,loaded,audit)==C::Unavailable &&
      CompareNativeStaticGeometry(writes,sources,loaded,NativeBufferWrites::SnapshotPolicy{})==C::Unavailable,
      "draw comparison ran under an audit policy");
    {
      NativeBufferWrites::WriterScope active(&writes,NativeBufferWrites::Range{0x2004,4});
      Require(CompareNativeStaticGeometry(writes,sources,loaded,policy)==C::Unavailable,"draw comparison ignored an overlapping writer");
    }
    Require(!writes.TryValidateObservedSet(views,policy),"refused comparison consumed the due observation");
    const auto verified=writes.Trust().verified;
    Require(CompareNativeStaticGeometry(writes,sources,loaded,policy)==C::Confirmed &&
      writes.Trust().verified==verified+2,"due draw comparison did not confirm unchanged geometry");
    // The due observation is consumed: later draws and the preload trust it again.
    Require(writes.TryValidateObservedSet(views,policy) && writes.Unchanged(views,loaded,policy),
      "confirmed draw comparison left the observation due");
    writes.Record(0x2010,4);
    Require(CompareNativeStaticGeometry(writes,sources,loaded,policy)==C::Changed,"tracked write kept the published geometry");
    Require(!writes.Trust().revoked,"tracked write revoked trust");
  }
  {
    NativeBufferWrites writes;
    const auto loaded=load(writes);
    vertex_bytes[7]=9; // An untracked store the revision never saw.
    Require(CompareNativeStaticGeometry(writes,sources,loaded,policy)==C::UnreportedChange && writes.Trust().revoked,
      "stale published geometry survived a due comparison");
    Require(CompareNativeStaticGeometry(writes,sources,loaded,policy)!=C::Confirmed,"revoked trust confirmed stale geometry");
  }
}
void PassCamera() {
  GeometryPublicationRetry();
  StaticGroupEligibility();
  StaticWorldResolve();
  StaticGeometryComparison();
  {
    NativeSceneExecution run;
    Require(!run.complete() && !run.Total() && !run.recordings(),"new group inherited execution evidence");
    for(uint32_t bit=0;bit<uint32_t(NativeSceneBoundary::Count);++bit) {
      const auto boundary=static_cast<NativeSceneBoundary>(bit);
      bool entered=false;
      Reject([&] { run.Enter(boundary,true); entered=true; });
      Require(!entered && run.Calls(boundary)==1 && (run.mask()&(1u<<bit)),
        "strict execution failed to count and reject a boundary before its call");
      run.Enter(boundary);
      Require(run.Calls(boundary)==2,"compatible execution lost repeated boundary calls");
    }
    run.Recorded(); run.Complete();
    Require(run.complete() && run.recordings()==1 && run.Total()==2*uint32_t(NativeSceneBoundary::Count),
      "group completion hid compatibility work");
    NativeSceneExecution next;
    Require(!next.complete() && !next.Total() && !next.mask(),"execution evidence leaked between groups");
    unsigned attempts=0;
    const auto recorded=next.RecordBatch([&] { ++attempts; return 7; });
    Require(recorded==7 && next.recordings()==1,"completed recording was not counted");
    Reject([&] { next.RecordBatch([&]()->int { ++attempts; throw std::runtime_error("partial GPU recording"); }); });
    Reject([&] { next.RecordBatch([&] { ++attempts; return 8; }); });
    Require(next.recording_failed() && attempts==2 && next.recordings()==1,
      "failed GPU recording was counted or retried");
    Reject([&] { next.Complete(); });
    Require(!next.complete(),"failed GPU group was marked complete");
  }
  {
    NativeSceneGeometryHandoff handoff;
    NativeSceneGeometrySource setup{10,20,30,12,6,40,50};
    std::vector<int> events;
    const auto install=[&](const auto& value) { Require(value==setup,"handoff lost owned geometry inputs"); events.push_back(1); };
    const auto consume=[&] { events.push_back(2); };
    handoff.Defer(setup);
    Require(events.empty() && handoff.pending()==setup,"deferred geometry installed before native submission");
    handoff.Finish(install,consume);
    Require(events==std::vector<int>{1},"fallback before a native draw consumed dirty draw state");
    events.clear(); handoff.Defer(setup); handoff.DrawAccepted(); handoff.DrawAccepted();
    handoff.Finish(install,consume); handoff.Finish(install,consume);
    Require(events==std::vector<int>({1,2}) && !handoff.pending(),"group handoff reordered or repeated compatibility work");
    events.clear(); handoff.Defer(setup); handoff.DrawAccepted();
    Reject([&] { handoff.Defer(setup); });
    Reject([&] { handoff.Finish(install,[] { throw std::runtime_error("tail failed"); }); });
    handoff.Finish(install,consume);
    Require(events==std::vector<int>({1,2}),"retry repeated resource binding installation");
    NativeSceneMaterialHandoff material_handoff;
    int world=42,activations=0,synchronizations=0;
    material_handoff.Defer();
    const auto activate=[&] {
      Require(material_handoff.activation_pending(),"compatibility activation lost its replay guard");
      ++activations; world=0;
    };
    const auto synchronize=[&] {
      Require(!material_handoff.activation_pending(),"world restored before material activation completed");
      ++synchronizations; world=42;
    };
    material_handoff.Finish(activate,synchronize); material_handoff.Finish(activate,synchronize);
    Require(world==42 && activations==1 && synchronizations==1,
      "material handoff overwrote the final world or repeated side effects");
    material_handoff.Defer();
    Reject([&] { material_handoff.Finish(activate,[] { throw std::runtime_error("world copy failed"); }); });
    material_handoff.Finish(activate,synchronize);
    Require(world==42 && activations==2 && synchronizations==2,"world retry repeated material activation");
    // Exercise the combined production boundary, not just each obligation in
    // isolation: a no-draw group must survive a failed material/world restore.
    for(bool drawn:{false,true}) for(bool fail_activation:{false,true}) {
      NativeSceneGeometryHandoff geometry;
      NativeSceneMaterialHandoff material;
      geometry.Defer(setup); material.Defer();
      if(drawn) geometry.DrawAccepted();
      int installs=0,activation_attempts=0,world_attempts=0,tails=0,invalidations=0;
      bool fail=true;
      const auto finish=[&] {
        FinishNativeSceneGroupHandoff(geometry,[] {},[] {},
          [&](const auto&) { ++installs; },
          [&] { material.Finish([&] {
            ++activation_attempts;
            if(fail && fail_activation) { fail=false; throw std::runtime_error("activation failed"); }
          },[&] {
            ++world_attempts;
            if(fail && !fail_activation) { fail=false; throw std::runtime_error("world failed"); }
          }); },[&] { ++tails; },[&] { ++invalidations; });
      };
      Reject(finish);
      Require(geometry.pending() && material.pending() && installs==1 && tails==0 && invalidations==0,
        "failed restoration lost the group obligation or consumed dirty state");
      finish(); finish();
      Require(!geometry.pending() && !material.pending() && installs==1 &&
        activation_attempts==(fail_activation?2:1) && world_attempts==(fail_activation?1:2) &&
        tails==int(drawn) && invalidations==1,
        "group restoration retry skipped or repeated completed work");
    }
  }
  NativeScenePassCamera camera;
  for(uint32_t i=0;i<16;++i) {
    camera.view[i]=0x3f800000+i; camera.projection[i]=0x40000000+i;
    camera.view_projection[i]=0x40800000+i;
  }
  const auto retained=camera;
  {
    NativeSceneAdapter producer;
    producer.PublishCameras({{100,camera},{200,camera}});
    const auto acquired=producer.AcquireCameras();
    const auto scene=producer.Publish(1);
    producer.PublishCameras({{100,camera},{200,camera}});
    Require(producer.AcquireCameras()==acquired,"unchanged cameras replaced their generation");
    auto changed=camera;
    changed.view[12]^=1;
    producer.PublishCameras({{100,changed}});
    Require(acquired->at(100)==camera && acquired->contains(200) && scene->cameras==acquired,
      "camera update or view removal mutated an acquired scene");
    Require(producer.AcquireCameras()->at(100)==changed && !producer.AcquireCameras()->contains(200),
      "producer camera update retained a removed view");
    producer.PublishCameras({});
    Require(producer.AcquireCameras()->empty() && acquired->size()==2,
      "empty active-view set failed to retire cameras");
  }
  for(const auto* name:{"g_mView","g_mViewTranspose","g_mProjection","g_mViewProjection"}) {
    NativeSceneMaterialInputs::Constant input{false,name,std::vector<uint8_t>(64),true};
    Require(camera.Apply(input),"native pass camera did not supply named matrix");
    const auto& expected=input.name=="g_mProjection"?camera.projection:
      input.name=="g_mViewProjection"?camera.view_projection:camera.view;
    for(size_t row=0;row<4;++row) for(size_t col=0;col<4;++col)
      Require(GuestBlockWord(input.registers.data()+(row*4+col)*4)==
        expected[input.name=="g_mViewTranspose"?row*4+col:col*4+row],
        "native pass camera changed matrix register orientation");
    input.global=false;
    Require(!camera.Apply(input),"native pass replaced a material-local matrix");
    input.global=true; input.registers.resize(16);
    Reject([&] { camera.Apply(input); });
  }
  NativeSceneMaterialInputs::Constant scalar{true,"g_SignalBrightness",std::vector<uint8_t>(16),true};
  Require(!camera.Apply(scalar) && camera==retained,"camera overwrote unrelated pass input or producer state");
  NativeScenePassAnimation animation{0x3e123456,63};
  Require(animation.Apply(scalar) && GuestBlockWord(scalar.registers.data())==0,
    "signal brightness changed before counter bit 6");
  animation.signal_counter=64;
  Require(animation.Apply(scalar) && GuestBlockWord(scalar.registers.data())==0x41200000,
    "signal brightness failed at counter bit 6");
  animation.signal_counter=128;
  Require(animation.Apply(scalar) && GuestBlockWord(scalar.registers.data())==0,
    "signal brightness failed to wrap");
  scalar.name="m_WaterTime";
  Require(animation.Apply(scalar) && GuestBlockWord(scalar.registers.data())==animation.water_time &&
    GuestBlockWord(scalar.registers.data()+4)==0 && GuestBlockWord(scalar.registers.data()+8)==0 &&
    GuestBlockWord(scalar.registers.data()+12)==0x3f800000,
    "water time lost register contents");
  NativeSceneAdapter animation_loader;
  animation_loader.PublishWorldAnimation(100,animation);
  const auto first=animation_loader.Publish(1);
  const auto first_pass=animation_loader.AcquireWorldAnimations();
  const auto first_animation=animation;
  ++animation.signal_counter;
  animation_loader.PublishWorldAnimation(100,animation);
  const auto second_pass=animation_loader.AcquireWorldAnimations();
  Require(first_pass->at(100)==first_animation && second_pass->at(100)==animation &&
    first->world_animations.at(100)==first_animation,
    "pass animation waited for scene publication or changed an acquired generation");
  animation_loader.PublishWorldAnimation(100,animation);
  Require(animation_loader.AcquireWorldAnimations()==second_pass,"unchanged animation created a generation");
  const auto second=animation_loader.Publish(2);
  animation_loader.RetireWorldAnimation(100);
  Require(animation_loader.AcquireWorldAnimations()->empty() && second_pass->at(100)==animation,
    "animation retirement changed an acquired pass");
  Require(first->world_animations.at(100)==first_animation &&
    second->world_animations.at(100)==animation && animation_loader.Publish(3)->world_animations.empty(),
    "world animation publication lost generation isolation or retirement");
}
void Visibility() {
  NativeSceneMembership membership;
  membership.Born(100); membership.Born(200);
  Require(membership.InsertAfter(100,1000,10) && membership.InsertAfter(100,2000,20) &&
    membership.InsertAfter(1000,3000,30),"membership insertion failed");
  membership.Publish();
  const auto original=membership.Acquire(100);
  const auto lists=membership.AcquirePublication();
  Require(membership.Current(*lists) && membership.AcquirePublication()==lists &&
    lists->lists.at(100)==original,"unchanged spatial publication lost list sharing");
  Require(original->members==std::vector<NativeSceneMembership::Member>{{2000,20},{1000,10},{3000,30}},
    "native membership changed insertion order");
  Require(membership.Acquire(100)==original,"unchanged membership publication was copied");
  membership.InsertAfter(200,1000,10);
  const auto moved=membership.AcquirePublication();
  Require(moved!=lists && moved->lists.size()==2 && !moved->lists.Shares(lists->lists) &&
    lists->lists.size()==2 && *lists->lists.Find(100)==original,"incremental publication changed an acquired generation");
  Require(!membership.Current(*lists) && membership.Current(*moved) &&
    lists->lists.at(100)->members.size()==3 && moved->lists.at(100)->members.size()==2,
    "cross-list mutation failed to invalidate the scene membership generation");
  Require(membership.Acquire(100)->members==std::vector<NativeSceneMembership::Member>{{2000,20},{3000,30}} &&
    membership.Acquire(200)->members==std::vector<NativeSceneMembership::Member>{{1000,10}},"cross-list move retained old membership");
  Require(original->members.size()==3,"membership update mutated a published list");
  membership.Remove(2000);
  Require(!membership.InsertAfter(9999,3000,30) && membership.Acquire(100)->members.empty(),
    "move to untracked list retained an old native node");
  membership.Remove(200);
  const auto removed=membership.AcquirePublication();
  Require(!membership.Current(*moved) && moved->lists.contains(200) && !removed->lists.contains(200),
    "list retirement changed a retained publication or kept the retired header");
  Require(!membership.Acquire(200) && membership.nodes()==0,"list-header retirement leaked members");
  membership.Born(100);
  Require(!membership.Current(*removed) && membership.AcquirePublication()->lists.at(100)->generation!=
    removed->lists.at(100)->generation,"recycled list did not invalidate spatial publication");
  NativeSceneAdapter membership_adapter;
  Require(membership_adapter.Publish(1,{},lists)->membership==lists,
    "scene publication omitted its spatial membership generation");
  Require(membership.Acquire(100)->generation!=original->generation && membership.Acquire(100)->members.empty(),
    "recycled list address inherited old membership");
  struct Memory {
    mutable size_t reads=0,writes=0;
    mutable std::array<uint8_t,12288> bytes{};
    bool writable=true;
    uint32_t Add(uint32_t a,uint32_t b) const { return a+b; }
    const uint8_t* Bytes(uint32_t at,size_t size) const {
      ++reads; if(at>bytes.size() || size>bytes.size()-at) throw std::runtime_error("range");
      return bytes.data()+at;
    }
    const uint8_t* WritableBytes(uint32_t at,size_t size,size_t alignment) const {
      ++writes;
      if(!writable || at%alignment) throw std::runtime_error("protection");
      return Bytes(at,size);
    }
  } memory;
  NativeSceneCpuWindow window(memory);
  Require(window.Word(4096)==0 && window.Word(4100)==0 && memory.reads==1,"CPU page admission was not reused");
  memory.bytes[4103]=17;
  Require(window.Word(4100)==17,"CPU window cached contents instead of access");
  window.WritableBytes(4100,4,4);
  Require(memory.writes==1,"read-only page was not upgraded through validation");
  window.WritableBytes(4104,4,4);
  Require(memory.writes==1,"writable page was revalidated for each store");
  memory.writable=false; window.Invalidate();
  Reject([&] { window.WritableBytes(4100,4,4); });
  memory.writable=true;
  const auto before_reads=memory.reads;
  window.Bytes(8190,4);
  Require(memory.reads==before_reads+1,"cross-page access bypassed the backing validator");
  Reject([&] { window.Bytes(12287,2); });
  {
    // A walk's window is keyed to the guest call count: admissions survive
    // native work and are dropped on the first access after a call.
    uint64_t guest_calls=0;
    const NativeSceneCpuWindow walk(memory,&guest_calls);
    const auto reads=memory.reads;
    walk.Word(4096); walk.Word(4200); walk.Word(8192); walk.Word(4104);
    Require(memory.reads==reads+2,"walk window readmitted a page without a guest call");
    ++guest_calls; walk.Word(4096);
    Require(memory.reads==reads+3,"walk window kept an admission across a guest call");
    const auto writes=memory.writes;
    walk.StoreWord(4108,0x01020304); walk.StoreWord(4112,5);
    Require(memory.writes==writes+1 && memory.bytes[4108]==1 && memory.bytes[4111]==4 && walk.Word(4112)==5,
      "window word store was not a big-endian store through one writable admission");
    memory.writable=false; ++guest_calls;
    Reject([&] { walk.StoreWord(4108,0); });
    memory.writable=true;
  }
  NativeSceneVisibilityView view;
  view.matrix=kNativeSceneIdentity; view.depth_scale=-1;
  const float n=std::sqrt(.5f);
  view.frustum[8]=n; view.frustum[10]=-n;
  view.frustum[12]=-n; view.frustum[14]=-n;
  view.frustum[17]=n; view.frustum[18]=-n;
  view.frustum[21]=-n; view.frustum[22]=-n;
  view.frustum[24]=1; view.frustum[25]=100;
  Require(NativeVisibilitySphere(view,{0,0,10,1},1)==1,"interior sphere culled");
  Require(NativeVisibilitySphere(view,{0,0,-1,1},1)==0,"near sphere accepted");
  Require(NativeVisibilitySphere(view,{0,0,101,1},2)==2,"far sphere intersection lost");
  Require(NativeVisibilitySphere(view,{20,0,10,1},1)==0,"side sphere accepted");
  Require(NativeVisibilitySphere(view,{0,0,1,1},0)==1,"near boundary was exclusive");
  NativeSceneVisibility object;
  object.box={0,0,10,1, 1,0,0,0, 0,1,0,0, 0,0,1,0};
  Require(NativeVisibilityBox(view,object.box)==1,"interior box culled");
  object.box[0]=30;
  Require(NativeVisibilityBox(view,object.box)==0,"outside box accepted");
  object.box[0]=10;
  Require(NativeVisibilityBox(view,object.box)==2,"intersecting box culled");
  object.box={0,0,50,1, 200,0,0,0, 0,200,0,0, 0,0,100,0};
  Require(NativeVisibilityBox(view,object.box)==2,"frustum-enclosing box culled because no corner was inside");
  object.lod_count=3; object.lod_thresholds={20,40};
  Require(NativeVisibilityLod(object,20)==0 && NativeVisibilityLod(object,21)==1 &&
    NativeVisibilityLod(object,40)==1 && NativeVisibilityLod(object,41)==2,"LOD threshold boundaries changed");
  const std::array<float,16> transform{1,2,3,0, 4,5,6,0, 7,8,9,0, 10,11,12,1};
  Require(NativeVisibilityTransform({2,3,4,1},transform)==std::array<float,4>{52,62,72,1},
    "visibility center matrix convention changed");
  NativeSceneSources sources; sources.Born(100);
  Require(sources.PublishVisibility(100,object),"visibility source not registered");
  const auto old=sources.Visibility(100);
  sources.PublishVisibility(100,object);
  Require(sources.Visibility(100)==old,"unchanged bound was recopied");
  object.distance=250; sources.PublishVisibility(100,object);
  Require(old->distance==0 && sources.Visibility(100)->distance==250,"bound update mutated retained state");
  sources.Retire(100); sources.Born(100);
  Require(!sources.Visibility(100) && old->lod_count==3,"recycled owner inherited old visibility");
  {
    const std::array<NativeSceneSources::Part,1> parts{{{1000,0,0,2000,3000,4}}};
    sources.Observe(100,parts);
    NativeSceneSources::World world{}; world[0]=1;
    sources.PublishWorld(100,world); sources.PublishVisibility(100,object);
    const auto generation=sources.AcquireSnapshot();
    const auto source=*generation->Find(1000);
    sources.Observe(100,parts); sources.PublishWorld(100,world); sources.PublishVisibility(100,object);
    Require(sources.AcquireSnapshot()==generation,"unchanged source events replaced a generation");
    world[0]=2; sources.PublishWorld(100,world);
    object.distance=500; sources.PublishVisibility(100,object);
    const auto updated=sources.AcquireSnapshot();
    Require(updated!=generation && updated->Groups().Shares(generation->Groups()),
      "world/visibility events copied unchanged group membership");
    Require(generation->WorldRegisters(source,2000)->at(0)==1 &&
      updated->WorldRegisters(source,2000)->at(0)==2 && generation->Visibility(100)->distance==250 &&
      updated->Visibility(100)->distance==500,"source snapshot mixed world or visibility generations");
    sources.Retire(100); sources.Born(100);
    auto replacement=parts; replacement[0].instance=1004; replacement[0].world_first=8;
    sources.Observe(100,replacement);
    const auto recycled=sources.AcquireSnapshot();
    Require(generation->Find(1000) && generation->FindGroup(3000)->parts.contains(1000) &&
      generation->LodParts(508)->front().instance==1000 && !recycled->Find(1000) &&
      recycled->Find(1004)->generation!=source.generation && !recycled->WorldRegisters(source,2000),
      "source retirement/reuse changed retained membership or inherited dead world data");
    NativeSceneAdapter adapter;
    Require(adapter.Publish(4,generation)->sources==generation,"scene publication omitted source generation");
  }
}
void QueuedGuestState() {
  for(bool column:{false,true}) {
    std::array<uint8_t,64> raw{};
    std::array<float,16> expected{};
    for(size_t row=0;row<4;++row) for(size_t col=0;col<4;++col) {
      const float value=float(row*4+col)-8.5f; expected[row*4+col]=value;
      const auto bits=std::bit_cast<uint32_t>(value);
      const auto offset=(column?col*4+row:row*4+col)*4;
      for(size_t b=0;b<4;++b) raw[offset+b]=uint8_t(bits>>(24-b*8));
    }
    Require(DecodeNativeQueuedWorld(raw,column)==expected,"direct world register matrix layout");
  }
  struct Reader {
    mutable std::vector<uint8_t> bytes=std::vector<uint8_t>(32768);
    uint32_t Add(uint32_t a,uint32_t b) const { if(b>UINT32_MAX-a) throw std::runtime_error("overflow"); return a+b; }
    const uint8_t* Bytes(uint32_t at,size_t size) const {
      if(!at || at>bytes.size() || size>bytes.size()-at) throw std::runtime_error("range");
      return bytes.data()+at;
    }
    const uint8_t* WritableBytes(uint32_t at,size_t size,size_t alignment) const {
      if(at%alignment) throw std::runtime_error("alignment"); return Bytes(at,size);
    }
    uint32_t Word(uint32_t at) const { return GuestBlockWord(Bytes(at,4)); }
    void StoreWord(uint32_t at,uint32_t value) const { ::Word(bytes,at,value); }
  } reader;
  {
    constexpr uint32_t manager=24000,node=24100,scene=25000;
    reader.StoreWord(manager,node); reader.StoreWord(manager+12,0);
    reader.StoreWord(node,0); reader.StoreWord(node+8,scene);
    for(uint32_t i=0;i<48;++i) reader.StoreWord(scene+32+i*4,0x3f800000+i);
    const auto cameras=ReadNativeScenePassCameras(reader,manager);
    Require(cameras.size()==1 && cameras.at(scene).projection[0]==0x3f800000 &&
      cameras.at(scene).view[0]==0x3f800010 && cameras.at(scene).view_projection[15]==0x3f80002f,
      "camera producer decoded the wrong matrix ranges");
    reader.StoreWord(node,node);
    Reject([&] { ReadNativeScenePassCameras(reader,manager); });
    reader.StoreWord(manager,0);
    Require(ReadNativeScenePassCameras(reader,manager).empty(),"empty view list retained a camera");
  }
  constexpr uint32_t device=1024,bank=device+1792;
  for(size_t i=0;i<256;++i) reader.bytes[16384+i]=uint8_t(i+1);
  const InstanceParameter patches[]{{16384,3,4},{16512,5,2},{16384,255,1}};
  auto expected=reader.bytes;
  // Retail upload copies bytes in list order. The native indexed CPU prefix
  // consumes the accumulated vertex dirty mask, leaving all five masks zero.
  for(const auto& p:patches) std::memcpy(expected.data()+bank+p.first*16,expected.data()+p.data,p.count*16);
  Require(NativeQueuedConstantsConsumed(reader,device,patches) && reader.bytes==expected,
    "native queued constants differ from ordered retail upload/consume effects");
  for(size_t offset=0;offset<40;++offset) {
    reader.bytes[device+offset]=1; const auto before=reader.bytes;
    Require(!NativeQueuedConstantsConsumed(reader,device,patches) && before==reader.bytes,"dirty state was bypassed");
    reader.bytes[device+offset]=0;
  }
  for(const auto bad:{InstanceParameter{bank,0,1},InstanceParameter{16384,256,1},InstanceParameter{16384,0,0}}) {
    const auto before=reader.bytes;
    Require(!NativeQueuedConstantsConsumed(reader,device,std::span(&bad,1)) && before==reader.bytes,"unsafe override changed state");
  }
  const InstanceParameter invalid[]{{16384,0,1},{32760,1,1}};
  const auto before=reader.bytes;
  Reject([&] { NativeQueuedConstantsConsumed(reader,device,invalid); });
  Require(before==reader.bytes,"invalid override partially wrote guest bank");
  constexpr uint32_t group=64,first=128,second=160,last=192,sentinel=224;
  reader.StoreWord(group+4,first); reader.StoreWord(group+8,sentinel);
  reader.StoreWord(first,1000); reader.StoreWord(first+4,second);
  reader.StoreWord(second,2000); reader.StoreWord(second+4,sentinel);
  reader.StoreWord(last,3000); reader.StoreWord(last+4,sentinel);
  std::vector<uint32_t> order;
  VisitNativeQueuedScene(reader,group,[&] { order.push_back(0); reader.StoreWord(group+4,sentinel); },[&](uint32_t instance) {
    order.push_back(instance); if(instance==1000) reader.StoreWord(first+4,last);
  });
  Require(order==std::vector<uint32_t>{0,1000,3000} && reader.Word(group+4)==0,
    "native queue changed callback order, initial selection, link mutation or final clear");
  reader.StoreWord(group+4,sentinel);
  VisitNativeQueuedScene(reader,group,[] { throw std::runtime_error("empty queue setup"); },[](uint32_t) { throw std::runtime_error("empty draw"); });
  Require(reader.Word(group+4)==sentinel,"empty queue was cleared");
  constexpr uint32_t other_group=96;
  reader.StoreWord(other_group+4,sentinel); reader.StoreWord(other_group+8,sentinel);
  NativeSceneQueues queues;
  const auto no_links=reader.bytes;
  queues.Push(group,first); queues.Push(other_group,second); queues.Push(group,last);
  Require(queues.Take(group)==std::vector<uint32_t>{last,first} &&
    queues.Take(other_group)==std::vector<uint32_t>{second} && queues.empty() && reader.bytes==no_links,
    "native queues changed order, crossed groups or wrote guest links");
  queues.Push(group,first); queues.Push(other_group,second); queues.Push(group,last);
  queues.Materialize(reader);
  Require(!queues.enabled && queues.empty() && reader.Word(group+4)==last &&
    reader.Word(last+4)==first && reader.Word(first+4)==sentinel &&
    reader.Word(other_group+4)==second && reader.Word(second+4)==sentinel,
    "native queue fallback did not restore original head-insertion order");
  Reject([&] { queues.Push(group,first); });
  {
    // The last-group lookup never outlives its group or crosses a copy.
    NativeSceneQueues cached;
    cached.Push(group,first);
    Require(cached.Contains(group) && !cached.Contains(other_group) && cached.Contains(group),"queue lookup lost a group");
    auto copy=cached; copy.Push(group,last);
    Require(cached.Take(group)==std::vector<uint32_t>{first} && !cached.Contains(group),"queue copy shared its groups");
    cached.Push(group,second);
    Require(copy.Take(group)==std::vector<uint32_t>{last,first} && cached.Take(group)==std::vector<uint32_t>{second},
      "queue lookup appended to a taken or copied group");
  }
  constexpr uint32_t owner=8192;
  NativeSceneSources sources;
  Require(!sources.HasOwner(owner),"unconstructed static owner exists");
  sources.Born(owner);
  reader.StoreWord(owner+404,2);
  reader.StoreWord(owner+412,17000); reader.StoreWord(owner+416,17056);
  reader.StoreWord(owner+456,18000); reader.StoreWord(owner+460,18028);
  auto parts=ReadNativeStaticSceneParts(reader,owner);
  Require(parts==std::vector<NativeSceneSources::Part>{{17000,0,0},{17028,0,1},{18000,1,0}},
    "static source event did not enumerate all LOD parts");
  sources.Observe(owner,parts);
  Require(sources.LodParts(owner+408)->size()==2 && sources.LodParts(owner+452)->size()==1 &&
    !sources.LodParts(30000),"native LOD selection did not resolve its registered parts");
  {
    // The visibility walk's single lookup must answer as LodParts/Visibility
    // do, and keep its parts after a producer event replaces the owner's.
    const auto candidate=sources.FindCandidate(owner);
    Require(candidate.registered && !candidate.visibility && candidate.Lod(0)->size()==2 &&
      candidate.Lod(1)->size()==1 && candidate.Lod(2)->empty() && !candidate.Lod(3) &&
      candidate.Lod(0)->data()==sources.LodParts(owner+408)->data(),"visibility candidate disagreed with LOD lookup");
    Require(!sources.FindCandidate(30000).registered && !sources.FindCandidate(30000).Lod(0),
      "unregistered visibility candidate resolved parts");
    const auto view=sources.FindCandidateView(owner);
    Require(view.registered && !view.visibility && view.Lod(0)->data()==candidate.Lod(0)->data() &&
      view.Lod(1)->size()==1 && !view.Lod(3) && !sources.FindCandidateView(30000).Lod(0) &&
      SameNativeSceneCandidate(view,NativeSceneSources::View(candidate)),"candidate view disagreed with the retaining lookup");
    const std::array<NativeSceneSources::Part,1> moved{{{19500,0,0}}};
    sources.Observe(owner,moved);
    Require(sources.FindCandidate(owner).Lod(0)->front().instance==19500 &&
      candidate.Lod(0)->front().instance==17000 && candidate.Lod(1)->front().instance==18000,
      "visibility candidate lost or mixed its membership after a producer event");
    NativeSceneVisibility bounds; bounds.lod_count=2;
    sources.PublishVisibility(owner,bounds);
    Require(sources.FindCandidate(owner).visibility==sources.Visibility(owner) && !candidate.visibility,
      "visibility candidate did not follow published bounds");
    sources.Observe(owner,parts);
  }
  reader.StoreWord(owner+436,20000); reader.StoreWord(20004,21000);
  reader.StoreWord(17016,22000); reader.StoreWord(17020,22012);
  reader.StoreWord(22000,21000); reader.StoreWord(22004,12); reader.StoreWord(22008,4);
  sources.Observe(owner,ReadNativeStaticSceneParts(reader,owner));
  const auto source=*sources.Find(17000);
  Require(source.world_first==12 && !sources.Find(17028)->world_first,
    "source publication did not distinguish world-only and empty instance parameters");
  reader.StoreWord(22004,16);
  sources.Observe(owner,ReadNativeStaticSceneParts(reader,owner));
  Require(source.world_first==12 && sources.Find(17000)->world_first==16,
    "source register update mutated a retained source or kept stale metadata");
  reader.StoreWord(17020,22024);
  reader.StoreWord(22012,23000); reader.StoreWord(22016,20); reader.StoreWord(22020,1);
  sources.Observe(owner,ReadNativeStaticSceneParts(reader,owner));
  Require(!sources.Find(17000)->world_first,"additional override was certified world-only");
  reader.StoreWord(17020,22012); reader.StoreWord(22000,21016);
  sources.Observe(owner,ReadNativeStaticSceneParts(reader,owner));
  Require(!sources.Find(17000)->world_first,"foreign matrix storage was certified world-only");
  reader.StoreWord(22000,21000); reader.StoreWord(22004,12);
  sources.Observe(owner,ReadNativeStaticSceneParts(reader,owner));
  for(size_t i=0;i<16;++i) reader.StoreWord(owner+224+uint32_t(i)*4,std::bit_cast<uint32_t>(float(i)+.25f));
  auto world=ReadNativeStaticWorld(reader,owner);
  for(size_t row=0;row<4;++row) for(size_t col=0;col<4;++col)
    Require(GuestBlockWord(world.data()+(col*4+row)*4)==reader.Word(owner+224+uint32_t(row*4+col)*4),
      "static event did not reproduce guest world-register transpose");
  Require(!sources.WorldRegisters(source,21000),"world existed before publication");
  Require(sources.PublishWorld(owner,world),"live world publication rejected");
  const auto original_world=sources.WorldRegisters(source,21000);
  Require(original_world && !sources.WorldRegisters(source,21016),"world storage identity was ignored");
  sources.PublishWorld(owner,world);
  Require(sources.WorldRegisters(source,21000)==original_world,"unchanged world was reallocated");
  world[3]^=1; sources.PublishWorld(owner,world);
  Require(*original_world!=*sources.WorldRegisters(source,21000),"world publication mutated an older retained value");
  reader.StoreWord(owner+404,1);
  reader.StoreWord(owner+412,19000); reader.StoreWord(owner+416,19028);
  sources.Observe(owner,ReadNativeStaticSceneParts(reader,owner));
  Require(!sources.Find(17000) && !sources.Find(18000) && sources.Find(19000),
    "model replacement retained removed/relocated LOD records");
  reader.StoreWord(owner+416,19029);
  Reject([&] { ReadNativeStaticSceneParts(reader,owner); });
  Require(sources.Find(19000),"invalid model publication changed previous source catalog");
  reader.StoreWord(owner+404,4);
  Reject([&] { ReadNativeStaticSceneParts(reader,owner); });
  reader.StoreWord(owner+404,0);
  sources.Observe(owner,ReadNativeStaticSceneParts(reader,owner));
  Require(sources.HasOwner(owner) && !sources.Find(19000),"empty model did not clear parts while preserving lifetime");
  sources.Retire(owner); Require(!sources.HasOwner(owner),"static source survived retirement");
  Require(!sources.LodParts(owner+408),"retired native LOD still accepted queue selections");
  Require(!sources.FindCandidate(owner).registered && !sources.FindCandidate(owner).Lod(0) &&
    !sources.FindCandidate(owner).visibility,"retired owner remained a visibility candidate");
  Require(!sources.WorldRegisters(source,21000) && !sources.PublishWorld(owner,world),"retired source accepted a world publication");
  sources.Born(owner); sources.PublishWorld(owner,world);
  Require(!sources.WorldRegisters(source,21000),"reused owner address accepted an old world generation");
  // These addresses describe an entirely unselected group: no device or draw
  // state is present, and decoding is read-only.
  reader.StoreWord(26012,27000);
  reader.StoreWord(27072,28000); reader.StoreWord(27076,28100);
  reader.StoreWord(28004,28200); reader.StoreWord(28128,28300);
  reader.StoreWord(27060,32); reader.StoreWord(27140,8);
  reader.StoreWord(27000,28400); reader.StoreWord(28416,29000);
  reader.StoreWord(29108,29200); reader.StoreWord(29200,29300); reader.StoreWord(29300,29400);
  const auto untouched=reader.bytes;
  Require(ReadNativeSceneGeometrySource(reader,26000)==NativeSceneGeometrySource{27004,27084,28300,32,6,29000,29400} &&
    untouched==reader.bytes,"native geometry descriptor changed resource roles, triangle count or guest state");
  reader.StoreWord(27076,28200); Reject([&] { ReadNativeSceneGeometrySource(reader,26000); });
  reader.StoreWord(27076,28100); reader.StoreWord(27140,0xffffffff);
  Reject([&] { ReadNativeSceneGeometrySource(reader,26000); });
  reader.StoreWord(27140,6); reader.StoreWord(27060,3);
  Reject([&] { ReadNativeSceneGeometrySource(reader,26000); });
  NativeMaterialParameters::Groups material_schema;
  material_schema[0].push_back({"local",31000,1,0});
  material_schema[1].push_back({"global",31500,1,4});
  material_schema[2].push_back({"unused",UINT32_MAX,1,0});
  reader.StoreWord(31004,31200); reader.StoreWord(31500,31400);
  reader.StoreWord(31400,31300); reader.StoreWord(31408,1);
  reader.StoreWord(31200,0x3f800000); reader.StoreWord(31300,0x40000000);
  reader.StoreWord(29204,29440); reader.StoreWord(29444,29500);
  material_schema.textures[0].push_back({"local_image",30000});
  material_schema.textures[1].push_back({"global_image",30040});
  material_schema.textures[1].push_back({"unused_image",UINT32_MAX});
  reader.StoreWord(30004,30300); reader.StoreWord(30008,2);
  reader.StoreWord(30040,30100); reader.StoreWord(30044,3); reader.StoreWord(30128,30400);
  for(uint32_t i=0;i<4;++i) { reader.StoreWord(30012+i*4,i+1); reader.StoreWord(30132+i*4,i+5); }
  reader.StoreWord(30344,0x3c0); reader.StoreWord(30444,0x3c4);
  reader.StoreWord(29096,30600); reader.StoreWord(29104,2);
  reader.StoreWord(30600,0x44); reader.StoreWord(30604,7);
  reader.StoreWord(30608,0x48); reader.StoreWord(30612,8);
  const auto load_material=[&] {
    return ReadNativeSceneMaterialInputs(reader,29000,material_schema,
      [](bool,const std::string& name) { return name=="unused"?size_t(0):size_t(16); },
      [](const std::string& name) { return name!="unused_image"; });
  };
  const auto material_memory=reader.bytes;
  const auto material_inputs=load_material();
  Require(material_inputs.vertex_registers==std::vector<NativeSceneMaterialDefinition::VertexRegisters>{{"local",0,1},{"global",4,1}},
    "material publication lost vertex register metadata");
  NativeSceneMaterialDefinition world_definition;
  world_definition.vertex_registers={{"g_mWorld",12,4},{"other",16,2}};
  Require(world_definition.WorldRegisterFirst()==12,"published world register not resolved");
  world_definition.vertex_registers[1].first=15;
  Require(!world_definition.WorldRegisterFirst(),"overlapping world register accepted");
  world_definition.vertex_registers={{"g_mWorld",12,4},{"g_mWorld",32,4}};
  Require(!world_definition.WorldRegisterFirst(),"ambiguous world register accepted");
  world_definition.vertex_registers={{"g_mWorld",253,4}};
  Require(!world_definition.WorldRegisterFirst(),"out-of-bank world register accepted");
  Require(material_inputs.vertex==29400 && material_inputs.pixel==29500 && material_inputs.constants.size()==2 &&
    !material_inputs.constants[0].global && material_inputs.constants[1].global &&
    GuestBlockWord(material_inputs.constants[1].registers.data())==0x40000000 && reader.bytes==material_memory,
    "material input publication read wrong values or changed guest state");
  Require(material_inputs.textures.size()==2 && material_inputs.textures[0].settings==std::array<uint32_t,4>{1,2,3,4} &&
    !material_inputs.textures[0].global && material_inputs.textures[1].global &&
    material_inputs.textures[1].settings==std::array<uint32_t,4>{5,6,7,8} && material_inputs.textures[1].lod_range==0x3c4 &&
    material_inputs.state_overrides==std::vector<std::array<uint32_t,2>>{{0x44,7},{0x48,8}},
    "material input publication lost sampler or ordered state overrides");
  reader.StoreWord(31300,0); reader.StoreWord(30132,99);
  Require(load_material()!=material_inputs && GuestBlockWord(material_inputs.constants[1].registers.data())==0x40000000,
    "material input snapshot borrowed source values");
  reader.StoreWord(31408,0); Reject(load_material); reader.StoreWord(31408,1);
  // Native reflection can require more global registers than Xbox metadata.
  const auto load_matrix=[&] {
    return ReadNativeSceneMaterialInputs(reader,29000,material_schema,
      [](bool,const std::string& name) { return name=="unused"?size_t(0):name=="global"?size_t(64):size_t(16); },
      [](const std::string& name) { return name!="unused_image"; });
  };
  reader.StoreWord(31408,4);
  reader.StoreWord(31360,0x3f800000);
  const auto matrix_inputs=load_matrix();
  Require(matrix_inputs.constants[1].registers.size()==64 &&
    GuestBlockWord(matrix_inputs.constants[1].registers.data()+60)==0x3f800000,
    "global material truncated native reflection to guest descriptor extent");
  reader.StoreWord(31408,3); Reject(load_matrix);
  reader.StoreWord(31408,4097); Reject(load_matrix);
  reader.StoreWord(31408,1);
  material_schema[0][0].registers=2; Reject(load_material);
  material_schema[0][0].registers=0; Reject(load_material);
  material_schema[0][0].registers=1;
  reader.StoreWord(30044,16); Reject(load_material); reader.StoreWord(30044,3);
  reader.StoreWord(29104,4097); Reject(load_material);
}
// Static preload reprocesses a group only when its membership revision, a
// recorded guest input or a tracked buffer write changed. Proving an unchanged
// group compares recorded bytes and revisions; it never re-decodes the group.
void PreloadChangeSignals() {
  struct CountingReader {
    const GeometryRetryReader& reader;
    mutable size_t reads=0;
    uint32_t Add(uint32_t address,uint32_t offset) const { return reader.Add(address,offset); }
    const uint8_t* Bytes(uint32_t at,size_t size) const { ++reads; return reader.Bytes(at,size); }
    uint32_t Word(uint32_t at) const { return GuestBlockWord(Bytes(at,4)); }
  };
  std::vector<uint8_t> memory(0x10000);
  const GeometryRetryReader reader{memory};
  reader.StoreWord(26012,27000);
  reader.StoreWord(27072,28000); reader.StoreWord(27076,28100);
  reader.StoreWord(28004,28200); reader.StoreWord(28128,28300);
  reader.StoreWord(27060,32); reader.StoreWord(27140,8);
  reader.StoreWord(27000,28400); reader.StoreWord(28416,29000);
  reader.StoreWord(29108,29200); reader.StoreWord(29200,29300); reader.StoreWord(29300,29400);
  const NativeSceneGeometrySource expected{27004,27084,28300,32,6,29000,29400};
  NativeRecordedReads descriptor;
  Require(ReadNativeSceneGeometrySource(NativeRecordingReader(reader,descriptor),26000)==expected,
    "recording reader changed the geometry descriptor");
  CountingReader decoded{reader};
  Require(ReadNativeSceneGeometrySource(decoded,26000)==expected,"counting reader changed the geometry descriptor");
  CountingReader proven{reader};
  Require(descriptor.Unchanged(proven) && proven.reads==descriptor.ranges() && proven.reads<decoded.reads,
    "unchanged group descriptor was re-read instead of compared");
  reader.StoreWord(27064,5); reader.StoreWord(29000,7);
  Require(descriptor.Unchanged(reader),"a byte the descriptor never read invalidated the group");
  reader.StoreWord(27060,48);
  Require(!descriptor.Unchanged(reader),"changed descriptor stride was not detected");
  NativeRecordedReads reread;
  Require(ReadNativeSceneGeometrySource(NativeRecordingReader(reader,reread),26000).stride==48 && reread.Unchanged(reader),
    "changed group was not re-read");
  reader.StoreWord(28416,29004);
  Require(!reread.Unchanged(reader),"changed material pointer behind the descriptor was not detected");
  reader.StoreWord(28416,29000);
  Require(reread.Unchanged(reader),"restored descriptor bytes were not recognized");
  Require(!NativeRecordedReads{}.Unchanged(reader),"an empty record proved a group unchanged");

  // Material: the program (pass, shaders, textures, samplers, state) is proven
  // by recorded bytes; constant values change per frame and are refreshed alone.
  NativeMaterialParameters::Groups schema;
  schema[0].push_back({"local",31000,1,0});
  schema[1].push_back({"global",31500,1,4});
  schema[1].push_back({"g_mView",31600,4,8});
  reader.StoreWord(31004,31200); reader.StoreWord(31500,31400);
  reader.StoreWord(31400,31300); reader.StoreWord(31408,1);
  reader.StoreWord(31200,0x3f800000); reader.StoreWord(31300,0x40000000);
  reader.StoreWord(31600,31700); reader.StoreWord(31700,31800); reader.StoreWord(31708,4);
  reader.StoreWord(29204,29440); reader.StoreWord(29444,29500);
  schema.textures[0].push_back({"local_image",30000});
  reader.StoreWord(30004,30300); reader.StoreWord(30008,2);
  for(uint32_t i=0;i<4;++i) reader.StoreWord(30012+i*4,i+1);
  reader.StoreWord(30344,0x3c0);
  reader.StoreWord(29096,30600); reader.StoreWord(29104,1);
  reader.StoreWord(30600,0x44); reader.StoreWord(30604,7);
  const auto required=[](bool,const std::string& name) { return name=="g_mView"?size_t(64):size_t(16); };
  const auto uses=[](const std::string&) { return true; };
  const auto layout=ResolveNativeSceneMaterialConstants(schema,required);
  Require(layout.size()==3 && !layout[0].pass_owned && !layout[1].pass_owned && layout[2].pass_owned,
    "material constant layout lost a parameter or its pass ownership");
  const auto load_program=[&](NativeRecordedReads& reads) {
    const NativeRecordingReader recorder(reader,reads);
    auto definition=ReadNativeSceneMaterialDefinition(recorder,29000,schema,uses);
    ReadNativeMaterialSamplerOperations(recorder,schema);
    return definition;
  };
  NativeRecordedReads program;
  const auto definition=load_program(program);
  const auto constants=ReadNativeSceneMaterialConstants(reader,schema,layout);
  const auto combined=ReadNativeSceneMaterialInputs(reader,29000,schema,required,uses);
  Require(static_cast<const NativeSceneMaterialDefinition&>(combined)==definition && combined.constants==constants &&
    constants.size()==3 && constants[2].registers.size()==64,"split material reads differ from the combined inputs");
  Require(program.Unchanged(reader) && !RefreshNativeSceneMaterialConstants(reader,schema,layout,constants),
    "unchanged material program or constants were reprocessed");
  reader.StoreWord(31320,9); reader.StoreWord(30608,0x48);
  Require(program.Unchanged(reader) && !RefreshNativeSceneMaterialConstants(reader,schema,layout,constants),
    "bytes outside the material inputs invalidated the group");
  // A constant moves, the program does not: only constant values are re-read.
  reader.StoreWord(31300,0x40400000);
  Require(program.Unchanged(reader),"changed constant value invalidated the material program");
  CountingReader constant_reads{reader};
  const auto refreshed=RefreshNativeSceneMaterialConstants(constant_reads,schema,layout,constants);
  Require(refreshed && GuestBlockWord((*refreshed)[1].registers.data())==0x40400000 &&
    (*refreshed)[0]==constants[0] && (*refreshed)[2]==constants[2] && constant_reads.reads==5,
    "constant refresh re-read the program, read a pass-owned constant or missed the new value");
  Require(!RefreshNativeSceneMaterialConstants(reader,schema,layout,*refreshed),"refreshed constants were not current");
  // Pass-owned globals are replaced before every use; their values never republish.
  reader.StoreWord(31800,0x3f800000);
  Require(program.Unchanged(reader) && !RefreshNativeSceneMaterialConstants(reader,schema,layout,*refreshed),
    "pass-owned camera constant republished the group");
  {
    // The preload's parallel precheck (ProbeNativeSceneMaterialGuestInputs)
    // decides what the serial path would: program bytes, then, only when they
    // hold, the constant refresh; a refresh that throws is reported, not thrown.
    const auto probed=ProbeNativeSceneMaterialGuestInputs(reader,program,schema,layout,*refreshed);
    Require(probed.program_unchanged && probed.constants_read && !probed.constants,"current material probed as changed");
    reader.StoreWord(31300,0x40800000);
    const auto moved=ProbeNativeSceneMaterialGuestInputs(reader,program,schema,layout,*refreshed);
    const auto serial=RefreshNativeSceneMaterialConstants(reader,schema,layout,*refreshed);
    Require(moved.program_unchanged && moved.constants_read && moved.constants && serial && *moved.constants==*serial,
      "probed constant refresh differs from the serial refresh");
    reader.StoreWord(31300,0x40400000);
    reader.StoreWord(30604,8);
    CountingReader skipped{reader};
    const auto changed=ProbeNativeSceneMaterialGuestInputs(skipped,program,schema,layout,*refreshed);
    Require(!changed.program_unchanged && !changed.constants && skipped.reads<=program.ranges(),
      "changed material program was refreshed or probed as current");
    reader.StoreWord(30604,7);
    reader.StoreWord(31408,0);
    const auto failed=ProbeNativeSceneMaterialGuestInputs(reader,program,schema,layout,*refreshed);
    Require(failed.program_unchanged && !failed.constants_read && !failed.constants,"a throwing refresh was probed as read");
    reader.StoreWord(31408,1);
  }
  reader.StoreWord(31408,0);
  Reject([&] { RefreshNativeSceneMaterialConstants(reader,schema,layout,*refreshed); });
  reader.StoreWord(31408,1);
  Reject([&] { RefreshNativeSceneMaterialConstants(reader,schema,layout,std::vector<NativeSceneMaterialInputs::Constant>{}); });
  // Program inputs still invalidate the program and are re-read.
  reader.StoreWord(30340,0x80000000);
  Require(!program.Unchanged(reader),"changed texture header was not detected");
  reader.StoreWord(30340,0); reader.StoreWord(30604,8);
  Require(!program.Unchanged(reader),"changed material state override was not detected");
  NativeRecordedReads reprogram;
  Require(load_program(reprogram).state_overrides[0][1]==8 && reprogram.Unchanged(reader),"changed material program was not re-read");
  reader.StoreWord(30604,7);
  Require(program.Unchanged(reader),"restored material program was not recognized");
  {
    // The global g_mWorld holds the last drawn object's world and moves every
    // tick; every consumer replaces it before it is observable, so it never
    // republishes the group, unless its published value is a NaN.
    auto world_schema=schema;
    world_schema[1].push_back({"g_mWorld",32000,4,12});
    reader.StoreWord(32000,32100); reader.StoreWord(32100,32200); reader.StoreWord(32108,4);
    reader.StoreWord(32200,0x3f800000);
    const auto world_required=[](bool,const std::string& name) { return name=="g_mView" || name=="g_mWorld"?size_t(64):size_t(16); };
    const auto world_layout=ResolveNativeSceneMaterialConstants(world_schema,world_required);
    Require(world_layout.size()==4 && world_layout[3].object_world && !world_layout[3].pass_owned &&
      !world_layout[0].object_world && !world_layout[1].object_world && !world_layout[2].object_world,
      "object world slot was not identified");
    const auto published=ReadNativeSceneMaterialConstants(reader,world_schema,world_layout);
    reader.StoreWord(32200,0x40000000); reader.StoreWord(32220,0x3f000000);
    CountingReader world_reads{reader};
    Require(!RefreshNativeSceneMaterialConstants(world_reads,world_schema,world_layout,published) && world_reads.reads==5,
      "moved object world was read or republished the group");
    const auto previous=GuestBlockWord(reader.Bytes(31300,4));
    reader.StoreWord(31300,0x40a00000);
    const auto other=RefreshNativeSceneMaterialConstants(reader,world_schema,world_layout,published);
    Require(other && (*other)[3]==published[3] && GuestBlockWord((*other)[1].registers.data())==0x40a00000,
      "another constant's refresh carried the moved object world");
    reader.StoreWord(31300,previous);
    auto invalid=published; Word(invalid[3].registers,4,0x7fc00000);
    const auto repaired=RefreshNativeSceneMaterialConstants(reader,world_schema,world_layout,invalid);
    Require(repaired && GuestBlockWord((*repaired)[3].registers.data())==0x40000000 &&
      !NativeSceneWorldHasNaN((*repaired)[3].registers) && NativeSceneWorldHasNaN(invalid[3].registers),
      "NaN published object world was not refreshed");
  }

  // Membership: identical observations keep the prune revision; erasing a group advances it.
  NativeSceneSources sources;
  sources.Born(100);
  const NativeSceneSources::Part parts[]{ {1000,0,0,0,500} };
  sources.Observe(100,parts);
  const auto membership=sources.GroupRevision();
  sources.Observe(100,parts);
  Require(sources.GroupRevision()==membership,"unchanged membership invalidated preload records");
  sources.Retire(100);
  Require(!sources.FindGroup(500) && sources.GroupRevision()!=membership,"erased group did not advance the prune revision");

  // Buffers: the probe consumes no observation, defers due comparisons and
  // reports tracked writes and overlapping writers.
  NativeBufferWrites writes;
  writes.Subscribe(1,0x1000,64); writes.Subscribe(2,0x2000,32);
  const std::vector<uint8_t> vertex_bytes(64,1),index_bytes(32,2);
  const auto vertex=std::make_shared<const std::vector<uint8_t>>(vertex_bytes);
  const auto index=std::make_shared<const std::vector<uint8_t>>(index_bytes);
  NativeBufferWrites::SnapshotPolicy policy{};
  policy.verify_initial=1; policy.verify_interval=4;
  using Source=NativeBufferWrites::SnapshotSource;
  const auto observe=[&] {
    return writes.CopyObservedSet(std::array<Source,2>{{
      {1,0x1000,vertex_bytes,vertex},{2,0x2000,index_bytes,index}}},nullptr,policy);
  };
  const auto first=observe();
  Require(first && (*first)[0].contents==vertex && (*first)[1].contents==index,"initial geometry observation copied");
  const std::array<NativeBufferWrites::ObservedVersion,2> loaded{(*first)[0].version,(*first)[1].version};
  using View=NativeBufferWrites::SnapshotIdentityView;
  const std::array<View,2> views{{{1,0x1000,64,&vertex},{2,0x2000,32,&index}}};
  for(int i=0;i<16;++i) Require(writes.Unchanged(views,loaded,policy),"unchanged buffers required revalidation");
  const auto before=writes.Trust();
  Require(before.trusted==0 && before.verified==2,"unchanged probe consumed or verified an observation");
  size_t accepted=0;
  while(writes.TryValidateObservedSet(views,policy)) ++accepted;
  Require(accepted==3 && !writes.Unchanged(views,loaded,policy),"probe skipped a due live comparison");
  const auto verified=observe();
  Require(verified && (*verified)[0].contents==vertex && (*verified)[0].version.revision==loaded[0].revision &&
    writes.Unchanged(views,loaded,policy),"verified unchanged buffers were not accepted as unchanged");
  writes.Record(0x3000,16);
  Require(writes.Unchanged(views,loaded,policy),"unrelated write invalidated the group");
  {
    NativeBufferWrites::WriterScope active(&writes,NativeBufferWrites::Range{0x1010,4});
    Require(!writes.Unchanged(views,loaded,policy),"overlapping active writer was ignored");
  }
  Require(writes.Unchanged(views,loaded,policy),"completed disjoint writer scope invalidated the group");
  writes.Record(0x2010,4);
  Require(!writes.Unchanged(views,loaded,policy),"tracked index write did not invalidate the group");
  const auto rewritten=observe();
  Require(rewritten && (*rewritten)[1].version.revision!=loaded[1].revision,"changed buffer did not require a re-load");
  const std::array<NativeBufferWrites::ObservedVersion,2> reloaded{(*rewritten)[0].version,(*rewritten)[1].version};
  Require(!writes.Unchanged(views,reloaded,NativeBufferWrites::SnapshotPolicy{}),
    "compare-every-observation policy skipped the live comparison");
}
std::span<const uint8_t> Bytes(ID3DBlob* code) {
  return {static_cast<const uint8_t*>(code->GetBufferPointer()),code->GetBufferSize()};
}
void Run(std::shared_ptr<NativeRenderBackend> backend,bool column_major) {
  Effect effect;
  effect.source=std::string(column_major?"column_major":"row_major")+R"( float4x4 g_mWorld : WORLD;
    row_major float4x4 g_mViewProjection;
    row_major float4x4 g_mViewTranspose;
    struct V { float4 position:SV_Position; };
    V VS(float3 position:POSITION0) { V o; o.position=mul(mul(float4(position,1),g_mWorld),g_mViewProjection); o.position.x*=g_mViewTranspose._11; return o; }
    float4 tint;
    Texture2D image; SamplerState imageSampler;
    float4 PS(V v):SV_Target { return tint*image.Sample(imageSampler,float2(.5,.5)); }
  )";
  auto vs=CompileNativeShader(nullptr,effect,{false,"VS","vs_3_0"},"native-scene-test.fx");
  auto ps=CompileNativeShader(nullptr,effect,{true,"PS","ps_3_0"},"native-scene-test.fx");
  Require(AddNativeWorldInstancing(vs,effect,"native-scene-test.fx"),"scene test instance shader");
  std::vector<uint8_t> declaration(12),vertices(48),indices{0,0,0,1,0,2,0,0,0,2,0,3};
  Word(declaration,4,0x2a23b9);
  const float points[]{-.125f,-.25f,.5f, -.125f,.25f,.5f, .125f,.25f,.5f, .125f,-.25f,.5f};
  for(size_t i=0;i<12;++i) Word(vertices,i*4,std::bit_cast<uint32_t>(points[i]));
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
  {
    NativeIndexedMesh mesh(*backend,vs,declaration,12,vertices,indices,2);
    geometry=std::make_shared<const NativeIndexedMesh::RetainedDraw>(mesh.RetainDraw(backend,0,6));
  }
  NativeBackendPipelineDesc desc;
  desc.vertex=Bytes(vs.bytecode.Get()); desc.pixel=Bytes(ps.bytecode.Get());
  desc.vertex_id=100+uint64_t(column_major); desc.pixel_id=102;
  desc.state={0x10001,0,0,0,15,0}; desc.render_targets=1; desc.rtv_format[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.input_layout=geometry->input_layout().elements(); desc.input_layout_id=geometry->input_layout().fingerprint();
  auto& pipeline=backend->CreatePipeline(desc);
  NativeOwnedInputLayout instanced;
  for(const auto& e:desc.input_layout) instanced.Add(e.semantic,e.semantic_index,e.format,e.slot,e.offset,e.per_instance,e.step_rate);
  for(uint32_t row=0;row<4;++row) instanced.Add("EDFINSTANCE",row,DXGI_FORMAT_R32G32B32A32_FLOAT,15,row*16,true,1);
  desc.vertex=Bytes(vs.instanced_bytecode.Get()); desc.vertex_id|=uint64_t(1)<<63;
  desc.input_layout=instanced.elements(); desc.input_layout_id=instanced.fingerprint();
  pipeline.world_instanced=&backend->CreatePipeline(desc);
  pipeline.instance_world_slot=vs.instance_world_slot; pipeline.instance_world_offset=vs.instance_world_offset;
  auto constants=[&](std::array<float,4> color) {
    std::vector<NativeSceneConstant> result;
    for(auto [stage,shader]:{std::pair{NativeBackendStage::Vertex,&vs},std::pair{NativeBackendStage::Pixel,&ps}}) {
      D3D11_SHADER_DESC shader_desc{}; shader->reflection->GetDesc(&shader_desc);
      for(UINT b=0;b<shader_desc.ConstantBuffers;++b) {
        auto* buffer=shader->reflection->GetConstantBufferByIndex(b);
        D3D11_SHADER_BUFFER_DESC cb{}; buffer->GetDesc(&cb);
        D3D11_SHADER_INPUT_BIND_DESC binding{}; shader->reflection->GetResourceBindingDescByName(cb.Name,&binding);
        NativeSceneConstant image; image.stage=stage; image.slot=binding.BindPoint; image.bytes.resize(cb.Size);
        for(UINT v=0;v<cb.Variables;++v) {
          D3D11_SHADER_VARIABLE_DESC variable{}; buffer->GetVariableByIndex(v)->GetDesc(&variable);
          if(!(variable.uFlags&D3D_SVF_USED)) continue;
          const std::string name=variable.Name;
          if(name=="g_mWorld") image.matrices.push_back({NativeSceneMatrixSource::World,variable.StartOffset,column_major});
          else if(name=="g_mViewProjection") image.matrices.push_back({NativeSceneMatrixSource::ViewProjection,variable.StartOffset,false});
          else if(name=="g_mViewTranspose") image.matrices.push_back({NativeSceneMatrixSource::ViewTranspose,variable.StartOffset,false});
          else if(name=="tint") std::memcpy(image.bytes.data()+variable.StartOffset,color.data(),16);
        }
        result.push_back(std::move(image));
      }
    }
    return result;
  };
  ShaderBindings source_vertex(nullptr,vs),source_pixel(nullptr,ps);
  std::vector<uint8_t> matrix_bytes(64),tint_bytes(16);
  for(size_t i=0;i<16;++i) Word(matrix_bytes,i*4,std::bit_cast<uint32_t>(kNativeSceneIdentity[i]));
  Word(tint_bytes,0,std::bit_cast<uint32_t>(1.f)); Word(tint_bytes,12,std::bit_cast<uint32_t>(1.f));
  source_vertex.SetGuestFloatRegisters("g_mWorld",matrix_bytes);
  source_vertex.SetGuestFloatRegisters("g_mViewProjection",matrix_bytes);
  source_vertex.SetGuestFloatRegisters("g_mViewTranspose",matrix_bytes);
  source_pixel.SetGuestFloatRegisters("tint",tint_bytes);
  NativeBackendTextureDesc texture_desc; texture_desc.width=texture_desc.height=1;
  texture_desc.format=DXGI_FORMAT_R8G8B8A8_UNORM;
  const uint8_t white_pixel[]{255,255,255,255};
  std::shared_ptr<NativeBackendTexture> white=backend->CreateTexture(texture_desc,white_pixel);
  source_pixel.SetTexture("image",white);
  NativeBackendSamplerDesc sampler_desc; sampler_desc.max_lod=15;
  source_pixel.SetSampler("imageSampler",&backend->CreateSampler(sampler_desc));
  const auto captured=CaptureNativeSceneMaterial(backend,pipeline,source_vertex,source_pixel);
  std::vector<NativeSceneMaterialInputs::Constant> program_constants;
  auto material_program=std::make_shared<NativeSceneMaterialProgram>();
  material_program->backend=backend; material_program->vertex=material_program->reversed_vertex=vs; material_program->pixel=ps;
  for(const auto* name:{"g_mWorld","g_mViewProjection","g_mViewTranspose"})
    program_constants.push_back({false,name,matrix_bytes});
  program_constants.push_back({true,"tint",tint_bytes});
  material_program->inputs.textures.push_back({"image"});
  material_program->inputs.textures.push_back({"imageSampler"});
  material_program->textures={white,{}};
  NativeBackendSampler* program_samplers[]{nullptr,&backend->CreateSampler(sampler_desc)};
  const auto program_capture=material_program->Capture(pipeline,false,program_constants,program_samplers);
  auto translated_capture=program_capture;
  auto translated_world=matrix_bytes;
  Word(translated_world,(column_major?3:12)*4,std::bit_cast<uint32_t>(0.25f));
  ApplyNativeScenePublishedWorld(translated_capture,std::span<const uint8_t,64>(translated_world.data(),64));
  auto expected_world=kNativeSceneIdentity; expected_world[12]=0.25f;
  Require(translated_capture.world==expected_world && program_capture.world==kNativeSceneIdentity,
    "published world lost shader layout or modified retained capture");
  Require(program_capture.material->Equivalent(*captured.material),"owned material program disagrees with live binding capture");
  ShaderBindings activated_vertex(nullptr,vs),activated_pixel(nullptr,ps);
  auto stale_tint=tint_bytes;
  Word(stale_tint,0,std::bit_cast<uint32_t>(0.25f));
  activated_pixel.SetGuestFloatRegisters("tint",stale_tint);
  material_program->ApplyBindings(activated_vertex,activated_pixel,program_constants,program_samplers);
  const auto activated=CaptureNativeSceneMaterial(backend,pipeline,activated_vertex,activated_pixel);
  Require(activated.material->Equivalent(*captured.material),
    "published activation left stale constants or resources in shared bindings");
  auto untextured=*material_program;
  untextured.inputs.textures.clear(); untextured.textures.clear();
  untextured.ApplyBindings(activated_vertex,activated_pixel,program_constants,{});
  Require(!activated_pixel.ReadTexture("image") && !activated_pixel.ReadSampler("imageSampler"),
    "published activation retained resources omitted by the next material");
  material_program->ApplyBindings(activated_vertex,activated_pixel,program_constants,program_samplers);
  Require(CaptureNativeSceneMaterial(backend,pipeline,activated_vertex,activated_pixel).material->Equivalent(*captured.material),
    "published activation failed to restore resources after an untextured material");
  Reject([&] { material_program->Capture(pipeline,false,program_constants,{}); });
  auto& retained_tint=program_constants.back();
  retained_tint.registers.resize(32);
  Reject([&] { material_program->Capture(pipeline,false,program_constants,program_samplers); });
  Reject([&] { material_program->Capture(pipeline,true,program_constants,program_samplers); });
  retained_tint.global=true;
  material_program->Capture(pipeline,false,program_constants,program_samplers);
  material_program->Capture(pipeline,true,program_constants,program_samplers);
  retained_tint.global=false; retained_tint.registers.resize(16);
  NativeMaterialRenderPass pass_state; pass_state.words=desc.state; pass_state.color_targets[0]=1;
  std::array<NativeMaterialSamplerPass,16> pass_samplers{};
  material_program->inputs.state_overrides={{0x3c,0},{0xd4,15}};
  Require(material_program->CanDeferCpuActivation(),"ordinary material incorrectly requires scissor callback");
  auto scissor_program=*material_program;
  scissor_program.inputs.state_overrides.push_back({0xc8,1});
  Require(!scissor_program.CanDeferCpuActivation(),"scissor rectangle callback was deferred without owned geometry");
  material_program->sampler_operations={{0,0x3c0,0,{0,1,1,1}}};
  auto material_desc=desc;
  material_desc.vertex_id=100+uint64_t(column_major);
  material_desc.input_layout=geometry->input_layout().elements();
  material_desc.input_layout_id=geometry->input_layout().fingerprint();
  const auto resolved=material_program->Resolve(material_desc,false,program_constants,pass_state,pass_samplers);
  Require(resolved.capture.material->Equivalent(*captured.material),
    "material resolved from explicit pass state differs from visible binding capture");
  Require(resolved.render.words==desc.state,"material resolution changed expected pipeline words");
  Require((resolved.samplers[0].words[2]&0x3fc)==0x3c0,"material resolution lost final inherited LOD state");
  Require(pass_samplers[0].words[2]==0,"material resolution mutated source pass");
  {
    // The world pass's cache derives a hit's camera from the pass constants
    // instead of resolving again: it must equal a resolve's bit for bit, and
    // the camera constants must not reach the material image.
    auto camera_constants=program_constants;
    std::vector<uint8_t> projection(64),transposed(64);
    for(size_t i=0;i<16;++i) {
      Word(projection,i*4,std::bit_cast<uint32_t>(float(i)+.5f));
      Word(transposed,i*4,std::bit_cast<uint32_t>(float(i*3%16)-4.f));
    }
    camera_constants[1].registers=projection; camera_constants[2].registers=transposed;
    const auto moved=material_program->Resolve(material_desc,false,camera_constants,pass_state,pass_samplers);
    Require(moved.capture.material->Equivalent(*resolved.capture.material),"camera constants reached the material image");
    Require(!NativeSceneCameraIdentical(moved.capture.camera,resolved.capture.camera),"camera test constants left the camera unchanged");
    std::vector<uint8_t> derived;
    const auto camera=NativeStaticCaptureCamera(*moved.capture.material,camera_constants,&derived);
    Require(camera && NativeSceneCameraIdentical(*camera,moved.capture.camera),"derived camera differs from the resolved capture");
    Require(derived==std::vector<uint8_t>{0,1,1,0},"derived camera marked the wrong constants");
    using Cache=NativeStaticWorldGroupCache<int,int>;
    Cache cache;
    Cache::Key key; key.group=1;
    const auto& entry=cache.Store(key,camera_constants,{},{},{},0,moved.capture.material.get(),moved.capture.camera);
    Require(entry.derived,"a resolved capture's camera was not derivable");
    Require(entry.world==std::vector<uint8_t>{1,0,0,0},"object world constant not marked as zeroed by the capture");
    NativeSceneView reused;
    Require(Cache::Current(entry,program_constants,resolved.capture.material.get(),reused) &&
      NativeSceneCameraIdentical(reused,resolved.capture.camera),"cache hit derived a camera other than a resolve's");
    auto tinted=program_constants; tinted[3].registers[3]^=1;
    Require(!Cache::Current(entry,tinted,resolved.capture.material.get(),reused),"non-camera constant change reused the material");
    // g_mWorld (the preload refreshes it every tick from the last drawn
    // object) is zeroed out of the image and replaced per instance: a moved
    // world resolves to the same material, so it reuses the entry.
    auto world=program_constants;
    Word(world[0].registers,48,std::bit_cast<uint32_t>(3.f)); Word(world[0].registers,52,std::bit_cast<uint32_t>(-2.f));
    const auto moved_world=material_program->Resolve(material_desc,false,world,pass_state,pass_samplers);
    Require(moved_world.capture.material->Equivalent(*resolved.capture.material),"g_mWorld reached the material image");
    Require(Cache::Current(entry,world,resolved.capture.material.get(),reused),"moved object world did not reuse the material");
    // A NaN world makes the capture's self-comparison throw: never reused.
    auto invalid_world=program_constants; Word(invalid_world[0].registers,20,0x7fc00000);
    Reject([&] { material_program->Resolve(material_desc,false,invalid_world,pass_state,pass_samplers); });
    NativeStaticWorldMiss why{};
    Require(!Cache::Current(entry,invalid_world,resolved.capture.material.get(),reused,&why) && why==NativeStaticWorldMiss::World,
      "NaN object world reused the material");
    // Bytes past the matrix, and a pixel-stage constant of the same name, are still compared.
    auto longer=program_constants; longer[0].registers.resize(80);
    Require(!Cache::Current(entry,longer,resolved.capture.material.get(),reused),"resized world constant reused the material");
    auto pixel_world=program_constants; pixel_world[0].pixel=true;
    Require(!Cache::Current(entry,pixel_world,resolved.capture.material.get(),reused),"pixel g_mWorld reused the material");
    // The full-frame sky keeps its draws' resolves in the same cache, keyed by
    // draw with the chained state, and resolves with pass-owned operations
    // appended (z write off). A hit must be that resolve: the same material,
    // the same scissor word and the camera the moved constants resolve to.
    constexpr std::array<std::array<uint32_t,2>,1> no_depth_write{{{0x30,0}}};
    using SkyCache=NativeStaticWorldGroupCache<int,std::pair<NativeSceneMaterialCapture,bool>>;
    SkyCache sky;
    SkyCache::Key sky_key; sky_key.group=0; sky_key.pass=NativeSceneMaterialPassState{pass_state,pass_samplers};
    const auto sky_first=material_program->Resolve(material_desc,false,program_constants,pass_state,pass_samplers,-1,false,no_depth_write);
    Require(!(sky_first.render.words[1]&4u),"sky resolve kept z write");
    sky.Store(sky_key,program_constants,{},{},{},{sky_first.capture,sky_first.render.words[5]!=0},
      sky_first.capture.material.get(),sky_first.capture.camera);
    auto* sky_entry=sky.Candidate(sky_key);
    Require(sky_entry && sky_entry->derived,"sky resolve not stored with a derivable camera");
    for(const auto* constants:{&program_constants,&camera_constants,&world}) {
      const auto fresh=material_program->Resolve(material_desc,false,*constants,pass_state,pass_samplers,-1,false,no_depth_write);
      NativeSceneView camera=sky_entry->material.first.camera;
      Require(SkyCache::Current(*sky_entry,*constants,sky_entry->material.first.material.get(),camera),"sky resolve missed its cache");
      Require(fresh.capture.material->Equivalent(*sky_entry->material.first.material),"sky cache hit material differs from a resolve");
      Require(NativeSceneCameraIdentical(camera,fresh.capture.camera),"sky cache hit camera differs from a resolve");
      Require((fresh.render.words[5]!=0)==sky_entry->material.second && fresh.render.words==sky_first.render.words,
        "sky cache hit scissor or state differs from a resolve");
    }
    // Another chained state is another key: never served from this row.
    auto other_state=pass_state; other_state.words[1]^=4;
    SkyCache::Key other_key=sky_key; other_key.pass=NativeSceneMaterialPassState{other_state,pass_samplers};
    Require(!sky.Candidate(other_key),"sky cache served another chained state");
    auto tinted_sky=program_constants; tinted_sky[3].registers[3]^=1;
    NativeSceneView ignored;
    Require(!SkyCache::Current(*sky.Candidate(sky_key),tinted_sky,sky_entry->material.first.material.get(),ignored),
      "sky cache reused a material whose constant moved");
  }
  NativeSceneMaterialPassState inherited{pass_state,pass_samplers};
  inherited.samplers[7].words[0]=0x12400;
  const auto first_pass=inherited.After(*material_program);
  NativeSceneMaterialProgram change_factors;
  change_factors.inputs.state_overrides={{0x48,2}};
  const auto second_pass=first_pass.After(change_factors);
  Require(second_pass.render.words[0]==0x10001 && (second_pass.render.blend_parameters&31)==2,
    "disabled blend discarded factors needed by a later material");
  Require(second_pass.samplers==first_pass.samplers && second_pass.samplers[7].words[0]==0x12400 &&
    (second_pass.samplers[0].words[2]&0x3fc)==0x3c0,
    "material sequence lost unmentioned sampler slots or inherited LOD");
  NativeSceneMaterialProgram enable_blend;
  enable_blend.inputs.state_overrides={{0x3c,1}};
  const auto third_pass=second_pass.After(enable_blend);
  Require((third_pass.render.words[0]&31)==2 && second_pass.render.words[0]==0x10001,
    "later blend enable failed to consume inherited factors or mutated an earlier pass");
  enable_blend.sampler_operations.push_back({16});
  Reject([&] { second_pass.After(enable_blend); });
  Require(second_pass.render.words[0]==0x10001 && inherited.samplers[0].words[2]==0,
    "failed material transition partially advanced published pass state");
  auto descriptors=second_pass;
  descriptors.samplers[7].words[0]|=0x80000001;
  descriptors.samplers[7].words[1]|=0x12345;
  descriptors.samplers[7].words[3]|=0x10000000;
  Require(descriptors.Inputs()==second_pass,"texture descriptor bits entered inherited sampler state");
  descriptors.samplers[7].words[2]^=1;
  Require(descriptors.Inputs()!=second_pass,"native pass normalization discarded mip-alias bookkeeping");
  descriptors=second_pass; descriptors.samplers[7].min_lod^=1;
  Require(descriptors.Inputs()!=second_pass,"native pass normalization discarded a sampler LOD constraint");
  Require(captured.world==kNativeSceneIdentity && captured.camera.view_projection==kNativeSceneIdentity,
    "retail material capture lost canonical matrix layout");
  auto red=resolved.capture.material;
  const size_t translation_offset=column_major?12:48;
  Word(matrix_bytes,translation_offset,std::bit_cast<uint32_t>(.25f));
  source_vertex.SetGuestFloatRegisters("g_mWorld",matrix_bytes);
  const auto refreshed=CaptureNativeSceneMaterial(backend,pipeline,source_vertex,source_pixel,{},red);
  Require(refreshed.material==red && refreshed.world[12]==.25f && captured.world[12]==0,
    "retained material refresh copied stale world state or rebuilt the material");
  Word(matrix_bytes,translation_offset,0); source_vertex.SetGuestFloatRegisters("g_mWorld",matrix_bytes);
  // Mutation of the bridge bindings cannot alter the published native material.
  Word(tint_bytes,0,0); Word(tint_bytes,8,std::bit_cast<uint32_t>(1.f));
  source_pixel.SetGuestFloatRegisters("tint",tint_bytes);
  auto blue=CaptureNativeSceneMaterial(backend,pipeline,source_vertex,source_pixel,{},red).material;
  Require(blue!=red,"retained material refresh ignored changed pixel constants");
  const std::weak_ptr<NativeBackendTexture> retained_texture=white;
  white.reset(); source_pixel.ClearTextures(); source_pixel.ClearSamplers();
  Require(!retained_texture.expired(),"native material failed to retain texture generation");
  auto malformed=constants({1,0,0,1}); malformed.front().matrices.push_back({NativeSceneMatrixSource::World,UINT32_MAX,false});
  Reject([&] { NativeSceneMaterial bad(backend,pipeline,malformed); });
  NativeSceneObject object; object.geometry=geometry; object.material=red;
  object.bounds=NativeSceneBounds{{-.125f,-.25f,.5f},{.125f,.25f,.5f}};
  NativeSceneDatabase scene;
  object.world[12]=-.5f; const auto left=scene.Create(object);
  object.world[12]=.5f; const auto right=scene.Create(object);
  const auto initial=scene.Publish(0);
  NativeBackendTextureDesc target_desc; target_desc.width=64; target_desc.height=32; target_desc.format=DXGI_FORMAT_R8G8B8A8_UNORM;
  auto target=backend->CreateRenderTarget(target_desc);
  NativeSceneView view; view.viewport={0,0,64,32,0,1};
  NativeSceneRenderer renderer;
  std::vector<uint8_t> pixels;
  auto render=[&](const NativeSceneSnapshot& snapshot,float fraction=1) {
    backend->BeginFrame(); auto& recorder=backend->Recorder();
    NativeBackendRenderTarget* targets[]{target.get()}; recorder.SetRenderTargets(targets,nullptr);
    recorder.ClearColor(*target,{0,0,0,1});
    const auto stats=renderer.Render(*backend,snapshot,view,fraction);
    backend->Submit(); pixels=backend->ReadRenderTarget(*target); return stats;
  };
  auto pixel=[&](size_t x,size_t channel) { return pixels[(16*64+x)*4+channel]; };
  auto stats=render(*initial);
  Require(stats.draws==1 && stats.instanced_draws==1 && pixel(16,0)==255 && pixel(48,0)==255 && pixel(32,0)==0,
    "native scene did not directly instance retained geometry");
  object.world[12]=0; scene.Update(left,object); const auto moved=scene.Publish(1);
  Require(initial->instances[0]->object.world[12]==-.5f,"published scene mutated");
  Require(initial->instances[1]==moved->instances[1],"unchanged scene object was rebuilt");
  render(*moved,.5f); Require(pixel(24,0)==255 && pixel(32,0)==0,"native scene transform interpolation");
  render(*moved,1); Require(pixel(32,0)==255 && pixel(16,0)==0,"native scene current transform");
  const auto unchanged=scene.Publish(2); render(*unchanged,0);
  Require(pixel(32,0)==255 && pixel(16,0)==0,"stationary object replayed old interpolation");
  view.view[12]=-.5f; render(*unchanged);
  Require(pixel(16,0)==255 && pixel(32,0)==255 && pixel(48,0)==0,"native camera could not render same scene again");
  view.view=kNativeSceneIdentity;
  object.world[12]=4; scene.Update(right,object); auto outside=scene.Publish(3);
  stats=render(*outside); Require(stats.visible==1 && stats.culled==1 && stats.draws==1,"native frustum culling");
  object.world[12]=0; object.material=blue; object.order=1; scene.Update(right,object);
  auto overlap=scene.Publish(4); stats=render(*overlap);
  Require(stats.draws==2 && pixel(32,2)==255 && pixel(32,0)==0,"native scene material/order separation");
  Require(scene.Remove(right),"remove native object"); auto removed=scene.Publish(5);
  render(*removed); Require(pixel(32,0)==255 && pixel(32,2)==0,"removed object remained visible");
  render(*overlap); Require(pixel(32,2)==255,"old scene lost retired object generation");
  Reject([&] { scene.Publish(5); }); Reject([&] { scene.Update(right,object); });
  scene.Clear(); auto empty=scene.Publish(6); Require(render(*empty).draws==0 && pixel(32,0)==0,"scene clear");
  const auto replacement=scene.Create(object); Require(replacement>right,"scene reused retired identity");
  // A gap in simulation publications must not invent motion through missing ticks.
  scene.Publish(7); object.world[12]=.5f; scene.Update(replacement,object);
  auto gap=scene.Publish(10); render(*gap,0);
  Require(pixel(48,2)==255 && pixel(32,2)==0,"scene interpolated across a publication gap");
  scene.Clear(); object.material=red; object.world=kNativeSceneIdentity;
  for(size_t i=0;i<257;++i) scene.Create(object);
  auto many=scene.Publish(11); stats=render(*many);
  Require(stats.visible==257 && stats.draws==2 && stats.instanced_draws==1 && pixel(32,0)==255,
    "native scene lost instances across batch capacity");
  NativeSceneAdapter adapter;
  Require(!adapter.PreviousGroupMaterial(500),"empty material history returned an asset");
  {
    auto temporary=std::make_shared<NativeSceneMaterial>(backend,pipeline,blue->constants());
    adapter.RememberGroupMaterial(500,temporary);
    Require(adapter.PreviousGroupMaterial(500)==temporary,"group material history lost a live asset");
  }
  Require(!adapter.PreviousGroupMaterial(500),"group material hint kept a retired asset alive");
  adapter.RememberGroupMaterial(500,captured.material);
  Require(adapter.PreviousGroupMaterial(500)==captured.material,"recycled group could not replace its material hint");
  uint64_t adapter_left=0,adapter_right=0;
  {
    NativeIndexedMesh mesh(*backend,vs,declaration,12,vertices,indices,2);
    auto left_capture=captured; left_capture.world[12]=-.5f;
    auto right_capture=captured; right_capture.world[12]=.5f;
    right_capture.material=std::make_shared<NativeSceneMaterial>(backend,pipeline,constants({1,0,0,1}),
      std::vector<NativeSceneTexture>{{source_pixel.TextureImages().front().slot,retained_texture.lock()}},
      std::vector<NativeSceneSampler>{{source_pixel.SamplerImages().front().slot,&backend->CreateSampler(sampler_desc)}});
    Require(right_capture.material->Equivalent(*captured.material) &&
      right_capture.material->fingerprint()==captured.material->fingerprint() && !right_capture.material->Equivalent(*blue),
      "material identity omitted bindings or changed equivalent captures");
    adapter_left=adapter.Observe({1,100,0,0},backend,mesh,0,6,0,left_capture);
    adapter_right=adapter.Observe({2,200,0,0},backend,mesh,0,6,0,right_capture);
    const auto before=adapter.SelectOne(adapter_left);
    Require(adapter.Observe({1,100,0,0},backend,mesh,0,6,0,left_capture)==adapter_left &&
      before==adapter.SelectOne(adapter_left),"unchanged adapter object lost native identity");
    auto changed=left_capture; changed.world[12]=0;
    adapter.Observe({1,100,0,0},backend,mesh,0,6,0,changed);
    Require(before->object.world[12]==-.5f && adapter.SelectOne(adapter_left)->object.world[12]==0,
      "adapter changed an already selected object");
    adapter.Observe({1,100,0,0},backend,mesh,0,6,0,left_capture);
  }
  const uint64_t selection[]{adapter_left,adapter_right};
  const auto retained_left=adapter.SelectOne(adapter_left);
  auto retained_capture=captured; retained_capture.world=retained_left->object.world;
  Require(adapter.Observe({1,100,0,0},retained_left->object.geometry,retained_capture)==adapter_left &&
    adapter.SelectOne(adapter_left)==retained_left,"retained scene group lost identity after mesh destruction");
  auto selected=adapter.Select(selection);
  Require(selected.instances[0]->object.geometry==selected.instances[1]->object.geometry,
    "adapter did not share retained geometry across owners");
  Require(selected.instances[0]->object.material==selected.instances[1]->object.material,
    "adapter did not intern independently captured equivalent materials");
  stats=render(selected);
  Require(stats.instanced_draws==1 && pixel(16,0)==255 && pixel(48,0)==255,"adapter did not render selected native objects");
  {
    // A native static world group: its instances share retained geometry, the
    // material program and the pass inputs, and differ only in g_mWorld. A
    // Resolve per instance gives every object its own material and so its own
    // draw; one interned Resolve per group shares it and records one instanced draw.
    constexpr size_t kGroupInstances=8;
    NativeSceneAdapter group_adapter;
    std::vector<NativeSceneSources::Source> group_sources;
    std::vector<std::array<uint8_t,64>> group_worlds;
    std::vector<uint64_t> group_ids;
    for(size_t i=0;i<kGroupInstances;++i) {
      NativeSceneSources::Source source; source.generation=1; source.owner=uint32_t(700+i);
      auto world=kNativeSceneIdentity; world[12]=-.875f+.25f*float(i);
      std::array<uint8_t,64> registers{};
      for(size_t row=0;row<4;++row) for(size_t col=0;col<4;++col) {
        const auto bits=std::bit_cast<uint32_t>(world[row*4+col]);
        const auto offset=(column_major?col*4+row:row*4+col)*4;
        for(size_t b=0;b<4;++b) registers[offset+b]=uint8_t(bits>>(24-b*8));
      }
      auto capture=resolved.capture; capture.world=world;
      group_ids.push_back(group_adapter.Observe(source,geometry,capture));
      group_sources.push_back(source); group_worlds.push_back(registers);
    }
    const auto group_publication=group_adapter.Publish(1);
    const auto resolve=[&] { return material_program->Resolve(material_desc,false,program_constants,pass_state,pass_samplers); };
    const auto instance=[&](size_t i,NativeSceneMaterialCapture capture) {
      ApplyNativeScenePublishedWorld(capture,std::span<const uint8_t,64>(group_worlds[i]));
      auto object=group_publication->Resolve(group_sources[i],geometry,capture);
      Require(object && object->object.world[12]==-.875f+.25f*float(i),"static group instance lost its lifetime or world");
      return object;
    };
    std::vector<std::shared_ptr<const NativeSceneInstance>> separate,grouped;
    for(size_t i=0;i<kGroupInstances;++i) separate.push_back(instance(i,resolve().capture));
    Require(separate[0]->object.material!=separate[1]->object.material,"per-instance resolves unexpectedly shared a material");
    stats=render(NativeSceneSnapshot{0,separate});
    Require(stats.visible==kGroupInstances && stats.draws==kGroupInstances && !stats.instanced_draws,
      "per-instance materials were instanced");
    auto shared=resolve().capture;
    shared.material=group_adapter.InternMaterial(std::move(shared.material));
    for(size_t i=0;i<kGroupInstances;++i) {
      grouped.push_back(instance(i,shared));
      Require(grouped[i]->object.material==shared.material && grouped[i]==group_publication->Find(group_ids[i]),
        "static group instances did not share the interned group material or rebuilt a retained instance");
    }
    stats=render(NativeSceneSnapshot{0,grouped});
    Require(stats.visible==kGroupInstances && stats.draws==1 && stats.instanced_draws==1 &&
      pixel(4,0)==255 && pixel(60,0)==255 && pixel(4,2)==0,"static group instances were not one instanced draw");
    // The world pass resolves each instance from the group's material and its
    // decoded world. An unchanged instance is its retained object; one whose
    // material differs from the retained object's is copied once and that copy
    // is returned again while its material and world hold, without allocating.
    const auto world_of=[&](size_t i) {
      auto capture=shared; ApplyNativeScenePublishedWorld(capture,std::span<const uint8_t,64>(group_worlds[i]));
      return capture.world;
    };
    NativeSceneInstanceReuse reuse;
    for(size_t i=0;i<kGroupInstances;++i)
      Require(group_publication->Resolve(group_sources[i],geometry,shared.material,world_of(i),&reuse)==group_publication->Find(group_ids[i]),
        "unchanged static instance did not resolve to its retained object");
    Require(!reuse.allocations && !reuse.reuses && !reuse.size(),"unchanged static instances allocated or recorded copies");
    const auto other=resolve().capture.material;
    Require(other!=shared.material,"independent resolve shared the interned material");
    std::vector<std::shared_ptr<const NativeSceneInstance>> copies;
    for(size_t i=0;i<kGroupInstances;++i) {
      copies.push_back(group_publication->Resolve(group_sources[i],geometry,other,world_of(i),&reuse));
      Require(copies[i] && copies[i]!=group_publication->Find(group_ids[i]) && copies[i]->object.material==other &&
        copies[i]->id==group_ids[i] && copies[i]->previous==copies[i]->object.world,"changed static instance copy");
    }
    Require(reuse.allocations==kGroupInstances && !reuse.reuses,"changed static instances were not copied once each");
    for(size_t pass=0;pass<3;++pass) {
      reuse.EndPass();
      for(size_t i=0;i<kGroupInstances;++i)
        Require(group_publication->Resolve(group_sources[i],geometry,other,world_of(i),&reuse)==copies[i],
          "unchanged static instance allocated a new copy");
    }
    Require(reuse.allocations==kGroupInstances && reuse.reuses==3*kGroupInstances,"static instance reuse count");
    auto moved=world_of(0); moved[12]+=.125f;
    const auto relocated=group_publication->Resolve(group_sources[0],geometry,other,moved,&reuse);
    Require(relocated && relocated!=copies[0] && relocated->object.world==moved && reuse.allocations==kGroupInstances+1,
      "moved static instance reused a stale copy");
    Require(!group_publication->Resolve(group_sources[0],nullptr,other,moved,&reuse),"static instance resolved without geometry");
    // Without reuse every changed resolve is a new object, as before.
    Require(group_publication->Resolve(group_sources[1],geometry,other,world_of(1))!=copies[1],"reuse leaked into a plain resolve");
    for(size_t pass=0;pass<256;++pass) reuse.EndPass();
    Require(!reuse.size(),"unused static instance copies were not released");
  }
  const auto publication=adapter.Publish(1);
  Require(adapter.AcquirePublication()==publication && publication->Find(adapter_left) &&
    !publication->Find(UINT64_MAX),"native publication lookup failed");
  NativeSceneSources::World world{};
  auto event_world=retained_left->object.world; event_world[12]=0;
  bool column=false;
  for(const auto& constant:retained_left->object.material->constants()) for(const auto& matrix:constant.matrices)
    if(matrix.source==NativeSceneMatrixSource::World) column=matrix.column_major;
  for(size_t row=0;row<4;++row) for(size_t col=0;col<4;++col) {
    const auto bits=std::bit_cast<uint32_t>(event_world[row*4+col]);
    const auto offset=(column?col*4+row:row*4+col)*4;
    for(size_t b=0;b<4;++b) world[offset+b]=uint8_t(bits>>(24-b*8));
  }
  Require(adapter.UpdateWorld(100,999,world)==0,"stale generation updated native scene");
  Require(adapter.UpdateWorld(100,1,world)==1,"simulation event did not update native object");
  const auto updated=adapter.Publish(2);
  Require(updated->Find(adapter_left)->object.world==event_world &&
    publication->Find(adapter_left)->object.world[12]==-.5f &&
    updated->Find(adapter_right)==publication->Find(adapter_right),"publication mutated history or copied unchanged objects");
  render(*updated->snapshot); Require(pixel(32,0)==255,"simulation publication did not render updated object");
  adapter.Retire(100); Reject([&] { adapter.SelectOne(adapter_left); });
  const auto retired=adapter.Publish(3);
  Require(!retired->Find(adapter_left) && updated->Find(adapter_left),"retirement changed an older publication");
  Require(adapter.objects()==1,"adapter retirement removed another owner");
  Require(retired->by_source.size()==1 && updated->by_source.size()==2 && !retired->by_source.Shares(updated->by_source),
    "retirement did not patch the source index");
  const auto idle=adapter.Publish(4);
  Require(idle->snapshot!=retired->snapshot && idle->snapshot->tick==4 &&
    idle->snapshot->instances.Shares(retired->snapshot->instances) && idle->by_id.Shares(retired->by_id) &&
    idle->by_source.Shares(retired->by_source) && idle->group_geometry.Shares(retired->group_geometry) &&
    idle->group_order.Shares(retired->group_order),
    "unchanged tick rebuilt its publication indexes");
  render(selected); Require(pixel(16,0)==255,"adapter retirement invalidated an in-flight scene");
  // Populate a part which has never been observed/drawn, then render solely
  // from its publication after the producer and its source records retire.
  NativeSceneAdapter populated;
  NativeSceneSources catalog;
  catalog.Born(300); catalog.Born(400);
  const NativeSceneSources::Part unseen[]{ {3000,0,0,9000,500} };
  const NativeSceneSources::Part unsupported[]{ {4000,0,0,9004,500} };
  catalog.Observe(300,unseen); catalog.Observe(400,unsupported);
  catalog.PublishWorld(300,world); catalog.PublishWorld(400,world);
  const auto populate=[&] {
    return populated.PopulateGroup(catalog,500,retained_left->object.geometry,captured,
      [](const auto& part) { return part.instance==3000; });
  };
  const auto population=populate();
  Require(population.examined==2 && population.created==1 && population.rejected==1 && populated.objects()==1,
    "group assets did not populate unseen eligible parts exclusively");
  Require(populate().examined==0,"unchanged group repeated asset population");
  populated.Retire(999);
  Require(populate().examined==0,"unrelated retirement invalidated populated groups");
  const auto unseen_publication=populated.Publish(1);
  const auto unseen_source=*catalog.Find(3000);
  render(*unseen_publication->snapshot);
  Require(pixel(32,0)==255 && unseen_publication->snapshot->instances[0]->object.world==event_world,
    "unseen part did not render from its event-published transform");
  populated.Retire(300);
  Require(populate().created==1,"same-address model replacement failed to repopulate assets");
  const auto before_rebirth=populated.Publish(2);
  populated.Retire(300); catalog.Born(300); catalog.Observe(300,unseen); catalog.PublishWorld(300,world);
  Require(populate().created==1,"reused owner did not acquire group assets");
  const auto after_rebirth=populated.Publish(3);
  Require(before_rebirth->snapshot->instances[0]->id!=after_rebirth->snapshot->instances[0]->id,
    "group population reused a retired native identity");
  populated.Retire(300); catalog.Retire(300); catalog.Retire(400);
  Require(!catalog.FindGroup(500),"retired parts kept a live group index");
  {
    // Retirement visits only the owner's own groups: a loader publication per
    // object must not scan every populated group (quadratic mission loading).
    NativeSceneAdapter scaled; NativeSceneSources owners;
    constexpr uint32_t count=512,shared=70000;
    const auto all=[](const auto&) { return true; };
    for(uint32_t i=0;i<count;++i) {
      owners.Born(1000+i);
      const NativeSceneSources::Part own[]{ {20000+i,0,0,30000+i,40000+i} };
      owners.Observe(1000+i,own); owners.PublishWorld(1000+i,world);
    }
    for(uint32_t i=0;i<count;++i)
      Require(scaled.PopulateGroup(owners,40000+i,retained_left->object.geometry,captured,all).created==1,
        "scaled group did not populate its owner");
    Require(scaled.objects()==count && scaled.populated_groups()==count,"scaled population is incomplete");
    for(uint32_t i=0;i<count;i+=2) {
      const auto checks=scaled.retire_checks();
      scaled.Retire(1000+i);
      Require(scaled.retire_checks()-checks==1,"owner retirement scanned unrelated populated groups");
      Require(!scaled.HasOwner(1000+i),"retired owner kept adapter state");
    }
    const auto checks=scaled.retire_checks();
    scaled.Retire(999); scaled.Retire(1000);
    Require(scaled.retire_checks()==checks,"unknown or repeated retirement scanned populated groups");
    Require(scaled.objects()==count/2 && scaled.populated_groups()==count/2,"retirement removed another owner's group");
    for(uint32_t i=0;i<count;++i)
      Require(scaled.PopulateGroup(owners,40000+i,retained_left->object.geometry,captured,all).created==size_t(i%2==0),
        "retirement did not invalidate exactly the retired owners' groups");
    // A group shared by several owners retires with any of them. Index entries
    // left behind by a repopulation must neither erase a group without that
    // owner nor miss one which still contains it.
    for(uint32_t i=0;i<3;++i) {
      owners.Retire(1000+i); owners.Born(1000+i);
      const NativeSceneSources::Part part[]{ {50000+i,0,0,60000+i,shared} };
      owners.Observe(1000+i,part); owners.PublishWorld(1000+i,world);
    }
    const auto share=[&] { return scaled.PopulateGroup(owners,shared,retained_left->object.geometry,captured,all); };
    Require(share().created==3,"shared group did not populate every owner");
    scaled.Retire(1000);
    Require(share().created==1 && share().examined==0,"shared group survived one owner's retirement");
    owners.Retire(1001); scaled.Retire(1001);
    Require(share().examined==2 && !scaled.HasOwner(1001),"shared group kept a retired owner");
    const NativeSceneSources::Part moved[]{ {50002,0,0,60002,0} };
    owners.Observe(1002,moved);
    Require(share().examined==1,"shared group kept a relocated part");
    scaled.Retire(1002);
    Require(share().examined==0,"stale owner index erased a repopulated group without that owner");
    scaled.Retire(1000);
    Require(share().examined==1,"owner index missed a group still containing the retired owner");
    Require(scaled.objects()==count-2,"shared retirement removed the wrong objects");
  }
  render(*unseen_publication->snapshot); Require(pixel(32,0)==255,"unseen publication borrowed retired producer assets");
  {
    auto pass=captured; pass.world=event_world; pass.world[12]=-.5f;
    const auto geometry=unseen_publication->snapshot->instances[0]->object.geometry;
    const auto selected=unseen_publication->Resolve(unseen_source,geometry,pass);
    Require(selected && selected->id==unseen_publication->snapshot->instances[0]->id &&
      selected->object.world==pass.world && selected->previous==pass.world &&
      unseen_publication->snapshot->instances[0]->object.world==event_world,
      "pass selection mutated its publication or lost the retained source lifetime");
    auto recycled=unseen_source; ++recycled.generation;
    Require(!unseen_publication->Resolve(recycled,geometry,pass) &&
      !unseen_publication->Resolve(unseen_source,{},pass),"pass selection accepted a different lifetime or geometry");
    NativeSceneSnapshot resolved_pass{1,{selected}};
    render(resolved_pass);
    Require(pixel(16,0)==255 && pixel(48,0)==0,
      "pass selection required the retired producer database or ignored the resolved world");
  }
  NativeSceneAdapter geometry_loader;
  NativeSceneSources geometry_sources; geometry_sources.Born(900);
  const NativeSceneSources::Part geometry_part[]{ {9000,0,0,0,500} };
  geometry_sources.Observe(900,geometry_part);
  const auto geometry_revision=geometry_sources.FindGroup(500)->revision;
  {
    NativeIndexedMesh unloaded(*backend,vs,declaration,12,vertices,indices,2);
    const auto geometry=geometry_loader.RetainGeometry(backend,unloaded,0,6);
    Require(geometry_loader.RetainGeometry(backend,unloaded,0,6)==geometry,"preloaded geometry was not interned");
    Reject([&] { geometry_loader.RetainGeometry({},unloaded,0,6); });
    geometry_loader.PublishGroupGeometry(500,geometry_revision,geometry);
  }
  const auto geometry_publication=geometry_loader.Publish(1);
  Require(geometry_publication->snapshot->instances.empty() && geometry_publication->group_geometry.size()==1,
    "geometry loading required a visible object or material");
  const auto geometry_record=geometry_publication->group_geometry[0];
  geometry_loader.PublishGroupGeometry(500,geometry_revision,geometry_record->geometry);
  Require(geometry_loader.GroupGeometry(500,geometry_revision)==geometry_record,
    "unchanged geometry publication did not share its record");
  NativeSceneGeometrySource setup{10,20,30,12,6,40,50};
  geometry_loader.PublishGroupGeometry(500,geometry_revision,geometry_record->geometry,setup);
  const auto setup_record=geometry_loader.GroupGeometry(500,geometry_revision);
  Require(setup_record!=geometry_record && setup_record->setup==setup && !geometry_record->setup,
    "geometry setup publication mutated an earlier frame");
  geometry_loader.PublishGroupGeometry(500,geometry_revision,geometry_record->geometry,setup);
  Require(geometry_loader.GroupGeometry(500,geometry_revision)==setup_record,
    "unchanged geometry setup rebuilt its publication");
  setup.material=41;
  geometry_loader.PublishGroupGeometry(500,geometry_revision,geometry_record->geometry,setup);
  Require(geometry_loader.GroupGeometry(500,geometry_revision)!=setup_record && setup_record->setup->material==40,
    "changed material identity reused stale geometry setup");
  geometry_sources.Retire(900); geometry_loader.PruneGroupGeometry(geometry_sources);
  Require(!geometry_loader.geometry_groups(),"retired source retained preloaded geometry");
  geometry_sources.Born(900); geometry_sources.Observe(900,geometry_part);
  const auto replacement_revision=geometry_sources.FindGroup(500)->revision;
  geometry_loader.PublishGroupGeometry(500,replacement_revision,geometry_record->geometry);
  Require(!geometry_loader.GroupGeometry(500,geometry_revision) &&
    geometry_record->revision==geometry_revision,"reused group inherited an old geometry identity");
  NativeSceneDatabase loaded_scene;
  NativeSceneObject loaded_object; loaded_object.geometry=geometry_record->geometry; loaded_object.material=captured.material;
  loaded_scene.Create(loaded_object);
  render(*loaded_scene.Publish(1));
  Require(pixel(32,0)==255,"published geometry could not render after its source and mesh retired");
  geometry_loader.PublishGroupMaterial(500,replacement_revision,material_program,program_constants);
  const auto material_publication=geometry_loader.Publish(2);
  auto next_constants=program_constants;
  next_constants.back().registers[0]^=1;
  geometry_loader.PublishGroupMaterial(500,replacement_revision,material_program,next_constants);
  const auto next_material=geometry_loader.GroupMaterial(500,replacement_revision);
  Require(next_material!=material_publication->group_materials[0] &&
    next_material->program==material_publication->group_materials[0]->program &&
    next_material->constants==next_constants &&
    material_publication->group_materials[0]->constants==program_constants,
    "frame constants rebuilt the program or mutated a retained publication");
  geometry_loader.PublishGroupMaterial(500,replacement_revision,material_program,next_constants);
  Require(geometry_loader.GroupMaterial(500,replacement_revision)==next_material,
    "unchanged material inputs replaced their publication record");
  geometry_sources.Retire(900); geometry_loader.PruneGroupGeometry(geometry_sources);
  Require(!geometry_loader.material_groups() && material_publication->group_materials.size()==1,
    "material retirement changed a published program or retained a live group");
  // Producer bindings now contain blue and no textures; the owned program
  // still constructs and renders its original red material without guest reads.
  loaded_object.material=material_publication->group_materials[0]->program->Capture(pipeline,false,material_publication->group_materials[0]->constants,program_samplers).material;
  NativeSceneDatabase material_scene; material_scene.Create(loaded_object);
  render(*material_scene.Publish(1));
  Require(pixel(32,0)==255 && pixel(32,2)==0,"owned material program borrowed producer constants or textures");
  for(const auto& message:backend->DrainValidationMessages()) throw std::runtime_error(message);
}
}
int main(int argc,char** argv) {
  // --static-world-bench: FullFrameStaticWorldFrames runs 600 frames and prints its per-frame timings.
  static_world_bench=argc>1 && std::string_view(argv[1])=="--static-world-bench";
  try {
    SharedIndex(); PassCamera(); Visibility();
    QueuedGuestState(); PreloadChangeSignals();
    TreePublicationReuse();
    AddressFilter(); FullFrameLiveRoutes(); FullFrameFixedRecord();
    WorldPublicationMirror(); GroupOrder(); FullFrameStaticWorld(); FullFrameStaticWorldFrames(); StaticWorldPass(); StaticWorldComposedBinds(); StaticWorldGroupCache(); StaticWorldGroupResolve(); WalkLock(); StaticWalkPlan();
    NativeSceneSources sources;
    const NativeSceneSources::Part first[]{ {1000,0,0,0,500},{1028,0,1,0,500} };
    Require(!sources.Observe(100,first),"scene inferred lifetime from an observation");
    const auto generation=sources.Born(100);
    Require(sources.Observe(100,first) && sources.Find(1028)->generation==generation,"scene source registration");
    const auto group_revision=sources.FindGroup(500)->revision;
    Require(sources.FindGroup(500)->parts.size()==2,"group index omitted a source part");
    sources.Observe(100,first);
    Require(sources.FindGroup(500)->revision==group_revision,"unchanged parts invalidated group assets");
    const NativeSceneSources::Part relocated[]{ {2000,1,0} };
    sources.Observe(100,relocated);
    Require(!sources.Find(1000) && sources.Find(2000)->lod==1,"scene source vector relocation");
    Require(!sources.FindGroup(500),"relocated sources retained old group membership");
    sources.Born(200); Reject([&] { sources.Observe(200,relocated); });
    Require(sources.Find(2000)->owner==100,"invalid source ownership changed mapping");
    sources.Retire(100); Require(!sources.Find(2000),"scene source survived destructor");
    Require(sources.Born(100)>generation,"scene source reused a dead generation");
    for(bool column:{false,true}) {
      Run(CreateNativeD3D11Backend({true,true}),column);
      NativeD3D12Options options; options.prefer_warp=true; options.debug_layer=true;
      options.geometry_workers=2; options.geometry_minimum_draws=1;
      Run(CreateNativeD3D12Backend(options),column);
    }
    std::cout<<"Native scene lifetime, publication, interpolation, camera, culling, materials and direct instancing passed on D3D11/D3D12\n";
  } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
