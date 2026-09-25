// Skeletal animation: the per-slot bone evaluation 821CE848 and the hierarchy
// propagation 821D1688, natively (native_skeletal.h) or audited against the
// recompiled originals, plus their CPU timings. Simulation code: it runs inside
// each object's update (slot 3), on the engine thread, and its outputs (node
// locals and worlds) feed hit shapes and attach points read later in the same
// step, so the native versions are bit-exact, same-thread, same-order
// replacements rather than a different schedule.
//
//   edf_native_skeletal_eval         native 821CE848
//   edf_native_skeletal_propagation  native 821D1688 (every entry: 821C8C58's
//                                    roots, 821D16E8's grandchildren, ...)
//   edf_native_skeletal_batched      the evaluation four records at a time
//                                    (NativeSkeletalEvaluate); off: one at a
//                                    time (NativeSkeletalEvaluateScalar). The
//                                    same bits either way.
//   edf_native_skeletal_rotation_cache  memoise the constant rotations (channel
//                                    flag 16) by their angles' bits (batched
//                                    evaluation only; per thread)
//   edf_native_skeletal_audit        each call computes natively into guest
//                                    memory, snapshots that, restores the
//                                    inputs, runs the original and compares
//                                    every output byte (a NaN matches any
//                                    NaN); the original's result is kept, so
//                                    the game runs the guest path. Overrides
//                                    the two above.
//   edf_native_skeletal_matrix       native 821C8198, the 4x4 multiply, for
//                                    every caller (the skinning palette 821C9478
//                                    in the frame transition above all), with
//                                    its guest stack writes; audited like the
//                                    others under edf_native_skeletal_audit
//   edf_native_hook_timings          per-thread inclusive wall time of
//                                    Animation_Update 8210B4E8, 821CE848 and
//                                    top-level 821D1688 entries, every 5 s.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../native_skeletal.h"
#include "../../bridge/native_cvars.h"
#include <rex/hook.h>
#include <rex/cvar.h>
#include <rex/ppc/func.h>
#include <rex/logging.h>
#include <windows.h>
#include <stdlib.h>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_set>
#include <vector>

REXCVAR_DEFINE_BOOL(edf_native_skeletal_eval,true,"EDF2027",
  "Evaluate animation slots (821CE848: DXA key lerp, Euler rotation build, blend, node locals) natively, bit for bit as the recompiled code");
REXCVAR_DEFINE_BOOL(edf_native_skeletal_propagation,true,"EDF2027",
  "Propagate model hierarchies (821D1688: node world = local x parent, recursively) natively, bit for bit as the recompiled code");
REXCVAR_DEFINE_BOOL(edf_native_skeletal_matrix,true,"EDF2027",
  "Run every 4x4 multiply 821C8198 (hierarchy worlds, skinning palettes 821C9478, view and effect matrices) natively with its guest memory effects, bit for bit");
REXCVAR_DEFINE_BOOL(edf_native_skeletal_batched,true,"EDF2027",
  "Evaluate animation slots natively four records at a time (the rotations in AVX2 lanes); off: one record at a time. The same bits either way");
REXCVAR_DEFINE_BOOL(edf_native_skeletal_rotation_cache,true,"EDF2027",
  "Memoise the batched native evaluation's constant Euler rotations (channel flag 16) by their angles' bits, a pure function of them under the usual constants and MXCSR (checked)");
REXCVAR_DEFINE_BOOL(edf_native_skeletal_audit,false,"EDF2027",
  "Run the native skeletal evaluation and propagation beside the originals and compare every output byte; the originals' results are kept (development)");

REX_EXTERN(__imp__sub_8210B4E8);
REX_EXTERN(__imp__sub_821CE848);
REX_EXTERN(__imp__sub_821D1688);
REX_EXTERN(__imp__sub_821C8198);

namespace {
using namespace edf::native;
using Clock=std::chrono::steady_clock;

// Guest memory as the recompiled code addresses it (REX_RAW_ADDR: the physical
// 0xE0000000+ view is 4 KiB higher on Windows).
struct GuestDirect {
  uint8_t* base;
  uint8_t* At(uint32_t address) const { return base+address+(address>=0xE0000000u?0x1000u:0u); }
  uint8_t U8(uint32_t address) const { return *At(address); }
  uint16_t U16(uint32_t address) const { uint16_t v; std::memcpy(&v,At(address),2); return _byteswap_ushort(v); }
  uint32_t U32(uint32_t address) const { uint32_t v; std::memcpy(&v,At(address),4); return _byteswap_ulong(v); }
  uint64_t U64(uint32_t address) const { uint64_t v; std::memcpy(&v,At(address),8); return _byteswap_uint64(v); }
  void StoreU8(uint32_t address,uint8_t value) const { *At(address)=value; }
  void StoreU32(uint32_t address,uint32_t value) const { value=_byteswap_ulong(value); std::memcpy(At(address),&value,4); }
  void StoreU64(uint32_t address,uint64_t value) const { value=_byteswap_uint64(value); std::memcpy(At(address),&value,8); }
  const uint8_t* Row(uint32_t address) const { return At(address); }
  // A 16-byte aligned row never straddles 0xE0000000 (native_skeletal.h checks
  // the unaligned rows it loads and stores through Row/StoreRow).
  void StoreRow(uint32_t address,__m128i bytes) const { _mm_storeu_si128(reinterpret_cast<__m128i*>(At(address)),bytes); }
  // [address, address+size) through one host pointer, unless it crosses the
  // jump at 0xE0000000.
  uint8_t* Host(uint32_t address,uint32_t size) const {
    return address<0xE0000000u && address+size>0xE0000000u?nullptr:At(address);
  }
  void Prefetch(uint32_t address) const { _mm_prefetch(reinterpret_cast<const char*>(At(address)),_MM_HINT_T0); }
};

// The object Animation_Update is running for (mismatch detail only).
thread_local constinit uint32_t skeletal_object=0;
// Set while the original 821D1688 runs under the hook: its recursion re-enters
// the hook, which must then only forward.
thread_local constinit uint32_t skeletal_passthrough=0;

// ---- Timings (edf_native_hook_timings). ----
struct SkeletalTimes {
  uint64_t update_calls=0,eval_calls=0,eval_records=0,eval_blended_calls=0,eval_blended_records=0,
    propagate_calls=0,propagate_nodes=0;
  double update_ms=0,eval_ms=0,propagate_ms=0;
  // Audit mode: the native and the original halves of the same calls.
  double audit_eval_native_ms=0,audit_eval_guest_ms=0,audit_propagate_native_ms=0,audit_propagate_guest_ms=0;
  Clock::time_point reported{};
};
SkeletalTimes& Times() {
  static thread_local SkeletalTimes times;
  return times;
}
// The constant-rotation memo (native_skeletal.h), per thread; null when off.
NativeSkeletalRotationCache& RotationCache() { static thread_local NativeSkeletalRotationCache cache; return cache; }
NativeSkeletalRotationCache* SelectedRotationCache() {
  return REXCVAR_GET(edf_native_skeletal_rotation_cache)?&RotationCache():nullptr;
}
const char* SkeletalMode() {
  if(REXCVAR_GET(edf_native_skeletal_audit)) return "audit";
  const bool eval=REXCVAR_GET(edf_native_skeletal_eval),propagation=REXCVAR_GET(edf_native_skeletal_propagation);
  if(!eval) return propagation?"native_propagation":"guest";
  const bool batched=REXCVAR_GET(edf_native_skeletal_batched),cached=REXCVAR_GET(edf_native_skeletal_rotation_cache);
  if(propagation) return batched?(cached?"native":"native_uncached"):"native_scalar";
  return batched?(cached?"native_eval":"native_eval_uncached"):"native_eval_scalar";
}
void MaybeReportTimes(SkeletalTimes& t,Clock::time_point now) {
  if(t.reported==Clock::time_point{}) { t.reported=now; return; }
  if(now-t.reported<std::chrono::seconds(5)) return;
  const double seconds=std::chrono::duration<double>(now-t.reported).count();
  REXLOG_INFO("Native skeletal timing: thread={} mode={} interval_s={:.2f} update calls={} ms={:.3f} "
    "eval calls={} records={} ms={:.3f} us/record={:.4f} propagate calls={} nodes={} ms={:.3f} "
    "audit[eval native/guest ms={:.3f}/{:.3f} propagate native/guest ms={:.3f}/{:.3f}] "
    "blended calls={} records={} rotation_cache hits={} misses={} "
    "(inclusive CPU wall time; eval and propagate are inside update when called from it)",
    GetCurrentThreadId(),SkeletalMode(),seconds,t.update_calls,t.update_ms,t.eval_calls,t.eval_records,t.eval_ms,
    t.eval_records?t.eval_ms*1000.0/double(t.eval_records):0.0,t.propagate_calls,t.propagate_nodes,t.propagate_ms,
    t.audit_eval_native_ms,t.audit_eval_guest_ms,t.audit_propagate_native_ms,t.audit_propagate_guest_ms,
    t.eval_blended_calls,t.eval_blended_records,RotationCache().hits,RotationCache().misses);
  RotationCache().hits=RotationCache().misses=0;
  const auto reported=now;
  t=SkeletalTimes{};
  t.reported=reported;
}
class SkeletalTiming {
 public:
  explicit SkeletalTiming(bool on):on_(on) { if(on_) start_=Clock::now(); }
  double Finish(Clock::time_point& now) {
    now=Clock::now();
    return std::chrono::duration<double,std::milli>(now-start_).count();
  }
  bool on() const { return on_; }
 private:
  bool on_;
  Clock::time_point start_{};
};

// The native evaluation the cvars select (the audit checks the same one).
template<class M>
NativeSkeletalEvalStats EvaluateSelected(const M& m,uint32_t slot) {
  if(REXCVAR_GET(edf_native_skeletal_batched)) return NativeSkeletalEvaluate(m,slot,SelectedRotationCache());
  return NativeSkeletalEvaluateScalar(m,slot);
}

// ---- Audit. ----
struct AuditRegion { uint32_t address,size; int32_t record; uint32_t kind; };  // kind 0 slot byte, 1 record, 2 local, 3 world
struct AuditCounters {
  std::atomic<uint64_t> eval_calls{0},eval_records{0},eval_bytes{0},eval_mismatched_calls{0},eval_mismatched_words{0},
    eval_skipped{0},matrix_calls{0},matrix_mismatched_calls{0},propagate_calls{0},propagate_nodes{0},propagate_bytes{0},propagate_mismatched_calls{0},
    propagate_mismatched_words{0},propagate_skipped{0},logged{0};
  std::atomic<int64_t> reported{0};
  // Distinct objects (Animation_Update's r3) and propagation roots audited.
  std::mutex mutex;
  std::unordered_set<uint32_t> objects,roots;
};
void NoteAudited(std::unordered_set<uint32_t> AuditCounters::*set,uint32_t address);
AuditCounters& Audit() { static AuditCounters counters; return counters; }
void MaybeReportAudit() {
  auto& a=Audit();
  const int64_t now=std::chrono::duration_cast<std::chrono::seconds>(Clock::now().time_since_epoch()).count();
  int64_t last=a.reported.load(std::memory_order_relaxed);
  if(last==0) { a.reported.compare_exchange_strong(last,now); return; }
  if(now-last<10 || !a.reported.compare_exchange_strong(last,now)) return;
  size_t objects=0,roots=0;
  {
    std::lock_guard lock(a.mutex);
    objects=a.objects.size(); roots=a.roots.size();
  }
  REXLOG_INFO("Native skeletal audit: eval calls={} records={} bytes={} mismatched_calls={} mismatched_words={} skipped={} "
    "propagate calls={} nodes={} bytes={} mismatched_calls={} mismatched_words={} skipped={} distinct_objects={} "
    "distinct_roots={} matrix calls={} mismatched_calls={} (cumulative)",
    a.eval_calls.load(),a.eval_records.load(),a.eval_bytes.load(),a.eval_mismatched_calls.load(),
    a.eval_mismatched_words.load(),a.eval_skipped.load(),a.propagate_calls.load(),a.propagate_nodes.load(),
    a.propagate_bytes.load(),a.propagate_mismatched_calls.load(),a.propagate_mismatched_words.load(),
    a.propagate_skipped.load(),objects,roots,a.matrix_calls.load(),a.matrix_mismatched_calls.load());
}
void NoteAudited(std::unordered_set<uint32_t> AuditCounters::*set,uint32_t address) {
  auto& a=Audit();
  std::lock_guard lock(a.mutex);
  (a.*set).insert(address);
}
void Snapshot(const GuestDirect& m,const std::vector<AuditRegion>& regions,std::vector<uint8_t>& out) {
  out.clear();
  for(const auto& region:regions) for(uint32_t i=0;i<region.size;++i) out.push_back(m.U8(region.address+i));
}
void Restore(const GuestDirect& m,const std::vector<AuditRegion>& regions,const std::vector<uint8_t>& bytes) {
  size_t at=0;
  for(const auto& region:regions) for(uint32_t i=0;i<region.size;++i) m.StoreU8(region.address+i,bytes[at++]);
}
bool IsNan(uint32_t bits) { return (bits&0x7F800000u)==0x7F800000u && (bits&0x007FFFFFu); }
// Compares native and guest bytes region by region, word by word (guest-endian
// words; a NaN matches any NaN), and calls report(region, offset, native, guest)
// for each differing word. Returns the differing words.
template<class Report>
uint64_t Compare(const std::vector<AuditRegion>& regions,const std::vector<uint8_t>& native,const std::vector<uint8_t>& guest,Report&& report) {
  uint64_t words=0;
  size_t at=0;
  for(const auto& region:regions) {
    if(region.size%4) {
      for(uint32_t i=0;i<region.size;++i) if(native[at+i]!=guest[at+i]) { ++words; report(region,i,native[at+i],guest[at+i]); }
    } else {
      for(uint32_t i=0;i<region.size;i+=4) {
        const auto word=[&](const std::vector<uint8_t>& b) {
          return uint32_t(b[at+i])<<24|uint32_t(b[at+i+1])<<16|uint32_t(b[at+i+2])<<8|uint32_t(b[at+i+3]); };
        const uint32_t n=word(native),g=word(guest);
        if(n!=g && !(IsNan(n) && IsNan(g))) { ++words; report(region,i,n,g); }
      }
    }
    at+=region.size;
  }
  return words;
}
constexpr uint32_t kAuditMaxRecords=4096,kAuditMaxNodes=16384,kAuditMaxDepth=256,kAuditMaxLogs=200;

void AuditEvaluate(PPCContext& ctx,uint8_t* base) {
  using S=NativeSkeletal;
  const GuestDirect m{base};
  const uint32_t slot=ctx.r3.u32;
  auto& audit=Audit();
  thread_local std::vector<AuditRegion> regions;
  thread_local std::vector<uint8_t> before,native,guest;
  regions.clear();
  const uint32_t palette=m.U32(slot+S::slot_palette);
  const uint32_t records=m.U32(slot+S::slot_records),count=m.U32(slot+S::slot_record_count);
  if(palette && count>kAuditMaxRecords) {
    audit.eval_skipped.fetch_add(1,std::memory_order_relaxed);
    __imp__sub_821CE848(ctx,base);
    return;
  }
  regions.push_back({slot+S::slot_blending,1,-1,0});
  if(palette) for(uint32_t i=0;i<count;++i) {
    const uint32_t record=records+i*S::record_size;
    regions.push_back({record,S::record_size,int32_t(i),1});
    const uint32_t bone=m.U32(record+S::record_bone);
    if(m.U8(bone+S::bone_enabled)) regions.push_back({m.U32(bone+S::bone_local),64,int32_t(i),2});
  }
  Snapshot(m,regions,before);
  const bool timed=REXCVAR_GET(edf_native_hook_timings);
  auto t0=timed?Clock::now():Clock::time_point{};
  ctx.fpscr.disableFlushMode();
  EvaluateSelected(m,slot);
  auto t1=timed?Clock::now():Clock::time_point{};
  Snapshot(m,regions,native);
  Restore(m,regions,before);
  auto t2=timed?Clock::now():Clock::time_point{};
  __imp__sub_821CE848(ctx,base);
  auto t3=timed?Clock::now():Clock::time_point{};
  if(timed) {
    auto& t=Times();
    t.audit_eval_native_ms+=std::chrono::duration<double,std::milli>(t1-t0).count();
    t.audit_eval_guest_ms+=std::chrono::duration<double,std::milli>(t3-t2).count();
  }
  Snapshot(m,regions,guest);
  uint64_t bytes=0;
  for(const auto& region:regions) bytes+=region.size;
  audit.eval_calls.fetch_add(1,std::memory_order_relaxed);
  if(skeletal_object) NoteAudited(&AuditCounters::objects,skeletal_object);
  audit.eval_records.fetch_add(palette?count:0,std::memory_order_relaxed);
  audit.eval_bytes.fetch_add(bytes,std::memory_order_relaxed);
  const uint64_t words=Compare(regions,native,guest,[&](const AuditRegion& region,uint32_t offset,uint32_t n,uint32_t g) {
    if(audit.logged.fetch_add(1,std::memory_order_relaxed)>=kAuditMaxLogs) return;
    const uint32_t object=skeletal_object;
    const uint32_t record=region.record>=0?records+uint32_t(region.record)*S::record_size:0;
    const uint32_t channel=record?m.U32(record+S::record_channel):0;
    REXLOG_WARN("Native skeletal audit mismatch: eval object={:#x} vtable={:#x} slot={:#x} slot_index={} record={} "
      "bone={:#x} channel={:#x} flags={:#x} region={} address={:#x} offset={} native={:#010x} guest={:#010x} "
      "time={} end={} blending={} palette={:#x}",
      object,object?m.U32(object):0,slot,object?int64_t(slot-m.U32(object+1116))/104:-1,region.record,
      record?m.U32(record+S::record_bone):0,channel,channel?m.U32(channel+S::channel_flags):0,
      region.kind==0?"slot_blending":region.kind==1?"record":"local",region.address,offset,n,g,
      std::bit_cast<float>(m.U32(slot+S::slot_time)),std::bit_cast<float>(m.U32(slot+S::slot_end)),
      uint32_t(before[0]),palette);
  });
  if(words) {
    audit.eval_mismatched_calls.fetch_add(1,std::memory_order_relaxed);
    audit.eval_mismatched_words.fetch_add(words,std::memory_order_relaxed);
  }
  MaybeReportAudit();
}

// The nodes 821D1688(node) writes, in its order; false if implausible.
bool CollectNodes(const GuestDirect& m,uint32_t node,uint32_t depth,std::vector<AuditRegion>& regions) {
  using S=NativeSkeletal;
  if(depth>kAuditMaxDepth || regions.size()>=kAuditMaxNodes || !node) return false;
  regions.push_back({node+S::node_world,64,int32_t(regions.size()),3});
  const uint32_t count=m.U32(node+S::node_child_count);
  if(count>kAuditMaxNodes) return false;
  const uint32_t children=m.U32(node+S::node_children);
  for(uint32_t i=0;i<count;++i) if(!CollectNodes(m,children+i*S::node_size,depth+1,regions)) return false;
  return true;
}

void AuditPropagate(PPCContext& ctx,uint8_t* base) {
  const GuestDirect m{base};
  const uint32_t node=ctx.r3.u32,parent=ctx.r4.u32;
  auto& audit=Audit();
  thread_local std::vector<AuditRegion> regions;
  thread_local std::vector<uint8_t> before,native,guest;
  regions.clear();
  if(!CollectNodes(m,node,0,regions)) {
    audit.propagate_skipped.fetch_add(1,std::memory_order_relaxed);
    ++skeletal_passthrough;
    __imp__sub_821D1688(ctx,base);
    --skeletal_passthrough;
    return;
  }
  Snapshot(m,regions,before);
  const bool timed=REXCVAR_GET(edf_native_hook_timings);
  auto t0=timed?Clock::now():Clock::time_point{};
  ctx.fpscr.enableFlushMode();
  NativeSkeletalPropagate(m,node,parent);
  auto t1=timed?Clock::now():Clock::time_point{};
  Snapshot(m,regions,native);
  Restore(m,regions,before);
  auto t2=timed?Clock::now():Clock::time_point{};
  ++skeletal_passthrough;
  __imp__sub_821D1688(ctx,base);
  --skeletal_passthrough;
  auto t3=timed?Clock::now():Clock::time_point{};
  if(timed) {
    auto& t=Times();
    t.audit_propagate_native_ms+=std::chrono::duration<double,std::milli>(t1-t0).count();
    t.audit_propagate_guest_ms+=std::chrono::duration<double,std::milli>(t3-t2).count();
  }
  Snapshot(m,regions,guest);
  audit.propagate_calls.fetch_add(1,std::memory_order_relaxed);
  NoteAudited(&AuditCounters::roots,node);
  audit.propagate_nodes.fetch_add(regions.size(),std::memory_order_relaxed);
  audit.propagate_bytes.fetch_add(regions.size()*64,std::memory_order_relaxed);
  const uint64_t words=Compare(regions,native,guest,[&](const AuditRegion& region,uint32_t offset,uint32_t n,uint32_t g) {
    if(audit.logged.fetch_add(1,std::memory_order_relaxed)>=kAuditMaxLogs) return;
    REXLOG_WARN("Native skeletal audit mismatch: propagate object={:#x} root={:#x} parent={:#x} node_index={} "
      "world={:#x} element={} native={:#010x} guest={:#010x} mxcsr={:#x}",
      skeletal_object,node,parent,region.record,region.address,offset/4,n,g,_mm_getcsr());
  });
  if(words) {
    audit.propagate_mismatched_calls.fetch_add(1,std::memory_order_relaxed);
    audit.propagate_mismatched_words.fetch_add(words,std::memory_order_relaxed);
  }
  MaybeReportAudit();
}

void AuditMultiply(PPCContext& ctx,uint8_t* base) {
  const GuestDirect m{base};
  const uint32_t out=ctx.r3.u32,a=ctx.r4.u32,b=ctx.r5.u32,stack=ctx.r1.u32;
  auto& audit=Audit();
  thread_local std::vector<AuditRegion> regions;
  thread_local std::vector<uint8_t> before,native,guest;
  regions.clear();
  regions.push_back({out,16u-(out&0xFu),0,4});
  for(uint32_t k=1;k<4;++k) regions.push_back({out+16*k,16u-(out&0xFu),int32_t(k),4});
  regions.push_back({stack+20,4,-1,5});
  for(uint32_t k=0;k<3;++k) regions.push_back({(stack-48+16*k)&~0xFu,16,int32_t(k),5});
  Snapshot(m,regions,before);
  ctx.fpscr.enableFlushMode();
  NativeSkeletalMultiply(m,out,a,b,stack);
  Snapshot(m,regions,native);
  Restore(m,regions,before);
  __imp__sub_821C8198(ctx,base);
  Snapshot(m,regions,guest);
  audit.matrix_calls.fetch_add(1,std::memory_order_relaxed);
  // Byte compare (a partial row is not whole words); NaN rows are rare and
  // would show up as mismatches here rather than pass silently.
  uint64_t differing=0;
  for(size_t i=0;i<native.size();++i) differing+=native[i]!=guest[i];
  if(differing) {
    audit.matrix_mismatched_calls.fetch_add(1,std::memory_order_relaxed);
    if(audit.logged.fetch_add(1,std::memory_order_relaxed)<kAuditMaxLogs)
      REXLOG_WARN("Native skeletal audit mismatch: matrix out={:#x} a={:#x} b={:#x} r1={:#x} lr={:#x} differing_bytes={}",
        out,a,b,stack,uint32_t(ctx.lr),differing);
  }
  MaybeReportAudit();
}

// Out of line, so the compiler cannot move the vector math across the flush-mode
// change (ldmxcsr) the hook makes before calling them.
__declspec(noinline) void RunNativeEvaluate(uint8_t* base,uint32_t slot,SkeletalTimes* times) {
  const auto stats=EvaluateSelected(GuestDirect{base},slot);
  if(times) {
    times->eval_records+=stats.records;
    if(stats.blended) { ++times->eval_blended_calls; times->eval_blended_records+=stats.records; }
  }
}
__declspec(noinline) void RunNativeMultiply(uint8_t* base,uint32_t out,uint32_t a,uint32_t b,uint32_t stack) {
  NativeSkeletalMultiply(GuestDirect{base},out,a,b,stack);
}
__declspec(noinline) uint32_t RunNativePropagate(uint8_t* base,uint32_t node,uint32_t parent) {
  return NativeSkeletalPropagate(GuestDirect{base},node,parent);
}
}  // namespace

// clAnimationObject_Base::Animation_Update: for each active slot, advance
// (821CE760), evaluate (821CE848), events (8210AA00). Timed and tagged only.
REX_HOOK_RAW(sub_8210B4E8) {
  const uint32_t previous=skeletal_object;
  skeletal_object=ctx.r3.u32;
  SkeletalTiming timing(REXCVAR_GET(edf_native_hook_timings));
  __imp__sub_8210B4E8(ctx,base);
  skeletal_object=previous;
  if(timing.on()) {
    Clock::time_point now;
    const double ms=timing.Finish(now);
    auto& t=Times();
    ++t.update_calls; t.update_ms+=ms;
    MaybeReportTimes(t,now);
  }
}

REX_HOOK_RAW(sub_821CE848) {
  SkeletalTiming timing(REXCVAR_GET(edf_native_hook_timings));
  SkeletalTimes* times=timing.on()?&Times():nullptr;
  if(REXCVAR_GET(edf_native_skeletal_audit)) {
    AuditEvaluate(ctx,base);
  } else if(REXCVAR_GET(edf_native_skeletal_eval)) {
    // The original's first FPU instruction turns the guest flush mode off, and
    // nothing in it turns it back on.
    ctx.fpscr.disableFlushMode();
    RunNativeEvaluate(base,ctx.r3.u32,times);
  } else {
    if(times) {
      const GuestDirect m{base};
      if(m.U32(ctx.r3.u32+NativeSkeletal::slot_palette)) times->eval_records+=m.U32(ctx.r3.u32+NativeSkeletal::slot_record_count);
    }
    __imp__sub_821CE848(ctx,base);
  }
  if(times) {
    Clock::time_point now;
    times->eval_ms+=timing.Finish(now);
    ++times->eval_calls;
    MaybeReportTimes(*times,now);
  }
}

REX_HOOK_RAW(sub_821D1688) {
  if(skeletal_passthrough) { __imp__sub_821D1688(ctx,base); return; }
  SkeletalTiming timing(REXCVAR_GET(edf_native_hook_timings));
  uint32_t nodes=0;
  if(REXCVAR_GET(edf_native_skeletal_audit)) {
    AuditPropagate(ctx,base);
  } else if(REXCVAR_GET(edf_native_skeletal_propagation)) {
    // 821C8198, the first thing the original does, turns the guest flush mode
    // on (vmsum4fp128) and nothing in the walk turns it off.
    ctx.fpscr.enableFlushMode();
    nodes=RunNativePropagate(base,ctx.r3.u32,ctx.r4.u32);
  } else {
    ++skeletal_passthrough;
    __imp__sub_821D1688(ctx,base);
    --skeletal_passthrough;
  }
  if(timing.on()) {
    Clock::time_point now;
    auto& t=Times();
    t.propagate_ms+=timing.Finish(now);
    ++t.propagate_calls; t.propagate_nodes+=nodes;
    MaybeReportTimes(t,now);
  }
}

// The 4x4 multiply (out=r3, a=r4, b=r5): out = a x b. Its first vector
// instruction turns the guest flush mode on; nothing in it turns it off. The
// original's own calls under the skeletal hooks' guest runs come here too.
REX_HOOK_RAW(sub_821C8198) {
  if(REXCVAR_GET(edf_native_skeletal_audit)) { AuditMultiply(ctx,base); return; }
  if(REXCVAR_GET(edf_native_skeletal_matrix)) {
    ctx.fpscr.enableFlushMode();
    RunNativeMultiply(base,ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r1.u32);
    return;
  }
  __imp__sub_821C8198(ctx,base);
}
