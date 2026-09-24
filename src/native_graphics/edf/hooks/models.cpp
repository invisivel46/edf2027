// Models: construction and retirement of their buffers, publication, pose and the model render hook.
// Moved from guest_shader_bridge.cpp unchanged (stage 3 of its split); what this file shares with the bridge
// and the other hook files is in bridge/bridge_shared.h.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../bridge/bridge_shared.h"
#include "../../guest_shader_bridge.h"
#include "../../native_renderer_preset.h"
#include "../../../pause_menu.h"
#include "../../native_scene_sources.h"
#include "../../native_scene_adapter.h"
#include "../../native_static_world_resolve.h"
#include "../../guest_sdk_readable_range.h"
#include "../../native_model_buffers.h"
#include "../../native_pool_backings.h"
#include "../../native_model_header.h"
#include "../../native_model_cleanup.h"
#include "../../native_full_frame_static_world.h"
#include "../../native_full_frame_models.h"
#include "../../native_physical_write_notify.h"
#include "../../native_load_trace.h"
#include "../../bridge/native_cvars.h"
#include "../../bridge/bridge_state.h"
#include "../../bridge/bridge_helpers.h"
#include <rex/hook.h>
#include <rex/cvar.h>
#include <rex/ppc/func.h>
#include <rex/logging.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
struct NativeModelRenderContext {
  uint32_t source=0;
  const std::vector<edf::native::NativePoseMatrix>* poses=nullptr;
};
thread_local const NativeModelRenderContext* native_model_render_context=nullptr;
}
REX_EXTERN(__imp__sub_821C9478);
REX_HOOK_RAW(sub_821C9478) {
  edf::native::HookTiming timing(edf::native::HookPhase::RenderPose);
  // r4 is the output pose vector; recording its address keeps the tick O(dirty).
  const auto vector=ctx.r4.u32;
  __imp__sub_821C9478(ctx,base);
  if(native_model_dirty_poses) native_model_dirty_poses->push_back(vector);
}
REX_EXTERN(sub_82137410);
REX_EXTERN(sub_821375C0);
REX_EXTERN(__imp__sub_820B2510);
REX_HOOK_RAW(sub_820B2510) {
  if(REXCVAR_GET(edf_native_unlock_framerate)) {
    auto& motion=ModelMotionState();
    std::lock_guard lock(motion.mutex);
    motion.Erase(ctx.r3.u32);
  }
  // Ungated so a live toggle cannot leave a layout past its free: two atomic
  // loads unless a capture is in flight or the address hits a key bucket, and
  // never the bridge lock.
  ModelPublications().RetireAddress(ctx.r3.u32);
  __imp__sub_820B2510(ctx,base);
}
namespace {
// Model draw entry (render helper). Capture and audit only: guest state and
// the draw are unchanged, and no failure reaches guest code.
void ObserveNativeModelPublication(uint8_t* base,uint32_t instance,uint32_t vector) {
  try {
    auto& models=ModelPublications();
    const edf::native::GuestReader reader(base);
    // Buffer identities are read under the bridge lock, which the model draw
    // path never holds (its indexed draws take it themselves).
    const auto decode=[&](const auto& body) {
      auto& state=edf::native::State();
      std::lock_guard lock(state.mutex);
      return body([&](uint32_t owner,edf::native::NativeModelBuffers::Kind kind)->uint64_t {
        const auto* found=state.model_buffers.Find(owner,kind);
        return found?found->generation:0;
      });
    };
    if(REXCVAR_GET(edf_native_model_publication_audit)) {
      if(const auto published=models.Find(instance)) {
        const auto poses=models.AcquirePoses();
        const auto audit=decode([&](const auto& lookup) {
          return edf::native::AuditNativeModelPublication(reader,published,poses.get(),vector,lookup);
        });
        static std::atomic<uint64_t> audits{0},layouts{0},published_poses{0},poses_changed{0};
        const auto count=audits.fetch_add(1,std::memory_order_relaxed)+1;
        const auto layout_mismatches=layouts.fetch_add(audit.layout_mismatch,std::memory_order_relaxed)+audit.layout_mismatch;
        const auto pose_count=published_poses.fetch_add(audit.published,std::memory_order_relaxed)+audit.published;
        const auto pose_mismatches=poses_changed.fetch_add(audit.pose_mismatch,std::memory_order_relaxed)+audit.pose_mismatch;
        // The live pose moved after its tick publication: keep the model pass off it.
        if(audit.pose_mismatch) models.MarkRenderDependent(instance,published.generation);
        if(audit.layout_mismatch && layout_mismatches<=8)
          REXLOG_WARN("Native model publication audit: layout mismatch instance={:#x} generation={} decoded={}",
            instance,published.generation,audit.decoded);
        if(count<=4 || count%1000==0)
          REXLOG_INFO("Native model publication audit: draws={} layout_mismatches={} published_poses={} pose_mismatches={} unpublished={}",
            count,layout_mismatches,pose_count,pose_mismatches,count-pose_count);
      }
    }
    const auto identity=edf::native::ReadGuestWords<2>(reader,instance);
    if(models.Current(instance,identity[0],identity[1],vector) || models.Rejected(instance,identity[1],vector)) return;
    // Open before the decode reads guest memory: a free (820B2510) of the
    // instance, node or pose storage from here to Register drops the capture.
    const edf::native::NativeModelPublications::CaptureScope capture(models);
    try {
      auto layout=decode([&](const auto& lookup) {
        return edf::native::DecodeNativeModelLayoutWith(reader,instance,vector,lookup);
      });
      const auto storage=edf::native::ReadNativeModelPoseRange(reader,vector).begin;
      const auto meshes=layout.meshes.size(),batches=layout.Batches();
      const auto captured=models.Register(std::move(layout),storage,&capture);
      if(!captured) {
        // Not a rejection: the next draw of a live instance captures again.
        static std::atomic<uint64_t> stale{0};
        const auto count=stale.fetch_add(1,std::memory_order_relaxed)+1;
        if(count<=8 || (count&(count-1))==0)
          REXLOG_INFO("Native model publication: dropped capture instance={:#x} freed during decode stale={}",instance,count);
        return;
      }
      static std::atomic<uint64_t> captures{0};
      const auto count=captures.fetch_add(1,std::memory_order_relaxed)+1;
      if(count<=8 || (count&(count-1))==0)
        REXLOG_INFO("Native model publication: captured instance={:#x} generation={} meshes={} batches={} bones={} registered={}",
          instance,captured.generation,meshes,batches,captured.layout->bones,models.size());
    } catch(const std::exception& error) {
      models.Reject(instance,identity[1],vector);
      static std::atomic<uint64_t> rejections{0};
      const auto count=rejections.fetch_add(1,std::memory_order_relaxed)+1;
      if(count<=8 || (count&(count-1))==0)
        REXLOG_WARN("Native model publication: rejected instance={:#x} node={:#x} rejections={}: {}",instance,identity[1],count,error.what());
    }
  } catch(const std::exception& error) {
    static std::atomic<uint64_t> failures{0};
    const auto count=failures.fetch_add(1,std::memory_order_relaxed)+1;
    if(count<=8 || (count&(count-1))==0)
      REXLOG_WARN("Native model publication: observation failed instance={:#x} failures={}: {}",instance,count,error.what());
  }
}
// A throw must never unwind through guest code: the model pass is switched off
// for the rest of the run and this object runs the original draw. A throw
// after recording began can draw that object twice in this frame.
bool TryNativeModelPass(PPCContext& ctx,uint8_t* base,const std::vector<edf::native::NativePoseMatrix>* interpolated,
    bool interpolating,bool render_dependent) {
  static std::atomic<bool> failed=false;
  if(failed.load(std::memory_order_relaxed)) return false;
  try { return edf::native::RenderNativeModelPass(ctx,base,interpolated,interpolating,render_dependent); }
  catch(const std::exception& error) {
    if(!failed.exchange(true)) REXLOG_ERROR("Native model pass disabled after failure: {}",error.what());
    return false;
  }
}
}
REX_EXTERN(__imp__sub_821C9C20);
REX_HOOK_RAW(sub_821C9C20) {
  edf::native::HookTiming model_timing(edf::native::HookPhase::RenderModel);
  if(EDF_NATIVE_FLAG(model_publication)) ObserveNativeModelPublication(base,ctx.r3.u32,ctx.r4.u32);
  // The native model pass takes the object only on the native A/B side; it
  // draws the poses the original would upload (interpolated when unlocked).
  const bool model_pass=edf::native::NativeModelPassEnabled() && edf::native::NativeAbNativeSide();
  if(!native_render_budget.unlocked || native_render_budget.divisor!=1 ||
     !REXCVAR_GET(edf_native_model_interpolation)) {
    if(model_pass && TryNativeModelPass(ctx,base,nullptr,false,false)) return;
    __imp__sub_821C9C20(ctx,base); return;
  }
  const edf::native::GuestReader reader(base);
  const auto vector=ctx.r4.u32;
  const auto range=edf::native::ReadGuestWords<2>(reader,reader.Add(vector,4));
  const auto begin=range[0],end=range[1];
  if(!begin || end<=begin || (end-begin)%64 || (end-begin)/64>1024) {
    __imp__sub_821C9C20(ctx,base); return;
  }
  bool render_dependent=false;
  const auto identity=edf::native::ReadGuestWords<2>(reader,ctx.r3.u32);
  std::vector<edf::native::NativePoseMatrix> input((end-begin)/64),output;
  const auto* bytes=reader.Bytes(begin,end-begin);
  for(size_t bone=0;bone<input.size();++bone) for(size_t i=0;i<16;++i)
    input[bone][i]=std::bit_cast<float>(edf::native::GuestBlockWord(bytes+bone*64+i*4));
  {
    auto& motion=ModelMotionState();
    std::lock_guard lock(motion.mutex);
    if(motion.sources.size()>=2048 && !motion.sources.contains(begin)) motion.Clear();
    auto& source=motion.sources[begin];
    const auto trace_limit=REXCVAR_GET(edf_native_motion_trace);
    if(!source.vector && trace_limit>0 && motion.trace_sources<16) {
      source.trace=true; ++motion.trace_sources;
    }
    if(source.vector!=vector || source.owner!=identity[0] || source.node!=identity[1] ||
       source.publication+1<native_render_publication || native_render_budget.steps>1)
      source.history.Reset();
    source.vector=vector; source.owner=identity[0]; source.node=identity[1];
    source.publication=native_render_publication;
    source.history.Sample(input,native_render_budget.tick,native_render_budget.fraction,output);
    render_dependent=source.history.render_dependent();
    motion.history_bytes-=source.history_bytes;
    source.history_bytes=source.history.StorageBytes();
    motion.history_bytes+=source.history_bytes;
    if(source.trace && source.samples<uint64_t(trace_limit) && source.trace_publication!=native_render_publication) {
      const auto hash=[](const auto& poses) {
        uint64_t result=14695981039346656037ull;
        for(const auto& pose:poses) for(float value:pose) {
          result^=std::bit_cast<uint32_t>(value); result*=1099511628211ull;
        }
        return result;
      };
      const auto original=hash(input),rendered=hash(output);
      if(source.samples) {
        if(original!=source.source_hash) ++source.source_changes;
        if(rendered!=source.rendered_hash) ++source.rendered_changes;
      }
      if(output!=input) ++source.blended;
      source.source_hash=original; source.rendered_hash=rendered;
      source.trace_publication=native_render_publication;
      if(++source.samples%120==0 || source.samples==uint64_t(trace_limit))
        REXLOG_INFO("Native model motion: source={:#x} vector={:#x} bones={} samples={} source_changes={} rendered_changes={} blended={} tick={} phase={} render_dependent={}",
          begin,vector,input.size(),source.samples,source.source_changes,source.rendered_changes,source.blended,
          native_render_budget.tick,native_render_budget.fraction,source.history.render_dependent());
    }
    // Count allocated capacity, including storage retained across skeleton
    // changes. The current draw owns its output and survives cache eviction.
    if(motion.history_bytes>64u*1024u*1024u) motion.Clear();
  }
  if(model_pass && TryNativeModelPass(ctx,base,&output,true,render_dependent)) return;
  const NativeModelRenderContext current{begin,&output};
  struct Scope {
    const NativeModelRenderContext* previous=native_model_render_context;
    ~Scope() { native_model_render_context=previous; }
  } scope;
  native_model_render_context=&current;
  __imp__sub_821C9C20(ctx,base);
}
// These two audited uploads consume the source vector while the model scope
// is active. Override only the shader scratch destination, never source bones.
REX_EXTERN(__imp__sub_821A1738);
REX_HOOK_RAW(sub_821A1738) {
  const auto* model=native_model_render_context;
  const auto vector=ctx.r4.u32,source=ctx.r5.u32,count=ctx.r6.u32;
  uint32_t destination=0,matrices=0;
  if(model && source==model->source && vector) {
    const edf::native::GuestReader reader(base);
    destination=reader.Word(vector);
    matrices=std::min({count,reader.Word(reader.Add(vector,16)),uint32_t(model->poses->size())});
  }
  __imp__sub_821A1738(ctx,base);
  if(!destination || !matrices) return;
  const edf::native::GuestReader reader(base);
  for(uint32_t bone=0;bone<matrices;++bone) {
    std::array<uint32_t,12> words;
    for(size_t column=0;column<3;++column) for(size_t row=0;row<4;++row)
      words[column*4+row]=std::bit_cast<uint32_t>((*model->poses)[bone][row*4+column]);
    reader.StoreCpuWords(reader.Add(destination,bone*48),words);
  }
}
REX_EXTERN(__imp__sub_821A17D8);
REX_HOOK_RAW(sub_821A17D8) {
  const auto* model=native_model_render_context;
  const auto source=ctx.r5.u32,vector=ctx.r4.u32;
  uint32_t destination=0;
  size_t bone=0;
  if(model && vector && source>=model->source && (source-model->source)%64==0) {
    bone=(source-model->source)/64;
    if(bone<model->poses->size()) destination=edf::native::GuestReader(base).Word(vector);
  }
  __imp__sub_821A17D8(ctx,base);
  if(!destination) return;
  std::array<uint32_t,16> words;
  for(size_t column=0;column<4;++column) for(size_t row=0;row<4;++row)
    words[column*4+row]=std::bit_cast<uint32_t>((*model->poses)[bone][row*4+column]);
  edf::native::GuestReader(base).StoreCpuWords(destination,words);
}
// Model-owned buffers are embedded resources, not necessarily reference-counted
// objects destroyed by 82134220. These cleanup routines also run before their
// creators reuse the same owner. Retire native meshes before freeing CPU storage;
// do not hold bridge locks across guest calls (cleanup can unbind a stream).
REX_EXTERN(__imp__sub_821D7468);
REX_EXTERN(__imp__sub_821D75F8);
REX_EXTERN(sub_82137410);
REX_EXTERN(sub_821375C0);
REX_EXTERN(sub_821D3EA0);
namespace {
void RetireNativeModelBuffer(uint32_t resource) {
  if (!EDF_NATIVE_FLAG(shader_bridge)) return;
  auto& state=edf::native::State();
  // Registry/cache retirement has no immediate-context work. Keep it atomic
  // with draws using the state lock, without waiting for refresh pacing.
  std::lock_guard lock(state.mutex);
  // Preserve aliases before erasing this owner: the later pool-release hook
  // cannot recover its physical extent after normal cleanup retires metadata.
  if(!state.model_buffers.RetireBackingAliases(resource,
       [&](uint32_t owner) { state.meshes.Invalidate(owner); }))
    state.meshes.Invalidate(resource);
}
void CleanupNativeModelBuffer(PPCContext& ctx,uint8_t* base,bool index) {
  const edf::native::GuestReader reader(base);
  auto work=ctx;
  bool unbound=false,released=false;
  edf::native::CleanupNativeModelResource(reader,ctx.r3.u32,index,
    [&] { return reader.Word(reader.Word(0x82000720)); },
    [&](uint32_t device,bool ib) {
      work.r3.u64=device; work.r4.u64=0;
      if(ib) { work.lr=0x821D7640; sub_821375C0(work,base); }
      else {
        work.r5.u64=0; work.r6.u64=0; work.r7.u64=0; work.r8.u64=0x1000;
        work.lr=0x821D74C8; sub_82137410(work,base);
      }
      unbound=true;
    },
    [&](uint32_t allocation) {
      work.r3.u64=allocation; work.lr=index?0x821D7648u:0x821D74D0u;
      sub_821D3EA0(work,base);
      released=true;
    });
  if(released) {
    static std::atomic<uint64_t> releases{0};
    const auto sequence=releases.fetch_add(1,std::memory_order_relaxed)+1;
    if(sequence<=8 || (sequence&(sequence-1))==0)
      REXLOG_INFO("Native model cleanup completed: owner={:#x}, index={}, unbound={}, releases={} (sampled)",
        ctx.r3.u32,index,unbound,sequence);
  }
}
}
REX_HOOK_RAW(sub_821D7468) {
  edf::native::LoadTraceScope trace(LoadTraceOn(),edf::native::LoadTraceKind::ModelRetire);
  LoadTraceTick();
  RetireNativeModelBuffer(ctx.r3.u32);
  if(EDF_NATIVE_FLAG(shader_bridge)) { CleanupNativeModelBuffer(ctx,base,false); return; }
  __imp__sub_821D7468(ctx,base);
}
REX_HOOK_RAW(sub_821D75F8) {
  edf::native::LoadTraceScope trace(LoadTraceOn(),edf::native::LoadTraceKind::ModelRetire);
  LoadTraceTick();
  RetireNativeModelBuffer(ctx.r3.u32);
  if(EDF_NATIVE_FLAG(shader_bridge)) { CleanupNativeModelBuffer(ctx,base,true); return; }
  __imp__sub_821D75F8(ctx,base);
}
// Generic pool release receives the allocation record in r4. Observe before
// its own guest critical section and before the pool makes these bytes reusable.
REX_EXTERN(__imp__sub_821D3DC8);
REX_HOOK_RAW(sub_821D3DC8) {
  if(EDF_NATIVE_FLAG(shader_bridge)) {
    edf::native::LoadTraceScope trace(LoadTraceOn(),edf::native::LoadTraceKind::PoolRetire);
    auto& state=edf::native::State();
    // Allocation ownership retirement is metadata-only, as above.
    std::lock_guard lock(state.mutex);
    state.model_buffers.RetireAllocationAliases(ctx.r4.u32,
      [&](uint32_t owner) { state.meshes.Invalidate(owner); });
  }
  __imp__sub_821D3DC8(ctx,base);
}
REX_EXTERN(__imp__sub_821D3748);
REX_HOOK_RAW(sub_821D3748) {
  std::optional<edf::native::NativeBufferWrites::WriterScope> release_scope;
  // This audited edge runs under the guest pool lock. Retire by actual block
  // extent even if its original model wrapper has already left the registry.
  // Keep pool -> renderer ordering; no renderer lock spans the guest helper.
  if(EDF_NATIVE_FLAG(shader_bridge) && uint32_t(ctx.lr)==0x821D3E30) {
    const auto block=edf::native::ReadNativePoolBlock(edf::native::GuestReader(base),ctx.r3.u32,ctx.r4.u64);
    auto* memory=REX_KERNEL_MEMORY();
    if(!memory || memory->virtual_membase()!=base)
      throw std::runtime_error("native pool block release memory mismatch");
    const auto extent=edf::native::MapCompletedNativePhysicalWrite(*memory,block.address,block.bytes);
    if(block.bytes && (!extent || extent->all))
      throw std::runtime_error("invalid native pool block physical extent");
    if(extent) {
      edf::native::LoadTraceScope trace(LoadTraceOn(),edf::native::LoadTraceKind::PoolRetire,extent->bytes);
      // Exclude overlapping guarded acquisitions through the original helper,
      // including owners published after the registry retirement below.
      // Scope entry/exit takes only the queue lock, never across guest code.
      release_scope.emplace(&edf::native::BufferWrites(),
        edf::native::NativeBufferWrites::Range{extent->address,extent->bytes},
        edf::native::NativeBufferWrites::WriterKind::AllocationRelease);
      auto& state=edf::native::State();
      std::lock_guard lock(state.mutex);
      size_t retired=0;
      state.model_buffers.RetirePhysicalRange(extent->address,extent->bytes,
        [&](uint32_t owner) { state.meshes.Invalidate(owner); ++retired; });
      static uint64_t releases=0;
      const auto sequence=++releases;
      if(retired || sequence<=8 || (sequence&(sequence-1))==0)
        REXLOG_INFO("Native pool block retirement: physical={:#x}, bytes={}, owners={}, sequence={} (before block reuse; completion not implied)",
          extent->address,extent->bytes,retired,sequence);
    }
  }
  __imp__sub_821D3748(ctx,base);
}
namespace {
// Whole-pool destruction has no per-owner release callbacks. Resolve all
// backing allocations before taking the registry lock, and retire before the
// original routine detaches/frees its nodes. Never hold bridge locks over free.
[[nodiscard]] edf::native::NativeBufferWrites::ReleaseScopes RetireNativePoolBackings(uint8_t* base,std::span<const uint32_t> backings) {
    auto* memory=REX_KERNEL_MEMORY();
    if(!memory || memory->virtual_membase()!=base)
      throw std::runtime_error("native pool release memory mismatch");
    std::vector<edf::native::NativePhysicalWriteExtent> extents;
    extents.reserve(backings.size());
    for(const auto address:backings) {
      auto* heap=memory->LookupHeap(address);
      rex::memory::HeapAllocationInfo allocation{};
      if(!heap || heap->heap_type()!=rex::memory::HeapType::kGuestPhysical ||
         !heap->QueryRegionInfo(address,&allocation) || allocation.allocation_base!=address ||
         !allocation.state || !allocation.allocation_size)
        throw std::runtime_error("invalid native pool backing allocation");
      const auto extent=edf::native::MapCompletedNativePhysicalWrite(*memory,address,allocation.allocation_size);
      if(!extent || extent->all) throw std::runtime_error("invalid native pool backing physical extent");
      extents.push_back(*extent);
    }
    std::vector<edf::native::NativeBufferWrites::Range> ranges;
    ranges.reserve(extents.size());
    for(const auto extent:extents) ranges.push_back({extent.address,extent.bytes});
    auto release_scopes=edf::native::BufferWrites().BeginReleaseSet(ranges);
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    size_t retired=0;
    for(const auto extent:extents)
      state.model_buffers.RetirePhysicalRange(extent.address,extent.bytes,
        [&](uint32_t owner) { state.meshes.Invalidate(owner); ++retired; });
    static uint64_t releases=0;
    const auto sequence=++releases;
    if(sequence<=8 || (sequence&(sequence-1))==0 || retired)
      REXLOG_INFO("Native physical backing retirement: allocations={}, model_owners={}, sequence={} (before guest free; successful completion not implied)",
        extents.size(),retired,sequence);
    return release_scopes;
}
}
REX_EXTERN(__imp__sub_821D3A40);
REX_HOOK_RAW(sub_821D3A40) {
  edf::native::NativeBufferWrites::ReleaseScopes release_scopes;
  if(EDF_NATIVE_FLAG(shader_bridge)) {
    const auto backings=edf::native::ReadNativePoolBackings(edf::native::GuestReader(base),ctx.r3.u32);
    release_scopes=RetireNativePoolBackings(base,backings);
  }
  __imp__sub_821D3A40(ctx,base);
}
REX_EXTERN(__imp__sub_8212FC28);
REX_HOOK_RAW(sub_8212FC28) {
  edf::native::NativeBufferWrites::ReleaseScopes release_scopes;
  // 821D3B68 has already selected/unlinked the node and loaded its backing
  // address into r3. Observe the actual free argument, not a predicted iterator
  // target. 821D1890/821D18E0 likewise load their owned physical buffer into
  // r3 before destruction/reallocation. 821D4380's conditional temporary
  // cleanup also loads the actual nonnull backing; do not assume that branch
  // unreachable from the temporary's initial zero value. Other callers keep
  // their prior policy.
  const auto caller=uint32_t(ctx.lr);
  if(EDF_NATIVE_FLAG(shader_bridge) &&
     (caller==0x821D3BEC || caller==0x821D18B4 || caller==0x821D190C || caller==0x821D4404)) {
    const std::array<uint32_t,1> backings{ctx.r3.u32};
    release_scopes=RetireNativePoolBackings(base,backings);
  }
  __imp__sub_8212FC28(ctx,base);
}
REX_EXTERN(__imp__sub_8212F4B8);
REX_HOOK_RAW(sub_8212F4B8) {
  edf::native::NativeBufferWrites::ReleaseScopes release_scopes;
  // Preserve the retail selector: only low-word bit31 denotes physical
  // memory, and its null case is a no-op. CPU-heap releases are untouched.
  // Resolve/retire before forwarding, with no bridge lock spanning guest code.
  if(EDF_NATIVE_FLAG(shader_bridge) && (ctx.r4.u32&0x80000000u) && ctx.r3.u32) {
    const std::array<uint32_t,1> backings{ctx.r3.u32};
    release_scopes=RetireNativePoolBackings(base,backings);
  }
  __imp__sub_8212F4B8(ctx,base);
}
namespace {
// The model loader copies into owner+48 storage before associating it with
// its embedded resource. Publish after the complete creator, not mid-copy.
void PublishNativeModelBuffer(uint8_t* base,uint32_t owner,edf::native::NativeModelBuffers::Kind kind,
                              uint32_t stride,uint32_t count) {
  if(!EDF_NATIVE_FLAG(shader_bridge)) return;
  edf::native::LoadTraceScope trace(LoadTraceOn(),edf::native::LoadTraceKind::ModelPublish,uint64_t(stride)*count);
  auto& state=edf::native::State();
  // Drain/apply write notifications and publish the generation atomically.
  // NativeIndexBuffer creates an immutable device resource with initial data;
  // neither it nor cache invalidation submits immediate-context commands.
  std::lock_guard lock(state.mutex);
  const edf::native::GuestReader reader(base);
  const auto address=reader.Word(reader.Add(owner,48));
  std::optional<uint32_t> physical;
  auto* memory=REX_KERNEL_MEMORY();
  const auto bytes=uint64_t(stride)*count;
  auto* heap=memory->LookupHeap(address);
  if(bytes && bytes<=0x03fffffcu && uint64_t(address)+bytes<=0x100000000ull && heap &&
     heap->heap_type()==rex::memory::HeapType::kGuestPhysical &&
     heap==memory->LookupHeap(uint32_t(address+bytes-1))) {
    const auto start=memory->GetPhysicalAddress(address);
    if(uint64_t(start)+bytes<=0x20000000ull && memory->GetPhysicalAddress(uint32_t(address+bytes-1))==start+bytes-1)
      physical=start;
  }
  // The constructor's bulk copy is already queued. Apply older notifications
  // before publishing this completed generation, otherwise its first draw would
  // immediately retire the just-created storage for its own initialization.
  if(edf::native::BufferWrites().Pending()) {
    const auto writes=edf::native::BufferWrites().Drain();
    edf::native::AuditGeneratedWrites(state,writes);
    size_t affected=0;
    state.model_buffers.ApplyWrites(writes,[&](uint32_t resource) { state.meshes.Invalidate(resource); ++affected; });
    if(writes.all) ++state.buffer_write_all_batches;
    const bool report_all=writes.all && (state.buffer_write_all_batches & (state.buffer_write_all_batches-1))==0;
    if(++state.buffer_write_batches<=8 || report_all || writes.pages)
      REXLOG_INFO("Native buffer write batch: ranges={}, all={}, affected={}, all_batches={}, pages={} (before model publication)",
        writes.count,writes.all,affected,state.buffer_write_all_batches,writes.pages?writes.pages->count():0);
  }
  // Register before constructing the host snapshot so completed writes during
  // construction can reject attachment. First-use byte validation stays enabled.
  state.model_buffers.Publish(owner,kind,address,stride,count,physical);
  if(count) {
    const auto generation=state.model_buffers.Find(owner,kind)->generation;
    const std::span<const uint8_t> source{reader.Bytes(address,size_t(bytes)),size_t(bytes)};
    std::optional<edf::native::NativeBufferWrites::ObservedVersion> version;
    std::shared_ptr<const std::vector<uint8_t>> contents;
    if(physical) {
      if(auto snapshot=edf::native::BufferWrites().CopyObserved(owner,*physical,source)) {
        version=snapshot->version; contents=std::move(snapshot->contents);
      }
      // Busy/unknown owners retain metadata only. Do not fall back to an
      // unguarded publication read; first-use validation still handles them.
    } else if(kind==edf::native::NativeModelBuffers::Kind::Vertex) {
      contents=std::make_shared<const std::vector<uint8_t>>(source.begin(),source.end());
    }
    if(kind==edf::native::NativeModelBuffers::Kind::Vertex && contents)
      state.model_buffers.RetainVertexContents(owner,generation,std::move(contents),version);
    if(kind==edf::native::NativeModelBuffers::Kind::Index && (!physical || contents)) {
      if(contents && version) state.model_buffers.RetainIndexContents(owner,generation,contents,*version);
      // GPU construction is outside the queue lock and reads the owned copy for
      // physical buffers. Later writes can still reject registry attachment.
      auto index_storage=std::make_shared<const edf::native::NativeIndexBuffer>(edf::native::EnsureSceneBackendLocked(state),
        contents?std::span<const uint8_t>(*contents):source,stride,contents);
      if(version) state.model_buffers.CommitObservedIndex(owner,generation,*version,std::move(index_storage));
      else state.model_buffers.RetainIndexStorage(owner,generation,std::move(index_storage));
    }
  }
  if(kind==edf::native::NativeModelBuffers::Kind::Index && count) {
    // Summing all retained payloads at every publication makes model loading
    // quadratic. Sample this diagnostic; indexed-draw telemetry still reports
    // current ownership totals independently. Serialized by state.mutex above.
    static uint64_t publications=0;
    const auto publication=++publications;
    if(publication>8 && (publication & (publication-1))!=0) return;
    size_t retained=0,storage_bytes=0;
    state.model_buffers.VisitIndexStorage([&](uint32_t,const edf::native::NativeIndexBuffer& storage) {
      ++retained; storage_bytes+=storage.StorageBytes();
    });
    REXLOG_INFO("Native model index publication: owner={:#x}, indices={}, retained={}, storage_bytes={}, publication={} (sampled; CPU snapshots plus GPU payload; shared with meshes)",
      owner,count,retained,storage_bytes,publication);
  }
}
}
REX_EXTERN(__imp__sub_821D7530);
REX_EXTERN(sub_821D4700);
REX_EXTERN(sub_821E8320);
namespace {
void ConstructNativeModelBuffer(PPCContext& ctx,uint8_t* base,bool index) {
  const auto owner=ctx.r3.u32,source=ctx.r4.u32;
  const auto stride=index?2u:ctx.r5.u32,count=index?ctx.r5.u32:ctx.r6.u32;
  static std::atomic<uint64_t> constructions{0};
  const bool trace=constructions.fetch_add(1,std::memory_order_relaxed)<8;
  if(trace) REXLOG_INFO("Native model constructor: owner={:#x}, source={:#x}, index={}, stride={}, count={}",owner,source,index,stride,count);
  const uint64_t bytes=uint64_t(stride)*count;
  if(!owner || (index?false:(!stride || stride>2048 || stride%4)) || bytes>0x03fffffcu)
    throw std::runtime_error("invalid native model construction extent");
  const edf::native::GuestReader reader(base);
  // Keep the engine allocator and its CPU-visible payload until all consumers
  // have migrated. Native orchestration removes the Xbox header-builder and
  // generic relocation calls; it does not certify payload immutability.
  auto work=ctx;
  work.lr=index?0x821D76C4u:0x821D7550u;
  if(index) sub_821D75F8(work,base); else sub_821D7468(work,base);
  if(trace) REXLOG_INFO("Native model constructor: cleanup complete owner={:#x}",owner);
  work.r3.u64=reader.Add(owner,32); work.r4.u64=bytes;
  work.lr=index?0x821D76D4u:0x821D7560u;
  sub_821D4700(work,base);
  // The pool may return failure after releasing the previous allocation.
  // Do not copy through an empty/stale record or publish a successful owner.
  if(!work.r3.u32) throw std::runtime_error("native model allocation failed");
  const auto address=reader.Word(reader.Add(owner,48));
  if((bytes && (!address || address%4)) || uint64_t(address)+bytes>0x100000000ull)
    throw std::runtime_error("invalid native model allocation extent");
  if(trace) REXLOG_INFO("Native model constructor: allocation complete owner={:#x}, address={:#x}",owner,address);
  work.r3.u64=address; work.r4.u64=source; work.r5.u64=bytes;
  work.lr=index?0x821D76E8u:0x821D7574u;
  sub_821E8320(work,base);
  if(trace) REXLOG_INFO("Native model constructor: copy complete owner={:#x}",owner);
  const auto header=edf::native::NativeModelHeader(index,address,uint32_t(bytes));
  reader.StoreCpuWords(owner,header);
  reader.StoreWord(reader.Add(owner,56),index?count:stride);
  if(!index) {
    reader.StoreWord(reader.Add(owner,60),count);
    reader.StoreWord(reader.Add(owner,64),uint32_t(bytes));
  }
  *const_cast<uint8_t*>(reader.WritableBytes(reader.Add(owner,52),1,4))=1;
  PublishNativeModelBuffer(base,owner,index?edf::native::NativeModelBuffers::Kind::Index:
    edf::native::NativeModelBuffers::Kind::Vertex,stride,count);
  ctx.r3.u64=1;
}
}
REX_HOOK_RAW(sub_821D7530) {
  edf::native::LoadTraceScope trace(LoadTraceOn(),edf::native::LoadTraceKind::ModelConstruct,uint64_t(ctx.r5.u32)*ctx.r6.u32);
  if(EDF_NATIVE_FLAG(shader_bridge)) { ConstructNativeModelBuffer(ctx,base,false); return; }
  const auto owner=ctx.r3.u32,stride=ctx.r5.u32,count=ctx.r6.u32;
  __imp__sub_821D7530(ctx,base);
  if(ctx.r3.u32) PublishNativeModelBuffer(base,owner,edf::native::NativeModelBuffers::Kind::Vertex,stride,count);
}
REX_EXTERN(__imp__sub_821D76A8);
REX_HOOK_RAW(sub_821D76A8) {
  edf::native::LoadTraceScope trace(LoadTraceOn(),edf::native::LoadTraceKind::ModelConstruct,uint64_t(ctx.r5.u32)*2);
  if(EDF_NATIVE_FLAG(shader_bridge)) { ConstructNativeModelBuffer(ctx,base,true); return; }
  const auto owner=ctx.r3.u32,count=ctx.r5.u32;
  __imp__sub_821D76A8(ctx,base);
  if(ctx.r3.u32) PublishNativeModelBuffer(base,owner,edf::native::NativeModelBuffers::Kind::Index,2,count);
}
