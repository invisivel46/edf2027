#include "native_graphics/guest_sdk_readable_range.h"
#include "native_graphics/guest_physical_versions.h"
#include "native_graphics/guest_mesh_watch_audit.h"
#include "native_graphics/guest_audio_output_ranges.h"
#include "native_graphics/native_model_buffers.h"
#include "native_graphics/native_pool_backings.h"
#include "native_graphics/native_buffer_write_frame.h"
#include "native_graphics/native_model_header.h"
#include "native_graphics/guest_draw_state.h"
#include "native_graphics/native_physical_write_notify.h"
#include <rex/thread/mutex.h>
#include <Windows.h>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <atomic>
#include "native_graphics/native_render_state_snapshot.h"

namespace {
void Require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
bool HostReadable(const uint8_t* at,size_t size,bool require_write=false) {
  while(size) {
    MEMORY_BASIC_INFORMATION region{};
    if(!VirtualQuery(at,&region,sizeof(region)) || region.State!=MEM_COMMIT ||
       (region.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
    if(require_write && !(region.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) return false;
    const auto end=static_cast<const uint8_t*>(region.BaseAddress)+region.RegionSize;
    if(end<=at) return false;
    const auto count=(std::min)(size,size_t(end-at));
    at+=count; size-=count;
  }
  return true;
}
// The locked SDK region query the page-table probe replaced: the reference
// answer it must reproduce after every allocation, protection and release.
bool QueriedHeapAdmits(rex::memory::BaseHeap& heap,uint32_t address,size_t size,uint32_t required) {
  if(!address || !size || address<heap.heap_base() || size>0x100000000ull-address ||
     uint64_t(address)-heap.heap_base()+size>heap.heap_size()) return false;
  return edf::native::GuestRangeCommittedReadable(address,size,[&](uint32_t at) {
    const uint32_t page=heap.heap_base()+((at-heap.heap_base())/heap.page_size())*heap.page_size();
    rex::memory::HeapAllocationInfo info{};
    if(!heap.QueryRegionInfo(page,&info)) return edf::native::GuestReadableRegion{};
    return edf::native::GuestReadableRegion{info.base_address,info.region_size,
      (info.state&rex::memory::kMemoryAllocationCommit)!=0,(info.protect&required)==required};
  });
}
void CheckPhysicalWriteWatch(rex::memory::Memory& memory,uint32_t address,uint32_t size) {
  struct Probe { unsigned calls=0; uint32_t start=0,length=0; bool exact=false; } probe;
  struct Registration {
    rex::memory::Memory& memory;
    void* handle;
    ~Registration() { if(handle) memory.UnregisterPhysicalMemoryInvalidationCallback(handle); }
  } registration{memory,memory.RegisterPhysicalMemoryInvalidationCallback(
    [](void* context,uint32_t start,uint32_t length,bool exact) -> std::pair<uint32_t,uint32_t> {
      auto& probe=*static_cast<Probe*>(context);
      ++probe.calls; probe.start=start; probe.length=length; probe.exact=exact;
      return {start,length};
    },&probe)};
  const auto physical=memory.GetPhysicalAddress(address);
  edf::native::GuestPhysicalVersions versions(memory);
  versions.Arm(physical,size);
  const auto initial_version=versions.Version(physical,size);
  for(const auto range:{std::pair{0u,0u},std::pair{0x20000000u,1u},
      std::pair{0x1fffffffu,2u},std::pair{UINT32_MAX,1u}}) {
    bool rejected=false;
    try { (void)versions.Version(range.first,range.second); }
    catch(const std::runtime_error&) { rejected=true; }
    Require(rejected,"invalid physical version range accepted");
  }
  auto* guest=memory.TranslateVirtual<volatile uint8_t*>(address);
  auto arm=[&] { memory.EnablePhysicalMemoryAccessCallbacks(physical,size,true,false); };
  arm();
  const auto before=probe.calls;
  // Direct host physical writes deliberately bypass this SDK watch contract.
  // A future cache must audit these writers, not assume watches cover them.
  *memory.TranslatePhysical<volatile uint8_t*>(physical)=17;
  Require(*guest==17 && probe.calls==before,"physical provider write/watch contract changed");
  Require(versions.Version(physical,size)==initial_version,
    "version tracker must not claim coverage for unnotified host writes");
  *guest=23;
  Require(probe.calls==before+1 && probe.start<=physical &&
    uint64_t(physical)<uint64_t(probe.start)+probe.length,"guest write did not invalidate physical page");
  const auto written_version=versions.Version(physical,size);
  Require(written_version>initial_version,"physical version did not advance on guest write");
  Require(versions.Version(physical+size,size)==0,"unrelated physical page version changed");
  *guest=29;
  Require(probe.calls==before+1,"write watch must be one-shot in the accessed alias");
  Require(versions.Version(physical,size)==written_version,"one-shot write unexpectedly changed version");
  arm();
  *guest=31;
  Require(probe.calls==before+2,"rearmed guest write watch did not fire");
  Require(versions.Version(physical,size)>written_version,"rearmed physical version did not advance");
  // Match XFile::ReadInternal's successful physical-read completion path.
  arm();
  *memory.TranslatePhysical<volatile uint8_t*>(physical)=33;
  Require(memory.TriggerPhysicalMemoryCallbacks(
    rex::thread::global_critical_region::AcquireDirect(),address,size,true,true),
    "provider completion did not find watched pages");
  Require(probe.calls==before+3 && probe.exact && *guest==33,
    "explicit provider completion did not invalidate updated bytes");
  Require(versions.Version(physical,size)>written_version,
    "provider completion not represented in physical versions");
  Require(!memory.TriggerPhysicalMemoryCallbacks(
    rex::thread::global_critical_region::AcquireDirect(),address,size,true,true),
    "provider completion did not clear one-shot watch");

  // A different allocation must invalidate before its address can be reused.
  auto* heap=memory.LookupHeap(address);
  uint32_t recycled=0;
  Require(heap->Alloc(size,heap->page_size(),
    rex::memory::kMemoryAllocationReserve|rex::memory::kMemoryAllocationCommit,
    rex::memory::kMemoryProtectRead|rex::memory::kMemoryProtectWrite,false,&recycled),
    "allocate write-watch lifetime fixture");
  memory.EnablePhysicalMemoryAccessCallbacks(memory.GetPhysicalAddress(recycled),size,true,false);
  const auto before_release=probe.calls;
  Require(heap->Release(recycled),"release write-watch lifetime fixture");
  Require(probe.calls>before_release && probe.exact,"release did not invalidate watched allocation");
  Require(heap->AllocFixed(recycled,size,heap->page_size(),
    rex::memory::kMemoryAllocationReserve|rex::memory::kMemoryAllocationCommit,
    rex::memory::kMemoryProtectRead|rex::memory::kMemoryProtectWrite),
    "reuse write-watch fixture address");
  memory.EnablePhysicalMemoryAccessCallbacks(memory.GetPhysicalAddress(recycled),size,true,false);
  const auto before_reuse=probe.calls;
  *memory.TranslateVirtual<volatile uint8_t*>(recycled)=41;
  Require(probe.calls==before_reuse+1,"reused allocation could not be watched");
  // Consume every host-page watch, not just the first page of a larger guest
  // allocation. Remaining watched pages can still notify during Release.
  for(uint32_t offset=4096;offset<size;offset+=4096)
    *memory.TranslateVirtual<volatile uint8_t*>(recycled+offset)=41;
  // Callback registration alone is not an unconditional allocation-free observer.
  const auto after_consumed_watch=probe.calls;
  Require(heap->Release(recycled),"release reused write-watch fixture");
  Require(probe.calls==after_consumed_watch,
    "SDK consumed-watch release contract changed; reassess native lifetime coverage");
  arm();
  const auto before_unregister=probe.calls;
  memory.UnregisterPhysicalMemoryInvalidationCallback(registration.handle);
  registration.handle=nullptr;
  *guest=37;
  Require(probe.calls==before_unregister,"unregistered invalidation callback was invoked");
  {
    edf::native::GuestMeshWatchAudit audit(memory);
    const std::span<const uint8_t> bytes{memory.TranslateVirtual(address),16};
    audit.Check(address,bytes);
    audit.Check(address,bytes);
    Require(audit.counters().stable==1 && audit.counters().missed==0,
      "shadow audit did not recognize unchanged buffer");
    *memory.TranslatePhysical<volatile uint8_t*>(physical)=51;
    audit.Check(address,bytes);
    Require(audit.counters().missed==1,"shadow audit missed unnotified byte mutation");
    *guest=53;
    audit.Check(address,bytes);
    Require(audit.counters().invalidated==1 && audit.counters().missed==1,
      "shadow audit misclassified notified guest mutation");
    audit.Check(0x40000000u,bytes);
    Require(audit.counters().unsupported==1,"shadow audit admitted ordinary virtual memory");
    audit.ExcludePhysical(physical+15,1);
    audit.Check(address,bytes);
    *memory.TranslatePhysical<volatile uint8_t*>(physical)=55;
    audit.Check(address,bytes);
    Require(audit.counters().excluded==2 && audit.counters().missed==1,
      "audio-owned page was admitted after direct physical mutation");
    const std::span<const uint8_t> next_bytes{memory.TranslateVirtual(address+size),16};
    audit.Check(address+size,next_bytes);
    audit.Check(address+size,next_bytes);
    Require(audit.counters().stable==2,"audio exclusion spilled into unrelated page");
    std::thread foreign_writer([&] {
      *memory.TranslateVirtual<volatile uint8_t*>(address+size)=61;
    });
    foreign_writer.join();
    audit.Check(address+size,next_bytes);
    Require(audit.counters().foreign==1 && audit.counters().stable==2,
      "foreign-thread write remained eligible for stable-version audit");
    memory.EnablePhysicalMemoryAccessCallbacks(physical+size,size,true,false);
    *memory.TranslateVirtual<volatile uint8_t*>(address+size)=63;
    audit.Check(address+size,next_bytes);
    Require(audit.counters().foreign==2,"re-arm erased foreign-writer exclusion");
    std::thread foreign_reader([&] { audit.Check(address+size,next_bytes); });
    foreign_reader.join();
    Require(audit.counters().unsupported==2,"audit changed drawing-thread ownership");
    bool rejected=false;
    try { audit.ExcludePhysical(0x1fffffffu,2); }
    catch(const std::runtime_error&) { rejected=true; }
    Require(rejected,"overflowing writer exclusion range admitted");
    const auto stable_before_disable=audit.counters().stable;
    const auto unsupported_before_disable=audit.counters().unsupported;
    audit.Disable();
    audit.Check(address,bytes);
    audit.Disable();
    audit.Check(address+size,next_bytes);
    Require(audit.counters().stable==stable_before_disable &&
      audit.counters().unsupported==unsupported_before_disable+2,
      "disabled provider audit resumed eligibility");
  }
  // Destruction unregisters the audit while Memory is still alive.
  {
    edf::native::GuestMeshWatchAudit audit(memory);
    const auto begin=audit.Begin(address,16);
    Require(begin.has_value(),"owned audit did not begin observation");
    const std::vector<uint8_t> snapshot(memory.TranslateVirtual(address),memory.TranslateVirtual(address)+16);
    audit.Finish(*begin,snapshot);
    const auto raced=audit.Begin(address,16);
    Require(raced.has_value(),"owned audit did not re-arm observation");
    *memory.TranslateVirtual<volatile uint8_t*>(address)=71;
    audit.Finish(*raced,snapshot);
    Require(audit.counters().invalidated==1 && audit.counters().missed==0 && audit.counters().stable==0,
      "owned audit tagged old bytes with a newer watch version");
    const auto fresh=audit.Begin(address,16);
    Require(fresh.has_value(),"owned audit could not begin fresh observation");
    const std::vector<uint8_t> fresh_snapshot(memory.TranslateVirtual(address),memory.TranslateVirtual(address)+16);
    audit.Finish(*fresh,fresh_snapshot);
    const auto stable=audit.Begin(address,16);
    Require(stable.has_value(),"owned audit could not begin stable observation");
    audit.Finish(*stable,fresh_snapshot);
    Require(audit.counters().stable==1 && audit.counters().missed==0,
      "owned audit retained a raced baseline");
    bool rejected=false;
    try { audit.Finish(*stable,std::span<const uint8_t>(fresh_snapshot).first(8)); }
    catch(const std::runtime_error&) { rejected=true; }
    Require(rejected,"owned audit accepted wrong snapshot extent");
  }
  *guest=59;
  {
    edf::native::GuestPhysicalVersions owned(memory);
    Require(owned.ClaimDrawingThread() && owned.ClaimDrawingThread(),"drawing owner claim failed");
    owned.Arm(physical,size*2);
    *guest=65;
    Require(!owned.HasForeignWrites(physical,size),"same-thread write rejected as foreign");
    const uint32_t alias=0xc0000000u+physical+size;
    const auto prior_version=owned.Version(physical+size,1);
    std::thread writer([&] { *memory.TranslateVirtual<volatile uint8_t*>(alias)=67; });
    writer.join();
    Require(owned.HasForeignWrites(physical+size,1) && !owned.HasForeignWrites(physical,size),
      "foreign writer extent/physical alias classification failed");
    Require(*memory.TranslateVirtual<volatile uint8_t*>(address+size)==67 &&
      owned.Version(physical+size,1)>prior_version,"supplemental alias watch missed mutation");
    owned.Arm(physical+size,size);
    Require(owned.HasForeignWrites(physical+size,size),"foreign marker changed on re-arm");
    owned.Arm(physical,size);
    const auto before_provider=owned.Version(physical,1);
    *memory.TranslatePhysical<volatile uint8_t*>(physical)=69;
    Require(owned.Version(physical,1)==before_provider,"direct provider unexpectedly triggered virtual watch");
    edf::native::NotifyPhysicalProviderWrite(memory,physical,1);
    Require(owned.Version(physical,1)>before_provider,"completed provider did not notify watched aliases");
  }
  {
    const uint32_t other_physical=physical+size*2,alias=0xc0000000u+other_physical;
    auto* pointer=memory.TranslateVirtual(alias);
    MEMORY_BASIC_INFORMATION initial{};
    Require(VirtualQuery(pointer,&initial,sizeof(initial))!=0,"query alias fixture protection");
    if(memory.LookupHeap(alias)->QueryRangeAccess(alias,alias)==rex::memory::PageAccess::kNoAccess &&
       initial.Protect==PAGE_READWRITE) {
      unsigned notifications=0;
      auto notify=[](void* context,uint32_t,uint32_t) { ++*static_cast<unsigned*>(context); };
      {
        edf::native::NativeAliasWatch first(memory,notify,&notifications);
        first.Arm(other_physical,1);
        {
          edf::native::NativeAliasWatch second(memory,notify,&notifications);
          second.Arm(other_physical,1);
        }
        MEMORY_BASIC_INFORMATION shared{};
        VirtualQuery(pointer,&shared,sizeof(shared));
        Require(shared.Protect==PAGE_READONLY,"destroying overlapping watcher removed live protection");
        DWORD old=0;
        Require(VirtualProtect(pointer,4096,PAGE_READWRITE,&old)!=0,"open stale alias fixture");
        first.Arm(other_physical,1);
        Require(notifications==1,"re-arm did not invalidate stale alias snapshot");
        *static_cast<volatile uint8_t*>(pointer)=71;
        Require(notifications==2,"re-armed alias did not notify on write");
        first.Arm(other_physical,1);
      }
      MEMORY_BASIC_INFORMATION restored{};
      VirtualQuery(pointer,&restored,sizeof(restored));
      Require(restored.Protect==PAGE_READWRITE,"alias watcher destruction left orphan protection");
      *static_cast<volatile uint8_t*>(pointer)=73;
      Require(notifications==2,"destroyed alias watcher invoked callback");
    }
  }
}
}
int main() {
  {
    edf::native::NativeModelBuffers buffers;
    for(const uint32_t stride:{0u,1u,3u,8u}) {
      bool rejected=false;
      try { buffers.Publish(1,edf::native::NativeModelBuffers::Kind::Index,0xa0001000,stride,6,0x1000); }
      catch(const std::runtime_error&) { rejected=true; }
      Require(rejected,"native index registry accepted unsupported element width");
    }
  }
  for(const uint32_t index_stride:{2u,4u}) for(unsigned mode=0;mode<4;++mode) {
    using namespace edf::native;
    NativeBufferWrites writes;
    NativeModelBuffers buffers(&writes);
    const auto kind=NativeModelBuffers::Kind::Index;
    buffers.Publish(1,kind,0xa0001000,index_stride,6,0x1000);
    const auto generation=buffers.Find(1,kind)->generation;
    const std::vector<uint8_t> source(index_stride*6);
    Require(buffers.Find(1,kind)->stride==index_stride && buffers.Find(1,kind)->bytes==source.size(),
      "index ownership lost format or full extent");
    const auto snapshot=writes.CopyObserved(1,0x1000,source);
    Require(snapshot && buffers.RetainIndexContents(1,generation,snapshot->contents,snapshot->version) &&
      buffers.Find(1,kind)->index_contents==snapshot->contents && !buffers.Find(1,kind)->index_storage,
      "CPU index ownership required a GPU object");
    Require(!buffers.RetainIndexContents(1,generation+1,snapshot->contents,snapshot->version) &&
      !buffers.RetainIndexContents(1,generation,std::make_shared<const std::vector<uint8_t>>(2),snapshot->version),
      "index contents accepted wrong lifetime or partial extent");
    if(mode==0) buffers.NotifyUpdate(1);
    else {
      NativeBufferWrites::Batch batch;
      if(mode==1) { batch.count=1; batch.ranges[0]={0x1004,1}; }
      if(mode==2) batch.all=true;
      if(mode==3) {
        auto pages=std::make_shared<NativeBufferWrites::PageMask>(); pages->set(1); batch.pages=pages;
      }
      buffers.ApplyWrites(batch,[](uint32_t) {});
    }
    Require(!buffers.Find(1,kind)->index_contents && snapshot->contents->size()==source.size(),
      "index invalidation retained registry contents or destroyed owned draw snapshot");
    buffers.Publish(1,kind,0xa0001000,index_stride,6,0x1000);
    Require(!buffers.RetainIndexContents(1,buffers.Find(1,kind)->generation,snapshot->contents,snapshot->version),
      "index contents crossed allocation owner reuse");
  }
  {
    // Retail constructors pass usage=0, offset=0; index format=1 (uint16).
    for(const auto bytes:{0u,2u,40u,0x03fffffcu}) {
      const auto vb=edf::native::NativeModelHeader(false,0xa0001000,bytes);
      const auto ib=edf::native::NativeModelHeader(true,0xa0001000,bytes);
      Require(vb==std::array<uint32_t,8>{1,1,0,0,0,0xffff0000,0xa0001003,(bytes&0x03fffffc)|0x10000002},
        "native model VB compatibility header differs");
      Require(ib==std::array<uint32_t,8>{0x20000002,1,0,0,0,0xffff0000,0xa0001000,bytes},
        "native model IB compatibility header differs");
    }
    for(const auto invalid:{std::pair{0xa0001001u,40u},std::pair{0xa0001000u,0x04000000u}}) {
      bool rejected=false;
      try { (void)edf::native::NativeModelHeader(false,invalid.first,invalid.second); }
      catch(const std::runtime_error&) { rejected=true; }
      Require(rejected,"invalid model header accepted");
    }
  }
  {
    using Snapshots=edf::native::NativeRenderStateSnapshots;
    using Result=Snapshots::Result;
    Snapshots snapshots;
    Snapshots::Words words{1,2,3,4,15,1};
    Require(snapshots.Check(7,words)==Result::Missing && !snapshots.Find(7),"draw check seeded missing render state");
    snapshots.Publish(7,words,0x82135108);
    const auto revision=snapshots.Find(7)->revision;
    Require(snapshots.Check(7,words)==Result::Match,"published render state mismatched");
    for(size_t field=0;field<words.size();++field) {
      auto changed=words; changed[field]^=1;
      Require(snapshots.Check(7,changed)==Result::Mismatch && snapshots.Check(7,changed)==Result::Mismatch,
        "render-state audit silently repaired a missing writer");
      Require(snapshots.Find(7)->words==words && snapshots.Find(7)->revision==revision,"consumer changed render-state generation");
    }
    auto changed=words; changed[0]=77;
    snapshots.Publish(8,changed,0x82135208);
    Require(snapshots.Check(7,words)==Result::Match && snapshots.Check(8,changed)==Result::Match,"render-state devices aliased");
    snapshots.Retire(7);
    Require(snapshots.Check(7,words)==Result::Missing && snapshots.Find(8),"render-state retirement retained or erased wrong device");
    snapshots.Publish(7,changed,0x821470a8);
    Require(snapshots.Find(7)->revision>revision && snapshots.Find(7)->producer==0x821470a8 &&
      snapshots.Check(7,changed)==Result::Match,"render-state reused device kept stale lifetime");
    Require(snapshots.counters().mismatches==12 && snapshots.counters().missing==2,"render-state audit accounting changed");
    bool rejected=false;
    try {snapshots.Publish(0,words,0);} catch(const std::runtime_error&) {rejected=true;}
    Require(rejected && !snapshots.Find(0),"invalid render-state owner accepted");
    Snapshots partial;
    for(size_t field=0;field<words.size();++field) {
      partial.Publish(9,words,uint32_t(field+1),1u<<field);
      Require(partial.Check(9,words)==(field==5?Result::Match:Result::Missing),
        "partial state claimed complete ownership");
    }
    auto untracked=words;
    untracked[0]^=0x80;
    untracked[1]^=0x40;
    partial.Publish(9,untracked,0x82135530,2);
    Require(partial.Check(9,untracked)==Result::Mismatch &&
      partial.Find(9)->words[0]==words[0] && partial.Find(9)->words[1]==untracked[1] &&
      partial.Find(9)->producers[0]==1 && partial.Find(9)->producers[1]==0x82135530,
      "unrelated setter concealed an untracked field write");
    partial.Retire(9);
    partial.Publish(9,words,0x82135108,1);
    Require(partial.Check(9,words)==Result::Missing,"partial reused owner inherited valid fields");
    Require(edf::native::NativeRenderStateProducerFields(0x82134f18)==8 &&
      edf::native::NativeRenderStateProducerFields(0x82136478)==4 &&
      edf::native::NativeRenderStateProducerFields(0x82137988)==16 &&
      edf::native::NativeRenderStateProducerFields(0x8214eff8)==4,
      "non-obvious render-state producer mapping changed");
    rejected=false;
    try {(void)edf::native::NativeRenderStateProducerFields(0x821470a8);}
    catch(const std::runtime_error&) {rejected=true;}
    Require(rejected,"initializer must not republish all fields after nested setters");
    unsigned live_reads=0,audits=0;
    auto read_live=[&]{++live_reads; return words;};
    auto audit_live=[&](const auto&){++audits;};
    Snapshots owned;
    owned.Publish(10,words,0x82139638);
    Require(edf::native::ResolveNativeRenderState(owned,10,true,false,read_live,audit_live)==words &&
      live_reads==0 && audits==0,"owned render-state path touched guest memory");
    Require(edf::native::ResolveNativeRenderState(owned,10,true,true,read_live,audit_live)==words &&
      live_reads==1 && audits==1,"owned audit path did not independently validate live state");
    owned.Publish(10,untracked,0x82135108,1);
    rejected=false;
    try {(void)edf::native::ResolveNativeRenderState(owned,10,true,true,read_live,audit_live);}
    catch(const std::runtime_error&) {rejected=true;}
    Require(rejected,"owned audit mismatch silently fell back to live state");
    owned.Retire(10);
    const auto before_missing=live_reads;
    rejected=false;
    try {(void)edf::native::ResolveNativeRenderState(owned,10,true,false,read_live,audit_live);}
    catch(const std::runtime_error&) {rejected=true;}
    Require(rejected && live_reads==before_missing,"missing owned state fell back to guest reads");
    Require(edf::native::ResolveNativeRenderState(owned,10,false,false,read_live,audit_live)==words,
      "legacy render-state path requires native ownership");
    // Keep bit patterns (including signed zero and NaN payloads) authoritative.
    const Snapshots::BlendWords blend{0x3e000000,0x80000000,0x7fc01234,0x3f800000};
    Require(owned.CheckBlend(10,blend)==Result::Missing,"blend audit seeded missing owner");
    owned.PublishBlend(10,blend,0x82135418);
    owned.Publish(10,words,0x82135108);
    Require(owned.RequireBlend(10)==blend && owned.CheckBlend(10,blend)==Result::Match &&
      owned.RequireOwned(10)==words,"render publication lost independent blend ownership");
    for(size_t channel=0;channel<blend.size();++channel) {
      auto changed_blend=blend; changed_blend[channel]^=1;
      Require(owned.CheckBlend(10,changed_blend)==Result::Mismatch && owned.RequireBlend(10)==blend,
        "blend audit repaired differing channel bits");
    }
    owned.PublishBlend(11,{},0x82139638);
    Require(owned.RequireBlend(10)==blend && owned.RequireBlend(11)==Snapshots::BlendWords{},
      "blend-factor devices alias");
    owned.Retire(10);
    owned.Publish(10,words,0x82135108);
    rejected=false;
    try {(void)owned.RequireBlend(10);} catch(const std::runtime_error&) {rejected=true;}
    Require(rejected && owned.CheckBlend(10,blend)==Result::Missing,
      "retired blend factor survived owner reuse");
    Require(owned.blend_counters().mismatches==4 && owned.blend_counters().missing==2,
      "blend-factor audit counters changed");
  }
  {
    using namespace edf::native;
    NativeBufferWrites writes;
    struct Expected { uint32_t begin,bytes; uint64_t revision; };
    std::map<uint32_t,Expected> expected;
    uint32_t seed=0x13579bdf;
    auto random=[&] { seed=seed*1664525u+1013904223u; return seed; };
    for(unsigned round=0;round<600;++round) {
      const uint32_t owner=random()%64+1;
      if(round%7==0) { writes.Unsubscribe(owner); expected.erase(owner); }
      else {
        const uint32_t begin=0x1000+(random()%128)*4,bytes=(random()%128+1)*4;
        writes.Subscribe(owner,begin,bytes); expected.insert_or_assign(owner,Expected{begin,bytes,0});
      }
      const uint32_t begin=0x0ff0+(random()%260)*4,bytes=random()%520;
      writes.Record(begin,bytes,true);
      uint64_t hits=0;
      for(auto& [id,range]:expected) {
        if(bytes && uint64_t(begin)+bytes>range.begin && uint64_t(range.begin)+range.bytes>begin) {
          ++range.revision; ++hits;
        }
        Require(writes.Version(id)->revision==range.revision,"indexed subscriptions differ from overlap oracle");
      }
      Require(writes.Drain().generated_exact_owner_hits==hits,"indexed owner-hit count differs from oracle");
    }
  }
  {
    using namespace edf::native;
    NativeBufferWrites writes;
    writes.Subscribe(1,0x1000,16); writes.Subscribe(2,0x1008,16);
    const auto initial=*writes.Version(1);
    writes.Record(0x1100,4,true);
    Require(writes.Version(1)==initial,"same-page unrelated write changed exact version");
    writes.Record(0x100c,4,true);
    Require(writes.Version(1)->revision==1 && writes.Version(2)->revision==1,
      "exact version missed aliases");
    writes.Record(0x1000,4);
    auto batch=writes.Drain();
    Require(batch.generated_exact_owner_hits==2 && batch.provider_exact_owner_hits==1,
      "exact version provenance counts");
    Require(writes.Version(1)->revision==2,"drain reset owner version");
    for(unsigned i=0;i<300;++i) writes.Record(0x8000+i*8,4,true);
    writes.Record(0x1000,4,true);
    Require(writes.Drain().pages && writes.Version(1)->revision==3,
      "page-overflow batch lost exact version");
    writes.Record(UINT32_MAX,1);
    Require(writes.Version(1)->revision==4 && writes.Version(2)->revision==2,
      "unknown write extent did not advance all versions");
    writes.Subscribe(1,0x1000,16);
    Require(writes.Version(1)->lifetime!=initial.lifetime && writes.Version(1)->revision==0,
      "identical owner republish reused version token");
    writes.Unsubscribe(1); Require(!writes.Version(1),"retired owner retained version");
    writes.Subscribe(1,0x1000,16);
    Require(writes.Version(1)->lifetime>initial.lifetime,"owner reuse reset lifetime identity");
    writes.Drain();
    std::array<std::thread,4> writers;
    for(auto& thread:writers) thread=std::thread([&] { for(unsigned i=0;i<100;++i) writes.Record(0x1000,4,true); });
    for(auto& thread:writers) thread.join();
    Require(writes.Version(1)->revision==400 && writes.Drain().generated_exact_owner_hits==400,
      "concurrent completed writes lost exact version increments");
  }
  {
    using namespace edf::native;
    NativeBufferWrites writes;
    writes.Subscribe(1,0x1000,16);
    const auto before=*writes.Version(1);
    bool committed=false;
    {
      NativeBufferWrites::WriterScope outer(&writes);
      Require(!writes.Version(1),"active writer issued a snapshot token");
      Require(!writes.CommitObserved(1,before,[&] { committed=true; }),"active writer accepted attachment");
      {
        NativeBufferWrites::WriterScope nested(&writes);
        writes.Subscribe(2,0x1008,16);
        Require(!writes.Version(2),"publication during write escaped transaction");
      }
      writes.Record(0x1000,4);
      writes.Drain();
      Require(!writes.Version(1),"notification/drain or nested exit ended outer transaction");
    }
    Require(!committed && !writes.CommitObserved(1,before,[&] { committed=true; }),"old token survived bulk write");
    auto after=*writes.Version(1);
    Require(writes.CommitObserved(1,after,[&] { committed=true; }) && committed,"finished writer blocked fresh attachment");
    try { NativeBufferWrites::WriterScope failed(&writes); throw std::runtime_error("partial provider failure"); }
    catch(const std::runtime_error&) {}
    Require(writes.Version(1).has_value() && !writes.CommitObserved(1,after,[] {}),
      "exception retained active writer or accepted pre-write token");
    Require(writes.Pending() && writes.Version(1)->revision==after.revision+1,
      "partial failure did not invalidate retained versions");
    const auto aborted=writes.Drain();
    Require(aborted.all && aborted.aborted_writers==1 && aborted.provider_calls==0 &&
      aborted.provider_exact_owner_hits==0,"partial failure falsely attributed completed writes");
    {
      NativeBufferWrites::WriterScope normal(&writes);
      try { throw std::runtime_error("caught inside provider"); } catch(const std::runtime_error&) {}
    }
    Require(!writes.Pending(),"normal scope exit invalidated all geometry");
    try {
      NativeBufferWrites::WriterScope outer(&writes),inner(&writes);
      throw std::runtime_error("nested provider failure");
    } catch(const std::runtime_error&) {}
    const auto nested_abort=writes.Drain();
    Require(nested_abort.all && nested_abort.aborted_writers==2 && writes.Version(2).has_value(),
      "nested unwind lost invalidation or retained active writer");
    after=*writes.Version(1);
    { NativeBufferWrites::WriterScope disabled(nullptr); }
    Require(writes.Version(1)==after,"disabled scope changed observation");
    std::atomic<bool> entered=false,release=false;
    std::thread writer([&] {
      NativeBufferWrites::WriterScope scope(&writes);
      entered.store(true,std::memory_order_release);
      while(!release.load(std::memory_order_acquire)) std::this_thread::yield();
    });
    while(!entered.load(std::memory_order_acquire)) std::this_thread::yield();
    const bool excluded=!writes.Version(1) && !writes.CommitObserved(1,after,[] {});
    release.store(true,std::memory_order_release); writer.join();
    Require(excluded && writes.Version(1).has_value() && !writes.CommitObserved(1,after,[] {}),
      "cross-thread write transaction failed");
  }
  {
    using namespace edf::native;
    NativeBufferWrites writes;
    writes.Subscribe(1,0x1000,16);
    std::atomic<bool> start=false;
    std::thread writer([&] {
      while(!start.load(std::memory_order_acquire)) std::this_thread::yield();
      for(unsigned i=0;i<1000;++i) writes.Record(i%2?0x1000:0x2000,4,true);
    });
    start.store(true,std::memory_order_release);
    for(unsigned i=0;i<1000;++i) {
      writes.Subscribe(1,i%2?0x1000:0x2000,16);
      writes.Subscribe(2,0x1000,16);
      writes.Unsubscribe(2);
    }
    writer.join();
    const auto batch=writes.Drain();
    Require(batch.generated_calls==1000 && batch.generated_subscribed_calls<=1000 && !batch.all,
      "concurrent subscription update lost writes or double-counted matches");
    Require(batch.count==2 && batch.ranges[0].address==0x1000 && batch.ranges[0].bytes==4 &&
      batch.ranges[1].address==0x2000 && batch.ranges[1].bytes==4,
      "subscription race changed write coverage");
    writes.Unsubscribe(1);
    writes.Record(0x1000,4,true); writes.Record(0x2000,4,true);
    Require(writes.Drain().generated_subscribed_calls==0,"subscription race leaked page references");
  }
  {
    using namespace edf::native;
    NativeBufferWrites writes;
    std::array<uint8_t,64> source{};
    writes.Subscribe(1,0x1000,uint32_t(source.size()));
    const auto initial=writes.CopyObserved(1,0x1000,source);
    Require(initial && initial->contents->size()==source.size() &&
      writes.CommitObserved(1,initial->version,[] {}),"idle publication snapshot rejected");
    Require(!writes.CopyObserved(2,0x1000,source) && !writes.CopyObserved(1,0x1004,source) &&
      !writes.CopyObserved(1,0x1000,std::span(source).first(32)),"snapshot accepted unknown owner or wrong extent");
    {
      NativeBufferWrites::WriterScope writer(&writes);
      Require(!writes.CopyObserved(1,0x1000,source),"snapshot read through active writer");
      writes.Subscribe(2,0x1000,uint32_t(source.size()));
      Require(!writes.CopyObserved(2,0x1000,source),"new alias read through active writer");
      source.fill(7); writes.Record(0x1000,uint32_t(source.size()));
    }
    Require(std::all_of(initial->contents->begin(),initial->contents->end(),[](uint8_t b){return b==0;}) &&
      !writes.CommitObserved(1,initial->version,[] {}),"writer mutated or revived retained snapshot");
    const auto updated=writes.CopyObserved(1,0x1000,source);
    Require(updated && updated->contents->front()==7 && writes.CommitObserved(1,updated->version,[] {}),
      "completed writer prevented fresh snapshot");
    std::atomic<bool> start=false;
    std::thread writer([&] {
      while(!start.load(std::memory_order_acquire)) std::this_thread::yield();
      for(unsigned i=1;i<=2000;++i) {
        NativeBufferWrites::WriterScope scope(&writes);
        std::fill_n(source.begin(),32,uint8_t(i));
        std::this_thread::yield();
        std::fill(source.begin()+32,source.end(),uint8_t(i));
      }
    });
    bool coherent=true; start.store(true,std::memory_order_release);
    for(unsigned i=0;i<2000;++i) if(auto snapshot=writes.CopyObserved(1,0x1000,source))
      coherent&=std::all_of(snapshot->contents->begin(),snapshot->contents->end(),
        [&](uint8_t b){return b==snapshot->contents->front();});
    writer.join();
    const auto final=writes.CopyObserved(1,0x1000,source);
    Require(coherent && final && std::all_of(final->contents->begin(),final->contents->end(),
      [](uint8_t b){return b==uint8_t(2000);}),"snapshot observed a torn tracked-provider write");
    writes.Unsubscribe(1);
    Require(!writes.CopyObserved(1,0x1000,source),"retired owner permitted source copy");
    writes.Subscribe(1,0x1000,uint32_t(source.size()));
    Require(!writes.CommitObserved(1,final->version,[] {}),"snapshot crossed owner reuse");
  }
  {
    using namespace edf::native;
    // Even a callback-free guest copy can fault on a watched alias. Model the
    // fault handler's actual SDK lock dependency while its producer scope is
    // active: snapshot rejection must not wait while the caller owns that lock.
    NativeBufferWrites writes;
    std::array<uint8_t,16> source{};
    writes.Subscribe(1,0x1000,uint32_t(source.size()));
    const std::array<NativeBufferWrites::SnapshotSource,1> sources{{{1,0x1000,source}}};
    auto global_lock=rex::thread::global_critical_region::AcquireDirect();
    std::atomic<bool> writer_started=false;
    std::thread writer([&] {
      NativeBufferWrites::WriterScope scope(&writes,NativeBufferWrites::Range{0x1000,16},
        NativeBufferWrites::WriterKind::Bulk);
      writer_started.store(true,std::memory_order_release);
      auto fault_lock=rex::thread::global_critical_region::AcquireDirect();
      source.fill(0x5a);
      writes.Record(0x1000,16);
    });
    while(!writer_started.load(std::memory_order_acquire)) std::this_thread::yield();
    NativeBufferWrites::SnapshotFailure failure;
    const auto busy=writes.CopyObservedSet(sources,&failure);
    global_lock.unlock();
    writer.join();
    const auto completed=writes.CopyObservedSet(sources);
    Require(!busy && failure.reason==NativeBufferWrites::SnapshotRejection::ActiveWriter &&
      failure.overlapping_writers==1 && failure.unknown_writers==0 && failure.same_thread_writers==0,
      "snapshot admission read or waited through a lock-dependent physical writer");
    Require(completed && std::all_of((*completed)[0].contents->begin(),(*completed)[0].contents->end(),
      [](uint8_t value) { return value==0x5a; }),"snapshot did not recover after fault-lock release");
  }
  {
    using namespace edf::native;
    NativeBufferWrites writes;
    writes.Subscribe(1,0x1000,16); writes.Subscribe(2,0x2000,12);
    {
      using Range=NativeBufferWrites::Range;
      const std::array<Range,2> backings{{{0x1000,16},{0x2000,12}}};
      std::array<uint8_t,16> bytes{};
      auto releases=writes.BeginReleaseSet(backings);
      auto transferred=std::move(releases);
      Require(releases.empty() && transferred.size()==2,"release scope ownership did not transfer");
      NativeBufferWrites::SnapshotFailure failure;
      const std::array<NativeBufferWrites::SnapshotSource,1> first{{{1,0x1000,bytes}}};
      Require(!writes.CopyObservedSet(first,&failure) && failure.overlapping_writers==1 && failure.releasing_writers==1,
        "moved backing release lost exclusion");
      writes.Unsubscribe(1);
      Require(!writes.CopyObservedSet(first,&failure) &&
        failure.reason==NativeBufferWrites::SnapshotRejection::ActiveWriter && failure.releasing_writers==1,
        "retired subscription hid an in-flight allocation release");
      writes.Subscribe(1,0x1000,8);
      Require(!writes.CopyObservedSet(first,&failure) &&
        failure.reason==NativeBufferWrites::SnapshotRejection::ActiveWriter && failure.releasing_writers==1,
        "changed subscription extent hid an in-flight allocation release");
      writes.Subscribe(1,0x1000,16);
      writes.Subscribe(3,0x2004,4); // An owner arriving during whole-pool release.
      const std::array<NativeBufferWrites::SnapshotSource,1> late{{{3,0x2004,std::span<const uint8_t>(bytes).first(4)}}};
      Require(!writes.CopyObservedSet(late),"late owner escaped whole-backing exclusion");
      transferred.erase(transferred.begin()); // Exercise non-LIFO removal.
      Require(writes.CopyObservedSet(first,&failure).has_value() && failure.releasing_writers==0 && !writes.CopyObservedSet(late),
        "partial release-scope exit lost remaining exclusion");
      transferred.clear();
      Require(writes.CopyObservedSet(late).has_value(),"completed backing release retained scope");
      writes.Unsubscribe(3);
      bool caught=false;
      try { auto aborted=writes.BeginReleaseSet(backings); throw std::runtime_error("injected free failure"); }
      catch(const std::runtime_error&) { caught=true; }
      const auto aborted=writes.Drain();
      Require(caught && aborted.all && aborted.aborted_writers==2 && writes.Version(1) && writes.Version(2),
        "failed backing release leaked scopes or lost conservative invalidation");
    }
    auto observe=[&] { return std::array<NativeBufferWrites::ObservedOwner,2>{{
      {1,*writes.Version(1)},{2,*writes.Version(2)}}}; };
    unsigned commits=0;
    auto pair=observe();
    Require(writes.CommitObservedSet(pair,[&] { ++commits; }) && commits==1,
      "stable paired observation did not commit exactly once");
    writes.Record(0x2000,1);
    Require(!writes.CommitObservedSet(pair,[&] { ++commits; }) && commits==1,
      "stale second owner partially committed paired observation");
    pair=observe(); writes.InvalidateObservedRange(0x1000,16);
    Require(!writes.CommitObservedSet(pair,[&] { ++commits; }) && commits==1,
      "explicit first-owner invalidation accepted stale pair");
    pair=observe();
    { NativeBufferWrites::WriterScope writer(&writes);
      Require(!writes.CommitObservedSet(pair,[&] { ++commits; }),"pair committed through active writer"); }
    Require(!writes.CommitObservedSet(pair,[&] { ++commits; }),"pair ignored ended writer epoch");
    pair=observe(); writes.Unsubscribe(2);
    Require(!writes.CommitObservedSet(pair,[&] { ++commits; }),"pair accepted missing owner");
    writes.Subscribe(2,0x2000,12);
    Require(!writes.CommitObservedSet(pair,[&] { ++commits; }) && commits==1,
      "pair crossed owner reuse or ran a rejected commit callback");
    pair=observe();
    Require(writes.CommitObservedSet(pair,[&] { ++commits; }) && commits==2,
      "fresh paired observations did not recover after invalidation");
  }
  {
    using namespace edf::native;
    NativeBufferWrites reuse;
    std::vector<uint8_t> payload(12,7);
    reuse.Subscribe(1,0x1000,12);
    using Source=NativeBufferWrites::SnapshotSource;
    using Identity=NativeBufferWrites::SnapshotIdentity;
    const NativeBufferWrites::SnapshotPolicy policy{0,2,false};
    std::array<Source,1> sources{{{1,0x1000,payload}}};
    sources[0].candidate=reuse.CopyObservedSet(sources,nullptr,policy)->front().contents;
    std::array<Identity,1> identity{{{1,0x1000,12,sources[0].candidate}}};
    Require(!reuse.TryReuseObservedSet(identity,{}) && !reuse.TryReuseObservedSet(identity,{0,2,true}),
      "metadata reuse bypassed strict/audit policy");
    auto invalid=identity; invalid[0].bytes=11;
    Require(!reuse.TryReuseObservedSet(invalid,policy),"metadata reuse accepted wrong extent");
    invalid=identity; invalid[0].physical+=4;
    Require(!reuse.TryReuseObservedSet(invalid,policy),"metadata reuse accepted wrong physical address");
    invalid=identity; invalid[0].candidate=std::make_shared<const std::vector<uint8_t>>(payload);
    Require(!reuse.TryReuseObservedSet(invalid,policy),"metadata reuse accepted another candidate");
    Require(!reuse.TryReuseObservedSet(std::array<Identity,2>{{identity[0],{99,0x2000,12,sources[0].candidate}}},policy),
      "metadata pair accepted missing second owner");
    Require(!reuse.TryReuseObservedSet(std::array<Identity,2>{{identity[0],identity[0]}},policy),
      "duplicate owners bypassed sequential verification schedule");
    {
      NativeBufferWrites::WriterScope writer(&reuse);
      Require(!reuse.TryReuseObservedSet(identity,policy),"active writer allowed metadata reuse");
    }
    using View=NativeBufferWrites::SnapshotIdentityView;
    std::array<View,1> borrowed{{{1,0x1000,12,&sources[0].candidate}}};
    auto null_view=borrowed; null_view[0].candidate=nullptr;
    Require(!reuse.TryValidateObservedSet(null_view,policy),"borrowed identity accepted a missing handle");
    const auto owners=sources[0].candidate.use_count();
    const auto accepted=reuse.TryValidateObservedSet(borrowed,policy);
    Require(accepted && sources[0].candidate.use_count()==owners,
      "failed metadata attempts advanced the successful owner's schedule");
    Require(!reuse.TryValidateObservedSet(borrowed,policy),"borrowed validation skipped periodic comparison");
    Require(!reuse.TryReuseObservedSet(identity,policy),"metadata reuse skipped periodic comparison");
    (void)reuse.CopyObservedSet(sources,nullptr,policy);
    reuse.Record(0x1000,4);
    Require(!reuse.TryReuseObservedSet(identity,policy),"changed revision reused old geometry");
    Require(!reuse.TryValidateObservedSet(borrowed,policy),"borrowed identity crossed a changed revision");
    Require(!reuse.CommitObserved(1,accepted->front(),[]{}),"metadata token crossed a tracked write");
    (void)reuse.CopyObservedSet(sources,nullptr,policy);
    reuse.Unsubscribe(1);
    Require(!reuse.TryReuseObservedSet(identity,policy),"retired owner allowed metadata reuse");
    reuse.Subscribe(1,0x1000,12);
    Require(!reuse.TryReuseObservedSet(identity,policy),"reused owner inherited metadata baseline");
  }
  {
    using namespace edf::native;
    NativeBufferWrites writes;
    writes.Subscribe(1,0x1000,12);
    const auto initial=*writes.Version(1);
    std::array<uint8_t,12> vertices{},indices{};
    {
      using Scope=NativeBufferWrites::WriterScope;
      using Range=NativeBufferWrites::Range;
      const std::array<NativeBufferWrites::SnapshotSource,1> requested{{{1,0x1000,vertices}}};
      NativeBufferWrites::SnapshotFailure failure;
      auto older=std::make_unique<Scope>(&writes,Range{0x2000,12});
      auto newer=std::make_unique<Scope>(&writes,Range{0x100c,12});
      const auto disjoint_snapshot=writes.CopyObservedSet(requested,&failure);
      Require(disjoint_snapshot.has_value() && failure.same_thread_writers==0,
        "unrelated or adjacent physical writer blocked geometry snapshot");
      unsigned attachments=0;
      Require(writes.CommitObserved(1,(*disjoint_snapshot)[0].version,[&] { ++attachments; }) && attachments==1,
        "disjoint writer blocked attachment of guarded geometry snapshot");
      Require(!writes.CommitObserved(1,initial,[&] { ++attachments; }) && attachments==1,
        "disjoint writer entry revived an older epoch");
      Require(!writes.Version(1),"range optimization weakened attachment handshake");
      older.reset(); // Intrusive list must support cross-thread/non-LIFO exits.
      Require(writes.CommitObserved(1,(*disjoint_snapshot)[0].version,[&] { ++attachments; }) && attachments==2,
        "completed disjoint writer invalidated unchanged guarded snapshot");
      Require(writes.CopyObservedSet(requested).has_value(),"non-LIFO writer exit corrupted exclusion list");
      {
        Scope overlap(&writes,Range{0x100b,1});
        // Isolate range exclusion from epoch exclusion using the current epoch
        // obtained from a separate, disjoint subscription.
        writes.Subscribe(3,0x3000,12);
        const auto other=writes.CopyObservedSet(std::array<NativeBufferWrites::SnapshotSource,1>{{{3,0x3000,vertices}}});
        Require(other.has_value(),"overlap fixture blocked disjoint owner");
        auto current_epoch=(*disjoint_snapshot)[0].version;
        current_epoch.writer_epoch=(*other)[0].version.writer_epoch;
        Require(!writes.CommitObserved(1,current_epoch,[&] { ++attachments; }) && attachments==2,
          "attachment ignored overlapping active range at current epoch");
        Require(!writes.CopyObservedSet(requested,&failure) && failure.overlapping_writers==1 &&
          failure.unknown_writers==0 && failure.active_writers==2 && failure.same_thread_writers==1,
          "one-byte overlap escaped snapshot exclusion");
        Scope unknown(&writes);
        Require(!writes.CopyObservedSet(requested,&failure) && failure.overlapping_writers==1 &&
          failure.unknown_writers==1 && failure.same_thread_writers==2,"unknown writer was not conservatively excluded");
      }
      newer.reset();
      Require(writes.CopyObservedSet(requested).has_value(),"finished ranges remained active");
      for(size_t kind=0;kind<size_t(NativeBufferWrites::WriterKind::Count);++kind) {
        Scope provider(&writes,std::nullopt,NativeBufferWrites::WriterKind(kind));
        Require(!writes.CopyObservedSet(requested,&failure) && failure.unknown_by_kind[kind]==1 &&
          failure.unknown_writers==1,"unknown range lost provider attribution");
        for(size_t other=0;other<failure.unknown_by_kind.size();++other)
          if(other!=kind) Require(failure.unknown_by_kind[other]==0,"provider attribution retained old rejection");
      }
      Require(writes.CopyObservedSet(requested,&failure).has_value() && failure.same_thread_writers==0 &&
        std::all_of(failure.unknown_by_kind.begin(),failure.unknown_by_kind.end(),[](uint64_t n){return n==0;}),
        "successful acquisition retained provider rejection counts");
      for(const auto malformed: {Range{0x1000,0},Range{0x1fffffff,2},Range{0x20000000,1}}) {
        Scope invalid(&writes,malformed);
        Require(!writes.CopyObservedSet(requested,&failure) && failure.unknown_writers==1,
          "malformed active range weakened exclusion");
      }
    }
    writes.Subscribe(2,0x2000,12);
    const std::array<NativeBufferWrites::SnapshotSource,2> sources{{
      {1,0x1000,vertices},{2,0x2000,indices}}};
    const auto original=writes.CopyObservedSet(sources,nullptr,{0,0,true});
    Require(original && (*original)[0].version.writer_epoch==(*original)[1].version.writer_epoch,
      "geometry pair was not captured at a common writer boundary");
    Require(!(*original)[0].revision_audited && !(*original)[1].revision_audited,
      "first observations claimed a revision audit baseline");
    auto candidates=sources;
    candidates[0].candidate=(*original)[0].contents;
    candidates[1].candidate=(*original)[1].contents;
    const auto reused=writes.CopyObservedSet(candidates,nullptr,{0,0,true});
    Require(reused && (*reused)[0].contents==(*original)[0].contents &&
      (*reused)[1].contents==(*original)[1].contents,"unchanged geometry candidates were copied");
    Require((*reused)[0].revision_audited && (*reused)[1].revision_audited,
      "unchanged retained candidates were not counted as revision-audited");
    // An unreported write must still be detected: candidate reuse is not based
    // on notification versions alone while the writer audit remains open.
    vertices[0]=3;
    const auto replaced=writes.CopyObservedSet(candidates,nullptr,{0,0,true});
    Require(replaced && (*replaced)[0].contents!=(*original)[0].contents &&
      (*replaced)[0].contents->front()==3 && (*replaced)[1].contents==(*original)[1].contents,
      "unreported source change reused a stale snapshot candidate");
    Require((*replaced)[0].revision_audited && (*replaced)[1].revision_audited &&
      (*replaced)[0].unreported_change && !(*replaced)[1].unreported_change,
      "revision audit missed unreported change or blamed unchanged index data");
    candidates[0].candidate=(*replaced)[0].contents;
    vertices[0]=4;
    writes.Record(0x1000,1);
    const auto notified=writes.CopyObservedSet(candidates,nullptr,{0,0,true});
    Require(notified && !(*notified)[0].revision_audited && !(*notified)[0].unreported_change &&
      (*notified)[0].contents->front()==4,
      "revision audit blamed a notified write");
    candidates[0].candidate=(*notified)[0].contents;
    { NativeBufferWrites::WriterScope unrelated(&writes,NativeBufferWrites::Range{0x9000,4}); }
    vertices[0]=5;
    const auto epoch_changed=writes.CopyObservedSet(candidates,nullptr,{0,0,true});
    Require(epoch_changed && (*epoch_changed)[0].unreported_change,
      "unrelated writer epoch hid an unreported geometry change");
    candidates[0].candidate=(*epoch_changed)[0].contents;
    writes.Subscribe(1,0x1000,static_cast<uint32_t>(vertices.size()));
    vertices[0]=6;
    const auto new_lifetime=writes.CopyObservedSet(candidates,nullptr,{0,0,true});
    Require(new_lifetime && !(*new_lifetime)[0].revision_audited && !(*new_lifetime)[0].unreported_change,
      "revision audit carried a baseline across owner lifetime reuse");
    candidates[0].candidate=(*new_lifetime)[0].contents;
    // The baseline is maintained for every observation, not only audited ones:
    // the sampled comparison schedule depends on it being current.
    const auto audit_disabled=writes.CopyObservedSet(candidates);
    Require(audit_disabled && (*audit_disabled)[0].revision_audited &&
      (*audit_disabled)[1].revision_audited && (*audit_disabled)[0].verified &&
      (*audit_disabled)[1].verified,
      "default policy lost the revision baseline or skipped its comparison");
    candidates[0].candidate=std::make_shared<const std::vector<uint8_t>>(vertices.begin(),vertices.end());
    const auto different_candidate=writes.CopyObservedSet(candidates,nullptr,{0,0,true});
    Require(different_candidate && !(*different_candidate)[0].revision_audited &&
      !(*different_candidate)[0].unreported_change && (*different_candidate)[1].revision_audited,
      "different immutable candidate claimed the previous candidate's audit baseline");
    vertices[0]=0;
    auto invalid=sources; invalid[1].physical+=4;
    NativeBufferWrites::SnapshotFailure failure;
    using Rejection=NativeBufferWrites::SnapshotRejection;
    Require(!writes.CopyObservedSet(invalid,&failure) && failure.reason==Rejection::ExtentMismatch &&
      failure.owner==2 && failure.active_writers==0,"geometry pair misclassified mismatched second extent");
    invalid=sources; invalid[1].owner=99;
    Require(!writes.CopyObservedSet(invalid,&failure) && failure.reason==Rejection::MissingOwner &&
      failure.owner==99,"geometry pair misclassified missing owner");
    Require(writes.CopyObservedSet(sources,&failure) && failure.reason==Rejection::None &&
      failure.owner==0 && failure.active_writers==0,"successful snapshot retained failure state");
    {
      NativeBufferWrites::WriterScope scope(&writes);
      vertices.fill(7);
      Require(!writes.CopyObservedSet(sources),"geometry pair read half-completed provider");
      Require(!writes.CopyObservedSet(candidates,&failure) && failure.reason==Rejection::ActiveWriter &&
        failure.active_writers==1 && failure.owner==0 && failure.same_thread_writers==1,"retained candidates bypassed or misclassified active writer");
      {
        NativeBufferWrites::WriterScope nested(&writes);
        Require(!writes.CopyObservedSet(candidates,&failure) && failure.active_writers==2 && failure.same_thread_writers==2,
          "snapshot rejection lost nested active-writer count");
      }
      indices.fill(7);
      writes.Record(0x1000,12); writes.Record(0x2000,12);
    }
    Require(!writes.CommitObserved(1,initial,[] {}) && (*original)[0].contents->front()==0 &&
      (*original)[1].contents->front()==0,"pair snapshots changed with their source");
    std::atomic<bool> start=false;
    std::thread writer([&] {
      while(!start.load(std::memory_order_acquire)) std::this_thread::yield();
      for(unsigned i=1;i<=2000;++i) {
        NativeBufferWrites::WriterScope scope(&writes,NativeBufferWrites::Range{0x1000,0x100c});
        vertices.fill(uint8_t(i));
        std::this_thread::yield();
        indices.fill(uint8_t(i));
        writes.Record(0x1000,12); writes.Record(0x2000,12);
      }
    });
    bool coherent=true; start.store(true,std::memory_order_release);
    for(unsigned i=0;i<2000;++i) if(auto pair=writes.CopyObservedSet(sources)) {
      coherent&=(*pair)[0].version.writer_epoch==(*pair)[1].version.writer_epoch;
      const auto value=(*pair)[0].contents->front();
      for(const auto& snapshot:*pair)
        coherent&=std::all_of(snapshot.contents->begin(),snapshot.contents->end(),
          [&](uint8_t b) { return b==value; });
    }
    writer.join();
    const auto final=writes.CopyObservedSet(sources);
    Require(coherent && final && (*final)[0].contents->front()==uint8_t(2000) &&
      (*final)[1].contents->front()==uint8_t(2000),"geometry pair mixed tracked write generations");
    writes.Unsubscribe(2);
    Require(!writes.CopyObservedSet(sources),"geometry pair accepted retired index owner");
    writes.Subscribe(2,0x2000,12);
    Require(!writes.CommitObserved(2,(*final)[1].version,[] {}),"pair token crossed index owner reuse");
  }
  {
    using namespace edf::native;
    // Sampled comparison schedule. The byte comparison is the only oracle for
    // writer coverage, so trusting a revision-proven candidate must still
    // compare on a bounded schedule and must fail closed once a miss appears.
    NativeBufferWrites writes;
    std::vector<uint8_t> payload(64,1);
    writes.Subscribe(1,0x1000,uint32_t(payload.size()));
    std::array<NativeBufferWrites::SnapshotSource,1> sources{{{1,0x1000,payload}}};
    const NativeBufferWrites::SnapshotPolicy sampled{2,4,false};
    const auto first=writes.CopyObservedSet(sources,nullptr,sampled);
    Require(first && (*first)[0].verified && !(*first)[0].revision_audited,
      "first observation skipped its comparison or claimed a baseline");
    sources[0].candidate=(*first)[0].contents;
    std::array<NativeBufferWrites::SnapshotIdentity,1> identities{{{1,0x1000,payload.size(),sources[0].candidate}}};
    Require(!writes.TryReuseObservedSet(identities,sampled),"metadata reuse skipped initial verification");
    const auto second=writes.CopyObservedSet(sources,nullptr,sampled);
    Require(second && (*second)[0].verified && (*second)[0].revision_audited,
      "initial verification window ended early");
    // Observations 2 and 3 are proven and unsampled: no guest read at all.
    payload[0]=9; // Only an untrusted observation can still see this.
    const auto trusted=writes.TryReuseObservedSet(identities,sampled);
    Require(trusted && !(*trusted)[0].verified && (*trusted)[0].revision_audited &&
      (*trusted)[0].contents==sources[0].candidate && !(*trusted)[0].unreported_change,
      "proven candidate was re-read despite the sampling schedule");
    const auto third=writes.TryReuseObservedSet(identities,sampled);
    Require(third && !third->front().verified,"second unsampled observation was compared");
    Require(!writes.TryReuseObservedSet(identities,sampled) && !writes.TryReuseObservedSet(identities,sampled),
      "metadata attempts consumed or bypassed scheduled verification");
    auto trust=writes.Trust();
    Require(trust.trusted==2 && trust.verified==2 && !trust.revoked && !trust.unreported_changes,
      "trust counters did not separate compared and trusted observations");
    // Observation 4 is sampled again and must detect the uncovered write.
    const auto detected=writes.CopyObservedSet(sources,nullptr,sampled);
    Require(detected && (*detected)[0].verified && (*detected)[0].unreported_change &&
      (*detected)[0].contents!=sources[0].candidate && (*detected)[0].contents->front()==9,
      "sampled observation did not detect the unreported write");
    trust=writes.Trust();
    Require(trust.revoked && trust.unreported_changes==1,"detected miss did not revoke trust");
    sources[0].candidate=(*detected)[0].contents;
    identities[0].candidate=sources[0].candidate;
    Require(!writes.TryReuseObservedSet(identities,sampled),"revoked trust allowed metadata reuse");
    for(unsigned i=0;i<8;++i)
      Require(writes.CopyObservedSet(sources,nullptr,sampled)->front().verified,
        "revoked trust still skipped a comparison");
    // The default policy compares everything, and a candidate without a
    // baseline is compared whatever the schedule says.
    NativeBufferWrites strict;
    std::vector<uint8_t> other(32,2);
    strict.Subscribe(1,0x1000,uint32_t(other.size()));
    std::array<NativeBufferWrites::SnapshotSource,1> unproven{{{1,0x1000,other}}};
    for(unsigned i=0;i<8;++i) {
      const auto compared=strict.CopyObservedSet(unproven);
      Require(compared && (*compared)[0].verified,"default policy trusted a candidate");
      unproven[0].candidate=(*compared)[0].contents;
    }
    for(unsigned i=0;i<8;++i) {
      unproven[0].candidate=std::make_shared<const std::vector<uint8_t>>(other.begin(),other.end());
      const auto fresh=strict.CopyObservedSet(unproven,nullptr,sampled);
      Require(fresh && (*fresh)[0].verified && !(*fresh)[0].revision_audited,
        "a candidate without a baseline was trusted");
    }
    Require(!strict.Trust().revoked && strict.Trust().trusted==0,
      "strict queue reported trusted observations");
    // A reused owner lifetime restarts the initial verification window.
    strict.Subscribe(1,0x1000,uint32_t(other.size()));
    const auto reused=strict.CopyObservedSet(unproven,nullptr,sampled);
    Require(reused && (*reused)[0].verified && !(*reused)[0].revision_audited,
      "owner lifetime reuse carried a baseline");
    unproven[0].candidate=(*reused)[0].contents;
    Require(strict.CopyObservedSet(unproven,nullptr,sampled)->front().verified,
      "owner lifetime reuse did not restart the initial verification window");
    Require(!strict.CopyObservedSet(unproven,nullptr,sampled)->front().verified,
      "restarted window never reached its sampled schedule");
  }
  {
    using namespace edf::native;
    NativeBufferWrites writes;
    writes.Subscribe(1,0x1000,12);
    const auto initial=*writes.Version(1);
    bool rejected=false;
    try { NativeBufferWriteFrame::Current(); } catch(const std::runtime_error&) { rejected=true; }
    Require(rejected,"inline writer silently created missing producer frame");
    {
      NativeBufferWriteFrame outer;
      outer.Begin(1,&writes);
      Require(&NativeBufferWriteFrame::Current()==&outer && !writes.Version(1),"inline lock missed active interval");
      rejected=false;
      try { outer.Begin(1,&writes); } catch(const std::runtime_error&) { rejected=true; }
      Require(rejected,"inline writer accepted duplicate lock");
      rejected=false;
      try { outer.Finish(2); } catch(const std::runtime_error&) { rejected=true; }
      Require(rejected && !writes.Version(1),"wrong unlock ended inline transaction");
      bool isolated=false;
      std::thread other([&] {
        try { NativeBufferWriteFrame::Current(); } catch(const std::runtime_error&) { isolated=true; }
      });
      other.join(); Require(isolated,"inline producer frame crossed threads");
      {
        NativeBufferWriteFrame nested;
        nested.Begin(2,&writes);
        nested.Finish(2); nested.RequireFinished();
      }
      Require(&NativeBufferWriteFrame::Current()==&outer && !writes.Version(1),"nested inline writer cleared outer interval");
      writes.Record(0x1000,12);
      outer.Finish(1); outer.RequireFinished();
      Require(writes.Version(1).has_value(),"inline unlock leaked active scope");
    }
    Require(!writes.CommitObserved(1,initial,[] {}),"inline write reused old observation");
    writes.Drain();
    for(bool explicit_throw:{false,true}) {
      rejected=false;
      try {
        NativeBufferWriteFrame frame; frame.Begin(1,&writes);
        if(explicit_throw) throw std::runtime_error("injected inline write failure");
        frame.RequireFinished();
      } catch(const std::runtime_error&) { rejected=true; }
      Require(rejected && writes.Version(1).has_value(),"unfinished producer leaked write transaction");
      const auto aborted=writes.Drain();
      Require(aborted.all && aborted.aborted_writers==1,
        "unfinished inline producer failed to invalidate retained geometry");
    }
    rejected=false;
    try { NativeBufferWriteFrame::Current(); } catch(const std::runtime_error&) { rejected=true; }
    Require(rejected,"unwound producer retained TLS frame");
    {
      NativeBufferWriteFrame frame;
      frame.Begin(1,nullptr); frame.Finish(1); frame.RequireFinished();
    }
  }
  {
    using namespace edf::native;
    NativeBufferWrites writes;
    writes.Subscribe(1,0x1000,12);
    std::array<uint8_t,12> bytes{};
    std::mutex renderer;
    std::unique_lock renderer_lock(renderer);
    std::atomic<bool> completed=false;
    bool callback_scoped=false,renderer_seen=false;
    std::thread writer([&] {
      NativeBufferWriteFrame frame; frame.Begin(1,&writes,0x1000,NativeBufferWrites::Range{0x1000,12});
      bytes.fill(6);
      frame.Finish(1,[&](uint32_t destination) {
        callback_scoped=destination==0x1000 && !writes.Version(1);
        writes.Record(destination,12);
      });
      completed.store(true,std::memory_order_release);
      std::lock_guard lock(renderer); renderer_seen=true;
      frame.RequireFinished();
    });
    while(!completed.load(std::memory_order_acquire)) std::this_thread::yield();
    const auto snapshot=writes.CopyObserved(1,0x1000,bytes);
    const auto batch=writes.Drain();
    renderer_lock.unlock(); writer.join();
    Require(callback_scoped && renderer_seen && snapshot && snapshot->contents->front()==6 &&
      batch.count==1 && batch.ranges[0].address==0x1000 && batch.ranges[0].bytes==12,
      "inline completion retained writer while waiting for renderer bookkeeping");
    try {
      NativeBufferWriteFrame frame; frame.Begin(1,&writes,0x1000);
      frame.Finish(1,[](uint32_t) { throw std::runtime_error("completion failure"); });
    } catch(const std::runtime_error&) {}
    const auto aborted=writes.Drain();
    Require(aborted.all && aborted.aborted_writers==1 && writes.Version(1).has_value(),
      "failed inline completion released scope without invalidation");
  }
  {
    using namespace edf::native;
    NativeBufferWrites writes;
    std::array<uint8_t,12> payload{};
    std::array<uint8_t,1> adjacent{};
    writes.Subscribe(1,0x1000,12); writes.Subscribe(2,0x100c,1);
    NativeBufferWriteFrame frame;
    frame.Begin(1,&writes,0xc0001000,NativeBufferWrites::Range{0x1000,12});
    Require(writes.CopyObserved(2,0x100c,adjacent).has_value(),
      "exact inline payload blocks adjacent geometry");
    NativeBufferWrites::SnapshotFailure failure;
    const std::array<NativeBufferWrites::SnapshotSource,1> source{{{1,0x1000,payload}}};
    Require(!writes.CopyObservedSet(source,&failure) && failure.overlapping_writers==1 &&
      failure.unknown_writers==0,"exact inline payload lost overlap exclusion");
    frame.Finish(1,[&](uint32_t destination) {
      Require(destination==0xc0001000 && !writes.CopyObservedSet(source),
        "inline frame confused guest destination with physical extent or ended early");
      payload.fill(7); writes.Record(0x1000,12);
    });
    frame.RequireFinished();
    Require(writes.CopyObserved(1,0x1000,payload)->contents->front()==7,
      "exact inline payload unavailable after completion");
  }
  {
    using namespace edf::native;
    NativeBufferWrites writes;
    auto hit=[&](uint32_t address,uint32_t bytes=1) {
      writes.Record(address,bytes,true); return writes.Drain().generated_subscribed_calls;
    };
    Require(hit(0x1000)==0,"unsubscribed page hit");
    {
      NativeModelBuffers buffers(&writes);
      buffers.Publish(1,NativeModelBuffers::Kind::Vertex,0xc0001ff0,4,8,0x1ff0);
      buffers.Publish(2,NativeModelBuffers::Kind::Index,0xa0002000,2,4,0x2000);
      Require(hit(0x1000)==1 && hit(0x2fff)==1 && hit(0x3000)==0,
        "subscription page coverage/false-positive contract");
      Require(hit(0x0fff,2)==1,"cross-page write missed subscription");
      buffers.Retire(1);
      Require(hit(0x1000)==0 && hit(0x2000)==1,"alias retirement removed shared page");
      buffers.Publish(2,NativeModelBuffers::Kind::Index,0xe0004000,2,4,0x4000);
      Require(hit(0x2000)==0 && hit(0x4000)==1,"republish subscription retained old page");
      buffers.Publish(2,NativeModelBuffers::Kind::Index,0x4000,2,4);
      Require(hit(0x4000)==0,"nonphysical republish retained subscription");
      buffers.Publish(3,NativeModelBuffers::Kind::Index,0xc0005000,2,4,0x5000);
      for(unsigned i=0;i<40;++i) writes.Record(0x5000,2,true);
      const auto sampled=writes.Drain();
      Require(sampled.generated_subscribed_calls==40 && sampled.subscribed_sample_count==32 &&
        !sampled.subscriptions_unknown,"subscribed sampling counts/bound");
      Require(sampled.subscribed_samples[0].address==0x5000 && sampled.count==1,
        "subscribed sampling changed original extent/coverage");
    }
    Require(hit(0x5000)==0,"registry destruction retained subscription");
    Require(hit(UINT32_MAX)==1,"unknown extent not conservatively classified");
    writes.Subscribe(9,0x1ffffffc,4);
    Require(hit(0x1fffffff)==1,"physical-end subscription missing");
    writes.Unsubscribe(9);
    writes.Unsubscribe(9);
    Require(hit(0x1fffffff)==0,"idempotent retirement underflowed page refs");
  }
  {
    using namespace edf::native;
    NativeBufferWrites writes;
    writes.Record(0x1000,4,true); writes.Record(0x1002,8);
    for(unsigned i=0;i<40;++i) writes.Record(0x1000,4,true);
    auto batch=writes.Drain();
    Require(batch.generated_calls==41 && batch.provider_calls==1 && batch.generated_sample_count==32,
      "write provenance counts/sample bound");
    Require(batch.count==1 && batch.ranges[0].bytes==10 && batch.generated_samples[0].bytes==4,
      "write provenance changed union or original sample extent");
    auto empty=writes.Drain();
    Require(empty.generated_calls==0 && empty.provider_calls==0 && empty.generated_sample_count==0,
      "write provenance survived drain");
    writes.Record(UINT32_MAX,1,true); auto invalid=writes.Drain();
    Require(invalid.all && invalid.generated_calls==1,"invalid write lost provenance");
    NativeModelBuffers buffers;
    buffers.Publish(1,NativeModelBuffers::Kind::Vertex,0xc0001000,4,16,0x1000);
    buffers.Publish(2,NativeModelBuffers::Kind::Index,0xc0001020,2,4,0x1020);
    std::vector<uint32_t> owners;
    buffers.VisitPhysicalOverlaps(0x1021,1,[&](uint32_t owner,const auto&) { owners.push_back(owner); });
    std::sort(owners.begin(),owners.end());
    Require(owners==std::vector<uint32_t>({1,2}),"provenance query lost nested aliases");
    Require(buffers.Find(1,NativeModelBuffers::Kind::Vertex)->notified_updates==0,
      "provenance query invalidated model");
    owners.clear();
    buffers.VisitPhysicalOverlaps(0x1040,1,[&](uint32_t owner,const auto&) { owners.push_back(owner); });
    buffers.VisitPhysicalOverlaps(UINT32_MAX,1,[&](uint32_t owner,const auto&) { owners.push_back(owner); });
    Require(owners.empty(),"provenance query accepted adjacency/invalid extent");
    buffers.Retire(2);
    buffers.Publish(1,NativeModelBuffers::Kind::Vertex,0xc0002000,4,16,0x2000);
    buffers.VisitPhysicalOverlaps(0x1021,1,[&](uint32_t owner,const auto&) { owners.push_back(owner); });
    Require(owners.empty(),"provenance query retained old owner interval");
  }
  {
    using Buffers=edf::native::NativeModelBuffers;
    edf::native::NativeBufferWrites sites;
    sites.Subscribe(1,0x1000,16);
    sites.Subscribe(2,0x1008,8);
    sites.Record(0x2000,4,true,{"writer.cpp",7});
    Require(sites.Drain().writer_hit_count==0,"unrelated write entered geometry writer inventory");
    sites.Record(0x1008,4,true,{"writer.cpp",7});
    sites.Record(0x1008,4,true,{"writer.cpp",7});
    sites.Unsubscribe(1);
    sites.Subscribe(1,0x1000,16);
    sites.Record(0x1008,4,true,{"writer.cpp",7});
    const auto hits=sites.Drain();
    Require(hits.writer_hit_count==3 && !hits.writer_hits_omitted,
      "writer attribution lost alias or reused lifetime");
    uint64_t hit_calls=0; unsigned first_lifetimes=0;
    for(size_t i=0;i<hits.writer_hit_count;++i) {
      const auto& hit=hits.writer_hits[i]; hit_calls+=hit.calls;
      Require(hit.site.line==7 && std::string_view(hit.site.file)=="writer.cpp" &&
        hit.first_range.address==0x1008 && hit.first_range.bytes==4,"writer source/range identity changed");
      if(hit.owner==1) ++first_lifetimes;
    }
    Require(hit_calls==6 && first_lifetimes==2,"writer deduplication lost overlap calls or lifetimes");
    Require(sites.Drain().writer_hit_count==0,"drain retained writer samples");
    sites.Record(0x1008,4,false,{nullptr,0,0x821e8320,0x821d1234});
    sites.Record(0x1008,4,false,{nullptr,0,0x821e8320,0x821d1234});
    sites.Record(0x1008,4,false,{nullptr,0,0x821e8320,0x821d5678});
    sites.Record(0x1008,4,false,{nullptr,0,0x821e8740,0x821d1234});
    const auto providers=sites.Drain();
    Require(providers.writer_hit_count==6 && providers.provider_exact_owner_hits==8 &&
      providers.generated_exact_owner_hits==0,"provider attribution merged callers/providers or lost aliases");
    for(size_t i=0;i<providers.writer_hit_count;++i) {
      const auto& hit=providers.writer_hits[i];
      Require(!hit.site.file && !hit.site.line && hit.site.provider && hit.site.caller &&
        hit.calls==unsigned(hit.site.provider==0x821e8320 && hit.site.caller==0x821d1234?2:1),
        "provider identity was recorded as a generated source line");
    }
    sites.Unsubscribe(2);
    for(uint32_t line=1;line<=65;++line) sites.Record(0x1000,4,true,{"writer.cpp",line});
    const auto overflow=sites.Drain();
    Require(overflow.writer_hit_count==64 && overflow.writer_hits_omitted==1 &&
      overflow.generated_exact_owner_hits==65,"writer inventory silently dropped overflow or update coverage");
  }
  {
    using Buffers=edf::native::NativeModelBuffers;
    edf::native::NativeBufferWrites writes;
    Buffers buffers;
    buffers.Publish(0x1000,Buffers::Kind::Vertex,0xf0001000,40,4,0x1000);
    buffers.Publish(0x1100,Buffers::Kind::Index,0xc0001200,2,6,0x1200);
    std::vector<uint32_t> dirty;
    auto drain=[&] { buffers.ApplyWrites(writes.Drain(),[&](uint32_t owner) { dirty.push_back(owner); }); };
    const auto vertex_extent=edf::native::DecodeNativeBufferUpdateExtent(0xc0001003,0x180000a2,false);
    Require(vertex_extent.address==0xc0001000 && vertex_extent.bytes==160,
      "vertex unlock extent retained address tags or size flags");
    const auto index_extent=edf::native::DecodeNativeBufferUpdateExtent(0xc0001200,12,true);
    Require(index_extent.address==0xc0001200 && index_extent.bytes==12,
      "index unlock extent changed plain address/size");
    Require(!buffers.NotifyUpdateAliases(0x9999,[&](uint32_t owner) { dirty.push_back(owner); }),
      "unregistered header unexpectedly resolved by owner");
    // Simulate the mapped physical extent of an unregistered alias header.
    writes.Record(vertex_extent.address-0xc0000000,vertex_extent.bytes); drain();
    Require(dirty==std::vector<uint32_t>{0x1000},"unregistered alias extent missed model ownership");
    dirty.clear();
    const auto baseline_updates=buffers.Find(0x1000,Buffers::Kind::Vertex)->notified_updates;
    writes.Record(0x10a0,0x100); drain();
    Require(dirty.empty(),"same-page nonoverlapping write dirtied model buffer");
    writes.Record(0x0fff,2); writes.Record(0x1001,1); drain();
    Require(dirty==std::vector<uint32_t>{0x1000},"physical alias/overlap batch notification");
    Require(buffers.Find(0x1000,Buffers::Kind::Vertex)->notified_updates==baseline_updates+1,"duplicate ranges counted twice in one batch");
    dirty.clear(); drain(); Require(dirty.empty() && !writes.Pending(),"drain replayed old writes");
    writes.Record(0x109f,0x162); drain();
    Require(dirty==std::vector<uint32_t>({0x1000,0x1100}),"write spanning two resource owners lost notification");
    dirty.clear();
    std::array<std::thread,4> producers;
    for(auto& producer:producers) producer=std::thread([&] { for(int i=0;i<100;++i) writes.Record(0x8000,4); });
    for(auto& producer:producers) producer.join();
    const auto repeated=writes.Drain();
    Require(!repeated.all && repeated.count==1 && repeated.ranges[0].address==0x8000 && repeated.ranges[0].bytes==4,
      "concurrent duplicate writes exhausted interval queue");
    for(uint32_t i=0;i<257;++i) writes.Record(0x8000+i*8,4);
    writes.Record(0xafff,2); // Subsequent writes in page mode span both pages.
    writes.Record(0x1fffffff,1);
    const auto overflow=writes.Drain();
    Require(!overflow.all && overflow.pages && !writes.Pending(),"valid overflow did not switch to bounded pages");
    Require(overflow.pages->count()==4 && overflow.pages->test(8) && overflow.pages->test(10) &&
      overflow.pages->test(11) && overflow.pages->test(edf::native::NativeBufferWrites::kPageCount-1),
      "overflow bitmap lost earlier/later writes or marked unrelated pages");
    buffers.Publish(0x1200,Buffers::Kind::Vertex,0xf0008ffc,4,2,0x8ffc);
    buffers.ApplyWrites(overflow,[&](uint32_t owner) { dirty.push_back(owner); });
    Require(dirty==std::vector<uint32_t>{0x1200},"page overflow invalidated unrelated owners or missed spanning owner");
    Require(buffers.Find(0x1200,Buffers::Kind::Vertex)->notified_updates==1,"page overflow counted owner twice");
    writes.Record(0x9000,1);
    const auto fresh=writes.Drain();
    Require(!fresh.pages && fresh.count==1 && !overflow.pages->test(9),"drain reused/mutated published page bitmap");
    buffers.Retire(0x1200);
    dirty.clear();
    writes.Record(UINT32_MAX,1); Require(writes.Drain().all,"invalid physical extent lost conservative notification");
    for(uint32_t i=0;i<256;++i) writes.Record(0x8000+i*8,4);
    writes.Record(0x8000,4); // Duplicate at full capacity must not overflow.
    writes.Record(0x8004,2040); // Bridge all gaps exactly, preserving both ends.
    const auto merged=writes.Drain();
    Require(!merged.all && merged.count==1 && merged.ranges[0].address==0x8000 && merged.ranges[0].bytes==2044,
      "full queue bridging merge lost extent or falsely overflowed");
    writes.Record(0x1ffffffc,4); writes.Record(0x1ffffffb,1);
    const auto boundary=writes.Drain();
    Require(!boundary.all && boundary.count==1 && boundary.ranges[0].bytes==5,"physical end adjacency overflowed");
    writes.Record(0x1ffffffc,5); Require(writes.Drain().all,"physical end overrun accepted");
    // Independent per-byte union oracle, including unordered overlaps and gaps.
    for(uint32_t pass=0;pass<40;++pass) {
      std::array<bool,1024> expected{},actual{};
      for(uint32_t i=0;i<150;++i) {
        const auto start=(i*97+pass*31)%990,bytes=(i*13+pass)%35;
        writes.Record(0x4000+start,bytes);
        for(uint32_t j=0;j<bytes;++j) expected[start+j]=true;
      }
      const auto batch=writes.Drain();
      Require(!batch.all,"small union unexpectedly overflowed");
      for(size_t i=0;i<batch.count;++i) {
        const auto& range=batch.ranges[i];
        Require(range.address>=0x4000 && uint64_t(range.address)+range.bytes<=0x4400,"coalesced extent outside oracle");
        if(i) Require(batch.ranges[i-1].address+batch.ranges[i-1].bytes<range.address,"intervals not disjoint/nonadjacent");
        for(uint32_t j=0;j<range.bytes;++j) actual[range.address-0x4000+j]=true;
      }
      Require(actual==expected,"coalesced writes lost bytes or filled unwritten gaps");
    }
    Buffers indexed;
    std::map<uint32_t,std::pair<uint32_t,uint32_t>> reference;
    for(uint32_t i=0;i<96;++i) {
      const auto owner=0x4000+i*64,physical=0x1000+i*8,bytes=4*(1+(i*17)%80);
      indexed.Publish(owner,Buffers::Kind::Vertex,0xf0000000+physical,4,bytes/4,physical);
      reference[owner]={physical,bytes};
    }
    for(uint32_t pass=0;pass<2;++pass) {
      if(pass) for(uint32_t i=0;i<96;i+=3) {
        const auto owner=0x4000+i*64;
        indexed.RetireAllocation(owner+32); reference.erase(owner);
        if(i%2) {
          indexed.Publish(owner,Buffers::Kind::Vertex,0xf0008000+i*4,4,2,0x8000+i*4);
          reference[owner]={0x8000+i*4,8};
        }
      }
      for(uint32_t i=0;i<120;++i) {
        const uint32_t start=pass && i>60?0x8000+(i-60)*7:0xff0+i*9,length=(i*13)%47;
        writes.Record(start,length); std::vector<uint32_t> actual,expected;
        indexed.ApplyWrites(writes.Drain(),[&](uint32_t owner) { actual.push_back(owner); });
        for(const auto& [owner,range]:reference)
          if(length && start<range.first+range.second && range.first<start+length) expected.push_back(owner);
        Require(actual==expected,"indexed write lookup disagreed with exact overlapping owner ranges");
      }
    }
  }
  {
    using Buffers=edf::native::NativeModelBuffers;
    Buffers buffers;
    buffers.Publish(0x1000,Buffers::Kind::Vertex,0x2000,40,4);
    const auto first=*buffers.Find(0x1000,Buffers::Kind::Vertex);
    Require(buffers.NotifyUpdate(0x1000) && !buffers.NotifyUpdate(0x9999),"explicit update owner lookup");
    Require(buffers.Find(0x1000,Buffers::Kind::Vertex)->notified_updates==1 &&
      buffers.Find(0x1000,Buffers::Kind::Vertex)->generation==first.generation,
      "content update changed lifetime generation or lost notification");
    Require(first.address==0x2000 && first.bytes==160 && first.stride==40,"native vertex creation metadata");
    Require(!buffers.Find(0x1000,Buffers::Kind::Index),"wrong model resource kind accepted");
    buffers.Publish(0x1100,Buffers::Kind::Index,0x3000,2,6);
    Require(buffers.Find(0x1100,Buffers::Kind::Index)->bytes==12,"native index creation metadata");
    for(const auto address:{0u,0xfffffffcu}) {
      bool rejected=false;
      try { buffers.Publish(0x1000,Buffers::Kind::Vertex,address,40,4); }
      catch(const std::runtime_error&) { rejected=true; }
      Require(rejected && buffers.Find(0x1000,Buffers::Kind::Vertex)->generation==first.generation,
        "invalid model publication replaced valid ownership");
    }
    bool overflow=false;
    try { buffers.Publish(0x1000,Buffers::Kind::Vertex,0x2000,40,UINT32_MAX); }
    catch(const std::runtime_error&) { overflow=true; }
    Require(overflow,"model size multiplication wrapped");
    buffers.Retire(0x1000); buffers.Retire(0x1000);
    Require(!buffers.Find(0x1000,Buffers::Kind::Vertex),"retired native model metadata retained");
    buffers.Publish(0x1000,Buffers::Kind::Vertex,0x4000,40,4);
    Require(buffers.Find(0x1000,Buffers::Kind::Vertex)->generation>first.generation,"owner reuse retained old generation");
    Require(buffers.Find(0x1000,Buffers::Kind::Vertex)->notified_updates==0,"owner recreation retained old content updates");
    Require(!buffers.RetireAllocation(16) && !buffers.RetireAllocation(0x1010),"unrelated pool release retired a model owner");
    Require(buffers.RetireAllocation(0x1020)==0x1000 && !buffers.Find(0x1000,Buffers::Kind::Vertex),
      "generic pool release retained native vertex owner");
    Require(!buffers.RetireAllocation(0x1020),"repeated pool release retired an absent owner");
    Require(buffers.RetireAllocation(0x1120)==0x1100 && !buffers.Find(0x1100,Buffers::Kind::Index),
      "generic pool release retained native index owner");
  }
  {
    using Buffers=edf::native::NativeModelBuffers;
    edf::native::NativeBufferWrites writes;
    Buffers buffers(&writes);
    buffers.Publish(1,Buffers::Kind::Vertex,0xa0001000,4,16,0x1000);
    buffers.Publish(2,Buffers::Kind::Index,0xc0001020,2,32,0x1020);
    buffers.Publish(3,Buffers::Kind::Vertex,0xe0001040,4,16,0x1040);
    buffers.Publish(4,Buffers::Kind::Vertex,0x2000,4,16);
    buffers.Publish(5,Buffers::Kind::Index,0,2,0,0x1000);
    std::vector<uint32_t> retired;
    auto retire=[&](uint32_t owner) { retired.push_back(owner); };
    const auto first=*writes.Version(1),alias=*writes.Version(2),adjacent=*writes.Version(3);
    Require(buffers.NotifyUpdateAliases(1,retire) && retired==std::vector<uint32_t>{1,2},
      "explicit update missed alias or invalidated adjacent/unknown/empty storage");
    Require(!writes.CommitObserved(1,first,[] {}) && !writes.CommitObserved(2,alias,[] {}) &&
      writes.CommitObserved(3,adjacent,[] {}) && !writes.Pending(),
      "explicit alias update retained stale tokens or queued duplicate cache retirement");
    Require(writes.Version(1)->revision==first.revision+1 && writes.Version(2)->revision==alias.revision+1 &&
      writes.Version(1)->writer_epoch==first.writer_epoch,
      "explicit alias invalidation must advance content revisions, not fake a writer transaction");
    Require(buffers.Find(1,Buffers::Kind::Vertex)->notified_updates==1 &&
      buffers.Find(2,Buffers::Kind::Index)->notified_updates==1 &&
      buffers.Find(3,Buffers::Kind::Vertex)->notified_updates==0,
      "alias update counts do not match physical overlap");
    retired.clear();
    Require(buffers.NotifyUpdateAliases(2,retire) && retired==std::vector<uint32_t>{1,2,3},
      "partial overlap update missed either physical alias");
    retired.clear();
    Require(buffers.NotifyUpdateAliases(4,retire) && buffers.NotifyUpdateAliases(5,retire) &&
      !buffers.NotifyUpdateAliases(99,retire) && retired==std::vector<uint32_t>{4,5},
      "unmapped/empty/unknown explicit update handling");
    buffers.Retire(2);
    buffers.Publish(2,Buffers::Kind::Index,0xc0003000,2,32,0x3000);
    const auto republished=*writes.Version(2);
    retired.clear(); buffers.NotifyUpdateAliases(1,retire);
    Require(retired==std::vector<uint32_t>{1},"republished owner retained stale physical alias interval");
    Require(writes.CommitObserved(2,republished,[] {}),"old alias invalidation touched republished storage");
  }
  {
    using Buffers=edf::native::NativeModelBuffers;
    edf::native::NativeBufferWrites writes;
    Buffers buffers(&writes);
    buffers.Publish(0x1000,Buffers::Kind::Vertex,0xa0004000,4,32,0x4000);
    buffers.Publish(0x2000,Buffers::Kind::Index,0xc0004020,2,16,0x4020);
    buffers.Publish(0x3000,Buffers::Kind::Vertex,0xe0004080,4,16,0x4080);
    const auto original=*writes.Version(0x1000),alias=*writes.Version(0x2000),adjacent=*writes.Version(0x3000);
    std::vector<uint32_t> retired;
    auto retire=[&](uint32_t owner) { retired.push_back(owner); };
    Require(!buffers.RetireAllocationAliases(31,retire) && !buffers.RetireAllocationAliases(0x9020,retire) &&
      retired.empty(),"unknown allocation release retired unrelated geometry");
    Require(buffers.RetireAllocationAliases(0x1020,retire)==0x1000,
      "allocation alias release lost original owner");
    std::sort(retired.begin(),retired.end());
    Require(retired==std::vector<uint32_t>{0x1000,0x2000} &&
      !buffers.Find(0x1000,Buffers::Kind::Vertex) && !buffers.Find(0x2000,Buffers::Kind::Index) &&
      buffers.Find(0x3000,Buffers::Kind::Vertex),"allocation release missed alias or retired adjacent block");
    Require(!writes.CommitObserved(0x1000,original,[] {}) && !writes.CommitObserved(0x2000,alias,[] {}) &&
      writes.CommitObserved(0x3000,adjacent,[] {}),"released aliases retained observation tokens");
    Require(!buffers.RetireAllocationAliases(0x1020,retire) && retired.size()==2,
      "repeated allocation release repeated cache retirement");
    buffers.Publish(0x2000,Buffers::Kind::Index,0xc0004020,2,16,0x4020);
    Require(!writes.CommitObserved(0x2000,alias,[] {}) && writes.Version(0x2000)->lifetime!=alias.lifetime,
      "physical address reuse accepted retired alias generation");
    buffers.Publish(0x1000,Buffers::Kind::Vertex,0xa0004000,4,32,0x4000);
    retired.clear();
    Require(buffers.RetireBackingAliases(0x1000,retire)==0x1000 &&
      !buffers.RetireAllocationAliases(0x1020,retire),
      "normal cleanup followed by pool release changed retirement ordering");
    std::sort(retired.begin(),retired.end());
    Require(retired==std::vector<uint32_t>{0x1000,0x2000},
      "normal cleanup erased extent before retiring backing aliases");
    buffers.Publish(0x2000,Buffers::Kind::Index,0xc0004020,2,16,0x4020);
    buffers.Publish(0x4000,Buffers::Kind::Vertex,0x8000,4,16);
    buffers.Publish(0x5000,Buffers::Kind::Index,0,2,0,0x4000);
    retired.clear();
    Require(buffers.RetireAllocationAliases(0x4020,retire)==0x4000 &&
      buffers.RetireAllocationAliases(0x5020,retire)==0x5000 &&
      retired==std::vector<uint32_t>{0x4000,0x5000} && buffers.Find(0x2000,Buffers::Kind::Index),
      "unmapped/empty release incorrectly used physical overlap");
  }
  {
    struct Reader {
      std::map<uint32_t,uint32_t> words;
      uint32_t Word(uint32_t address) const { return words.at(address); }
      uint32_t Add(uint32_t address,uint32_t offset) const {
        if(uint64_t(address)+offset>UINT32_MAX) throw std::runtime_error("pool address overflow");
        return address+offset;
      }
    } reader{{{0x104,0x200},{0x200,0x200}}};
    Require(edf::native::ReadNativePoolBackings(reader,0x100).empty(),"empty pool visited sentinel payload");
    reader.words[0x200]=0x300; reader.words[0x300]=0x400; reader.words[0x308]=0xa0004000;
    reader.words[0x400]=0x500; reader.words[0x408]=0; reader.words[0x500]=0x200; reader.words[0x508]=0xc0008000;
    Require(edf::native::ReadNativePoolBackings(reader,0x100)==std::vector<uint32_t>{0xa0004000,0xc0008000},
      "pool collector lost order or treated null backing as live allocation");
    for(uint32_t malformed:{0u,0x300u,0x400u}) {
      reader.words[0x500]=malformed;
      bool rejected=false;
      try { (void)edf::native::ReadNativePoolBackings(reader,0x100); }
      catch(const std::runtime_error&) { rejected=true; }
      Require(rejected,"pool collector accepted null/cyclic node chain");
    }
    reader.words[0x30c]=0x100;
    const auto block=edf::native::ReadNativePoolBlock(reader,0xf8,(uint64_t(0x100)<<32)|0x300);
    Require(block.address==0xa0004000 && block.bytes==0x100,"pool block iterator decoded wrong payload/size");
    for(const auto iterator:{uint64_t(0),uint64_t(0x100)<<32,(uint64_t(0x100)<<32)|0x200,
                            (uint64_t(0x108)<<32)|0x300}) {
      bool invalid=false;
      try { (void)edf::native::ReadNativePoolBlock(reader,0xf8,iterator); }
      catch(const std::runtime_error&) { invalid=true; }
      Require(invalid,"invalid pool block iterator accepted");
    }
    using Buffers=edf::native::NativeModelBuffers;
    edf::native::NativeBufferWrites writes;
    Buffers buffers(&writes);
    buffers.Publish(1,Buffers::Kind::Vertex,0xa0004020,4,16,0x4020);
    buffers.Publish(2,Buffers::Kind::Index,0xc0004030,2,8,0x4030);
    buffers.Publish(3,Buffers::Kind::Vertex,0xe0004100,4,16,0x4100);
    const auto adjacent=*writes.Version(3),stale=*writes.Version(2);
    std::vector<uint32_t> retired;
    // No wrapper at block.address exists: the only registered owners are
    // interior aliases. Retire using the decoded suballocation, not owner-32.
    buffers.RetirePhysicalRange(block.address-0xa0000000u,block.bytes,[&](uint32_t owner) { retired.push_back(owner); });
    std::sort(retired.begin(),retired.end());
    Require(retired==std::vector<uint32_t>{1,2} && !writes.Version(1) && !writes.Version(2) &&
      writes.CommitObserved(3,adjacent,[] {}),"whole backing retirement missed interior aliases or retired neighbor");
    buffers.Publish(2,Buffers::Kind::Index,0xc0004030,2,8,0x4030);
    Require(!writes.CommitObserved(2,stale,[] {}),"whole backing retirement crossed physical address reuse");
    bool rejected=false;
    try { buffers.RetirePhysicalRange(UINT32_MAX,2,[](uint32_t) {}); }
    catch(const std::runtime_error&) { rejected=true; }
    Require(rejected && buffers.Find(2,Buffers::Kind::Index),"invalid backing extent changed ownership");
  }
  try {
    {
      struct Reader {
        std::map<uint32_t,uint32_t> words;
        uint32_t Word(uint32_t at) const { return words.at(at); }
        uint32_t Add(uint32_t at,uint32_t offset) const { return at+offset; }
      } reader{{{0x100,1},{0x108,0x200},{0x200,1u<<22},{0x21c,0x1000},
                {0x240,0x300},{0x300,2u<<22},{0x31c,0x3000}}};
      std::vector<std::pair<uint32_t,uint32_t>> ranges;
      auto visit=[&](uint32_t start,uint32_t length) { ranges.emplace_back(start,length); };
      Require(edf::native::VisitGuestAudioOutputRanges(reader,0x100,visit)==2 &&
        ranges==std::vector<std::pair<uint32_t,uint32_t>>{{0x1000,256},{0x3000,512}},
        "audio exclusion must cover different pending and live output buffers");
      reader.words[0x240]=0; ranges.clear();
      Require(edf::native::VisitGuestAudioOutputRanges(reader,0x100,visit)==1,
        "deferred audio context lost pending output exclusion");
      reader.words[0x200]=0; ranges.clear();
      Require(edf::native::VisitGuestAudioOutputRanges(reader,0x100,visit)==0,
        "zero-capacity output should not exclude memory");
      reader.words[0x200]=31u<<22; reader.words[0x21c]=0x1fffffff;
      bool rejected=false;
      try { edf::native::VisitGuestAudioOutputRanges(reader,0x100,visit); }
      catch(const std::runtime_error&) { rejected=true; }
      Require(rejected,"overflowing audio output range admitted");
    }
    using namespace rex::memory;
    using edf::native::GuestHeapCommittedReadable;
    using edf::native::GuestVirtualHeapCommittedWritable;
    Memory memory; Require(memory.Initialize(),"SDK memory initialization");
    // Virtual, XEX and all three guest physical alias heaps. Fresh allocations
    // remain owned by this isolated Memory instance, never by the live game.
    for(uint32_t probe:{0x40000000u,0x80000000u,0xa0000000u,0xc0000000u,0xe0000000u}) {
      auto* heap=memory.LookupHeap(probe); Require(heap!=nullptr,"SDK heap lookup");
      const uint32_t page=heap->page_size();
      uint32_t address=0;
      Require(heap->Alloc(page*3,page,kMemoryAllocationReserve|kMemoryAllocationCommit,
        kMemoryProtectRead|kMemoryProtectWrite,false,&address),"allocate test pages");
      auto check=[&](uint32_t at,size_t size,bool expected) {
        const bool readable=GuestHeapCommittedReadable(*heap,at,size);
        Require(readable==expected,"unexpected SDK committed/readable range");
        Require(readable==QueriedHeapAdmits(*heap,at,size,kMemoryProtectRead),
          "page-table read probe disagrees with QueryRegionInfo");
        if(heap->heap_type()==HeapType::kGuestVirtual)
          Require(GuestVirtualHeapCommittedWritable(*heap,at,size)==
            QueriedHeapAdmits(*heap,at,size,kMemoryProtectRead|kMemoryProtectWrite),
            "page-table write probe disagrees with QueryRegionInfo");
        if(readable) Require(HostReadable(memory.TranslateVirtual(at),size),
          "SDK fast read admitted a host-inaccessible range");
      };
      check(address,3*page,true);
      const bool virtual_heap=heap->heap_type()==HeapType::kGuestVirtual;
      Require(GuestVirtualHeapCommittedWritable(*heap,address,3*page)==virtual_heap,
        "fast CPU write validation must admit only ordinary virtual heaps");
      if(virtual_heap) Require(HostReadable(memory.TranslateVirtual(address),3*page,true),
        "SDK CPU write proof admitted host read-only memory");
      check(address+page-1,page+2,true);
      if(heap->heap_type()==HeapType::kGuestPhysical) {
        const auto mapped=edf::native::MapCompletedNativePhysicalWrite(memory,address+page-1,page+2);
        Require(mapped && !mapped->all && mapped->address==memory.GetPhysicalAddress(address+page-1) &&
          mapped->bytes==page+2,"actual SDK alias mapping lost a cross-page write extent");
        edf::native::NativeBufferWrites alias_writes;
        edf::native::NativeModelBuffers alias_buffers(&alias_writes);
        alias_buffers.Publish(1,edf::native::NativeModelBuffers::Kind::Vertex,address,4,page*3/4,
          memory.GetPhysicalAddress(address));
        alias_writes.Record(mapped->address,mapped->bytes);
        unsigned invalidations=0;
        alias_buffers.ApplyWrites(alias_writes.Drain(),[&](uint32_t owner) {
          Require(owner==1,"mapped alias invalidated wrong owner"); ++invalidations;
        });
        Require(invalidations==1,"actual SDK mapped write failed native owner invalidation");
        HeapAllocationInfo backing{};
        Require(heap->QueryRegionInfo(address,&backing) && backing.allocation_base==address &&
          backing.allocation_size==page*3,"SDK backing allocation extent mismatch");
        const auto backing_extent=edf::native::MapCompletedNativePhysicalWrite(memory,address,backing.allocation_size);
        Require(backing_extent && !backing_extent->all,"SDK whole-backing mapping failed");
        unsigned backing_retirements=0;
        alias_buffers.RetirePhysicalRange(backing_extent->address,backing_extent->bytes,
          [&](uint32_t owner) { Require(owner==1,"backing retired wrong owner"); ++backing_retirements; });
        Require(backing_retirements==1 && !alias_writes.Version(1),"SDK backing extent failed ownership retirement");
        const auto crossing=edf::native::MapCompletedNativePhysicalWrite(memory,
          uint32_t(uint64_t(heap->heap_base())+heap->heap_size()-1),2);
        Require(crossing && crossing->all,"cross-heap completed write was silently dropped");
        alias_writes.Record(crossing->address,crossing->bytes);
        Require(alias_writes.Drain().all,"malformed mapped extent lost conservative invalidation");
        Require(!edf::native::MapCompletedNativePhysicalWrite(memory,address,0),
          "empty write generated a physical invalidation");
        CheckPhysicalWriteWatch(memory,address,page);
        memory.EnablePhysicalMemoryAccessCallbacks(memory.GetPhysicalAddress(address),page,true,false);
        check(address,page,true); // Write watches must not remove read access.
        Require(!GuestVirtualHeapCommittedWritable(*heap,address,page),"physical write watch admitted by SDK CPU write proof");
      } else Require(!edf::native::MapCompletedNativePhysicalWrite(memory,address,page),
        "nonphysical SDK heap entered physical write registry");
      Require(heap->Protect(address+page,page,kMemoryProtectRead),"protect read-only middle page");
      Require(!GuestVirtualHeapCommittedWritable(*heap,address+page-1,page+2),"read-only middle page admitted for CPU write");
      Require(GuestVirtualHeapCommittedWritable(*heap,address,1)==virtual_heap,"unmodified writable first page rejected");
      check(address+page-1,page+2,true);
      Require(heap->Protect(address+page,page,kMemoryProtectNoAccess),"protect noaccess middle page");
      check(address+page-1,page+2,false);
      check(address,1,true); check(address+2*page,1,true);
      Require(heap->Protect(address+page,page,kMemoryProtectRead|kMemoryProtectWrite),"restore middle read/write access");
      Require(GuestVirtualHeapCommittedWritable(*heap,address+page,1)==virtual_heap,"restored writable page not refreshed");
      Require(heap->Decommit(address+page,page),"decommit readable middle page");
      Require(!GuestVirtualHeapCommittedWritable(*heap,address+page,1),"decommitted page admitted for CPU write");
      check(address+page-1,page+2,false);
      check(address+page,1,false);
      Require(heap->AllocFixed(address+page,page,page,kMemoryAllocationCommit,kMemoryProtectRead),"recommit middle page");
      check(address+page-1,page+2,true);
      Require(heap->Release(address),"release test allocation");
      Require(!GuestVirtualHeapCommittedWritable(*heap,address,1),"released page admitted for CPU write");
      check(address,1,false);
      Require(heap->Alloc(page,page,kMemoryAllocationReserve,kMemoryProtectRead|kMemoryProtectWrite,false,&address),
        "reserve-only readable page");
      check(address,1,false);
      Require(!GuestVirtualHeapCommittedWritable(*heap,address,1),"reserve-only read/write page admitted");
      Require(heap->Release(address),"release reserved page");
      {
        // Unlocked probes racing Protect/Decommit/AllocFixed on another thread
        // see the page before or after each change, never a torn state.
        Require(heap->Alloc(page*2,page,kMemoryAllocationReserve|kMemoryAllocationCommit,
          kMemoryProtectRead|kMemoryProtectWrite,false,&address),"allocate race pages");
        std::atomic<bool> done{false};
        std::thread mutator([&] {
          for(int i=0;i<400;++i) {
            heap->Protect(address+page,page,i&1?kMemoryProtectRead:kMemoryProtectNoAccess);
            if(!(i%16)) {
              heap->Decommit(address+page,page);
              heap->AllocFixed(address+page,page,page,kMemoryAllocationCommit,kMemoryProtectRead|kMemoryProtectWrite);
            }
          }
          done=true;
        });
        uint64_t admitted=0,rejected=0;
        while(!done) {
          Require(GuestHeapCommittedReadable(*heap,address,page),"untouched first page rejected during race");
          (GuestHeapCommittedReadable(*heap,address+page,1)?admitted:rejected)++;
        }
        mutator.join();
        Require(heap->Release(address),"release race pages");
        Require(!GuestHeapCommittedReadable(*heap,address,1),"released race page admitted");
        std::cout<<"heap "<<std::hex<<probe<<" race probes admitted="<<std::dec<<admitted<<" rejected="<<rejected<<'\n';
      }
      Require(!GuestHeapCommittedReadable(*heap,heap->heap_base()+heap->heap_size()-1,2),
        "cross-heap range admitted");
      Require(!GuestVirtualHeapCommittedWritable(*heap,heap->heap_base()+heap->heap_size()-1,2) &&
        !GuestVirtualHeapCommittedWritable(*heap,UINT32_MAX,2) &&
        !GuestVirtualHeapCommittedWritable(*heap,0,1),"invalid CPU write range admitted");
      std::cout<<"heap "<<std::hex<<probe<<" commitment/protection boundaries passed\n";
    }
    Require(GetModuleHandleW(L"rexgpu-xenos.dll")==nullptr,"memory test loaded Xenos");
    return 0;
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n'; return 1;}
}
