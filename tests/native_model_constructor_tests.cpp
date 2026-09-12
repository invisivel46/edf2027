#include "native_graphics/native_model_header.h"
#include "native_graphics/native_model_cleanup.h"
#include "native_graphics/native_index_binding.h"
#include "native_graphics/native_stream_binding.h"
#include "native_graphics/native_fence_poll.h"
#include "native_graphics/native_fence_records.h"
#include "native_graphics/native_buffer_writes.h"
#include "native_graphics/native_allocator_wait.h"
#include "native_graphics/native_descriptor_submit.h"
#include "native_graphics/native_submission_observers.h"
#include "native_graphics/native_submission_dispatch.h"
#include "native_graphics/native_ring_cursor.h"
#include "native_graphics/native_submission_cursors.h"
#include "native_graphics/native_model_buffers.h"
#include "native_graphics/native_pool_backings.h"
#include <atomic>
#include <cstring>
#include <iostream>
#include <vector>

// Guest calls and memory services are fixtures, but orchestration below is
// extracted from production. This is not execution of the retail allocator.
union Register { uint64_t u64; uint32_t u32; Register():u64(0) {} };
struct PPCContext { Register r3,r4,r5,r6; uint64_t lr=0,nonvolatile=0; };
static std::array<uint8_t,32768> memory;
static std::vector<unsigned> events;
static bool index_case,fail_allocation,fail_copy,fail_publication;
static bool allocation_returns_failure=false;
static uint32_t allocation_address=4096;
static uint32_t stride,count;
static constexpr uint32_t owner=128,source=1024,payload=4096;
static void Require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
namespace edf::native {
class GuestReader {
 public:
  explicit GuestReader(uint8_t* base) { Require(base==memory.data(),"wrong memory base"); }
  const uint8_t* Bytes(uint32_t address,size_t bytes) const {
    Require(address<=memory.size() && bytes<=memory.size()-address,"invalid fixture range");
    return memory.data()+address;
  }
  uint32_t Add(uint32_t address,uint32_t offset) const {
    Require(uint64_t(address)+offset<memory.size(),"invalid fixture address"); return address+offset;
  }
  const uint8_t* WritableBytes(uint32_t address,size_t bytes,size_t alignment) const {
    // Mirror the production checked-write contract that rejected alignment 1.
    Require(bytes && (alignment==4 || alignment==8) && address%alignment==0 &&
      address<=memory.size() && bytes<=memory.size()-address,"invalid checked write");
    return memory.data()+address;
  }
  uint32_t Word(uint32_t address) const {
    Require(address<=memory.size()-4,"invalid fixture word");
    const auto* p=memory.data()+address;
    return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];
  }
  void StoreWord(uint32_t address,uint32_t value) const {
    auto* p=const_cast<uint8_t*>(WritableBytes(address,4,4));
    for(unsigned i=0;i<4;++i) p[i]=uint8_t(value>>(24-8*i));
  }
  void StoreDoubleWord(uint32_t address,uint64_t value) const {
    WritableBytes(address,8,8);
    StoreWord(address,uint32_t(value>>32)); StoreWord(address+4,uint32_t(value));
  }
  uint64_t DoubleWord(uint32_t address) const { return (uint64_t(Word(address))<<32)|Word(address+4); }
  void StoreByte(uint32_t address,uint8_t value) const {
    auto* p=const_cast<uint8_t*>(WritableBytes(address&~3u,4,4)); p[address&3u]=value;
  }
  template<size_t N> void StoreCpuWords(uint32_t address,const std::array<uint32_t,N>& words) const {
    WritableBytes(address,N*4,4);
    for(size_t i=0;i<N;++i) StoreWord(address+uint32_t(i*4),words[i]);
  }
};
}
static void Clobber(PPCContext& ctx) {
  ctx.r3.u64=ctx.r4.u64=ctx.r5.u64=ctx.r6.u64=0xdeadbeef;
  ctx.lr=0xbad; ctx.nonvolatile=0xbad;
}
static void Cleanup(PPCContext& ctx,bool index) {
  Require(index==index_case && ctx.r3.u32==owner,"cleanup target");
  Require(ctx.lr==(index?0x821D76C4u:0x821D7550u),"cleanup provenance");
  Require(events.empty(),"cleanup order"); events.push_back(1); Clobber(ctx);
}
static void sub_821D75F8(PPCContext& ctx,uint8_t*) { Cleanup(ctx,true); }
static void sub_821D7468(PPCContext& ctx,uint8_t*) { Cleanup(ctx,false); }
static void sub_821D4700(PPCContext& ctx,uint8_t* base) {
  Require(events==std::vector<unsigned>{1},"allocation order");
  Require(ctx.r3.u32==owner+32 && ctx.r4.u64==uint64_t(stride)*count,"allocation arguments");
  Require(ctx.lr==(index_case?0x821D76D4u:0x821D7560u),"allocation provenance");
  events.push_back(2);
  if(fail_allocation) throw std::runtime_error("injected allocation failure");
  edf::native::GuestReader(base).StoreWord(owner+48,allocation_address); Clobber(ctx);
  ctx.r3.u64=allocation_returns_failure?0:1;
}
static void sub_821E8320(PPCContext& ctx,uint8_t*) {
  Require(events==std::vector<unsigned>({1,2}),"copy order");
  Require(ctx.r3.u32==payload && ctx.r4.u32==source && ctx.r5.u64==uint64_t(stride)*count,"copy arguments");
  Require(ctx.lr==(index_case?0x821D76E8u:0x821D7574u),"copy provenance"); events.push_back(3);
  if(fail_copy) throw std::runtime_error("injected copy failure");
  std::memcpy(memory.data()+payload,memory.data()+source,size_t(stride)*count); Clobber(ctx);
}
static void PublishNativeModelBuffer(uint8_t* base,uint32_t resource,
    edf::native::NativeModelBuffers::Kind kind,uint32_t actual_stride,uint32_t actual_count) {
  Require(events==std::vector<unsigned>({1,2,3}),"publication before copy completion");
  Require(resource==owner && actual_stride==stride && actual_count==count &&
    (kind==edf::native::NativeModelBuffers::Kind::Index)==index_case,"publication arguments");
  edf::native::GuestReader reader(base);
  const auto header=edf::native::NativeModelHeader(index_case,payload,stride*count);
  for(unsigned i=0;i<8;++i) Require(reader.Word(owner+i*4)==header[i],"publication before header complete");
  Require(reader.Word(owner+56)==(index_case?count:stride),"count/stride metadata");
  Require(memory[owner+52]==1 && memory[owner+53]==0xcc && memory[owner+54]==0xcc && memory[owner+55]==0xcc,
    "constructed flag overwrote adjacent bytes");
  if(!index_case) Require(reader.Word(owner+60)==count && reader.Word(owner+64)==stride*count,"VB metadata");
  else Require(reader.Word(owner+60)==0xcccccccc && reader.Word(owner+64)==0xcccccccc,"IB overwrote VB-only fields");
  Require(std::memcmp(memory.data()+payload,memory.data()+source,stride*count)==0,"payload changed");
  events.push_back(4);
  if(fail_publication) throw std::runtime_error("injected publication failure");
}
// Exercise the production release hook with explicit memory/renderer seams.
// The pool's guest critical section itself is outside this fixture.
static bool pool_bridge_enabled=true,pool_mapping_invalid=false,pool_original_called=false;
static bool pool_expect_retired=false;
static PPCContext pool_entry;
namespace edf::native {
struct PoolTestMutex {
  bool held=false;
  void lock() { Require(!held,"recursive pool retirement lock"); held=true; }
  void unlock() { Require(held,"unbalanced pool retirement lock"); held=false; }
};
struct PoolTestState {
  PoolTestMutex mutex;
  NativeBufferWrites writes;
  NativeModelBuffers model_buffers{&writes};
  struct Meshes {
    PoolTestMutex* mutex;
    std::vector<uint32_t> retired;
    void Invalidate(uint32_t owner) { Require(mutex->held,"retirement outside registry lock"); retired.push_back(owner); }
  } meshes{&mutex};
};
static PoolTestState pool_state;
static PoolTestState& State() { return pool_state; }
static NativeBufferWrites& BufferWrites() { return pool_state.writes; }
struct PoolTestMemory { uint8_t* virtual_membase() { return memory.data(); } };
struct NativePhysicalWriteExtent { uint32_t address,bytes; bool all=false; };
static std::optional<NativePhysicalWriteExtent> MapCompletedNativePhysicalWrite(PoolTestMemory&,uint32_t address,uint32_t bytes) {
  if(!bytes || pool_mapping_invalid) return {};
  Require(address==0xa0004000 && bytes==0x100,"release hook mapped wrong block");
  return NativePhysicalWriteExtent{0x4000,bytes};
}
}
static edf::native::PoolTestMemory pool_memory;
static void __imp__sub_821D3748(PPCContext& ctx,uint8_t* base) {
  Require(!pool_original_called && base==memory.data(),"pool helper duplicated/wrong memory");
  Require(!edf::native::pool_state.mutex.held,"renderer lock spans guest block release");
  Require(ctx.r3.u64==pool_entry.r3.u64 && ctx.r4.u64==pool_entry.r4.u64 &&
    ctx.r5.u64==pool_entry.r5.u64 && ctx.r6.u64==pool_entry.r6.u64 &&
    ctx.lr==pool_entry.lr && ctx.nonvolatile==pool_entry.nonvolatile,"pool release arguments changed");
  const auto& retired=edf::native::pool_state.meshes.retired;
  Require(retired.size()==(pool_expect_retired?2u:0u),"original block release preceded expected retirement");
  if(pool_expect_retired) {
    auto& writes=edf::native::BufferWrites();
    writes.Subscribe(4,0x4020,32); // Simulate a new owner after early retirement.
    const std::array<uint8_t,32> source{};
    edf::native::NativeBufferWrites::SnapshotFailure failure;
    Require(!writes.CopyObservedSet(std::array<edf::native::NativeBufferWrites::SnapshotSource,1>{{{4,0x4020,source}}},&failure) &&
      failure.overlapping_writers==1 && failure.releasing_writers==1,"new owner snapshot read block during release or lost release classification");
    Require(writes.CopyObservedSet(std::array<edf::native::NativeBufferWrites::SnapshotSource,1>{{{3,0x4100,source}}}).has_value(),
      "block release excluded an adjacent snapshot");
    writes.Unsubscribe(4);
  }
  edf::native::GuestReader(base).StoreWord(0x510,0); // Simulate first free-state store.
  pool_original_called=true; Clobber(ctx);
}
#define REXCVAR_GET(name) pool_bridge_enabled
#define REX_KERNEL_MEMORY() (&pool_memory)
#define REX_HOOK_RAW(name) static void name(PPCContext& ctx,uint8_t* base)
#define REXLOG_INFO(...) ((void)0)
#include "native_model_constructor_fixture.inc"
#undef REX_HOOK_RAW
#undef REX_KERNEL_MEMORY
#undef REXCVAR_GET

int main() {
  for(unsigned mode=0;mode<4;++mode) {
    using Buffers=edf::native::NativeModelBuffers;
    auto& state=edf::native::pool_state;
    for(uint32_t owner:{1u,2u,3u}) state.model_buffers.Retire(owner);
    state.meshes.retired.clear(); memory.fill(0xcc);
    edf::native::GuestReader reader(memory.data());
    reader.StoreWord(0x40c,0x600); // Sentinel; packed iterator names node0x500.
    reader.StoreWord(0x508,0xa0004000); reader.StoreWord(0x50c,0x100);
    reader.StoreWord(0x510,1);
    state.model_buffers.Publish(1,Buffers::Kind::Vertex,0xa0004020,4,8,0x4020);
    state.model_buffers.Publish(2,Buffers::Kind::Index,0xc0004040,2,8,0x4040);
    state.model_buffers.Publish(3,Buffers::Kind::Vertex,0xa0004100,4,8,0x4100);
    const auto stale=*state.writes.Version(1),adjacent=*state.writes.Version(3);
    pool_bridge_enabled=mode!=1; pool_mapping_invalid=mode==3;
    pool_original_called=false; pool_expect_retired=mode==0;
    PPCContext ctx; ctx.r3.u64=0x400; ctx.r4.u64=(uint64_t(0x408)<<32)|0x500;
    ctx.r5.u64=17; ctx.r6.u64=19; ctx.nonvolatile=23;
    ctx.lr=mode==2?0x12345678:0x821D3E30;
    pool_entry=ctx;
    bool failed=false;
    try { sub_821D3748(ctx,memory.data()); } catch(const std::runtime_error&) { failed=true; }
    Require(failed==(mode==3) && pool_original_called==(mode!=3),"unexpected pool release outcome");
    Require(reader.Word(0x510)==(mode==3?1u:0u),"invalid mapping reached guest free-state store");
    Require(!state.mutex.held,"pool release leaked renderer lock");
    const auto after=state.writes.Version(3);
    Require(state.writes.CommitObserved(1,stale,[] {})==(mode!=0) && after &&
      after->lifetime==adjacent.lifetime && after->revision==adjacent.revision &&
      state.writes.CommitObserved(3,*after,[] {}),"pool release lost retirement/adjacent lifetime policy or leaked scope");
    if(mode!=3) Require(ctx.r3.u64==0xdeadbeef && ctx.lr==0xbad && ctx.nonvolatile==0xbad,
      "hook discarded original helper return context");
  }
  pool_bridge_enabled=true; pool_mapping_invalid=false;
  {
    constexpr uint32_t device=8192;
    memory.fill(0); edf::native::GuestReader reader(memory.data());
    edf::native::NativeSubmissionCursors cursors;
    cursors.Initialize(device,31); cursors.Initialize(7,127);
    const auto snapshot=cursors.Get(device);
    reader.StoreWord(device+10820,1234); reader.StoreWord(device+13480,0);
    const edf::native::NativeSubmissionCursorAccess access(reader,device,snapshot,[&](uint32_t cursor) {
      cursors.Publish(device,snapshot,cursor); reader.StoreWord(device+10820,cursor);
    });
    const std::array<edf::native::NativeSubmissionDescriptor,1> ranges{{{4,0x1000}}};
    edf::native::SubmitNativeObservers(access,device,ranges,[](auto...) { throw std::runtime_error("unexpected observer"); });
    Require(cursors.Get(device).cursor==3 && cursors.Get(device).mask==31 && reader.Word(device+10820)==3,
      "native cursor imported a changed guest mirror");
    Require(cursors.Get(7).cursor==0 && cursors.Get(7).mask==127,"device cursor isolation");
    cursors.Retire(device); cursors.Initialize(device,63);
    bool rejected=false;
    try { cursors.Publish(device,snapshot,9); } catch(const std::runtime_error&) { rejected=true; }
    Require(rejected && cursors.Get(device).cursor==0 && cursors.Get(device).mask==63,"reset reused stale cursor generation");
    cursors.Retire(device); rejected=false;
    try { cursors.Get(device); } catch(const std::runtime_error&) { rejected=true; }
    Require(rejected,"retired device cursor was silently imported");
  }
  // Reset from the final normal callback used to evade cursor publication's
  // generation check; special mode never publishes a cursor at all.
  for(bool special:{false,true}) for(bool recreate:{false,true}) for(bool reset:{false,true}) {
    constexpr uint32_t device=8192;
    memory.fill(0); edf::native::GuestReader reader(memory.data());
    edf::native::NativeSubmissionCursors cursors;
    cursors.Initialize(device,31); const auto snapshot=cursors.Get(device);
    memory[device+10809]=special?2:0;
    reader.StoreWord(device+19956,4096);
    const edf::native::NativeSubmissionCursorAccess access(reader,device,snapshot,[&](uint32_t cursor) {
      cursors.Publish(device,snapshot,cursor);
    });
    const std::array<edf::native::NativeSubmissionDescriptor,2> ranges{{{4,0x1000},{8,0x2000}}};
    bool rejected=false,armed=false; unsigned callbacks=0;
    try {
      edf::native::SubmitNativeObservers(access,device,ranges,
        [&](uint32_t,uint32_t slot,auto...) {
          ++callbacks;
          if(slot==28 && reset) {
            cursors.Retire(device);
            if(recreate) cursors.Initialize(device,63);
          }
          cursors.Validate(device,snapshot);
        });
      cursors.Validate(device,snapshot);
      armed=true;
    } catch(const std::runtime_error&) { rejected=true; }
    Require(rejected==reset && armed!=reset,"observer reset armed stale completion ranges");
    Require(callbacks==(special?(reset?2u:4u):3u),"observer continued after reset");
    if(reset && recreate) Require(cursors.Get(device).cursor==0 && cursors.Get(device).mask==63,
      "stale observer changed replacement cursor");
  }
  for(uint32_t mask=0;mask<256;++mask) for(uint32_t initial:{0u,1u,5u,255u,256u,UINT32_MAX}) {
    uint32_t expected=initial;
    for(uint32_t count=0;count<512;++count) {
      Require(edf::native::AdvanceNativeRingCursor(initial,count,mask)==expected,"masked cursor arithmetic differs from loop");
      expected=(expected+1u)&mask;
    }
  }
  for(uint32_t mask:{0u,0x7fffffffu,0x80000001u,0xfffffffeu,UINT32_MAX})
      for(uint32_t initial:{0u,0x7fffffffu,UINT32_MAX}) {
    uint32_t expected=initial;
    for(uint32_t count=0;count<1024;++count) {
      Require(edf::native::AdvanceNativeRingCursor(initial,count,mask)==expected,"high-bit cursor arithmetic");
      expected=(expected+1u)&mask;
    }
    Require(edf::native::AdvanceNativeRingCursor(initial,UINT32_MAX,mask)==
      edf::native::AdvanceNativeRingCursor(edf::native::AdvanceNativeRingCursor(initial,0x80000000u,mask),0x7fffffffu,mask),
      "large cursor count composition");
  }
  for(unsigned mode=0;mode<3;++mode) for(uint32_t increment:{0u,1u,UINT32_MAX})
      for(bool failure:{false,true}) {
    constexpr uint32_t device=8192;
    memory.fill(0); events.clear(); edf::native::GuestReader reader(memory.data());
    reader.StoreWord(device+10868,mode?7:0);
    bool caught=false;
    try {
      const auto result=edf::native::DispatchNativeSubmission(reader,device,0x100001000ull,{13,0xabcdef},increment,
        [&] { events.push_back(1); if(mode==2) reader.StoreWord(device+10868,0); return 0x123456789ull; },
        [&](uint64_t token) { Require(token==0x123456789ull,"release token truncated"); events.push_back(3); },
        [&](uint64_t cursor,edf::native::NativeSubmissionDescriptor range) {
          Require(mode==1 && cursor==0x100001000ull && range.words==13 && range.address==0xabcdef,"enqueue arguments");
          events.push_back(2); if(failure) throw std::runtime_error("injected enqueue failure"); return cursor+16;
        },
        [&](edf::native::NativeSubmissionDescriptor range) {
          Require(mode!=1 && range.words==13 && range.address==0xabcdef && reader.Word(device+10868)==increment,
            "direct host descriptor or busy publication");
          events.push_back(4); if(failure) throw std::runtime_error("injected submit failure");
        });
      Require(!failure && result==0x100001000ull+(mode==1?16:0),"dispatch cursor return");
    } catch(const std::runtime_error& error) {
      Require(failure && std::string(error.what()).starts_with("injected"),"unexpected dispatch error"); caught=true;
    }
    Require(caught==failure,"dispatch exception lost");
    Require(events==(mode==0?std::vector<unsigned>{4}:mode==1?std::vector<unsigned>{1,2,3}:
      std::vector<unsigned>{1,3,4}),"enqueue/release/submission order");
    Require(reader.Word(device+10868)==(mode==1?7u+(failure?0:increment):increment),"busy count publication/wrap");
  }
  for(bool special:{false,true}) for(bool object:{false,true}) for(bool external:{false,true})
      for(uint32_t mask:{0u,5u,31u,UINT32_MAX}) for(unsigned count:{0u,1u,3u}) {
    constexpr uint32_t device=8192;
    memory.fill(0); edf::native::GuestReader reader(memory.data());
    reader.StoreWord(device+13480,mask); reader.StoreWord(device+10820,UINT32_MAX);
    reader.StoreWord(device+19956,object?512:0); reader.StoreWord(device+20100,external?768:0);
    reader.StoreByte(device+10809,special?2:0);
    const std::array<edf::native::NativeSubmissionDescriptor,3> ranges{{{0,0},{7,0x20000000},{9,UINT32_MAX}}};
    std::vector<unsigned> observed,expected;
    unsigned index=0;
    edf::native::SubmitNativeObservers(reader,device,std::span(ranges).first(count),
      [&](uint32_t target,uint32_t slot,uint64_t address,uint32_t words,uint32_t kind,uint32_t caller) {
        observed.push_back(caller);
        if(slot==24 || kind==1) {
          Require(target==(slot?512:768),"observer target");
          const auto& range=ranges[index];
          Require(words==range.words && address==edf::native::NativeObserverAddress(range.address),"observer range");
          if(special || !external || !slot) ++index;
        }
        if(kind==2) Require(reader.Word(device+10820)==words,"cursor must publish before final observer");
      });
    uint32_t cursor=UINT32_MAX;
    if(special) {
      if(object) for(unsigned i=0;i<count;++i) { expected.push_back(0x8213C48C); expected.push_back(0x8213C4A0); }
    } else {
      for(unsigned i=0;i<count;++i) {
        if(object) expected.push_back(0x8213C518);
        if(external) expected.push_back(0x8213C548);
        for(unsigned j=0;j<3;++j) cursor=(cursor+1)&mask;
      }
      if(object) expected.push_back(0x8213C5C0);
      if(external) expected.push_back(0x8213C5E4);
    }
    Require(observed==expected && reader.Word(device+10820)==cursor,"native submission observer ordering/cursor");
  }
  Require(edf::native::NativeObserverAddress(0)==uint64_t(0)-0x40000000ull &&
    edf::native::NativeObserverAddress(UINT32_MAX)==0xbeffffffull,"observer 64-bit subtraction");
  // Exercise every descriptor dispatch gate, allocation boundary, and cursor
  // postcondition. Services are controlled fixtures, not real GPU completion.
  for(unsigned mode=0;mode<6;++mode) for(bool empty:{false,true})
      for(bool grow:{false,true}) for(bool pressure:{false,true})
      for(uint32_t backward:{0u,4096u,4097u}) for(bool page_align:{false,true}) {
    constexpr uint32_t device=8192,descriptor=512,entry=2048,grown=3072,first=4096;
    memory.fill(0); events.clear(); edf::native::GuestReader reader(memory.data());
    const uint32_t next=first+(empty?0:12);
    reader.StoreWord(device+13508,first); reader.StoreWord(device+40,next-4);
    reader.StoreByte(device+10809,mode==0?0x40:0);
    reader.StoreByte(device+10808,mode>=3?0x80:0);
    reader.StoreWord(device+12944,mode==2?1:0);
    reader.StoreWord(device+13140,mode==3?0:descriptor);
    reader.StoreWord(descriptor+152,mode==4?1:0);
    reader.StoreWord(device+13160,entry); reader.StoreWord(device+13164,grow?entry:entry+8);
    reader.StoreWord(device+13008,entry); reader.StoreWord(device+13012,grow?entry+4:entry+8);
    reader.StoreWord(device+48,pressure?0:UINT32_MAX);
    reader.StoreWord(device+19956,page_align); reader.StoreWord(device+13496,backward);
    const bool direct=mode==1, queued=(mode==2 || mode==5) && !empty;
    const uint32_t final_next=direct?next+44:next;
    edf::native::SubmitNativeDescriptor(reader,device,
      [&] { Require(mode==5 && grow && !empty,"unexpected descriptor growth"); events.push_back(1); return grown; },
      [&] { Require(mode==2 && grow && !empty,"unexpected worker growth"); events.push_back(2); return grown; },
      [&](uint32_t cursor) {
        Require(direct && cursor==next && events.empty(),"capture order/endpoint");
        events.push_back(3); return cursor+8;
      },
      [&](uint32_t captured,uint32_t address,uint32_t words) {
        Require(events==std::vector<unsigned>{3} && captured==next+8 && address==first &&
          words==(next+8-first)/4,"submission must follow capture and use captured range");
        events.push_back(4); return final_next;
      },
      [&] {
        Require(pressure && reader.Word(device+40)==final_next-4 && reader.Word(device+13508)==first,
          "refill cursor publication/order");
        events.push_back(5);
      });
    std::vector<unsigned> expected;
    if(queued && grow) expected.push_back(mode==5?1:2);
    if(direct) { expected.push_back(3); expected.push_back(4); }
    if(pressure) expected.push_back(5);
    Require(events==expected,"descriptor service sequence");
    const auto destination=grow?grown:entry;
    if(queued) {
      Require(reader.Word(destination)==(mode==5?first:0x82000003u) &&
        reader.Word(destination+4)==(mode==5?3u:first),"recorded command fields");
    } else Require(reader.Word(entry)==0 && reader.Word(grown)==0,"inactive queue modified");
    Require(reader.Word(device+13160)==(queued && mode==5?destination+8:entry) &&
      reader.Word(device+13008)==(queued && mode==2?destination+8:entry),"queue cursor changed incorrectly");
    const uint32_t aligned=(final_next+31)&~31u;
    Require(reader.Word(device+40)==(pressure?final_next-4:aligned-4) &&
      reader.Word(device+13508)==(pressure?first:aligned),"submission final cursor");
    Require(reader.Word(device+13496)==(!pressure && page_align && backward?(backward+4095)&~4095u:backward),
      "backward allocation page alignment");
  }
  Require(edf::native::NativeCommandAddress(0xffffffffu)==0x20000fffu &&
    edf::native::NativeCommandAddress(0x82000000u)==0x02000000u,"native command address conversion");
  for(uint32_t mask=0;mask<64;++mask) for(uint32_t generation=0;generation<4;++generation)
    for(bool stop:{false,true}) {
      constexpr uint32_t device=8192,signals=4096;
      memory.fill(0); edf::native::GuestReader reader(memory.data());
      std::vector<uint32_t> active;
      for(uint32_t slot=0;slot<6;++slot) {
        const auto entry=device+11208+slot*56;
        reader.StoreWord(entry+4,(mask>>slot)&1);
        reader.StoreWord(entry-4,0x12340003u+slot*4);
        reader.StoreWord(entry,0xabcdef00u|generation);
        // Inactive slots deliberately have an invalid completion pointer.
        reader.StoreWord(entry+16,((mask>>slot)&1)?signals+slot*4:UINT32_MAX);
        if((mask>>slot)&1) active.push_back(slot);
      }
      size_t current=0; unsigned polls=0,ends=0;
      edf::native::WaitNativeWorkerSlots(reader,device,
        [&] { Require(current<active.size() && ends==current,"worker slot begin order"); polls=0; },
        [&] {
          Require(++polls<=3,"worker slot did not observe completion");
          if(!stop && polls==3) {
            const auto slot=active[current],entry=device+11208+slot*56;
            // A new pointed-to signal must be observed, while the target stays captured.
            reader.StoreWord(entry-4,0); reader.StoreWord(entry,0);
            reader.StoreWord(entry+16,signals+64+slot*4);
            reader.StoreWord(signals+64+slot*4,(0x12340000u+slot*4)|generation);
          }
          return !stop;
        },
        [&] { Require(polls==(stop?1u:3u),"worker poll-before-check order"); ++ends; ++current; });
      Require(current==active.size() && ends==active.size(),"worker traversal missed active slots");
    }
  for(bool failed:{false,true}) for(bool advanced:{false,true}) for(bool exempt:{false,true})
    for(unsigned elapsed:{0u,4999u,5000u,5001u,60000u}) {
    constexpr uint32_t device=8192,record=256,tls=1024,thread=2048,completion=4096;
    struct NoKernelClockReader : edf::native::GuestReader {
      using GuestReader::GuestReader;
      uint32_t Word(uint32_t address) const {
        Require(address!=2048+88,"host progress read guest kernel clock");
        return GuestReader::Word(address);
      }
    };
    memory.fill(0); NoKernelClockReader guest(memory.data());
    edf::native::NativeFenceRecord owned{};
    edf::native::NativeFenceRecordAccess reader(guest,record,owned);
    reader.StoreWord(record,device); reader.StoreWord(record+8,7);
    guest.StoreWord(tls+256,thread); guest.StoreWord(thread+332,42);
    guest.StoreWord(device+10760,42); guest.StoreWord(device+10864,exempt?1:0);
    guest.StoreWord(device+10768,completion); guest.StoreWord(completion,advanced?9:7);
    guest.StoreByte(device+10809,failed?2:0);
    const auto epoch=edf::native::NativeWaitProgress::Clock::time_point{};
    owned.progress.Refresh(epoch);
    unsigned errors=0; bool timeout=false;
    const bool again=edf::native::PollHostFenceProgress(reader,record,tls,owned.progress,
      epoch+std::chrono::milliseconds(elapsed),std::chrono::milliseconds(5000),
      [&](uint32_t found,bool stalled) { Require(found==device,"host wait failure target"); ++errors; timeout=stalled; });
    const bool stalled=!failed && !advanced && !exempt && elapsed>=5000;
    Require(again==(!failed && !stalled) && errors==unsigned(failed || stalled) && timeout==stalled,
      "host wait deadline or device failure policy");
    Require(guest.Word(completion)==(advanced?9u:7u),"host failure fabricated completion");
    for(unsigned i=0;i<24;++i) Require(memory[record+i]==0,"host wait touched guest record");
  }
  {
    edf::native::NativeFenceRecords records,other_thread;
    edf::native::NativeFenceRecord first{1,2,3,4,5,6},second{7,8,9,10,11,12};
    records.Publish(1,256,first); records.Publish(1,512,second); records.Publish(2,256,second);
    auto& stable=records.Find(1,256);
    for(uint32_t i=0;i<100;++i) records.Publish(3,1024+i*32,second);
    Require(stable==first && records.Find(2,256)==second,"native record nesting/TLS/rehash isolation");
    bool duplicate=false,missing=false;
    try { records.Publish(1,256,second); } catch(const std::runtime_error&) { duplicate=true; }
    try { other_thread.Find(1,256); } catch(const std::runtime_error&) { missing=true; }
    Require(duplicate && missing && records.Find(1,256)==first,"native record invalid lifecycle accepted");
    auto detached=records.Take(1,256); records.Publish(1,256,second);
    Require(detached==first && records.Find(1,256)==second,"native record detach/reentrant reuse");
    records.Take(1,256); missing=false;
    try { records.Take(1,256); } catch(const std::runtime_error&) { missing=true; }
    Require(missing,"native record ended twice");
    records.Publish(1,256,first); records.Discard(1,256); records.Discard(1,256);
    records.Publish(1,256,second);
    Require(records.Find(1,256)==second,"native wait exception discard blocked address reuse");
  }
  for(uint32_t kind:{0u,1u,3u,4u,UINT32_MAX}) for(bool callback:{false,true})
    for(uint32_t ticks:{0u,1u,5000u,UINT32_MAX}) {
    constexpr uint32_t device=8192,record=256,tls=1024,thread=2048,completion=4096;
    memory.fill(0xcc); edf::native::GuestReader guest(memory.data());
    edf::native::NativeFenceRecord owned{};
    edf::native::NativeFenceRecordAccess reader(guest,record,owned);
    reader.StoreWord(tls+256,thread); reader.StoreWord(thread+88,UINT32_MAX);
    reader.StoreWord(device+10768,completion); reader.StoreWord(completion,123);
    unsigned clock_calls=0,callbacks=0;
    edf::native::BeginNativeFenceRecord(reader,record,device,kind,tls,[&] {
      ++clock_calls; Require(reader.Word(record+16)==UINT32_MAX,"begin clock ordering");
      return uint64_t(0x12345678fffffffe);
    });
    Require(reader.Word(record)==device && reader.Word(record+4)==kind && reader.Word(record+8)==123 &&
      reader.Word(record+12)==UINT32_MAX && reader.Word(record+16)==UINT32_MAX && reader.Word(record+20)==0xfffffffe,
      "native wait record initialization");
    Require(memory[record-1]==0xcc && memory[record+24]==0xcc,"wait record extent");
    reader.StoreWord(thread+88,1);
    reader.StoreDoubleWord(device+20024,UINT64_MAX-2); reader.StoreDoubleWord(device+20032,UINT64_MAX-2);
    reader.StoreWord(device+13068,callback?0x82100000:0);
    edf::native::NativeBufferWrites counter_writes;
    const auto total=device+(kind==3?20032:20024);
    counter_writes.Subscribe(1,total,8);
    const auto before=*counter_writes.Version(1);
    unsigned accumulations=0;
    edf::native::EndNativeFenceRecord(reader,record,tls,[&] { ++clock_calls; return uint32_t(0xfffffffeu+ticks); },
      [&](uint32_t address,uint32_t found_device,uint32_t found_kind,uint32_t elapsed,uint64_t kernel_elapsed) {
        ++callbacks;
        Require(address==0x82100000 && found_device==device && found_kind==kind && elapsed==ticks &&
          kernel_elapsed==uint64_t(1)-uint64_t(UINT32_MAX),"wait profiling callback arguments");
        Require(reader.DoubleWord(device+(kind==3?20032:20024))==UINT64_MAX-2+ticks,"profile callback before total update");
        Require(accumulations==1 && counter_writes.Version(1).has_value(),
          "counter guard retained across profiling callback");
      },[&](uint32_t address,uint32_t elapsed) {
        Require(address==total && elapsed==ticks,"accounting supplied wrong tracked extent/value");
        const edf::native::NativeBufferWrites::WriterScope writer(&counter_writes,
          edf::native::NativeBufferWrites::Range{address,8});
        Require(!counter_writes.CopyObserved(1,total,std::span<const uint8_t>(memory.data()+total,8)),
          "accounting counter admitted overlapping snapshot during update");
        reader.StoreDoubleWord(address,reader.DoubleWord(address)+elapsed);
        counter_writes.Record(address,8); ++accumulations;
      });
    const auto batch=counter_writes.Drain();
    Require(accumulations==unsigned(kind!=0) && batch.count==unsigned(kind!=0) &&
      counter_writes.Version(1).has_value() &&
      counter_writes.CommitObserved(1,before,[] {})==(kind==0),
      "accounting lost counter tracking, empty-kind bypass or scope release");
    if(kind) Require(batch.ranges[0].address==total && batch.ranges[0].bytes==8,
      "accounting notified wrong counter extent");
    Require(clock_calls==(kind?2u:1u) && callbacks==(kind && callback?1u:0u),"wait accounting optional services");
    Require(reader.DoubleWord(device+20024)==UINT64_MAX-2+(kind && kind!=3?ticks:0u) &&
      reader.DoubleWord(device+20032)==UINT64_MAX-2+(kind==3?ticks:0u),"wait accounting counter selection/wrap");
    for(uint32_t i=0;i<24;++i) Require(memory[record+i]==0xcc,"native record touched guest storage");
  }
  for(bool stopped:{false,true}) for(bool progress:{false,true})
    for(bool same_thread:{false,true}) for(bool exempt:{false,true})
      for(uint32_t elapsed:{0u,4999u,5000u,5001u,UINT32_MAX}) {
    constexpr uint32_t device=8192,record=256,tls=1024,thread=2048,completion=4096;
    memory.fill(0); edf::native::GuestReader guest(memory.data());
    edf::native::NativeFenceRecord owned{};
    edf::native::NativeFenceRecordAccess reader(guest,record,owned);
    reader.StoreWord(record,device); reader.StoreWord(record+8,7);
    constexpr uint32_t now=3;
    reader.StoreWord(record+12,now-elapsed);
    reader.StoreWord(tls+256,thread); reader.StoreWord(thread+88,now);
    reader.StoreWord(thread+332,42); reader.StoreWord(device+10760,same_thread?42:43);
    reader.StoreWord(device+10864,exempt?1:0);
    guest.StoreByte(device+10809,stopped?2:0);
    reader.StoreWord(device+10768,completion); reader.StoreWord(completion,progress?9:7);
    unsigned errors=0;
    const bool refreshed=progress || (same_thread && exempt);
    const bool timed_out=!stopped && !refreshed && elapsed>=5000;
    const bool again=edf::native::PollNativeFenceProgress(reader,record,tls,[&](uint32_t failed) {
      Require(failed==device,"poll error device"); ++errors;
    });
    Require(again==(!stopped && !timed_out) && errors==(timed_out?1u:0u),"poll stop/timeout policy");
    Require(reader.Word(record+8)==(!stopped && progress?9u:7u),"poll completion update");
    Require(reader.Word(record+12)==(!stopped && refreshed?now:now-elapsed),"poll timestamp update");
  }
  for(bool index:{false,true}) for(uint32_t elements:{0u,3u,19u}) for(unsigned failure=0;failure<4;++failure) {
    index_case=index; stride=index?2:40; count=elements;
    fail_allocation=failure==1; fail_copy=failure==2; fail_publication=failure==3;
    memory.fill(0xcc); events.clear();
    for(unsigned i=0;i<stride*count;++i) memory[source+i]=uint8_t(i*17);
    PPCContext ctx; ctx.r3.u64=owner; ctx.r4.u64=source; ctx.r5.u64=index?count:stride; ctx.r6.u64=count;
    ctx.lr=0x12345678; ctx.nonvolatile=0xabcdef0123456789ull;
    auto expected=ctx; if(!failure) expected.r3.u64=1;
    bool threw=false;
    try { ConstructNativeModelBuffer(ctx,memory.data(),index); } catch(const std::runtime_error&) { threw=true; }
    Require(threw==bool(failure),"unexpected constructor outcome");
    Require(events.size()==(failure?failure+1:4),"failure did not stop subsequent stages");
    Require(ctx.r3.u64==expected.r3.u64 && ctx.r4.u64==expected.r4.u64 && ctx.r5.u64==expected.r5.u64 &&
      ctx.r6.u64==expected.r6.u64 && ctx.lr==expected.lr && ctx.nonvolatile==expected.nonvolatile,"caller context changed");
  }
  // A failed pool call can leave a stale nonzero record; the status, not just
  // its address, must prevent copying. Bad successful extents must also stop
  // before the copy fixture can access them or publication writes a header.
  for(bool index:{false,true}) for(unsigned invalid=0;invalid<4;++invalid) {
    index_case=index; stride=index?2:40; count=3;
    fail_allocation=fail_copy=fail_publication=false;
    allocation_returns_failure=invalid==0;
    allocation_address=invalid==0?payload:invalid==1?0:invalid==2?payload+1:0xfffffffcu;
    memory.fill(0xcc); events.clear();
    PPCContext ctx; ctx.r3.u64=owner; ctx.r4.u64=source;
    ctx.r5.u64=index?count:stride; ctx.r6.u64=count;
    ctx.lr=0x12345678; ctx.nonvolatile=0xabcdef0123456789ull;
    const auto expected=ctx;
    bool rejected=false;
    try { ConstructNativeModelBuffer(ctx,memory.data(),index); }
    catch(const std::runtime_error&) { rejected=true; }
    Require(rejected && events==std::vector<unsigned>({1,2}),
      "failed/invalid allocation reached geometry copy or publication");
    Require(ctx.r3.u64==expected.r3.u64 && ctx.r4.u64==expected.r4.u64 &&
      ctx.r5.u64==expected.r5.u64 && ctx.r6.u64==expected.r6.u64 &&
      ctx.lr==expected.lr && ctx.nonvolatile==expected.nonvolatile,
      "failed allocation changed caller context");
    for(size_t i=0;i<32;++i) Require(memory[owner+i]==0xcc,"failed allocation published resource header");
  }
  allocation_returns_failure=false; allocation_address=payload;
  for(unsigned invalid=0;invalid<4;++invalid) {
    PPCContext ctx; ctx.r3.u64=owner; ctx.r4.u64=source; ctx.r5.u64=40; ctx.r6.u64=3;
    if(invalid==0) ctx.r3.u64=0;
    if(invalid==1) ctx.r5.u64=0;
    if(invalid==2) ctx.r5.u64=41;
    if(invalid==3) ctx.r6.u64=UINT32_MAX;
    const auto expected=ctx;
    events.clear(); bool rejected=false;
    try { ConstructNativeModelBuffer(ctx,memory.data(),false); }
    catch(const std::runtime_error&) { rejected=true; }
    Require(rejected && events.empty(),"invalid extent mutated resource before validation");
    Require(ctx.r3.u64==expected.r3.u64 && ctx.r4.u64==expected.r4.u64 && ctx.r5.u64==expected.r5.u64 &&
      ctx.r6.u64==expected.r6.u64,"invalid extent changed caller arguments");
  }
  for(bool index:{false,true}) for(bool constructed:{false,true}) for(int slot:{-1,0,15}) {
    constexpr uint32_t device=8192;
    memory.fill(0xcc); events.clear();
    edf::native::GuestReader reader(memory.data());
    reader.StoreCpuWords(owner,edf::native::NativeModelHeader(index,payload,120));
    memory[owner+52]=constructed?1:0;
    reader.StoreWord(device+52,UINT32_MAX);
    reader.StoreWord(device+12164,index && slot>=0?owner:0);
    for(int at=0;at<16;++at) reader.StoreWord(device+12188+at*4,!index && at==slot?owner:0);
    edf::native::CleanupNativeModelResource(reader,owner,index,
      [&] { events.push_back(1); return device; },
      [&](uint32_t dev,bool ib) {
        Require(dev==device && ib==index && events==std::vector<unsigned>{1},"cleanup unbind ordering");
        Require(reader.Word(device+52)==0,"device refcount was not incremented with uint32 wrap");
        events.push_back(2);
      },
      [&](uint32_t allocation) {
        Require(allocation==owner+32 && memory[owner+52]==1,"cleanup release target or flag order");
        Require(events== (slot<0?std::vector<unsigned>{1}:std::vector<unsigned>{1,2}),"cleanup release before unbind");
        events.push_back(3);
      });
    Require(events==(constructed?(slot<0?std::vector<unsigned>{1,3}:std::vector<unsigned>{1,2,3}):std::vector<unsigned>{}),
      "unexpected cleanup callbacks");
    Require(memory[owner+52]==0 && memory[owner+53]==0xcc && memory[owner+54]==0xcc && memory[owner+55]==0xcc,
      "cleanup flag overwrote adjacent bytes");
    Require(reader.Word(owner+56)==0,"cleanup count not reset");
    Require(reader.Word(owner+60)==(index?0xcccccccc:0) && reader.Word(owner+64)==(index?0xcccccccc:0),"cleanup metadata extent");
    Require(reader.Word(device+52)==(constructed?0u:UINT32_MAX),"unconstructed cleanup accessed device");
  }
  for(unsigned mode=0;mode<5;++mode) for(uint32_t legacy:{0u,0xffffffffu}) for(uint32_t replacement:{0u,owner,512u}) {
    constexpr uint32_t device=8192,queue=24000;
    memory.fill(0); events.clear(); edf::native::GuestReader reader(memory.data());
    reader.StoreWord(device+12164,mode==0?0:owner);
    reader.StoreWord(owner,0x20000002);
    reader.StoreWord(owner+8,17);
    reader.StoreWord(device+10780,mode==1?123:0);
    reader.StoreWord(device+10784,mode>=3?2:0);
    reader.StoreWord(device+13148,queue);
    reader.StoreWord(device+13152,mode==4?queue:queue+16);
    unsigned observations=0;
    edf::native::SetNativeIndexResource(reader,device,replacement,
      [&] {
        Require(mode==4,"unexpected retirement allocation"); events.push_back(1);
        reader.StoreWord(device+13152,queue+16); return queue;
      },
      [&] { events.push_back(2); return legacy; },
      [&](edf::native::NativeRetirementPath path,uint32_t previous,uint32_t value,uint32_t cursor) {
        ++observations;
        Require(static_cast<unsigned>(path)==(mode>=3?3:mode),"incorrect retirement observation branch");
        Require(previous==(mode?owner:0),"incorrect observed previous resource");
        Require(reader.Word(device+12164)==previous,"observation after binding replacement");
        if(mode==1) Require(value==123 && reader.Word(owner+8)==value && cursor==0,"fence observed before store");
        else if(mode>=3) Require(value==((owner>>2)|(legacy&0x80000000u)) && cursor==queue &&
          reader.Word(queue)==value && reader.Word(queue+4)==UINT32_MAX && reader.Word(device+13148)==queue+8,
          "deferred observation precedes completed record");
        else Require(value==0 && cursor==0,"inactive branch observation payload");
      });
    Require(observations==1,"retirement observation count");
    Require(reader.Word(device+12164)==replacement,"native index binding not published");
    Require(reader.Word(owner+8)==(mode==1?123u:17u),"old resource fence side effect");
    if(mode>=3) {
      Require(events==(mode==4?std::vector<unsigned>{1,2}:std::vector<unsigned>{2}),"retirement event order");
      Require(reader.Word(queue)==((owner>>2)|(legacy&0x80000000u)) && reader.Word(queue+4)==UINT32_MAX &&
        reader.Word(device+13148)==queue+8,"retirement record or cursor differs");
    } else Require(events.empty() && reader.Word(queue)==0 && reader.Word(device+13148)==queue,"inactive retirement path mutated queue");
  }
  for(uint32_t stream:{0u,1u,15u}) for(uint32_t byte_stride:{0u,40u,68u,1024u,2048u})
      for(bool present:{false,true}) for(unsigned mode=0;mode<3;++mode) {
    constexpr uint32_t device=8192,queue=24000,resource=512;
    constexpr uint64_t input_dirty=uint64_t(1)<<56;
    memory.fill(0); events.clear(); edf::native::GuestReader reader(memory.data());
    reader.StoreWord(resource+24,0xfca46003); reader.StoreWord(resource+28,0x10000102);
    reader.StoreWord(device+1784-stream*8,0xabcdef01); reader.StoreWord(device+1788-stream*8,0xabcdef02);
    reader.StoreDoubleWord(device+16,8);
    reader.StoreWord(device+12188+stream*4,mode?owner:0); reader.StoreWord(owner,1);
    reader.StoreWord(device+10780,mode==1?123:0); reader.StoreWord(device+10784,1);
    reader.StoreWord(device+13148,queue); reader.StoreWord(device+13152,queue);
    for(unsigned i=0;i<16;++i) memory[device+12256+i]=0xcc;
    memory[device+11552+stream]=17;
    edf::native::SetNativeStreamResource(reader,device,stream,present?resource:0,40,byte_stride,input_dirty,
      [&] {
        Require(mode==2,"unexpected stream retirement growth");
        Require(reader.Word(device+1784-stream*8)==(present?0x1ca4702bu:0xabcdef01u),"retirement ran before descriptor update");
        Require(reader.Word(device+12188+stream*4)==owner,"stream replaced before retirement");
        reader.StoreWord(device+13152,queue+16); events.push_back(1); return queue;
      },[&] { events.push_back(2); return 0x80000000u; });
    Require(reader.Word(device+1784-stream*8)==(present?0x1ca4702bu:0xabcdef01u) &&
      reader.Word(device+1788-stream*8)==(present?0x100000dau:0xabcdef02u),"stream descriptor words");
    Require(reader.Word(device+12188+stream*4)==(present?resource:0),"stream binding pointer");
    for(unsigned i=0;i<16;++i) Require(memory[device+12256+i]==(i==stream?uint8_t(byte_stride>>2):0xcc),"stride byte extent");
    const bool stride_dirty=byte_stride && byte_stride!=68;
    Require(reader.DoubleWord(device+16)==(8|(present?input_dirty:0)|(stride_dirty?(uint64_t(1)<<51):0)),"full stride or 64-bit dirty mask lost");
    if(mode==2) Require(events==std::vector<unsigned>({1,2}) && reader.Word(queue)==((owner>>2)|0x80000000u) &&
      reader.Word(queue+4)==UINT32_MAX && reader.Word(device+13148)==queue+8,"stream deferred retirement contract");
    else Require(events.empty() && reader.Word(owner+8)==(mode==1?123u:0u),"stream fence effects");
  }
  std::cout<<"Production model construction, cleanup, index and stream binding contract tests passed\n";
}
