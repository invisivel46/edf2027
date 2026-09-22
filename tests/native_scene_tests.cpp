#include "native_graphics/native_scene.h"
#include "native_graphics/native_scene_bindings.h"
#include "native_graphics/native_scene_sources.h"
#include "native_graphics/native_scene_adapter.h"
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
#include "native_graphics/native_texture_binding.h"
#include "native_graphics/d3d11_backend.h"
#include "native_graphics/d3d12_backend.h"
#include <bit>
#include <cstring>
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
    // Null slot: record+12's texture bits are M1's under M2's null bind.
    Require(((reader.Word(device+1024+24+12)^setup.Word(0x51000+28+12))&~0x7ff80000u)==0 &&
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
// A BasicLockable that records every acquisition, so tests can count them.
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
  plans.Publish(r,owner,1,sources.CandidateRevision(),find);
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

  // Material inputs: constant stores are untracked, so their bytes are compared.
  NativeMaterialParameters::Groups schema;
  schema[0].push_back({"local",31000,1,0});
  schema[1].push_back({"global",31500,1,4});
  reader.StoreWord(31004,31200); reader.StoreWord(31500,31400);
  reader.StoreWord(31400,31300); reader.StoreWord(31408,1);
  reader.StoreWord(31200,0x3f800000); reader.StoreWord(31300,0x40000000);
  reader.StoreWord(29204,29440); reader.StoreWord(29444,29500);
  schema.textures[0].push_back({"local_image",30000});
  reader.StoreWord(30004,30300); reader.StoreWord(30008,2);
  for(uint32_t i=0;i<4;++i) reader.StoreWord(30012+i*4,i+1);
  reader.StoreWord(30344,0x3c0);
  reader.StoreWord(29096,30600); reader.StoreWord(29104,1);
  reader.StoreWord(30600,0x44); reader.StoreWord(30604,7);
  const auto load_material=[&](NativeRecordedReads& reads) {
    const NativeRecordingReader recorder(reader,reads);
    auto inputs=ReadNativeSceneMaterialInputs(recorder,29000,schema,
      [](bool,const std::string&) { return size_t(16); },[](const std::string&) { return true; });
    ReadNativeMaterialSamplerOperations(recorder,schema);
    return inputs;
  };
  NativeRecordedReads material;
  const auto inputs=load_material(material);
  Require(material.Unchanged(reader),"unchanged material inputs were not proven");
  reader.StoreWord(31320,9); reader.StoreWord(30608,0x48);
  Require(material.Unchanged(reader),"bytes outside the material inputs invalidated the group");
  reader.StoreWord(31300,0x40400000);
  Require(!material.Unchanged(reader),"changed global constant was not detected");
  NativeRecordedReads refreshed;
  const auto changed=load_material(refreshed);
  Require(changed!=inputs && GuestBlockWord(changed.constants[1].registers.data())==0x40400000 && refreshed.Unchanged(reader),
    "changed material constant was not re-read");
  reader.StoreWord(30340,0x80000000);
  Require(!refreshed.Unchanged(reader),"changed texture header was not detected");
  reader.StoreWord(30340,0); reader.StoreWord(30604,8);
  Require(!refreshed.Unchanged(reader),"changed material state override was not detected");
  reader.StoreWord(30604,7);
  Require(refreshed.Unchanged(reader),"restored material inputs were not recognized");

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
int main() {
  try {
    SharedIndex(); PassCamera(); Visibility();
    QueuedGuestState(); PreloadChangeSignals();
    TreePublicationReuse();
    GroupOrder(); StaticWorldPass(); WalkLock(); StaticWalkPlan();
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
