#include "edf2017_funcs.26.h"
#include "native_graphics/native_display_gamma.h"
#include "native_graphics/native_pacing.h"
#include "native_graphics/guest_fence.h"
#include "native_graphics/native_allocator_wait.h"
#include "native_graphics/native_profile_result.h"
#include "native_graphics/native_buffer_writes.h"
#include "native_graphics/native_pix_monitor.h"
#include <cmath>
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>
REX_EXTERN(__imp__edf_native_present_cpu_tail);
REX_EXTERN(__imp__sub_82151460);
REX_EXTERN(__imp__edf_test_original_color_tail);
REX_EXTERN(__imp__edf_native_color_cpu_tail);
REX_EXTERN(__imp__edf_test_original_gamma_table);
REX_EXTERN(__imp__edf_test_original_gamma_pwl);
REX_EXTERN(__imp__edf_native_gamma_table_cpu_tail);
REX_EXTERN(__imp__edf_native_gamma_pwl_cpu_tail);
REX_EXTERN(__imp__edf_test_original_swap_callback);
REX_EXTERN(__imp__edf_test_original_swap_wait);
REX_EXTERN(__imp__edf_native_swap_wait_cpu_tail);
REX_EXTERN(__imp__edf_native_present_reset);
REX_EXTERN(__imp__sub_821377E8);
REX_EXTERN(__imp__edf_test_original_scaler);
REX_EXTERN(__imp__edf_native_scaler_cpu_tail);
REX_EXTERN(__imp__edf_native_scaler_config);
REX_EXTERN(__imp__sub_82151050);
REX_EXTERN(__imp__edf_test_original_fence_wait);
REX_EXTERN(__imp__edf_test_original_8213BC48);
REX_EXTERN(__imp__edf_test_original_8213BCE0);
REX_EXTERN(__imp__edf_test_original_profile_result);
REX_EXTERN(__imp__edf_test_counter_reset);
REX_EXTERN(__imp__edf_test_pix_dispatch);
static unsigned pix_callbacks=0;
static uint32_t pix_target=0;
static std::array<uint64_t,10> pix_callback_context{};
static auto PixCallbackContext(const PPCContext& ctx) {
  return std::array<uint64_t,10>{ctx.r1.u64,ctx.r3.u64,ctx.r4.u64,ctx.lr,ctx.ctr.u64,
    ctx.r27.u64,ctx.r28.u64,ctx.r29.u64,ctx.r30.u64,ctx.r31.u64};
}
REX_EXTERN(edf_test_pix_special_service) { throw std::runtime_error("idle PIX state entered capture service"); }
REX_EXTERN(edf_test_pix_callback) {
  if(ctx.lr!=0x82138B8C || ctx.r3.u32!=46 || ctx.r4.u32!=123 || ctx.ctr.u32!=pix_target)
    throw std::runtime_error("PIX idle event contract changed");
  pix_callback_context=PixCallbackContext(ctx);
  ++pix_callbacks; ctx.r3.u64=0xabcdef01;
}
REX_EXTERN(__imp__edf_native_counter_reset_cpu_tail);
static edf::native::NativeBufferWrites counter_writes;
static bool counter_tracking=false;
static unsigned counter_notifications=0;
static unsigned counter_field_notifications=0;
static void CheckCounterReleased() {
  if(counter_tracking && !counter_writes.Version(1)) throw std::runtime_error("counter guard spans guest service");
}
edf::native::NativeBufferWrites::WriterScope edf_native_begin_counter_reset_write(uint8_t*,uint32_t device) {
  CheckCounterReleased();
  return edf::native::NativeBufferWrites::WriterScope(&counter_writes,
    edf::native::NativeBufferWrites::Range{device+20000,48});
}
void edf_native_complete_counter_reset_write(uint8_t* base,uint32_t device) {
  if(counter_writes.Version(1) || REX_LOAD_U64(device+20024) || REX_LOAD_U64(device+20032))
    throw std::runtime_error("counter notification escaped scope or preceded reset");
  counter_writes.Record(device+20000,48); ++counter_notifications;
}
void edf_native_counter_store_word(uint8_t* base,uint32_t address,uint32_t value) {
  CheckCounterReleased();
  const edf::native::NativeBufferWrites::WriterScope writer(&counter_writes,
    edf::native::NativeBufferWrites::Range{address,4});
  REX_STORE_U32(address,value);
  if(counter_writes.Version(1)) throw std::runtime_error("device field notification escaped scope");
  counter_writes.Record(address,4); ++counter_field_notifications;
}
static uint32_t counter_callback_result=0;
static unsigned counter_callbacks=0;
REX_EXTERN(edf_test_counter_callback) {
  CheckCounterReleased();
  if(ctx.lr!=0x82139274 && ctx.lr!=0x82139420 && ctx.lr!=0x821394D0)
    throw std::runtime_error("unexpected counter reset callback site");
  ++counter_callbacks; ctx.r3.u64=counter_callback_result;
}
REX_EXTERN(sub_82137FA0) { throw std::runtime_error("unexpected profile consumer branch"); }
REX_EXTERN(sub_82138040) { throw std::runtime_error("unexpected legacy GPU stats branch"); }
namespace {
constexpr uint32_t device=0x1000,params=0x10000,packet=0x40000;
std::vector<uint32_t> trace;
struct FenceReader {
  uint8_t* base;
  uint32_t Add(uint32_t address,uint32_t offset) const { return address+offset; }
  uint32_t Word(uint32_t address) const { return __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base+address)); }
};
unsigned swaps=0;
unsigned scanout_writes=0;
bool persist_success=false;
uint32_t swap_phase=1;
unsigned swap_acknowledgements=0;
uint32_t swap_argument=0;
uint32_t scaler_words=200;
uint32_t scaler_video_flags=0;
void record(uint32_t id) { CheckCounterReleased(); trace.push_back(id); }
}
void edf_test_present_mmio(uint32_t address,uint32_t) {
  if(address==0x70004) { ++swap_acknowledgements; return; }
  if(address!=0x7fc86110) throw std::runtime_error("unexpected presentation MMIO address");
  ++scanout_writes;
}
uint32_t edf_test_swap_mmio_read(uint32_t address) {
  if(address==0x7fc86530) return swap_phase-1;
  if(address==0x7fc86584) return 100;
  throw std::runtime_error("unexpected swap callback MMIO read");
}
// CPU helpers are controlled identically for the paired real wrapper bodies.
REX_EXTERN(sub_821E8320) { std::memcpy(base+ctx.r3.u32,base+ctx.r4.u32,ctx.r5.u32); }
REX_EXTERN(sub_821E9BA0) { std::memset(base+ctx.r3.u32,ctx.r4.u8,ctx.r5.u32); }
REX_EXTERN(sub_8213D160) { record(0xD160); ctx.r3.u64=ctx.lr==0x821515B4?REX_LOAD_U32(device+40):packet; }
REX_EXTERN(sub_8213CF60) {
  // Distinguish removed scanout/scaler capacity checks from retained submits.
  record(ctx.lr == 0x8215184C || ctx.lr == 0x821518CC || ctx.lr == 0x8215194C ||
         ctx.lr == 0x82137818 || ctx.lr == 0x82142360 || ctx.lr == 0x82134C0C || ctx.lr == 0x82134E14 || ctx.lr == 0x821390E8 ? uint32_t(ctx.lr) : 0xCF60);
  ctx.r3.u64=REX_LOAD_U32(device+40);
}
REX_EXTERN(sub_82139148) {
  if(ctx.r3.u32!=device || ctx.r4.u32>1) throw std::runtime_error("invalid swap profiling contract");
  record(0x39148+ctx.r4.u32);
}
REX_EXTERN(sub_8213C9F0) {
  if(ctx.r3.u32!=device || ctx.r6.u32!=0x82151248 || ctx.r5.u32)
    throw std::runtime_error("invalid swap callback contract");
  swap_argument=ctx.r7.u32;
  record(0xC9F0);
  ctx.r3.u64=ctx.r4.u32+92;
}
REX_EXTERN(edf_native_swap_wait) {
  if(ctx.r3.u32!=device) throw std::runtime_error("invalid native swap device");
  record(0xC9F0); // Native barrier replaces this exact ordering point.
  const auto mode=REX_LOAD_U32(device+13220);
  swap_argument=((mode==4?3:mode==2?2:1)<<16)|((REX_LOAD_U32(device+11580)>>23)&127);
}
#define STUB(name,id) REX_EXTERN(name) { record(id); }
REX_EXTERN(edf_test_profile_marker_seen) {
  const bool end=ctx.lr==0x8215162C;
  const auto sequence=REX_LOAD_U32(device+20052);
  if((!end && ctx.lr!=0x82151544) || ctx.r3.u32!=device ||
     ctx.r4.u32!=0x70000+64+4*((sequence+uint32_t(end))&7))
    throw std::runtime_error("invalid profiling marker boundary");
  record(0x390B8);
}
REX_EXTERN(edf_test_native_profile_marker) { edf_test_profile_marker_seen(ctx,base); }
STUB(sub_82147028,0x47028)
STUB(sub_821475C8,0x475C8)
REX_EXTERN(edf_test_scaler_notification) {
  if(ctx.r3.u32!=1) throw std::runtime_error("invalid scaler notification type");
  record(0x4F3CC);
  ctx.r3.u64=0;
}
STUB(sub_82150D60,0x50D60)
STUB(edf_test_native_scaler_call,0x50D60)
REX_EXTERN(edf_test_video_flags) {
  record(0x4F298);
  ctx.r3.u64=scaler_video_flags;
}
REX_EXTERN(sub_821507F8) {
  record(0x507F8);
  // Capture computed arguments and model a persistent CPU helper side effect.
  REX_STORE_U32(0x90000,ctx.r4.u32); REX_STORE_U32(0x90004,ctx.r5.u32);
  REX_STORE_U32(0x90008,ctx.r6.u32); REX_STORE_U32(0x9000c,ctx.r7.u32);
  REX_STORE_U32(0x90010,ctx.r8.u32);
  REX_STORE_U32(ctx.r3.u32+15000,0x12345678);
}
REX_EXTERN(edf_test_fill_scaler) {
  record(0x50E80);
  for(uint32_t i=0;i<ctx.r4.u32;i+=4) REX_STORE_U32(ctx.r3.u32+i,ctx.r5.u32);
}
REX_EXTERN(edf_test_initialize_scaler) {
  record(0x50F98);
  const auto output=REX_LOAD_U32(ctx.r1.u32+100),count=REX_LOAD_U32(ctx.r1.u32+108);
  if(count!=200 || scaler_words>count) throw std::runtime_error("invalid scaler command capacity");
  for(uint32_t i=0;i<count;++i) REX_STORE_U32(output+i*4,0x80000000);
  ctx.r3.u64=scaler_words;
}
STUB(sub_82151168,0x51168)
STUB(sub_82151080,0x51080)
STUB(sub_8213CC20,0x3CC20)
STUB(sub_8213C928,0x3C928)
STUB(sub_82139228,0x39228)
STUB(sub_82142398,0x42398)
REX_EXTERN(sub_82142480) { record(0x42480); ctx.r3.u64=0x30000; }
STUB(sub_82142518,0x42518)
REX_EXTERN(sub_8212F420) { record(0x2F420); ctx.r3.u64=0x31000; }
STUB(sub_8252B718,0x52B718)
STUB(sub_82142B38,0x42B38)
STUB(sub_82138858,0x38858)
STUB(sub_821394D8,0x394D8)
STUB(sub_82139508,0x39508)
REX_EXTERN(sub_82139688) { record(0x39688); ctx.r3.u64=0; }
STUB(sub_82142130,0x42130)
STUB(sub_82142050,0x42050)
REX_EXTERN(sub_82142278) { record(0x42278); REX_STORE_U32(ctx.r3.u32,0x1234); }
REX_EXTERN(sub_82142220) { record(0x42220); REX_STORE_U32(ctx.r3.u32,0x5678); }
REX_EXTERN(edf_test_swap) {
  ++swaps;
  // Audited SDK VdSwap only writes its 256-byte packet reservation; it does
  // not change the system-command-buffer outputs consumed by the wrapper.
  std::memset(base+ctx.r3.u32,0xA5,256);
}
REX_EXTERN(edf_test_VdGetSystemCommandBuffer) {
  record(0x515A8);
  std::memset(base+ctx.r3.u32,0,148);
  REX_STORE_U32(ctx.r3.u32,0xbeef0000);
  REX_STORE_U32(ctx.r4.u32,0xbeef0001);
}
REX_EXTERN(edf_test_VdPersistDisplay) {
  if(ctx.r3.u32!=ctx.r1.u32+120 || ctx.r4.u32!=ctx.r1.u32+136 ||
     REX_LOAD_U32(ctx.r3.u32) || REX_LOAD_U32(ctx.r3.u32+4) || REX_LOAD_U32(ctx.r3.u32+8))
    throw std::runtime_error("invalid persisted display descriptor");
  record(0x51990);
  if(persist_success) REX_STORE_U32(ctx.r4.u32,0x72000);
  ctx.r3.u64=persist_success?1:0;
}
REX_EXTERN(edf_test_MmFreePhysicalMemory) {
  if(ctx.r3.u32!=1 || ctx.r4.u32!=0x72000) throw std::runtime_error("invalid persisted display release");
  record(0x519A4);
}
int main() {
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
  auto* base=static_cast<uint8_t*>(VirtualAlloc(nullptr,size_t(1)<<32,MEM_RESERVE,PAGE_NOACCESS));
  if(!base) return 2;
  if(!VirtualAlloc(base,1024*1024,MEM_COMMIT,PAGE_READWRITE) ||
     !VirtualAlloc(base+0x82000000,4096,MEM_COMMIT,PAGE_READWRITE) ||
     !VirtualAlloc(base+0x82578000,4096,MEM_COMMIT,PAGE_READWRITE)) return 2;
  std::vector<uint8_t> expected(0xe0000);
  int failures=0;
  for(uint32_t state:{0u,1u,10u,18u,UINT32_MAX}) for(unsigned mode=0;mode<5;++mode) {
    std::memset(base,0,1024*1024);
    REX_STORE_U32(0x82578cf8,state); REX_STORE_U32(device+20000,123);
    REX_STORE_U32(0x8200071c,0x23000); REX_STORE_U32(0x82000800,0x20000);
    REX_STORE_U32(0x23000,mode>=2?0x24000:0);
    REX_STORE_U32(0x24000,mode==3?0:0x2222);
    REX_STORE_U32(0x20000,mode==1 || mode>=3?0x21000:0);
    REX_STORE_U32(0x21018,0x1111);
    pix_callbacks=0; pix_target=mode>=2?0x2222:0x1111;
    PPCContext ctx{}; ctx.r3.u64=device; ctx.r1.u64=0xf0000; ctx.lr=0x12345678;
    ctx.r27.u64=27; ctx.r28.u64=28; ctx.r29.u64=29; ctx.r30.u64=30; ctx.r31.u64=31;
    auto native_ctx=ctx;
    __imp__edf_test_pix_dispatch(ctx,base);
    unsigned native_callbacks=0;
    if(!edf::native::DispatchNativePixIdle(FenceReader{base},device,[&](uint32_t target,uint32_t frame) {
      if(target!=pix_target || frame!=123) ++failures;
      ++native_callbacks;
    }) || native_callbacks!=pix_callbacks) ++failures;
    unsigned native_invocations=0;
    if(!edf::native::RunNativePixIdle(native_ctx,FenceReader{base},[&](uint32_t target,auto& work) {
      ++native_invocations;
      if(target!=pix_target || PixCallbackContext(work)!=pix_callback_context || work.last_indirect_target!=target) ++failures;
      work.r3.u64=0xabcdef01;
    }) || native_invocations!=pix_callbacks || native_ctx.r3.u64!=ctx.r3.u64 ||
      native_ctx.r1.u64!=ctx.r1.u64 || native_ctx.lr!=ctx.lr || native_ctx.r27.u64!=27 ||
      native_ctx.r28.u64!=28 || native_ctx.r29.u64!=29 || native_ctx.r30.u64!=30 || native_ctx.r31.u64!=31) ++failures;
    if(pix_callbacks!=unsigned(mode==1 || mode==2 || mode==4) || REX_LOAD_U32(0x82578cf8)!=state ||
      ctx.r1.u32!=0xf0000 || ctx.lr!=0x12345678 || ctx.r27.u64!=27 || ctx.r28.u64!=28 ||
      ctx.r29.u64!=29 || ctx.r30.u64!=30 || ctx.r31.u64!=31) ++failures;
  }
  for(uint32_t state=11;state<=17;++state) {
    REX_STORE_U32(0x82578cf8,state);
    if(edf::native::DispatchNativePixIdle(FenceReader{base},device,[&](uint32_t,uint32_t) { ++failures; })) ++failures;
    if(REX_LOAD_U32(0x82578cf8)!=state) ++failures;
  }
  for(uint32_t flags:{0u,0x100u,0x400u}) for(uint32_t result:{0u,0x80000001u})
    for(uint32_t profiling:{0u,1u,2u}) for(uint32_t previous:{0u,0xfffffff0u})
    for(bool final_callback:{false,true}) {
    std::memset(base,0,1024*1024); trace.clear();
    REX_STORE_U32(0x82000800,0x20000); REX_STORE_U32(0x20000,0x21000);
    REX_STORE_U32(0x21020,0x22000); REX_STORE_U32(0x2200c,0x1111); REX_STORE_U32(0x22010,0x2222);
    REX_STORE_U32(0x8200071c,0x23000); REX_STORE_U32(0x23000,0x24000);
    REX_STORE_U32(0x24004,final_callback?0x3333:0);
    REX_STORE_U32(device+15144,flags); REX_STORE_U32(device+20080,profiling);
    REX_STORE_U32(device+20000,UINT32_MAX); REX_STORE_U32(device+20004,0x76543210);
    REX_STORE_U32(device+20008,previous);
    REX_STORE_U64(device+20024,0x123456789abcdef0); REX_STORE_U64(device+20032,0xfedcba9876543210);
    counter_callback_result=result; counter_callbacks=0;
    PPCContext ctx{}; ctx.r3.u64=device; ctx.r1.u64=0xf0000; ctx.lr=0x12345678;
    ctx.r28.u64=28; ctx.r29.u64=29; ctx.r30.u64=30; ctx.r31.u64=31;
    const auto initial_ctx=ctx;
    const std::vector<uint8_t> initial_memory(base,base+1024*1024);
    __imp__edf_test_counter_reset(ctx,base);
    if(REX_LOAD_U32(device+20000)!=0 || REX_LOAD_U64(device+20024)!=0 || REX_LOAD_U64(device+20032)!=0 ||
      REX_LOAD_U32(device+20040)!=0x9abcdef0 || REX_LOAD_U32(device+20044)!=0x76543210 ||
      REX_LOAD_U32(device+20008)!=0xabcdef01 || REX_LOAD_U32(device+20012)!=0x12345678 ||
      REX_LOAD_U32(device+20004)!=(previous?uint32_t(0xabcdef01-previous):0x76543210) ||
      counter_callbacks!=unsigned(flags!=0)+unsigned(final_callback) ||
      ctx.r1.u32!=0xf0000 || ctx.lr!=0x12345678 || ctx.r28.u64!=28 || ctx.r29.u64!=29 ||
      ctx.r30.u64!=30 || ctx.r31.u64!=31) ++failures;
    const auto original_ctx=ctx;
    const auto original_trace=trace;
    const auto original_callbacks=counter_callbacks;
    const std::vector<uint8_t> original_memory(base,base+1024*1024);
    std::copy(initial_memory.begin(),initial_memory.end(),base); ctx=initial_ctx;
    trace.clear(); counter_callbacks=0; counter_notifications=0; counter_field_notifications=0;
    counter_writes.Subscribe(1,device+20000,48);
    const auto before=*counter_writes.Version(1);
    counter_tracking=true;
    __imp__edf_native_counter_reset_cpu_tail(ctx,base);
    CheckCounterReleased(); counter_tracking=false;
    const auto batch=counter_writes.Drain();
    if(std::memcmp(&ctx,&original_ctx,sizeof(ctx)) || std::memcmp(base,original_memory.data(),original_memory.size()) ||
      trace!=original_trace || counter_callbacks!=original_callbacks || counter_notifications!=1 ||
      counter_field_notifications!=(flags?2u:0u)+(profiling==1?8u:profiling==2?1u:0u) ||
      batch.provider_calls!=1+counter_field_notifications ||
      batch.count!=1+unsigned(flags!=0)+unsigned(profiling!=0) ||
      counter_writes.CommitObserved(1,before,[] {})) ++failures;
    bool accounting_extent=false;
    for(size_t i=0;i<batch.count;++i) {
      const auto range=batch.ranges[i];
      if(range.address==device+20000 && range.bytes==48) accounting_extent=true;
      else if(!((range.address==device+15144 && range.bytes==8) ||
        (profiling==1 && range.address==device+20060 && range.bytes==32) ||
        (profiling==2 && range.address==device+20088 && range.bytes==4))) ++failures;
    }
    if(!accounting_extent) ++failures;
    counter_writes.Unsubscribe(1);
  }
  if(!VirtualAlloc(base+0x82003000,4096,MEM_COMMIT,PAGE_READWRITE) ||
     !VirtualAlloc(base+0x82009000,4096,MEM_COMMIT,PAGE_READWRITE)) return 2;
  REX_STORE_U32(0x82003e3c,std::bit_cast<uint32_t>(100.f));
  REX_STORE_U32(0x820009a4,0);
  for(uint32_t consumer:{0u,1u,6u,7u,0xfffffffeu,0xffffffffu})
    for(const auto points:{std::array<uint32_t,3>{100,125,200}, {100,199,200},
                          {0xfffffff0u,4,20}, {0xfffffff0u,0xfffffff8u,20},
                          {0,125,200}, {100,0,200}, {100,125,0}}) {
      std::memset(base,0,1024*1024);
      REX_STORE_U32(device+20048,consumer); REX_STORE_U32(device+10768,0x70000);
      for(unsigned i=0;i<3;++i) {
        const auto slot=0x70000+64+4*((consumer+i-1)&7);
        REX_STORE_U32(slot,__builtin_bswap32(points[i]));
      }
      PPCContext ctx{}; ctx.r1.u64=0xf0000; ctx.lr=0x12345678;
      ctx.r3.u64=device; ctx.r4.u64=6; ctx.r30.u64=30; ctx.r31.u64=31;
      ctx.f30.f64=30; ctx.f31.f64=31;
      __imp__edf_test_original_profile_result(ctx,base);
      std::optional<double> ratio;
      if(points[0] && points[1] && points[2]) {
        uint64_t middle=points[1],end=points[2];
        if(middle<=points[0]) middle+=uint64_t(1)<<32;
        if(end<=middle) end+=uint64_t(1)<<32;
        ratio=double(end-middle)/double(end-points[0]);
      }
      const auto expected_result=edf::native::NativeProfileResult(ratio,100,0);
      if((ratio?ctx.f1.f64!=expected_result:!std::isnan(ctx.f1.f64)) ||
         REX_LOAD_U32(device+20048)!=consumer+(points[2]?2u:0u) ||
         ctx.r1.u32!=0xf0000 || ctx.lr!=0x12345678 || ctx.r30.u64!=30 || ctx.r31.u64!=31 ||
         ctx.f30.f64!=30 || ctx.f31.f64!=31) ++failures;
    }
  for(uint32_t issued:{0u,1u,2u,3u,0x7fffffffu,0xfffffffeu,0xffffffffu})
    for(uint32_t target:{0u,1u,2u,3u,0x7fffffffu,0xfffffffeu,0xffffffffu})
      for(uint32_t completed:{0u,1u,2u,3u,0x7fffffffu,0xfffffffeu,0xffffffffu})
        for(bool recording:{false,true}) {
          std::memset(base,0,1024*1024);
          REX_STORE_U32(device+10768,0x70000); REX_STORE_U32(0x70000,completed);
          REX_STORE_U32(device+10780,issued); REX_STORE_U32(device+12944,recording?1:0);
          PPCContext ctx{}; ctx.r1.u64=0xf0000; ctx.lr=0x12345678;
          ctx.r3.u64=device; ctx.r4.u64=target; ctx.r5.u64=3;
          ctx.r29.u64=29; ctx.r30.u64=30; ctx.r31.u64=31;
          trace.clear(); __imp__edf_test_original_fence_wait(ctx,base);
          using edf::native::FenceWaitEntryAction;
          const auto action=edf::native::EvaluateFenceWaitEntry(issued,target,completed,recording);
          std::vector<uint32_t> wanted;
          if(action==FenceWaitEntryAction::Submit) wanted.push_back(0xCF60);
          if(action==FenceWaitEntryAction::Submit || action==FenceWaitEntryAction::Wait)
            wanted.insert(wanted.end(),{0x394D8,0x39688,0x39508});
          if(trace!=wanted || ctx.r1.u32!=0xf0000 || ctx.lr!=0x12345678 ||
             ctx.r29.u64!=29 || ctx.r30.u64!=30 || ctx.r31.u64!=31) ++failures;
          const auto original_trace=trace;
          trace.clear();
          edf::native::WaitNativeResourceFence(FenceReader{base},device,target,
            [&] { record(0xCF60); },[&] { record(0x394D8); },
            [&] { record(0x39688); return false; },[&] { record(0x39508); });
          if(trace!=original_trace) ++failures;
        }
  for(uint32_t first:{0u,1u,4u,16u,0x7fffffffu,0xfffffffeu,UINT32_MAX})
    for(uint32_t second:{0u,1u,2u,3u,16u,0xfffffffeu,UINT32_MAX})
      for(uint32_t completed:{0u,1u,2u,3u,16u,0xfffffffeu,UINT32_MAX}) {
        std::memset(base,0,1024*1024);
        REX_STORE_U32(device+10768,0x70000); REX_STORE_U32(0x70004,completed);
        PPCContext ctx{}; ctx.r1.u64=0xf0000; ctx.lr=0x12345678;
        ctx.r3.u64=device; ctx.r4.u64=first; ctx.r5.u64=second;
        ctx.r29.u64=29; ctx.r30.u64=30; ctx.r31.u64=31;
        trace.clear(); __imp__edf_test_original_8213BC48(ctx,base);
        const auto original_trace=trace; trace.clear();
        edf::native::WaitNativeAllocationGeneration(FenceReader{base},device,first,second,
          [&] { record(0x394D8); },[&] { record(0x39688); return false; },[&] { record(0x39508); });
        if(trace!=original_trace || ctx.r1.u32!=0xf0000 || ctx.lr!=0x12345678 ||
          ctx.r29.u64!=29 || ctx.r30.u64!=30 || ctx.r31.u64!=31) ++failures;
        for(uint32_t mask:{0u,31u,UINT32_MAX}) {
          REX_STORE_U32(0x7003c,completed); REX_STORE_U32(device+13480,mask);
          ctx.r3.u64=device; ctx.r4.u64=first; ctx.r5.u64=second;
          trace.clear(); __imp__edf_test_original_8213BCE0(ctx,base);
          const auto ring_trace=trace; const auto ring_result=ctx.r3.u32; trace.clear();
          const auto native_result=edf::native::WaitNativeRingRange(FenceReader{base},device,first,second,
            [&] { record(0x394D8); },[&] { record(0x39688); return false; },[&] { record(0x39508); });
          if(trace!=ring_trace || native_result!=ring_result || ctx.r1.u32!=0xf0000 || ctx.lr!=0x12345678 ||
            ctx.r29.u64!=29 || ctx.r30.u64!=30 || ctx.r31.u64!=31) ++failures;
        }
      }
  for(bool generation:{false,true}) {
    std::memset(base,0,1024*1024);
    REX_STORE_U32(device+10768,0x70000); REX_STORE_U32(device+13480,63);
    REX_STORE_U32(0x70004,2); REX_STORE_U32(0x7003c,24);
    unsigned begins=0,polls=0,ends=0;
    auto begin=[&] { ++begins; };
    auto poll=[&] {
      if(++polls>3) throw std::runtime_error("allocator wait failed to observe progress");
      if(polls==3) { REX_STORE_U32(device+10768,0x71000); REX_STORE_U32(0x71004,0); REX_STORE_U32(0x7103c,33); }
      return true;
    };
    auto end=[&] { ++ends; };
    if(generation) edf::native::WaitNativeAllocationGeneration(FenceReader{base},device,16,0,begin,poll,end);
    else if(edf::native::WaitNativeRingRange(FenceReader{base},device,16,16,begin,poll,end)!=32) ++failures;
    if(begins!=1 || polls!=3 || ends!=1) ++failures;
  }
  for(unsigned scenario=0;scenario<5;++scenario) {
    std::memset(base,0,1024*1024);
    REX_STORE_U32(device+10768,0x70000); REX_STORE_U32(0x70000,3);
    REX_STORE_U32(device+10780,7);
    unsigned submits=0,begins=0,polls=0,ends=0;
    edf::native::WaitNativeResourceFence(FenceReader{base},device,7,
      [&] { ++submits; REX_STORE_U32(device+10780,9); if(scenario==0) REX_STORE_U32(0x70000,7); },
      [&] { ++begins; if(scenario==1) REX_STORE_U32(0x70000,7); },
      [&] {
        ++polls;
        if(polls>3) throw std::runtime_error("native fence loop missed completion");
        if(scenario==2 && polls==3) REX_STORE_U32(0x70000,7);
        if(scenario==4) { REX_STORE_U32(device+10768,0x70004); REX_STORE_U32(0x70004,7); }
        return scenario!=3;
      },[&] { ++ends; });
    if(submits!=1 || begins!=(scenario==0?0u:1u) || ends!=begins ||
       polls!=(scenario<2?0u:scenario==2?3u:1u)) ++failures;
  }
  for(uint32_t mode=0;mode<64;++mode) for(uint32_t flags:{0u,0xafu,0xffu}) {
    for(bool native:{false,true}) {
      std::memset(base,0,1024*1024);
      std::memset(base+0xe0000,0xcc,0x10000);
      for(uint32_t i=0;i<6;++i) REX_STORE_U32(0x20000+i*4, i<2?i+1:640+i*31);
      if(mode&1) std::memset(base+0x20000,0,16);
      if(mode&2) REX_STORE_U32(0x20010,0);
      if(mode&4) REX_STORE_U32(0x20014,0);
      REX_STORE_U32(0x20018,(mode&8)?0xffffffffu:0);
      REX_STORE_U32(device+13408,(mode&16)?params:0);
      REX_STORE_U32(params+36,0x01000100);
      REX_STORE_U32(device+13168,1280); REX_STORE_U32(device+13172,720);
      REX_STORE_U32(device+19964,1920); REX_STORE_U32(device+19968,1080);
      REX_STORE_U8(device+10810,flags);
      scaler_video_flags=(mode>>5)&1;
      PPCContext ctx{}; ctx.r1.u64=0xf0000; ctx.lr=0x12345678; ctx.r3.u64=device; ctx.r4.u64=0x20000;
      ctx.r28.u64=28; ctx.r29.u64=29; ctx.r30.u64=30; ctx.r31.u64=31;
      trace.clear();
      if(native) __imp__edf_native_scaler_config(ctx,base);
      else __imp__sub_82151050(ctx,base);
      if(ctx.r1.u32!=0xf0000 || ctx.lr!=0x12345678 || ctx.r3.u32 ||
         ctx.r28.u64!=28 || ctx.r29.u64!=29 || ctx.r30.u64!=30 || ctx.r31.u64!=31) ++failures;
      if(!native) {
        if(std::count(trace.begin(),trace.end(),0x4F3CC)!=1) ++failures;
        std::memcpy(expected.data(),base,expected.size());
      } else if(!trace.empty() || std::memcmp(base,expected.data(),expected.size())) ++failures;
    }
  }
  for(uint32_t seed=0;seed<64;++seed) for(uint32_t words:{0u,7u,200u}) {
    scaler_words=words;
    scaler_video_flags=(seed>>4)&1;
    std::vector<uint32_t> scaler_expected_trace;
    for(bool native:{false,true}) {
      std::memset(base,0,1024*1024);
      std::memset(base+0xe0000,0xcc,0x10000);
      std::memset(base+packet+4,0x5a,4092);
      REX_STORE_U32(device+40,packet);
      REX_STORE_U32(device+19964,640+seed*13);
      REX_STORE_U32(device+19968,480+seed*7);
      REX_STORE_U32(device+19972,(seed&1)?320:640+seed*13);
      for(uint32_t i=0;i<14;++i) REX_STORE_U32(0x20000+i*4,(seed*31+i*97)&2047);
      if(seed&2) std::memset(base+0x20000,0,16); // Default source rectangle.
      if(seed&4) REX_STORE_U32(0x20010,0); // Default output width.
      if(seed&8) REX_STORE_U32(0x20014,0); // Derived height and video-flag branches.
      REX_STORE_U32(device+13212,(seed>>5)&1);
      PPCContext ctx{}; ctx.r1.u64=0xf0000; ctx.lr=0x12345678;
      ctx.r3.u64=device; ctx.r4.u64=640+seed; ctx.r5.u64=480+seed; ctx.r6.u64=0x20000;
      ctx.r18.u64=18; ctx.r29.u64=29; ctx.r30.u64=30; ctx.r31.u64=31;
      trace.clear();
      if(native) __imp__edf_native_scaler_cpu_tail(ctx,base);
      else __imp__edf_test_original_scaler(ctx,base);
      if(ctx.r1.u32!=0xf0000 || ctx.lr!=0x12345678 || ctx.r18.u64!=18 || ctx.r29.u64!=29 ||
         ctx.r30.u64!=30 || ctx.r31.u64!=31 || REX_LOAD_U32(device+40)!=(native?packet:packet+80+words*4)) ++failures;
      if(!native) {
        if(std::count(trace.begin(),trace.end(),0x507F8)!=1 ||
           std::count(trace.begin(),trace.end(),0x50E80)!=1 ||
           std::count(trace.begin(),trace.end(),0xD160)!=1 ||
           std::count(trace.begin(),trace.end(),0x50F98)!=1) ++failures;
        scaler_expected_trace=trace;
        for(uint32_t removed:{0x50E80u,0xD160u,0x50F98u}) std::erase(scaler_expected_trace,removed);
        std::memcpy(expected.data(),base,expected.size());
        for(unsigned i=0;i<4;++i) expected[device+40+i]=uint8_t(packet>>(24-i*8));
      } else {
        if(trace!=scaler_expected_trace ||
           std::memcmp(base,expected.data(),packet+4) ||
           std::memcmp(base+packet+4096,expected.data()+packet+4096,expected.size()-packet-4096)) ++failures;
        for(unsigned i=4;i<4096;++i) if(base[packet+i]!=0x5a) ++failures;
      }
    }
  }
  // Exercise the real reset -> mode -> scissor chain, including tiled paths.
  for(bool rollover:{false,true}) for(uint32_t flags=0;flags<256;++flags)
    for(uint32_t tiles:{0u,1u,3u}) for(uint32_t mode=0;mode<4;++mode) {
      std::memset(base,0,1024*1024);
      std::memset(base+device,0x37,40);
      REX_STORE_U32(device+40,packet);
      REX_STORE_U32(device+48,rollover?packet-4:packet+4096);
      REX_STORE_U32(device+10788,(0xabcdef01u & ~0x3000u) | (mode<<12));
      REX_STORE_U32(device+12168,(flags&128)?0x20000:0);
      REX_STORE_U8(device+10808,flags);
      REX_STORE_U8(device+10811,0x20);
      REX_STORE_U32(device+10308,0x7ffe7fffu);
      REX_STORE_U32(device+10312,0x01000100u);
      REX_STORE_U32(device+12472,tiles);
      for(uint32_t i=0;i<tiles;++i) {
        REX_STORE_U32(device+12476+i*16,i*3);
        REX_STORE_U32(device+12480+i*16,i*2);
        REX_STORE_U32(device+12484+i*16,200+i);
        REX_STORE_U32(device+12488+i*16,150+i);
      }
      std::vector<uint8_t> initial(base,base+0xe0000);
      PPCContext ctx{}; ctx.r1.u64=0xf0000; ctx.r3.u64=device; ctx.lr=0x12345678;
      __imp__sub_821377E8(ctx,base);
      if(REX_LOAD_U32(device+40)<=packet || ctx.r1.u32!=0xf0000 || ctx.lr!=0x12345678) ++failures;
      std::memcpy(expected.data(),base,expected.size());
      std::memcpy(base,initial.data(),initial.size());
      ctx.r3.u64=device;
      __imp__edf_native_present_reset(ctx,base);
      for(unsigned i=0;i<4;++i) expected[device+40+i]=initial[device+40+i];
      if(std::memcmp(base,expected.data(),packet) ||
         std::memcmp(base+packet+4096,expected.data()+packet+4096,expected.size()-packet-4096)) ++failures;
      if(std::memcmp(base+packet,initial.data()+packet,4096)) ++failures;
    }
  for(uint32_t mode:{0u,1u,2u,4u,0x80000000u}) for(uint32_t flags:{0u,0xfecd7006u})
    for(bool rollover:{false,true}) {
      std::vector<uint32_t> expected_trace;
      uint32_t expected_argument=0;
      for(bool native:{false,true}) {
        std::memset(base,0,1024*1024);
        std::memset(base+packet+4,0x5a,4092);
        REX_STORE_U32(device+40,packet);
        REX_STORE_U32(device+48,rollover?packet-4:packet+4096);
        REX_STORE_U32(device+10772,0x70000);
        REX_STORE_U32(device+13220,mode);
        REX_STORE_U32(device+13456,flags);
        REX_STORE_U32(device+11580,0x80200000);
        PPCContext ctx{}; ctx.r1.u64=0xf0000; ctx.lr=0x12345678;
        ctx.r3.u64=device; ctx.r29.u64=29; ctx.r30.u64=30; ctx.r31.u64=31;
        trace.clear(); swap_argument=0;
        const bool active=mode!=0x80000000u && (flags&0xfffff000u);
        if(native) __imp__edf_native_swap_wait_cpu_tail(ctx,base);
        else __imp__edf_test_original_swap_wait(ctx,base);
        if(ctx.r1.u32!=0xf0000 || ctx.lr!=0x12345678 || ctx.r29.u64!=29 ||
           ctx.r30.u64!=30 || ctx.r31.u64!=31 ||
           REX_LOAD_U32(device+40)!=packet+(!native&&active?148u:0u)) ++failures;
        if(!native) {
          expected_trace=trace; expected_argument=swap_argument;
          std::memcpy(expected.data(),base,expected.size());
        } else {
          if(std::count(expected_trace.begin(),expected_trace.end(),0xCF60)!=(active&&rollover?1:0)) ++failures;
          std::erase(expected_trace,0xCF60);
          expected[device+40]=uint8_t(packet>>24); expected[device+41]=uint8_t(packet>>16);
          expected[device+42]=uint8_t(packet>>8); expected[device+43]=uint8_t(packet);
          if(trace!=expected_trace || swap_argument!=expected_argument ||
             std::memcmp(base,expected.data(),packet+4) ||
             std::memcmp(base+packet+4096,expected.data()+packet+4096,expected.size()-packet-4096)) ++failures;
          for(unsigned i=4;i<4096;++i) if(base[packet+i]!=0x5a) ++failures;
        }
      }
    }
  // Compare the host bookkeeping model against the actual retail callback,
  // supplying a controlled scanout phase only to the original GPU MMIO reads.
  // Native integration must supply a host clock and a completed native event;
  // this fixture does not claim either runtime boundary has been replaced.
  for(uint32_t ticks:{0u,1u,65535u,0xfffffffdu})
    for(uint32_t acknowledged:{0u,1u,0xfffffffdu})
      for(uint32_t interval:{0u,1u,3u,65535u})
        for(uint32_t threshold:{0u,1u,50u,100u,127u})
          for(uint32_t phase:{1u,49u,50u,51u,100u}) {
            std::memset(base,0,1024*1024);
            REX_STORE_U32(0x82000720,0x800);
            REX_STORE_U32(0x800,device);
            REX_STORE_U32(device+10772,0x70000);
            REX_STORE_U32(device+15124,ticks);
            REX_STORE_U32(device+15128,acknowledged);
            REX_STORE_U32(device+15132,99);
            REX_STORE_U32(device+15136,0xffffffffu);
            const uint32_t argument=(interval<<16)|threshold;
            edf::native::NativeSwapPacingState model{ticks,acknowledged,99,0xffffffffu};
            const bool ack=model.Complete(argument,phase);
            PPCContext ctx{}; ctx.r3.u64=argument;
            swap_phase=phase; swap_acknowledgements=0;
            __imp__edf_test_original_swap_callback(ctx,base);
            if(swap_acknowledgements!=(ack?1u:0u) ||
               REX_LOAD_U32(device+15124)!=model.ticks ||
               REX_LOAD_U32(device+15128)!=model.acknowledged ||
               REX_LOAD_U32(device+15132)!=model.pending ||
               REX_LOAD_U32(device+15136)!=model.callbacks) ++failures;
          }
  for(bool rollover:{false,true})
  for(uint8_t packet_fill:{uint8_t(0),uint8_t(0x5a)}) for(uint32_t mode=0;mode<256;++mode) {
    std::vector<uint32_t> expected_trace;
    for(bool native:{false,true}) {
      std::memset(base,0,1024*1024);
      // Result initialization must work with dirty stack storage, not depend
      // on the fixture's zero-filled allocation masking an omitted write.
      std::memset(base+0xe0000,0xcc,0x10000);
      std::memset(base+packet+4,packet_fill,4092);
      REX_STORE_U32(device+40,packet);
      REX_STORE_U32(device+48,rollover?packet-4:packet+4096);
      REX_STORE_U32(device+10768,0x70000);
      REX_STORE_U32(device+19956,(mode&1)?1:0);
      REX_STORE_U32(device+20056,(mode&2)?1:0);
      REX_STORE_U32(device+20052,7);
      REX_STORE_U32(device+20048,7);
      REX_STORE_U32(params+32,(mode&4)?0x80000:0);
      REX_STORE_U32(params+36,0x01000100);
      REX_STORE_U32(0x11000,(mode&8)?1:0);
      REX_STORE_U8(device+10809,((mode&16)?0x10:0)|((mode&64)?2:0));
      REX_STORE_U8(device+10810,(mode&32)?0x10:0);
      PPCContext ctx{}; ctx.r1.u64=0xf0000; ctx.lr=0x12345678;
      ctx.r3.u64=device; ctx.r4.u64=params; ctx.r5.u64=0x11000;
      ctx.r25.u64=25; ctx.r26.u64=26; ctx.r27.u64=27; ctx.r28.u64=28;
      ctx.r29.u64=29; ctx.r30.u64=30; ctx.r31.u64=31;
      trace.clear(); swaps=0; scanout_writes=0;
      persist_success=(mode&128)!=0;
      if(native) __imp__edf_native_present_cpu_tail(ctx,base);
      else __imp__sub_82151460(ctx,base);
      if(ctx.r1.u32!=0xf0000 || ctx.lr!=0x12345678 || ctx.r25.u64!=25 || ctx.r26.u64!=26 ||
         ctx.r27.u64!=27 || ctx.r28.u64!=28 || ctx.r29.u64!=29 || ctx.r30.u64!=30 || ctx.r31.u64!=31) ++failures;
      if(!native) {
        if(swaps!=((mode&1)?0u:1u)) ++failures;
        if(scanout_writes!=((mode&64)&&(mode&4)?1u:0u)) ++failures;
        std::memcpy(expected.data(),base,expected.size());
        expected_trace=trace;
        if(std::count(expected_trace.begin(),expected_trace.end(),0x515A8)!=((mode&1)?0:1)) ++failures;
        std::erase(expected_trace,0x515A8); // Native result initialization has no SDK command-buffer request.
        if(std::count(expected_trace.begin(),expected_trace.end(),0x51080)!=1) ++failures;
        std::erase(expected_trace,0x51080); // Deliberately removed Xbox EDRAM maintenance.
        if(std::count(expected_trace.begin(),expected_trace.end(),0xD160)!=((mode&1)?0:1)) ++failures;
        std::erase(expected_trace,0xD160); // No reservation for the removed swap packet.
        if(std::count(expected_trace.begin(),expected_trace.end(),0x8215184C)!=(rollover && (mode&4)?1:0) ||
           std::count(expected_trace.begin(),expected_trace.end(),0x821518CC)!=(rollover && (mode&(8|32))?1:0) ||
           std::count(expected_trace.begin(),expected_trace.end(),0x8215194C)!=(rollover && (mode&16)?1:0)) ++failures;
        std::erase(expected_trace,0x8215184C);
        std::erase(expected_trace,0x821518CC);
        std::erase(expected_trace,0x8215194C);
        if(std::count(expected_trace.begin(),expected_trace.end(),0x51990)!=((mode&16)?1:0) ||
           std::count(expected_trace.begin(),expected_trace.end(),0x519A4)!=((mode&16) && persist_success?1:0)) ++failures;
        std::erase(expected_trace,0x51990); // Native image ownership requires no Xbox placeholder allocation.
        std::erase(expected_trace,0x519A4); // No allocation was made, so there is nothing to release.
        if(std::count(expected_trace.begin(),expected_trace.end(),0x4F3CC)!=((mode&8)?1:0)) ++failures;
        std::erase(expected_trace,0x4F3CC); // Configuration CPU effects are real on both sides; no native Xbox notification.
        std::erase(expected_trace,0x4F298); // Only used to construct the removed notification descriptor.
        for(uint32_t call:{0x82137818u,0x82142360u,0x82134C0Cu,0x82134E14u}) std::erase(expected_trace,call);
        if(std::count(expected_trace.begin(),expected_trace.end(),0x390B8)!=((mode&2)?2:0) ||
           std::count(expected_trace.begin(),expected_trace.end(),0x821390E8)!=(rollover && (mode&2)?2:0)) ++failures;
        std::erase(expected_trace,0x821390E8);
        const uint32_t removed_bytes=64u+((mode&2)?32u:0u)+((mode&1)?0u:256u)+((mode&4)?8u:0u)+((mode&(8|32))?40u:0u)+((mode&16)?16u:0u);
        const uint32_t native_cursor=REX_LOAD_U32(device+40)-removed_bytes;
        if(native_cursor!=packet) ++failures;
        expected[device+40]=uint8_t(native_cursor>>24); expected[device+41]=uint8_t(native_cursor>>16);
        expected[device+42]=uint8_t(native_cursor>>8); expected[device+43]=uint8_t(native_cursor);
      } else {
        if(swaps || scanout_writes || trace!=expected_trace || REX_LOAD_U32(device+40)!=packet) ++failures;
        // Exclude only the GPU packet arena after normalizing the explicitly
        // checked reservation difference. All other CPU state must agree.
        if(std::memcmp(base,expected.data(),packet+4) ||
           std::memcmp(base+packet+4096,expected.data()+packet+4096,expected.size()-packet-4096)) ++failures;
        for(unsigned i=4;i<4096;++i) if(base[packet+i]!=packet_fill) ++failures;
      }
    }
  }
  for(uint32_t format:{0u,0x28280106u,0x28280136u}) for(uint32_t state:{0u,0x20000u})
    for(bool rollover:{false,true}) {
      std::vector<uint32_t> expected_trace;
      for(bool native:{false,true}) {
        std::memset(base,0,1024*1024);
        std::memset(base+packet+4,0x5a,4092);
        REX_STORE_U32(device+40,packet);
        REX_STORE_U32(device+48,rollover?packet-4:packet+4096);
        PPCContext ctx{}; ctx.r1.u64=0xf0000; ctx.lr=0x12345678;
        ctx.r3.u64=device; ctx.r4.u64=format; ctx.r5.u64=state;
        ctx.r28.u64=28; ctx.r29.u64=29; ctx.r30.u64=30; ctx.r31.u64=31;
        trace.clear();
        if(native) __imp__edf_native_color_cpu_tail(ctx,base);
        else __imp__edf_test_original_color_tail(ctx,base);
        if(ctx.r1.u32!=0xf0000 || ctx.lr!=0x12345678 || ctx.r28.u64!=28 || ctx.r29.u64!=29 ||
           ctx.r30.u64!=30 || ctx.r31.u64!=31 || REX_LOAD_U32(device+40)!=(native?packet:packet+24)) ++failures;
        if(!native) {
          expected_trace=trace;
          std::memcpy(expected.data(),base,expected.size());
        } else {
          if(std::count(expected_trace.begin(),expected_trace.end(),0xCF60)!=(rollover?1:0)) ++failures;
          std::erase(expected_trace,0xCF60); // No native format packet capacity flush.
          expected[device+40]=uint8_t(packet>>24); expected[device+41]=uint8_t(packet>>16);
          expected[device+42]=uint8_t(packet>>8); expected[device+43]=uint8_t(packet);
          if(trace!=expected_trace || std::memcmp(base,expected.data(),packet+4) ||
             std::memcmp(base+packet+28,expected.data()+packet+28,expected.size()-packet-28)) ++failures;
          for(unsigned i=4;i<4096;++i) if(base[packet+i]!=0x5a) ++failures;
        }
      }
    }
  for(bool pwl:{false,true}) for(uint32_t seed:{0u,177u,65535u}) {
    std::vector<uint32_t> expected_trace;
    for(bool native:{false,true}) {
      std::memset(base,0,1024*1024);
      std::memset(base+packet+4,0x5a,16380);
      for(unsigned i=0;i<768;++i) REX_STORE_U16(0x20000+i*2,uint16_t(seed+i*193));
      const auto gamma=edf::native::NativeDisplayGamma::Decode({base+0x20000,1536},
        pwl?edf::native::NativeDisplayGamma::Mode::Piecewise128:edf::native::NativeDisplayGamma::Mode::Table256);
      PPCContext ctx{}; ctx.r1.u64=0xf0000; ctx.lr=0x12345678;
      ctx.r3.u64=device; ctx.r4.u64=native?0xfffffffcu:0x20000;
      ctx.r29.u64=29; ctx.r30.u64=30; ctx.r31.u64=31;
      trace.clear();
      if(native) {
        if(pwl) __imp__edf_native_gamma_pwl_cpu_tail(ctx,base);
        else __imp__edf_native_gamma_table_cpu_tail(ctx,base);
      } else {
        if(pwl) __imp__edf_test_original_gamma_pwl(ctx,base);
        else __imp__edf_test_original_gamma_table(ctx,base);
      }
      const uint32_t bytes=(pwl?1413:2309)*4;
      if(REX_LOAD_U32(device+40)!=(native?0u:packet+bytes) || ctx.r3.s64!=(pwl?-1:6434) ||
         ctx.r1.u32!=0xf0000 || ctx.lr!=0x12345678 ||
         ctx.r29.u64!=29 || ctx.r30.u64!=30 || ctx.r31.u64!=31) ++failures;
      if(!native) {
        expected_trace=trace;
        std::memcpy(expected.data(),base,expected.size());
        for(unsigned i=0;i<(pwl?128u:256u);++i) {
          if(pwl) {
            for(unsigned channel=0;channel<3;++channel) {
              const auto packed=REX_LOAD_U32(packet+28+i*44+channel*4);
              for(unsigned fraction=0;fraction<8;++fraction)
                if(gamma.EvaluateCode(channel,i*8+fraction)!=(packed&0xffc0)+fraction*((packed>>16)&0xffc0)/8) ++failures;
            }
          } else {
            const auto packed=REX_LOAD_U32(packet+28+i*36);
            for(unsigned channel=0;channel<3;++channel)
              if(gamma.EvaluateCode(channel,i)!=((packed>>(20-channel*10))&1023)) ++failures;
          }
        }
      } else {
        if(!trace.empty() || expected_trace!=std::vector<uint32_t>{0xD160}) ++failures;
        // Only the original GPU upload advances command storage. Normalize
        // that deliberate difference after checking both cursor contracts.
        std::memset(expected.data()+device+40,0,4);
        if(std::memcmp(base,expected.data(),packet+4) ||
           std::memcmp(base+packet+bytes+4,expected.data()+packet+bytes+4,expected.size()-packet-bytes-4)) ++failures;
        for(unsigned i=4;i<16384;++i) if(base[packet+i]!=0x5a) ++failures;
      }
    }
  }
  VirtualFree(base,0,MEM_RELEASE);
  std::cout << "Native presentation CPU failures: " << failures << '\n';
  return failures?1:0;
}
