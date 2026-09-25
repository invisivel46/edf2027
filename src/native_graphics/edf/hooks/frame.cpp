// The frame loop: step dispatch 821A4BA0, transition 821A4DE8, render helper 821A5080, heartbeat, pacing, pause, the swap wait and present, and the scene's begin and end.
// Moved from guest_shader_bridge.cpp unchanged (stage 3 of its split); what this file shares with the bridge
// and the other hook files is in bridge/bridge_shared.h.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../bridge/bridge_shared.h"
#include "../../guest_shader_bridge.h"
#include "../../native_renderer_preset.h"
#include "../../../scripted_input_logic.h"
#include "../../../frame_stats.h"
#include "../../../pause_menu.h"
#include "../../../input_latency.h"
#include "../../native_frame_flight.h"
#include "../../d3d12_backend.h"
#include "../../native_scene_sources.h"
#include "../../native_scene_adapter.h"
#include "../../native_scene_cpu_window.h"
#include "../../native_recorded_reads.h"
#include "../../native_static_world_resolve.h"
#include "../../native_static_world_pass.h"
#include "../../native_decode_workers.h"
#include "../../guest_draw_state.h"
#include "../../guest_sdk_readable_range.h"
#include "../../native_model_buffers.h"
#include "../../native_frame_dispatch.h"
#include "../../native_full_frame.h"
#include "../../native_coverage_census.h"
#include "../../native_frame_times.h"
#include "../../../console/console_hook.h"
#include "../../native_first_use.h"
#include "../../native_full_frame_static_world.h"
#include "../../native_full_frame_models.h"
#include "../../native_declarations.h"
#include "../../native_load_trace.h"
#include "../../native_pacing.h"
#include "../../../guest_state_hash.h"
#include "../../native_render_registry.h"
#include "../../native_profile_result.h"
#include "../../native_capture_policy.h"
#include "../../native_fsr.h"
#include "../../native_ffx.h"
#include "../../native_d3d12_raw.h"
#include "../../d3d11_texture.h"
#include "../../d3d11_completion.h"
#include "../../d3d11_signals.h"
#include "../../d3d11_gpu_timer.h"
#include "../../bridge/native_cvars.h"
#include "../../bridge/bridge_state.h"
#include "../../bridge/bridge_helpers.h"
#include "../../edf/full_frame/host.h"
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
// The frame transition hook calls this on the engine thread: it tags the
// thread for LoadTraceScope and keeps a handle for its CPU time.
void LoadTraceMarkEngineThread() {
  if(edf::native::native_load_trace_engine_thread) return;
  edf::native::native_load_trace_engine_thread=true;
  HANDLE handle=nullptr;
  if(DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),&handle,
                     THREAD_QUERY_LIMITED_INFORMATION,FALSE,0)) {
    if(auto* previous=LoadTraceState().engine.exchange(handle)) CloseHandle(previous);
  }
}
}
namespace edf::native {
namespace {
// Extra simulation steps folded into one engine update. This is diagnostic
// bookkeeping only; the retail result and clock writeback remain unchanged.
std::atomic<uint64_t>& FrameExtraSimulationSteps() {
  static std::atomic<uint64_t> total{0};
  return total;
}
// edf_native_frame_trace's per-frame cost columns: wall time inside the step
// dispatch (821A4BA0, engine thread), the render helper (821A5080, render
// thread) and the frame transition (821A4DE8, engine thread), and the steps
// dispatched. Cumulative; the trace writes the delta per swap. Only counted
// with the trace on (the path is read once: it needs a restart anyway).
struct FrameTraceCostTotals {
  std::atomic<uint64_t> steps{0},dispatch_ns{0},helper_ns{0},transition_ns{0};
};
FrameTraceCostTotals& FrameTraceCosts() {
  static FrameTraceCostTotals totals;
  return totals;
}
bool FrameTraceCostsOn() {
  static const bool on=!REXCVAR_GET(edf_native_frame_trace).empty();
  return on;
}
class FrameTraceCostScope {
 public:
  explicit FrameTraceCostScope(std::atomic<uint64_t>& total):total_(FrameTraceCostsOn()?&total:nullptr) {
    if(total_) start_=std::chrono::steady_clock::now();
  }
  ~FrameTraceCostScope() {
    if(total_) total_->fetch_add(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now()-start_).count()),std::memory_order_relaxed);
  }
  FrameTraceCostScope(const FrameTraceCostScope&)=delete;
  FrameTraceCostScope& operator=(const FrameTraceCostScope&)=delete;
 private:
  std::atomic<uint64_t>* total_;
  std::chrono::steady_clock::time_point start_{};
};
void CaptureScene(Bridge& state,uint32_t owner) {
  const auto prefix=REXCVAR_GET(edf_native_scene_capture);
  if (prefix.empty() || state.scene_captures>=3 ||
      !NativeSceneDrew(state.indexed_submitted,state.scene_indexed_start,state.scene_full_frame)) return;
  const auto scene=state.scenes.find(owner);
  if (scene==state.scenes.end()) return;
  const auto number=++state.scene_captures;
  try {
    const auto path=std::filesystem::path(prefix+"."+std::to_string(number)+".bmp");
    if (std::filesystem::exists(path)) throw std::runtime_error("native capture path already exists");
    // The resolved scene rather than the multisampled surface when the fetch
    // goes through the seam: a multisampled resource cannot be read back on
    // either API, and this is the same picture one step later.
    std::vector<uint8_t> bmp;
    if(scene->second.color.surface)
      bmp=CaptureNativeHdrBmp(*state.context.Get(),*scene->second.color.surface.Get());
    else if(scene->second.color.sampled.backend && scene->second.color.sampled.content_valid) {
      SubmitSceneFrameLocked(state);
      bmp=CaptureNativeBmp(EnsureSceneBackendLocked(state),*scene->second.color.sampled.backend,
                           scene->second.color.sampled.format);
    } else throw std::runtime_error("this scene has neither a D3D11 surface nor a resolved texture to capture");
    std::ofstream output(path,std::ios::binary);
    output.write(reinterpret_cast<const char*>(bmp.data()),bmp.size());
    output.close();
    if (!output) throw std::runtime_error("cannot write native scene capture");
    REXLOG_INFO("Native partial scene capture: {}, indexed_draws={}, initialized={}, frame_complete={}, linear RGB clamped; not final tone mapping",
      path.string(),state.indexed_submitted-state.scene_indexed_start,scene->second.color.content_valid,scene->second.frame_complete);
    if(scene->second.samples==1 && scene->second.depth.surface) {
      const auto depth=InspectNativeDepth(*state.context.Get(),*scene->second.depth.surface.Get(),0);
      REXLOG_INFO("Native scene depth diagnostic: changed_pixels={}, top/middle/bottom={}/{}/{}, min={}, max={}, nonfinite={}",
        depth.changed_pixels,depth.vertical_bands[0],depth.vertical_bands[1],depth.vertical_bands[2],
        depth.minimum,depth.maximum,depth.nonfinite_pixels);
    } else REXLOG_INFO("Native scene depth diagnostic unavailable for {} samples; no depth-coverage claim",scene->second.samples);
    // Surface readback above has completed prior GPU work. Never busy-wait
    // here: unavailable queries are reported, not interpreted as zero samples.
    struct Counts { uint64_t draws=0,empty=0,samples=0; };
    std::map<std::array<uint32_t,4>,Counts> groups;
    size_t unavailable=0;
    for (const auto& draw:state.visibility) {
      uint64_t samples=0;
      if (state.context->GetData(draw.query.Get(),&samples,sizeof(samples),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK) {
        ++unavailable; continue;
      }
      auto& counts=groups[draw.key]; ++counts.draws; counts.empty+=samples==0; counts.samples+=samples;
    }
    for (const auto& [key,counts]:groups)
      REXLOG_INFO("Native visibility: VS={:#x}, PS={:#x}, raster={:#x}, depth={:#x}, draws={}, zero_sample_draws={}, passed_samples={}",
        key[0],key[1],key[2],key[3],counts.draws,counts.empty,counts.samples);
    REXLOG_INFO("Native visibility coverage: queries={}, unavailable={}, total_indexed={}, outside_scene={}",
      state.visibility.size(),unavailable,state.indexed_draws,state.indexed_outside_scene);
  } catch (const std::exception& error) { REXLOG_ERROR("Native scene capture: {}",error.what()); }
}
// Whether the scene targets are made for FSR (1x colour, sampled depth):
// edf_native_fsr on at the first scene allocation and no validation run.
// Restart-time, like the two settings it overrides.
bool NativeFsrSceneAtStartup() {
  static const bool on=[] {
    const auto mode=NativeFsrRequestedMode();
    if(mode==NativeFsrMode::Off) return false;
    if(const auto* excluded=NativeFsrExcludedBy(NativeFsrCurrentExclusions())) {
      REXLOG_WARN("FSR requested (edf_native_fsr={}) but {} is set: FSR stays off and the scene targets are not changed",
        NativeFsrModeName(mode),excluded);
      return false;
    }
    REXLOG_INFO("FSR requested at startup (edf_native_fsr={}): scene MSAA forced to 1x (edf_native_msaa={} not used) and the "
      "scene depth made sampled (edf_native_scene_depth_srv); both hold until restart",NativeFsrModeName(mode),REXCVAR_GET(edf_native_msaa));
    return true;
  }();
  return on;
}
std::unique_ptr<NativeCompletionQueue> CreateCompletionQueueLocked(Bridge& state,size_t capacity=4096) {
  auto& backend=EnsureSceneBackendLocked(state);
  if(backend.name()=="d3d12") return std::make_unique<NativeCompletionQueue>(backend,capacity);
  return std::make_unique<NativeCompletionQueue>(*state.device.Get(),*state.context.Get(),capacity);
}
}
}
REX_EXTERN(__imp__sub_821A4BA0);
namespace {
// Coarse boundary tracing only: no per-draw work and no guest state changes.
// Shared sequence numbers and thread IDs expose overlap between dispatchers.
struct NativeLoopTrace {
  const char* phase;
  uint64_t sequence=0,thread=0;
  std::chrono::steady_clock::time_point begin;
  NativeLoopTrace(const char* name,uint32_t object,uint64_t caller,uint32_t steps=0):phase(name) {
    const auto limit=REXCVAR_GET(edf_native_loop_trace);
    if(limit<=0) return;
    static std::atomic<uint64_t> calls{0};
    const auto next=calls.fetch_add(1,std::memory_order_relaxed)+1;
    if(next>uint64_t(limit)) return;
    sequence=next;
    thread=std::hash<std::thread::id>{}(std::this_thread::get_id());
    begin=std::chrono::steady_clock::now();
    const auto us=std::chrono::duration_cast<std::chrono::microseconds>(begin.time_since_epoch()).count();
    REXLOG_INFO("Native loop trace: begin seq={} phase={} thread={} us={} object={:#x} caller={:#x} steps={}",
                sequence,phase,thread,us,object,caller,steps);
  }
  ~NativeLoopTrace() {
    if(!sequence) return;
    const auto end=std::chrono::steady_clock::now();
    REXLOG_INFO("Native loop trace: end seq={} phase={} thread={} us={} duration_us={}",sequence,phase,thread,
      std::chrono::duration_cast<std::chrono::microseconds>(end.time_since_epoch()).count(),
      std::chrono::duration_cast<std::chrono::microseconds>(end-begin).count());
  }
};
}
REX_HOOK_RAW(sub_821A4BA0) {
  if(native_loop_budget.unlocked && ctx.lr==0x821A65D8)
    ctx.r4.u64=native_loop_budget.steps;
  edf::native::ApplyNativeThreadQos(edf::native::NativeThreadRole::Engine);
  NativeLoopTrace trace("step_dispatch",ctx.r3.u32,ctx.lr,ctx.r4.u32);
  if(edf::native::FrameTraceCostsOn())
    edf::native::FrameTraceCosts().steps.fetch_add(ctx.r4.u32,std::memory_order_relaxed);
  edf::native::FrameTraceCostScope trace_cost(edf::native::FrameTraceCosts().dispatch_ns);
  {
    edf::native::HookTiming timing(edf::native::HookPhase::ResourceCoordinator);
    edf::native::HookTiming engine_timing(edf::native::HookPhase::SimulationDispatch);
    const edf::native::EngineRegionScope region(edf::native::EngineRegion::Dispatch,ctx.r4.u32,ctx.fpscr.csr);
    // In-game console (src/console/console.h): due commands run here, on the engine thread,
    // before this iteration's simulation steps; nothing when the console is idle.
    const uint32_t dispatched_steps=ctx.r4.u32;
    edf::console::EngineStepBegin(ctx,base,ctx.r4.u32);
    // Step timing and the exactness gate (src/guest_state_hash.h): off unless
    // --edf_step_timing / --edf_guest_hash_trace are set.
    const auto dispatch_started=edf::gate::BeforeStepDispatch();
    __imp__sub_821A4BA0(ctx,base);
    edf::gate::AfterStepDispatch(base,dispatched_steps,dispatch_started);
    edf::console::EngineStepEnd();
  }
  edf::native::RunEngineCalibration(edf::native::EngineRegion::Dispatch);
}
REX_EXTERN(__imp__sub_821A5080);
// Inclusive engine phases below the helper. These keep the original guest
// calls intact and use the existing opt-in timing/sampling controls.
#define EDF_RENDER_PHASE(address, phase) \
  REX_EXTERN(__imp__sub_##address); \
  REX_HOOK_RAW(sub_##address) { \
    edf::native::HookTiming timing(edf::native::HookPhase::phase); \
    __imp__sub_##address(ctx,base); \
  }
EDF_RENDER_PHASE(821A3BA0, RenderBuckets)
EDF_RENDER_PHASE(821B2C28, RenderMesh)
EDF_RENDER_PHASE(820D3FD0, RenderOverlay)
EDF_RENDER_PHASE(821BE9D8, RenderSceneEnd)
EDF_RENDER_PHASE(8216DA80, RenderListener)
EDF_RENDER_PHASE(820A6978, RenderUiListener)
#undef EDF_RENDER_PHASE
// Render helper entries whose model slot 4s ran as guest code (the guest
// helper or frame dispatch, e.g. A/B alternate frames): their 821A1730 stores
// are in the guest effect pool, which the full-frame models' pool carry then
// takes again (NativeFullFrameModelPass::guest_frames).
std::atomic<uint64_t> native_guest_slot4_frames{0};
namespace edf::native {
namespace {
// Workers for the static preloads' per-group checks (edf_native_preload_workers).
// Created on first use, so a run without a static world starts no thread.
NativeDecodeWorkers& PreloadWorkers() {
  static NativeDecodeWorkers workers([] {
    const auto requested=REXCVAR_GET(edf_native_preload_workers);
    if(requested>=0) return uint32_t((std::min)(requested,16));
    // The engine thread takes a slice itself. 821A4DE8 runs between the helper
    // join and the next frame, while the render thread is idle, so a few cores
    // are free; the checks are pointer chases through cold host maps and guest
    // pages, which stop scaling well before the core count.
    const auto cores=std::thread::hardware_concurrency();
    return uint32_t(cores>=8?3u:cores>=4?1u:0u);
  }());
  return workers;
}
// The preloads' prechecks over the preload workers (RunNativeSlices); slices
// under 32 groups are not worth a hand-off. body must not throw.
template<class Body> void RunPreloadSlices(size_t count,const Body& body) {
  // Inside sim.preload_geometry/sim.preload_material: the prechecks' share.
  HookTiming timing(HookPhase::SimPreloadPrecheck);
  RunNativeSlices(PreloadWorkers(),count,32,body);
}
// Parallel prechecks of the preloads (below). 821A4DE8 runs serially between
// the helper join and the next frame, so the ~2.1 ms the two preloads spent
// proving 412 unchanged groups one by one (~2-3 us each: cold host maps, the
// buffer writer lock, guest pages) was frame time. The "current" predicates
// are pure: they read bridge state, guest memory through a per-slice window
// and BufferWrites under its own lock (Unchanged observes and counts nothing).
// While this thread holds the bridge mutex no bridge writer can run (every
// writer takes it), and the workers' reads are ordered after this thread's
// acquisition and before its release by the pool's own lock, so a slice reads
// exactly the state the serial loop would. What they decide is then applied
// serially, in group order, by the loop as before; an exception in a slice
// leaves that group to the serial check, which throws or not as it did.
enum class NativeStaticPrecheck : uint8_t { Unknown,Current,Changed };
void PreloadStaticSceneGeometryLocked(Bridge& state,const GuestReader& backing) {
  if(!state.initialized) return;
  // Only a membership change can leave an adapter entry or load record stale.
  if(state.scene_preload_group_revision!=state.scene_sources.GroupRevision()) {
    state.scene_adapter.PruneGroupGeometry(state.scene_sources);
    std::erase_if(state.scene_geometry_loads,[&](const auto& entry) { return !state.scene_sources.FindGroup(entry.first); });
    std::erase_if(state.scene_material_loads,[&](const auto& entry) { return !state.scene_sources.FindGroup(entry.first); });
    state.scene_preload_group_revision=state.scene_sources.GroupRevision();
  }
  const NativeSceneCpuWindow reader(backing);
  NativeBufferWrites::SnapshotPolicy policy{};
  policy.audit_revisions=REXCVAR_GET(edf_native_retirement_audit);
  if(!policy.audit_revisions && state.mesh_watch_audit.expired()) {
    policy.verify_interval=uint64_t(std::clamp(REXCVAR_GET(edf_native_geometry_verify_interval),0,1<<20));
    policy.verify_initial=uint64_t(std::clamp(REXCVAR_GET(edf_native_geometry_verify_initial),0,1<<16));
  }
  // Change signals: group revision, descriptor bytes, buffer generations and
  // write revisions, declaration/shader identity. Buffer comparisons that are
  // due (the sampled writer-coverage oracle) and every audit policy fall
  // through to the guarded path, so the live comparison keeps its cadence.
  const auto current=[&](const auto& window,uint32_t address,const auto& group,const auto& load) {
    if(load.revision!=group.revision || !load.reads.Unchanged(window)) return false;
    const auto* vb=state.model_buffers.Find(load.source.vertex,NativeModelBuffers::Kind::Vertex);
    const auto* ib=state.model_buffers.Find(load.source.index,NativeModelBuffers::Kind::Index);
    if(!vb || !ib || !vb->physical || !ib->physical ||
       vb->generation!=load.vertex_generation || ib->generation!=load.index_generation) return false;
    const auto shader=state.shaders.find(load.source.shader);
    if(shader==state.shaders.end() || !shader->second.bindings ||
       shader->second.bindings->shader().bytecode.Get()!=load.shader.Get()) return false;
    try { if(state.declarations.Get(load.source.declaration)!=load.declaration) return false; }
    catch(const std::exception&) { return false; }
    if(!state.scene_adapter.GroupGeometry(address,group.revision)) return false;
    const auto index_contents=ib->index_contents?ib->index_contents:
      (ib->index_storage?ib->index_storage->SourceSnapshot():nullptr);
    using View=NativeBufferWrites::SnapshotIdentityView;
    return BufferWrites().Unchanged(std::array<View,2>{{
      {load.source.vertex,*vb->physical,vb->bytes,&vb->vertex_contents},
      {load.source.index,*ib->physical,ib->bytes,&index_contents}}},load.versions,policy);
  };
  std::vector<std::pair<uint32_t,const NativeSceneSources::Group*>> groups;
  groups.reserve(state.scene_sources.Groups().size());
  for(const auto& [address,group]:state.scene_sources.Groups()) groups.emplace_back(address,&group);
  std::vector<NativeStaticPrecheck> precheck(groups.size(),NativeStaticPrecheck::Unknown);
  RunPreloadSlices(groups.size(),[&](size_t begin,size_t end) {
    const NativeSceneCpuWindow window(backing);
    for(size_t i=begin;i<end;++i) {
      try {
        const auto cached=state.scene_geometry_loads.find(groups[i].first);
        precheck[i]=cached!=state.scene_geometry_loads.end() && current(window,groups[i].first,*groups[i].second,cached->second)?
          NativeStaticPrecheck::Current:NativeStaticPrecheck::Changed;
      } catch(...) { precheck[i]=NativeStaticPrecheck::Unknown; }
    }
  });
  // Groups can share buffers, and a load, reuse or scheduled verification
  // below commits buffer state and consumes BufferWrites observations that a
  // later group's check reads. So a precheck stands only while no group has
  // left the unchanged path this tick; after the first that does, each group
  // is checked here, serially, exactly as before.
  bool mutated=false;
  const auto loaded_before=state.scene_geometry_loaded;
  for(size_t index=0;index<groups.size();++index) {
    const auto address=groups[index].first; const auto& group=*groups[index].second; // Captured below.
    const auto cached=state.scene_geometry_loads.find(address);
    const bool unchanged=!mutated && precheck[index]!=NativeStaticPrecheck::Unknown?precheck[index]==NativeStaticPrecheck::Current:
      cached!=state.scene_geometry_loads.end() && current(reader,address,group,cached->second);
    if(unchanged) {
      ++state.scene_geometry_unchanged; continue;
    }
    mutated=true;
    try {
      NativeRecordedReads reads;
      const auto input=ReadNativeSceneGeometrySource(NativeRecordingReader(reader,reads),address);
      const auto* vb=state.model_buffers.Find(input.vertex,NativeModelBuffers::Kind::Vertex);
      const auto* ib=state.model_buffers.Find(input.index,NativeModelBuffers::Kind::Index);
      if(!vb || !ib || !vb->physical || !ib->physical || vb->stride!=input.stride || !vb->bytes || !ib->bytes)
        throw std::runtime_error("static preload requires registered physical geometry");
      const auto declaration=state.declarations.Get(input.declaration);
      const auto& shader=state.shaders.at(input.shader).bindings->shader();
      const auto index_contents=ib->index_contents?ib->index_contents:
        (ib->index_storage?ib->index_storage->SourceSnapshot():nullptr);
      using View=NativeBufferWrites::SnapshotIdentityView;
      const auto versions=BufferWrites().TryValidateObservedSet(std::array<View,2>{{
        {input.vertex,*vb->physical,vb->bytes,&vb->vertex_contents},
        {input.index,*ib->physical,ib->bytes,&index_contents}}},policy);
      const auto same_identity=[&] {
        return cached!=state.scene_geometry_loads.end() && cached->second.source==input &&
          cached->second.revision==group.revision &&
          cached->second.vertex_generation==vb->generation && cached->second.index_generation==ib->generation &&
          cached->second.declaration==declaration && cached->second.shader.Get()==shader.bytecode.Get() &&
          state.scene_adapter.GroupGeometry(address,group.revision);
      };
      const auto same_versions=[&](const std::array<NativeBufferWrites::ObservedVersion,2>& observed) {
        for(size_t i=0;i<2;++i) if(observed[i].lifetime!=cached->second.versions[i].lifetime ||
            observed[i].revision!=cached->second.versions[i].revision) return false;
        return true;
      };
      if(versions && same_identity() && same_versions(*versions)) {
        cached->second.reads=std::move(reads);
        ++state.scene_geometry_reused; continue;
      }
      using Snapshots=std::array<NativeBufferWrites::ObservedSnapshot,2>;
      std::optional<Snapshots> observed;
      if(versions) observed=Snapshots{{
        {(*versions)[0],vb->vertex_contents,false,true,false},
        {(*versions)[1],index_contents,false,true,false}}};
      else {
        // A failed fast validation may require the scheduled live comparison,
        // but it never permits an unguarded copy into native assets.
        using Source=NativeBufferWrites::SnapshotSource;
        observed=BufferWrites().CopyObservedSet(std::array<Source,2>{{
          {input.vertex,*vb->physical,{backing.Bytes(vb->address,vb->bytes),vb->bytes},vb->vertex_contents},
          {input.index,*ib->physical,{backing.Bytes(ib->address,ib->bytes),ib->bytes},index_contents}}},nullptr,policy);
      }
      if(!observed) throw std::runtime_error("static preload geometry writer transaction is unavailable");
      for(const auto& value:*observed) if(value.unreported_change)
        REXLOG_WARN("Native scene preload detected an unreported geometry write; source comparison repaired the snapshot");
      // A scheduled comparison that found the retained candidates unchanged at
      // the loaded revisions is a verification, not a new load: rebuilding the
      // mesh here re-loaded ~verify_interval-th of all groups every tick.
      if(same_identity() && (*observed)[0].contents==vb->vertex_contents && (*observed)[1].contents==index_contents &&
         same_versions({(*observed)[0].version,(*observed)[1].version})) {
        cached->second.reads=std::move(reads);
        ++state.scene_geometry_verified; continue;
      }
      auto geometry=RetainNativeSceneGeometryLocked(state,input,*vb,*ib,declaration,shader,*observed);
      state.scene_adapter.PublishGroupGeometry(address,group.revision,std::move(geometry),input);
      state.scene_geometry_loads[address]={input,group.revision,vb->generation,ib->generation,
        {(*observed)[0].version,(*observed)[1].version},declaration,shader.bytecode,std::move(reads)};
      ++state.scene_geometry_loaded;
    } catch(const std::exception& error) {
      state.scene_adapter.RetireGroupGeometry(address);
      state.scene_geometry_loads.erase(address);
      ++state.scene_geometry_deferred;
      if(state.scene_geometry_reasons.size()<32 && state.scene_geometry_reasons.insert(error.what()).second)
        REXLOG_INFO("Native scene geometry preload deferred: {}",error.what());
    }
  }
  if(!state.scene_sources.Groups().empty() &&
     (loaded_before!=state.scene_geometry_loaded || state.scene_publication_tick%120==0))
    REXLOG_INFO("Native scene geometry preload: groups={} ready={} loaded={} reused={} verified={} unchanged={} deferred={} (simulation publication; no draws)",
      state.scene_sources.Groups().size(),state.scene_adapter.geometry_groups(),state.scene_geometry_loaded,
      state.scene_geometry_reused,state.scene_geometry_verified,state.scene_geometry_unchanged,state.scene_geometry_deferred);
}
void PreloadStaticSceneMaterialsLocked(Bridge& state,const GuestReader& backing) {
  const NativeSceneCpuWindow reader(backing);
  // Program change signals: group revision, the descriptor's material, the
  // published program, schema/shader/texture identities and every recorded
  // program byte. Constant values are not program inputs: they change per
  // frame (untracked stores) and are refreshed on their own below.
  const auto host_current=[&](uint32_t address,const auto& group,const auto& load) {
    const auto geometry=state.scene_geometry_loads.find(address);
    if(load.revision!=group.revision || !load.published || geometry==state.scene_geometry_loads.end() ||
       geometry->second.source.material!=load.material ||
       state.scene_adapter.GroupMaterial(address,group.revision)!=load.published) return false;
    return NativeSceneMaterialHostCurrent(state,*load.published->program,load.material,load.schema);
  };
  const auto current=[&](uint32_t address,const auto& group,const auto& load) {
    return host_current(address,group,load) && load.reads.Unchanged(reader);
  };
  // Prechecked in parallel, as the geometry (RunPreloadSlices): the host
  // checks, the program bytes and, when both hold, the constant refresh
  // (ProbeNativeSceneMaterialGuestInputs), all pure. Unlike geometry, groups do
  // not depend on each other here: a group's check reads its own load, its own
  // adapter entry and host state this pass never writes, and what the loop
  // writes (the adapter's entry, the load) is that group's alone. So every
  // precheck stands; one that threw is redone here.
  struct Precheck { bool known=false,current=false; NativeSceneMaterialGuestProbe guest; };
  std::vector<std::pair<uint32_t,const NativeSceneSources::Group*>> groups;
  groups.reserve(state.scene_sources.Groups().size());
  for(const auto& [address,group]:state.scene_sources.Groups()) groups.emplace_back(address,&group);
  std::vector<Precheck> precheck(groups.size());
  RunPreloadSlices(groups.size(),[&](size_t begin,size_t end) {
    const NativeSceneCpuWindow window(backing);
    for(size_t i=begin;i<end;++i) {
      auto& result=precheck[i];
      try {
        const auto cached=state.scene_material_loads.find(groups[i].first);
        if(cached!=state.scene_material_loads.end() && host_current(groups[i].first,*groups[i].second,cached->second)) {
          const auto& load=cached->second;
          if(!load.schema) continue;  // Unknown: the serial check decides.
          result.guest=ProbeNativeSceneMaterialGuestInputs(window,load.reads,*load.schema,load.constants,load.published->constants);
          result.current=result.guest.program_unchanged;
        }
        result.known=true;
      } catch(...) { result=Precheck{}; }
    }
  });
  for(size_t index=0;index<groups.size();++index) {
    const auto address=groups[index].first; const auto& group=*groups[index].second;
    const auto cached=state.scene_material_loads.find(address);
    auto* guest=precheck[index].known && precheck[index].current?&precheck[index].guest:nullptr;
    const bool unchanged=precheck[index].known?precheck[index].current:
      cached!=state.scene_material_loads.end() && current(address,group,cached->second);
    if(cached!=state.scene_material_loads.end() && unchanged) {
      // The program is unchanged; only constant values are re-read, and the
      // program object is kept when one of them moved.
      auto& load=cached->second;
      bool refreshed=false;
      try {
        // A prechecked refresh that threw is the same failure as one thrown here.
        if(guest && !guest->constants_read) throw std::runtime_error("prechecked material constant refresh failed");
        auto constants=guest?std::move(guest->constants):
          RefreshNativeSceneMaterialConstants(reader,*load.schema,load.constants,load.published->constants);
        if(constants) {
          static std::set<std::string> reported;
          for(size_t i=0;i<constants->size() && reported.size()<32;++i)
            if((*constants)[i].registers!=load.published->constants[i].registers &&
               reported.insert((*constants)[i].name).second)
              REXLOG_INFO("Native scene material constant refreshed without a program change: {}",(*constants)[i].name);
          state.scene_adapter.PublishGroupMaterial(address,group.revision,load.published->program,std::move(*constants));
          load.published=state.scene_adapter.GroupMaterial(address,group.revision);
          ++state.scene_material_constants;
        } else ++state.scene_material_unchanged;
        refreshed=load.published!=nullptr;
      } catch(const std::exception&) {}
      if(refreshed) continue;
    }
    try {
      const auto geometry=state.scene_geometry_loads.find(address);
      if(geometry==state.scene_geometry_loads.end()) throw std::runtime_error("material awaits group descriptor");
      const auto material=geometry->second.source.material;
      NativeRecordedReads reads;
      const NativeRecordingReader recorder(reader,reads);
      const auto previous=state.scene_adapter.GroupMaterial(address,group.revision);
      // Values are read through `reader`, not recorded: the constant refresh compares them.
      auto build=BuildNativeSceneMaterialLocked(state,recorder,reader,material,previous?previous->program.get():nullptr);
      if(build.reused) {
        // Recorded program bytes moved without changing the program: report
        // where, so a per-frame program input can be moved out of `reads`.
        if(cached!=state.scene_material_loads.end() && cached->second.revision==group.revision)
          if(const auto changed=cached->second.reads.FirstChange(reader)) {
            static std::set<uint32_t> reported;
            if(reported.size()<16 && reported.insert(*changed).second)
              REXLOG_INFO("Native scene material program bytes changed without a program change: group={:#x} material={:#x} address={:#x}",
                address,material,*changed);
          }
        // Same program: keep the published object world (never observed, see
        // NativeSceneObjectWorldConstant) so equal other constants republish nothing.
        if(previous->constants.size()==build.constants.size() && build.layout.size()==build.constants.size())
          for(size_t i=0;i<build.constants.size();++i) {
            auto& fresh=build.constants[i]; const auto& old=previous->constants[i];
            if(build.layout[i].object_world && old.name==fresh.name && old.pixel==fresh.pixel && old.global==fresh.global &&
               old.registers.size()==fresh.registers.size() && !NativeSceneWorldHasNaN(old.registers)) fresh.registers=old.registers;
          }
        state.scene_adapter.PublishGroupMaterial(address,group.revision,previous->program,std::move(build.constants));
        ++state.scene_material_reused;
      } else {
        state.scene_adapter.PublishGroupMaterial(address,group.revision,std::move(build.program),std::move(build.constants));
        ++state.scene_material_loaded;
      }
      state.scene_material_loads[address]={group.revision,material,build.schema,
        state.scene_adapter.GroupMaterial(address,group.revision),std::move(reads),std::move(build.layout)};
    } catch(const std::exception& error) {
      state.scene_adapter.RetireGroupMaterial(address); ++state.scene_material_deferred;
      state.scene_material_loads.erase(address);
      if(state.scene_material_reasons.size()<32 && state.scene_material_reasons.insert(error.what()).second)
        REXLOG_INFO("Native scene material preload deferred: {}",error.what());
    }
  }
  if(!state.scene_sources.Groups().empty() && state.scene_publication_tick%120==0)
    REXLOG_INFO("Native scene material preload: groups={} ready={} loaded={} reused={} constants={} unchanged={} deferred={} (owned inputs; pass state still explicit)",
      state.scene_sources.Groups().size(),state.scene_adapter.material_groups(),state.scene_material_loaded,
      state.scene_material_reused,state.scene_material_constants,state.scene_material_unchanged,state.scene_material_deferred);
}
}
}
namespace {
// The guest routes' tick gate: one per process, as the helper runs one at a
// time (821A6508 joins each call). Only a render with steps advances it, and
// the tick changes with every step, so it can never count a tick twice with
// the full frame's own gate (A/B alternation).
bool GuestRenderTickFrame() {
  static edf::native::NativeTickGate gate;
  return gate.Advance(edf::native::MakeNativeFrameMotion(native_render_budget.unlocked,native_render_budget.divisor,
    native_render_budget.tick,native_render_budget.fraction,native_render_budget.steps,false));
}
}
REX_HOOK_RAW(sub_821A5080) {
  edf::native::ApplyNativeThreadQos(edf::native::NativeThreadRole::RenderHelper);
  // Sampled by the engine region probe: is the helper running beside the step?
  struct HelperActive {
    HelperActive() { edf::native::native_render_helper_active.fetch_add(1,std::memory_order_relaxed); }
    ~HelperActive() { edf::native::native_render_helper_active.fetch_sub(1,std::memory_order_relaxed); }
  } helper_active;
  bool ab_native=true;
  if(const auto ab_period=REXCVAR_GET(edf_native_ab_alternate); ab_period>0) {
    uint64_t frame=0;
    {
      auto& state=edf::native::State();
      std::lock_guard submission(state.submissions);
      std::lock_guard lock(state.mutex);
      frame=state.indexed_output_frames+1;
    }
    ab_native=edf::native::AbSide(frame,REXCVAR_GET(edf_native_output_capture_start_frame),ab_period);
    static std::atomic<uint64_t> ab_logged=0;
    if(ab_logged.exchange(frame,std::memory_order_relaxed)!=frame)
      REXLOG_INFO("ab_alternate frame={} native={}",frame,ab_native?1:0);
  }
  const edf::native::NativeAbSideLatch ab_latch(ab_native);
  // Reuse off (native_reuse.h): the process-wide switch, mirrored for the
  // other threads' sites, and this frame's side of the reuse alternation,
  // latched for this helper call like the A/B side and tagged the same way
  // (reuse_alternate frame=F native=0 for a reuse-off reference frame, 1 for
  // a reuse-on judged frame; F is the indexed output frame the capture names).
  edf::native::native_reuse_off_all.store(REXCVAR_GET(edf_native_reuse_off),std::memory_order_relaxed);
  bool reuse_off=false;
  if(const auto reuse_period=REXCVAR_GET(edf_native_reuse_off_alternate); reuse_period>0) {
    if(REXCVAR_GET(edf_native_ab_alternate)>0) {
      static std::atomic<bool> reported=false;
      if(!reported.exchange(true)) REXLOG_WARN("edf_native_reuse_off_alternate ignored: edf_native_ab_alternate is on");
    } else {
      uint64_t frame=0;
      {
        auto& state=edf::native::State();
        std::lock_guard submission(state.submissions);
        std::lock_guard lock(state.mutex);
        frame=state.indexed_output_frames+1;
      }
      reuse_off=edf::native::NativeReuseOffSide(frame,REXCVAR_GET(edf_native_output_capture_start_frame),reuse_period);
      static std::atomic<uint64_t> reuse_logged=0;
      if(reuse_logged.exchange(frame,std::memory_order_relaxed)!=frame)
        REXLOG_INFO("reuse_alternate frame={} native={}",frame,reuse_off?0:1);
    }
  }
  const edf::native::NativeReuseOffLatch reuse_latch(reuse_off);
  native_render_frames.fetch_add(1,std::memory_order_relaxed);
  struct RestoreTickFrame {
    bool saved=native_render_tick_frame;
    ~RestoreTickFrame() { native_render_tick_frame=saved; }
  } restore_tick_frame;
  edf::native::NativeSceneQueues queues;
  struct RestoreSceneQueues {
    uint32_t animation_owner=edf::native::native_scene_animation_owner;
    std::optional<edf::native::NativeScenePassCamera> camera=edf::native::native_scene_pass_camera;
    std::shared_ptr<const edf::native::NativeScenePassCameras> cameras=edf::native::native_scene_pass_cameras;
    std::optional<edf::native::NativeScenePassAnimation> animation=edf::native::native_scene_pass_animation;
    std::shared_ptr<const edf::native::NativeSceneAdapter::WorldAnimations> animations=edf::native::native_scene_pass_animations;
    edf::native::NativeSceneQueues* previous=edf::native::native_scene_queues;
    std::shared_ptr<const edf::native::NativeScenePublication> publication=edf::native::native_scene_publication;
    ~RestoreSceneQueues() {
      edf::native::native_scene_animation_owner=animation_owner;
      edf::native::native_scene_pass_camera=std::move(camera);
      edf::native::native_scene_pass_cameras=std::move(cameras);
      edf::native::native_scene_pass_animation=std::move(animation);
      edf::native::native_scene_pass_animations=std::move(animations);
      edf::native::native_scene_queues=previous;
      if(!publication && !edf::native::native_scene_publication) return;
      auto& state=edf::native::State();
      std::lock_guard submission(state.submissions);
      std::lock_guard lock(state.mutex);
      edf::native::native_scene_publication=std::move(publication);
    }
  } restore_scene_queues;
  edf::native::native_scene_pass_camera.reset();
  edf::native::native_scene_pass_cameras.reset();
  edf::native::native_scene_pass_animation.reset();
  edf::native::native_scene_pass_animations.reset();
  edf::native::native_scene_animation_owner=0;
  if(EDF_NATIVE_FLAG(scene_queued) && EDF_NATIVE_FLAG(host) &&
     EDF_NATIVE_FLAG(shader_bridge) && EDF_NATIVE_FLAG(seam_draws)) {
    edf::native::native_scene_queues=&queues;
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    edf::native::native_scene_publication=state.scene_adapter.AcquirePublication();
    if(EDF_NATIVE_FLAG(scene_camera_owned))
      edf::native::native_scene_pass_cameras=state.scene_adapter.AcquireCameras();
    edf::native::native_scene_pass_animations=state.scene_adapter.AcquireWorldAnimations();
  }
  struct RestoreRenderBudget {
    NativeLoopBudget budget=native_render_budget;
    uint64_t publication=native_render_publication;
    ~RestoreRenderBudget() { native_render_budget=budget; native_render_publication=publication; }
  } restore_render_budget;
  {
    auto& motion=ModelMotionState();
    std::lock_guard lock(motion.mutex);
    native_render_budget=motion.published;
    native_render_publication=motion.publication;
  }
  edf::latency::OnFrameRecorded(native_render_budget.tick);
  NativeLoopTrace trace("helper_dispatch",ctx.r3.u32,ctx.lr);
  edf::native::HookTiming timing(edf::native::HookPhase::ResourceHelper);
  edf::native::HookTiming engine_timing(edf::native::HookPhase::RenderHelper);
  edf::native::FrameTraceCostScope trace_cost(edf::native::FrameTraceCosts().helper_ns);
  // A/B guest-side frames take today's path (frame dispatch or the guest helper).
  const auto route=edf::native::SelectNativeFrameRoute(EDF_NATIVE_FLAG(full_frame),EDF_NATIVE_FLAG(frame_dispatch),
    EDF_NATIVE_FLAG(shader_bridge),EDF_NATIVE_FLAG(host),ab_native);
  if(ab_native && EDF_NATIVE_FLAG(full_frame) && route!=edf::native::NativeFrameRoute::full_frame) {
    static std::atomic<bool> reported=false;
    if(!reported.exchange(true))
      REXLOG_WARN("Native full frame disabled: requires edf_native_host and edf_native_shader_bridge (guest helper retained)");
  }
  if(route==edf::native::NativeFrameRoute::full_frame) {
    // No guest helper: native scene begin, passes and post; the guest calls
    // left are listed in kNativeFrameRemainingGuestCalls. 821A6508 joins each helper call (821A53A8) before the next, so the
    // single instance is never run concurrently.
    static edf::native::NativeFullFrame full_frame;
    static const auto models=std::make_shared<edf::native::NativeFullFrameModelsShared>();
    static const bool wired=[&] {
      return full_frame.Replace(edf::native::MakeNativeFullFrameStaticWorldPass(base)) &&
        full_frame.Replace(edf::native::MakeNativeFullFrameModelsPass(base,models)) &&
        full_frame.Replace(edf::native::MakeNativeFullFrameSkyPass(base,models)) &&
        full_frame.Replace(edf::native::MakeNativeFullFrameEffectsPass(base,models)) &&
        full_frame.Replace(edf::native::MakeNativeFullFrameTransparentPass(base,models));
    }();
    if(!wired) throw std::runtime_error("native full frame is missing a pass slot");
    // The helper's stack frame and guest frame context (stack+80), as
    // DispatchNativeFrame builds them, for the remaining guest calls.
    const edf::native::GuestReader reader(base);
    auto work=ctx;
    if(work.r1.u32<224) throw std::runtime_error("invalid native full frame stack");
    const auto stack=work.r1.u32-224;
    reader.StoreWord(stack,work.r1.u32);
    work.r1.u64=stack;
    edf::native::RunNativeFullFrame(full_frame,base,ctx.r3.u32,reader.Add(stack,80),models,
      [&](uint32_t function,uint32_t object,uint32_t argument,uint32_t index,uint32_t lr) {
        work.r3.u64=object; work.r4.u64=argument; work.r5.u64=index;
        work.ctr.u64=function; work.lr=lr;
        rex::runtime::ResolveIndirectFunction(function)(work,base);
      });
  } else if(route==edf::native::NativeFrameRoute::frame_dispatch) {
    native_guest_slot4_frames.fetch_add(1,std::memory_order_relaxed);
    native_render_tick_frame=GuestRenderTickFrame();
    const edf::native::GuestReader reader(base);
    auto work=ctx;
    if(work.r1.u32<224) throw std::runtime_error("invalid native frame dispatch stack");
    const auto stack=work.r1.u32-224;
    reader.StoreWord(stack,work.r1.u32);
    work.r1.u64=stack;
    edf::native::DispatchNativeFrame(reader,ctx.r3.u32,reader.Add(stack,80),
      [&](uint32_t function,uint32_t object,uint32_t argument,uint32_t index,uint32_t lr) {
        work.r3.u64=object; work.r4.u64=argument; work.r5.u64=index;
        work.ctr.u64=function; work.lr=lr;
        if(function==0x821A3BA0) {
          auto bucket=work;
          if(bucket.r1.u32<128) throw std::runtime_error("invalid native bucket dispatch stack");
          const auto bucket_stack=bucket.r1.u32-128;
          reader.StoreWord(bucket_stack,bucket.r1.u32); bucket.r1.u64=bucket_stack;
          edf::native::HookTiming bucket_timing(edf::native::HookPhase::RenderBuckets);
          edf::native::DispatchNativeFrameBuckets(reader,object,argument,
            [&](uint32_t callback,uint32_t item,uint32_t input,uint32_t,uint32_t return_address) {
              bucket.r3.u64=item; bucket.r4.u64=input; bucket.ctr.u64=callback; bucket.lr=return_address;
              rex::runtime::ResolveIndirectFunction(callback)(bucket,base);
            });
        } else rex::runtime::ResolveIndirectFunction(function)(work,base);
      });
    static std::atomic<uint64_t> native_frames=0;
    const auto frame_count=++native_frames;
    if(frame_count<=4 || frame_count%1000==0)
      REXLOG_INFO("Native frame dispatch: frames={} (native outer and bucket traversal; world/overlay/presentation callbacks retained)",frame_count);
    ctx.r3=work.r3;
  } else {
    native_guest_slot4_frames.fetch_add(1,std::memory_order_relaxed);
    native_render_tick_frame=GuestRenderTickFrame();
    __imp__sub_821A5080(ctx,base);
  }
  if(!queues.empty()) throw std::runtime_error("native scene selections survived their render helper");
}
namespace {
// End of 821A4DE8 (r3 is the scene): after the scene+100 slot-2 walk, so this
// tick's pose builds are in memory. Failures stay native and are counted.
// tick: the engine thread's native_loop_budget.tick (thread-local), passed in
// because the tick may run on RegistryWorker. render_only: an unlocked
// iteration without a simulation step (the tick has not advanced), for which
// the registry reads only what its events ask for (NativeRenderRegistry::Tick
// with refresh false) unless edf_native_render_registry_idle_skip is off.
void TickNativeRenderRegistry(uint8_t* base,uint32_t scene,uint64_t tick,bool render_only) {
  auto& registry=edf::native::RenderRegistry();
  if(!REXCVAR_GET(edf_native_render_registry) && !EDF_NATIVE_FLAG(full_frame)) { if(registry.active()) registry.Clear(); return; }
  edf::native::HookTiming timing(edf::native::HookPhase::SimRegistry);
  try {
    const edf::native::GuestReader reader(base);
    // Object headers, LOD tables and poses of the re-read objects share pages.
    const edf::native::NativeSceneCpuWindow window(reader);
    // Buffer identities are read under the bridge lock, first sight only.
    const auto decode=[&](uint32_t instance,uint32_t vector,uint32_t bones) {
      auto& state=edf::native::State();
      std::lock_guard lock(state.mutex);
      return edf::native::DecodeNativeModelLayoutWith(reader,instance,vector,
        [&](uint32_t owner,edf::native::NativeModelBuffers::Kind kind)->uint64_t {
          const auto* found=state.model_buffers.Find(owner,kind);
          return found?found->generation:0;
        },bones);
    };
    // What a render-only iteration's refresh would re-read (scene+100
    // members, instanced worlds, frame-posed roots, the round robin) is
    // written by simulation steps: 821A4DE8 still runs the scene+100 slot-2
    // updates on such an iteration, over state no step has moved, and the
    // unlock notes measured source poses changing only at the 60 Hz step
    // cadence. edf_native_render_registry_idle_audit checks that claim: it
    // follows each such light tick with a compare-only read of every update
    // member and logs the entries a full tick would have changed, which the
    // light tick left until its probe reaches them or the next step. It
    // publishes nothing and moves no pose motion, so it cannot itself mark
    // the poses it reports render-dependent.
    // Reuse off (native_reuse.h, edf_native_reuse_off only: the registry ticks
    // per iteration, not per rendered frame): full ticks (so no probe or live
    // set either: every update member is read), and no frame-pose memo or
    // unchanged-entry and constant pointer sharing inside Tick.
    edf::native::native_reuse_off_all.store(REXCVAR_GET(edf_native_reuse_off),std::memory_order_relaxed);
    const bool light=render_only && REXCVAR_GET(edf_native_render_registry_idle_skip) && edf::native::NativeReuseAllowed();
    auto snapshot=registry.Tick(window,scene,tick,decode,!light);
    if(light && REXCVAR_GET(edf_native_render_registry_idle_audit)) {
      const auto changes=registry.AuditLight(window,scene,tick,decode);
      static uint64_t audited=0,mismatched=0,changed=0;
      ++audited; mismatched+=changes!=0; changed+=changes;
      if(changes && (mismatched<=16 || !(mismatched&(mismatched-1))))
        REXLOG_WARN("Native render registry idle audit mismatch: tick={} changed_entries={} audited={} mismatched={} total_changed={}",
          tick,changes,audited,mismatched,changed);
      else if(!changes && (audited<=4 || audited%1000==0))
        REXLOG_INFO("Native render registry idle audit: audited={} mismatched={} total_changed={}",audited,mismatched,changed);
    }
    // Coverage census: the resolved objects of classes the registry's table
    // does not hold, which no native pass draws unless their slot 4 is one the
    // static world or effect builders take (NativeCoverageSlotOwner). Present
    // objects, not visibility-tested: counted into every full frame while they exist.
    static bool census_population=false;
    if(REXCVAR_GET(edf_native_coverage_census)) {
      std::vector<edf::native::NativeCoverageCensus::Population> population;
      for(const auto& [vtable,count]:registry.UnknownClasses()) {
        uint32_t slot4=0;
        try { slot4=window.Word(window.Add(vtable,16)); } catch(const std::exception&) {}
        const auto owner=edf::native::NativeCoverageSlotOwner(vtable,slot4);
        if(owner!=edf::native::NativeCoverageOwner::None && owner!=edf::native::NativeCoverageOwner::Empty) continue;
        const bool empty=owner==edf::native::NativeCoverageOwner::Empty;
        population.push_back({empty?edf::native::NativeCoverageStatus::Parity:edf::native::NativeCoverageStatus::Uncovered,vtable,{},
          empty?"empty_slot4":"registry_unknown_class",std::format("slot=0x{:08X}",slot4),count});
      }
      edf::native::CoverageCensus().SetPopulation("registry",std::move(population));
      census_population=true;
    } else if(census_population) {
      edf::native::CoverageCensus().SetPopulation("registry",{});
      census_population=false;
    }
    static uint64_t ticks=0;
    const bool report=++ticks<=4 || ticks%1000==0;
    if(report) {
      const auto stats=registry.stats();
      REXLOG_INFO("Native render registry: generation={} tick={} entries={} light_ticks={} idle_ticks={} records={} subscribed={} births={} seeded={} deaths={} "
        "rebirths={} unknown_deaths={} unknown_classes={} deferred={} foreign={} builds={} changed={} unchanged={} pose_reuses={} "
        "read_failures={} layouts={} layout_failures={} retrying={} frame_poses={}/{}/{} constant_changes={} probes={} live={} live_reads={} "
        "same_tick_changes={} captures_pruned={}",snapshot->generation,snapshot->tick,snapshot->entries.size(),
        stats.light_ticks,stats.idle_ticks,stats.records,
        stats.subscribed,stats.births,stats.seeded,stats.deaths,stats.rebirths,stats.unknown_deaths,stats.unknown_classes,
        stats.deferred,stats.foreign,stats.builds,stats.changed,stats.unchanged,stats.pose_reuses,stats.read_failures,
        stats.layout_captures,stats.layout_failures,stats.retrying,stats.frame_poses,stats.frame_pose_reuses,stats.frame_pose_failures,
        stats.constant_changes,stats.probes,stats.live,stats.live_reads,stats.same_tick_changes,stats.captures_pruned);
    }
    if(REXCVAR_GET(edf_native_render_registry_audit)) {
      const auto audit=registry.AuditScene(reader,scene);
      static uint64_t audits=0,mismatched=0;
      ++audits; mismatched+=audit.mismatches()!=0;
      if(report || (audit.mismatches() && mismatched<=8))
        REXLOG_INFO("Native render registry audit: ticks={} mismatched_ticks={} guest={} guest_updates={} registry={} "
          "missing={} extra={} subscription={}",audits,mismatched,audit.guest,audit.guest_updates,audit.registry,
          audit.missing,audit.extra,audit.subscription);
    }
  } catch(const std::exception& error) {
    static uint64_t failures=0;
    if(++failures<=8 || (failures&(failures-1))==0)
      REXLOG_WARN("Native render registry tick failed ({}): {}",failures,error.what());
  }
}
// The registry tick (0.5-0.9 ms a frame in play) beside the step publication
// (edf_native_registry_overlap). 821A4DE8's tail runs serially between the
// helper join and the next frame, so its phases add up to frame time; the
// tick depends on none of them. It reads guest memory the guest call just
// left (no guest code runs until the hook returns, which joins it first) and
// its own state, which only the tick touches (its hooks append to a locked
// event list); its first-sight layout decodes take the bridge mutex, so while
// the preloads hold it they wait, and nothing here waits on the tick with the
// mutex held. One worker, so ticks never overlap each other; the pool's lock
// orders each tick after the previous one and before the hook returns.
edf::native::NativeDecodeWorkers& RegistryWorker() {
  static edf::native::NativeDecodeWorkers worker(1);
  return worker;
}
}
REX_EXTERN(__imp__sub_821A4DE8);
REX_HOOK_RAW(sub_821A4DE8) {
  const bool load_trace=LoadTraceOn();
  const bool trace=REXCVAR_GET(edf_native_load_timings) || load_trace;
  const uint32_t manager=ctx.r3.u32;
  if(load_trace) { LoadTraceMarkEngineThread(); LoadTraceTick(); }
  // These are the same valid manager fields immediately read by the original.
  // Snapshot before the call: desired/actual are not changed by instrumentation.
  uint32_t actual=0,desired=0;
  if(trace) {
    try {
      const edf::native::GuestReader reader(base);
      const auto* fields=reader.Bytes(manager+2261,2);
      actual=fields[0]; desired=fields[1];
    } catch(const std::exception& error) {
      REXLOG_WARN("Native resource transition snapshot unavailable: {}",error.what());
    }
  }
  const bool edge=trace && actual!=desired;
  if(edge && REXCVAR_GET(edf_native_load_timings)) REXLOG_INFO("Native resource transition: begin manager={:#x} actual={} desired={}",
                      manager,actual,desired);
  // The engine applies the requested loading state here: desired=1 starts the
  // loading presenter thread, desired=0 waits for it to leave and releases it.
  if(edge) LoadTraceEvent(desired?"transition_to_loading":"transition_to_play",manager);
  edf::native::LoadTraceScope transition_trace(load_trace,edf::native::LoadTraceKind::EngineTransition);
  // Before the transition's timings: the render helper has been joined here,
  // so this is the engine thread's speed with the helper idle.
  edf::native::RunEngineCalibration(edf::native::EngineRegion::Transition);
  edf::native::HookTiming timing(edf::native::HookPhase::ResourceTransition,edge);
  edf::native::HookTiming engine_timing(edf::native::HookPhase::FrameTransition);
  edf::native::FrameTraceCostScope trace_cost(edf::native::FrameTraceCosts().transition_ns);
  edf::native::EngineRegionScope region(edf::native::EngineRegion::Transition,native_loop_budget.steps,ctx.fpscr.csr);
  // Model poses (ModelPublications) feed only the hybrid model pass and its
  // audit: layouts are registered by 821C9C20 draws, which a full frame
  // without A/B guest frames makes only if a guest phase still draws a model.
  // With nothing registered there, neither the dirty-pose capture nor the pose
  // publication runs; a later registration is seeded at the next publication.
  const bool full_frame_only=EDF_NATIVE_FLAG(full_frame) && REXCVAR_GET(edf_native_ab_alternate)<=0 &&
    REXCVAR_GET(edf_native_shadow_render)<=0;
  const bool model_flag=EDF_NATIVE_FLAG(model_publication);
  const bool model_publication=model_flag && (!full_frame_only || ModelPublications().size());
  std::vector<uint32_t> dirty_poses;
  {
    struct Scope {
      std::vector<uint32_t>* previous=native_model_dirty_poses;
      ~Scope() { native_model_dirty_poses=previous; }
    } scope;
    native_model_dirty_poses=model_publication?&dirty_poses:nullptr;
    __imp__sub_821A4DE8(ctx,base);
  }
  // The registry tick starts now on RegistryWorker and is joined below, or on
  // any exit (the guard), before this hook returns to guest code.
  struct RegistryJoin {
    uint64_t ticket=0;
    void Wait() { if(ticket) RegistryWorker().Wait(std::exchange(ticket,0)); }
    ~RegistryJoin() { Wait(); }
  } registry_join;
  // A render-only iteration (unlocked, no simulation step): the tick has not
  // advanced and no step wrote simulation state. 821A4DE8 itself still ran
  // (camera interpolation, the scene+100 and +2228 listeners), so what it
  // publishes from the scene cameras is still read; the registry reads only
  // what its events ask for (TickNativeRenderRegistry).
  const bool render_only=native_loop_budget.unlocked && !native_loop_budget.steps;
  const bool registry_overlap=REXCVAR_GET(edf_native_registry_overlap);
  if(registry_overlap)
    registry_join.ticket=RegistryWorker().Submit([base,manager,tick=native_loop_budget.tick,render_only] {
      try { TickNativeRenderRegistry(base,manager,tick,render_only); } catch(...) {}  // A worker cannot carry it; the tick logs its own.
    });
  // This step's 820B4250 tree publications; cleared on every exit below.
  struct StepTrees { ~StepTrees() { native_step_trees.clear(); } } step_trees;
  if(EDF_NATIVE_FLAG(scene_camera_owned)) {
    // Every iteration: an unlocked render-only one interpolates the cameras
    // (821CDDF8 at 821A4EB0). The engine thread is PublishCameras' only
    // caller, so a set equal to the one it last published (a still camera)
    // skips the bridge mutex, which the render thread holds in slices.
    static std::shared_ptr<const edf::native::NativeScenePassCameras> published;
    auto cameras=edf::native::ReadNativeScenePassCameras(edf::native::GuestReader(base),manager);
    if(!published || *published!=cameras) {
      auto& state=edf::native::State();
      std::lock_guard lock(state.mutex);
      state.scene_adapter.PublishCameras(std::move(cameras));
      published=state.scene_adapter.AcquireCameras();
    }
  }
  {
    auto& motion=ModelMotionState();
    std::lock_guard lock(motion.mutex);
    motion.published=native_loop_budget;
    ++motion.publication;
    if(!native_loop_budget.unlocked) motion.Clear();
  }
  if(EDF_NATIVE_FLAG(scene_queued) && (!native_loop_budget.unlocked || native_loop_budget.steps)) {
    auto& state=edf::native::State();
    const edf::native::GuestReader backing(base);
    // Trees have their own lock: compared (and recaptured when they differ)
    // before the bridge locks, through a page window. A tree 820B4250
    // published during this step is skipped while its image is current (no
    // hooked mutation since); an unhooked writer after it is caught at the
    // next step's 820B4250 comparison.
    if(EDF_NATIVE_FLAG(scene_tree_published)) {
      edf::native::HookTiming trees_timing(edf::native::HookPhase::SimTrees);
      auto& trees=edf::native::TreePublications();
      const edf::native::NativeSceneCpuWindow reader(backing);
      for(const auto owner:trees.Owners()) {
        if(std::ranges::find(native_step_trees,owner)!=native_step_trees.end() && trees.Acquire(owner)) continue;
        try { trees.Publish(reader,owner); }
        catch(const std::exception&) { trees.Retire(owner); }
      }
    }
    // The rest mutates bridge state: the geometry and material preloads keep
    // the published group assets (which the full frame's static world draws)
    // current, and the adapter publication is what every frame acquires.
    //
    // The bridge mutex alone, not the submission gate: nothing here submits or
    // orders game commands, and the gate is held by the render thread's swap
    // for its whole GPU and pacing wait (releasing only the mutex between
    // polls), so taking it made every step wait out the render thread's frame
    // pacing. Lock order is unchanged (gate, then mutex; never the gate under
    // the mutex). The full-frame passes hold the mutex only in short slices
    // (NativeLockSlices) and read this step's results as the immutable
    // generations Publish swaps in, so a pass never sees a half-published step.
    edf::native::HookTiming wait(edf::native::HookPhase::SimLockWait);
    std::lock_guard lock(state.mutex);
    wait.Finish();
    state.scene_publication_tick+=std::max(1u,native_loop_budget.steps);
    if(EDF_NATIVE_FLAG(scene_visibility)) {
      edf::native::HookTiming membership_timing(edf::native::HookPhase::SimMembership);
      state.scene_membership.Publish();
    }
    if(EDF_NATIVE_FLAG(scene_preload)) {
      { edf::native::HookTiming preload(edf::native::HookPhase::SimPreloadGeometry);
        edf::native::LoadTraceScope trace(LoadTraceOn(),edf::native::LoadTraceKind::PreloadGeometry);
        edf::native::PreloadStaticSceneGeometryLocked(state,backing); }
      { edf::native::HookTiming preload(edf::native::HookPhase::SimPreloadMaterial);
        edf::native::LoadTraceScope trace(LoadTraceOn(),edf::native::LoadTraceKind::PreloadMaterial);
        edf::native::PreloadStaticSceneMaterialsLocked(state,backing); }
    }
    edf::native::HookTiming publish_timing(edf::native::HookPhase::SimPublish);
    if(state.scene_adapter.objects() || state.scene_adapter.geometry_groups() || state.scene_adapter.AcquirePublication())
      state.scene_adapter.Publish(state.scene_publication_tick,
        EDF_NATIVE_FLAG(scene_sources_owned)?state.scene_sources.AcquireSnapshot():nullptr,
        EDF_NATIVE_FLAG(scene_membership_owned)?state.scene_membership.AcquirePublication():nullptr,
        EDF_NATIVE_FLAG(scene_membership_owned)?edf::native::TreePublications().AcquireAll():edf::native::NativeSceneTreePublications::Images{});
  }
  // Pose generation for this tick, beside the scene publication: only vectors
  // the dirty walk rebuilt (and first-sight seeds) are read from guest memory.
  if(model_publication) {
    edf::native::HookTiming poses_timing(edf::native::HookPhase::SimPoses);
    try {
      const auto published=ModelPublications().PublishPoses(edf::native::GuestReader(base),native_loop_budget.tick,dirty_poses);
      static uint64_t publications=0;
      if(++publications<=4 || publications%1000==0)
        REXLOG_INFO("Native model poses: generation={} tick={} dirty={} published={} registered={} pose_failures={}",
          published->generation,published->tick,dirty_poses.size(),published->poses.size(),
          ModelPublications().size(),ModelPublications().pose_failures());
    } catch(const std::exception& error) {
      REXLOG_WARN("Native model pose publication failed: {}",error.what());
    }
  } else if(!model_flag && ModelPublications().size()) ModelPublications().Clear();
  if(registry_overlap) registry_join.Wait();
  else TickNativeRenderRegistry(base,manager,native_loop_budget.tick,render_only);
  timing.Finish();
  transition_trace.Finish();
  if(edge && REXCVAR_GET(edf_native_load_timings)) REXLOG_INFO("Native resource transition: end manager={:#x}",manager);
  // desired=0 returns once the presenter has left: the first play frame follows.
  if(edge) LoadTraceEvent(desired?"loading_started":"play_resumed",manager,false,!desired);
}
REX_EXTERN(__imp__sub_82142050);
REX_EXTERN(__imp__sub_82142130);
REX_EXTERN(__imp__edf_native_gamma_table_cpu_tail);
REX_EXTERN(__imp__edf_native_gamma_pwl_cpu_tail);
namespace {
void CaptureNativeDisplayGamma(uint8_t* base,uint32_t device,uint32_t address,
    edf::native::NativeDisplayGamma::Mode mode) {
  const edf::native::GuestReader reader(base);
  auto gamma=edf::native::NativeDisplayGamma::Decode({reader.Bytes(address,1536),1536},mode);
  auto& state=edf::native::State();
  std::lock_guard lock(state.mutex);
  // Device construction may upload its default ramp before publishing the
  // SDK global device pointer. Track explicit ownership, cleared by reset.
  if(state.display_gamma_device && state.display_gamma_device!=device)
    throw std::runtime_error("native gamma update changed display device without reset");
  state.display_gamma=std::move(gamma);
  state.display_gamma_device=device;
  static std::atomic<uint64_t> updates{0};
  const auto count=updates.fetch_add(1)+1;
  if(count<=4 || !(count&(count-1)))
    REXLOG_INFO("Native display gamma captured: count={}, device={:#x}, mode={}",count,device,int(mode));
}
}
REX_HOOK_RAW(sub_82142050) {
  if(EDF_NATIVE_FLAG(host)) {
    CaptureNativeDisplayGamma(base,ctx.r3.u32,ctx.r4.u32,edf::native::NativeDisplayGamma::Mode::Table256);
    __imp__edf_native_gamma_table_cpu_tail(ctx,base);
    return;
  }
  __imp__sub_82142050(ctx,base);
}
REX_HOOK_RAW(sub_82142130) {
  if(EDF_NATIVE_FLAG(host)) {
    CaptureNativeDisplayGamma(base,ctx.r3.u32,ctx.r4.u32,edf::native::NativeDisplayGamma::Mode::Piecewise128);
    __imp__edf_native_gamma_pwl_cpu_tail(ctx,base);
    return;
  }
  __imp__sub_82142130(ctx,base);
}
// Sideband observation only: the original producer still owns guest counters.
// Each event follows the native draws already submitted at this guest boundary.
REX_EXTERN(__imp__sub_8213C788);
REX_HOOK_RAW(sub_8213C788) {
  const auto device=ctx.r3.u32;
  if(EDF_NATIVE_FLAG(host)) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    const edf::native::GuestReader reader(base);
    const auto issued=reader.Word(reader.Add(device,10780));
    const auto cursor=reader.Word(reader.Add(device,40));
    const auto flags=reader.Word(reader.Add(device,13500));
    const auto next=reader.Add(ctx.r4.u32,40);
    auto& queue=state.completion_queues[device];
    if(!queue) queue=edf::native::CreateCompletionQueueLocked(state);
    edf::native::PublishNativeCompletion(reader,state,device);
    queue->Capture(edf::native::NativeSignalCommandAddress(ctx.r4.u32),40,issued,cursor|(flags&3));
    // Preserve CPU-side bookkeeping and reservation size while the remaining
    // command-buffer callers are migrated. No PM4 packet or premature guest
    // completion write from the retail producer is retained in native mode.
    reader.StoreWord(reader.Add(device,12956),flags);
    reader.StoreWord(reader.Add(device,12952),cursor);
    reader.StoreWord(reader.Add(device,10780),issued+2);
    ctx.r3.u64=next;
    if(++state.completion_submits<=8)
      REXLOG_INFO("Native fence ownership: captured={}, cursor={:#x}, device={:#x}; awaits actual submission, no GPU packet emitted",issued,cursor|(flags&3),device);
    return;
  }
  const bool probe=EDF_NATIVE_FLAG(shader_bridge) && REXCVAR_GET(edf_native_fence_probe);
  std::optional<uint32_t> issued;
  if(probe) {
    try {
      const edf::native::GuestReader reader(base);
      issued=reader.Word(reader.Add(device,10780));
    }
    catch(const std::exception& error) { REXLOG_ERROR("Native fence probe input: {}",error.what()); }
  }
  __imp__sub_8213C788(ctx,base);
  if(!probe || !issued) return;
  auto& state=edf::native::State();
  std::lock_guard lock(state.mutex);
  if(state.completion_faults.contains(device)) return;
  try {
    const edf::native::GuestReader reader(base);
    if(reader.Word(reader.Add(device,10780))!=uint32_t(*issued+2))
      throw std::runtime_error("guest fence producer did not advance by two");
    auto& queue=state.completion_queues[device];
    if(!queue) queue=edf::native::CreateCompletionQueueLocked(state);
    const auto completed=queue->Poll();
    edf::native::SubmitSceneFrameLocked(state);
    queue->Submit(*issued);
    if(++state.completion_submits<=4 || state.completion_submits%128==0)
      REXLOG_INFO("Native fence probe: device={:#x}, submitted={}, completed_valid={}, completed={}, pending={}; guest counters unchanged",
        device,*issued,completed.has_value(),completed.value_or(0),queue->pending());
  } catch(const std::exception& error) {
    state.completion_faults.insert(device);
    REXLOG_ERROR("Native fence probe disabled for device {:#x}: {}",device,error.what());
  }
}
REX_EXTERN(__imp__edf_native_swap_wait_cpu_tail);
REX_HOOK_RAW(sub_821512D8) {
  __imp__edf_native_swap_wait_cpu_tail(ctx,base);
}
namespace {
// Coarse frame boundaries avoid per-draw instrumentation. Between-swaps time
// includes guest CPU work and every wait outside this hook; it is not CPU time.
class NativeSwapFrameTrace {
 public:
  using Clock=std::chrono::steady_clock;
  explicit NativeSwapFrameTrace(uint32_t device):device_(device),
      enabled_(!REXCVAR_GET(edf_native_frame_trace).empty()) { Mark(0); }
  void Mark(size_t index) { if(enabled_) marks_[index]=Clock::now(); }
  void Finish(edf::native::NativeRenderBackend& backend) {
    if(!enabled_) return;
    Mark(4);
    std::array<uint64_t,4> waits{};
    for(size_t index=0;index<3;++index)
      waits[index]=edf::native::FrameWaitTotals()[index].load(std::memory_order_relaxed);
    waits[3]=backend.Statistics().frame_wait_ns;
    const auto extra_steps=edf::native::FrameExtraSimulationSteps().load(std::memory_order_relaxed);
    const auto thread_id=GetCurrentThreadId();
    const auto cpu_time=[](bool process) -> uint64_t {
      FILETIME created{},exited{},kernel{},user{};
      const bool valid=process ? GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)
                               : GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user);
      if(!valid) throw std::runtime_error("cannot query frame trace CPU time");
      const auto ticks=[](FILETIME value) { return (uint64_t(value.dwHighDateTime)<<32)|value.dwLowDateTime; };
      return ticks(kernel)+ticks(user); // 100 ns accounting units, not wall time.
    };
    const auto process_cpu=cpu_time(true),thread_cpu=cpu_time(false);
    auto& costs=edf::native::FrameTraceCosts();
    const std::array<uint64_t,6> totals{costs.steps.load(std::memory_order_relaxed),
      costs.dispatch_ns.load(std::memory_order_relaxed),costs.helper_ns.load(std::memory_order_relaxed),
      costs.transition_ns.load(std::memory_order_relaxed),backend.Statistics().geometry_draws,
      edf::SimulationTicks().load(std::memory_order_relaxed)};
    struct Writer {
      std::mutex mutex;
      std::ofstream file;
      struct Previous { Clock::time_point entry{},exit{}; std::array<uint64_t,4> waits{}; uint64_t extra_steps=0,process_cpu=0,thread_cpu=0; DWORD thread_id=0; std::array<uint64_t,6> totals{}; };
      std::map<uint32_t,Previous> previous;
      uint64_t samples=0;
    };
    static Writer writer;
    std::lock_guard lock(writer.mutex);
    if(!writer.file.is_open()) {
      const auto path=REXCVAR_GET(edf_native_frame_trace);
      if(std::filesystem::exists(path)) throw std::runtime_error("frame trace output already exists");
      writer.file.open(path);
      if(!writer.file) throw std::runtime_error("cannot create frame trace output");
      writer.file << "epoch_ms,device,interval_ms,between_swaps_ms,submit_ms,gpu_wait_ms,pacing_ms,engine_wait_ms,guest_fence_sleep_ms,shared_slot_wait_ms,backend_frame_wait_ms,engine_extra_steps,process_cpu_ms,swap_thread_cpu_ms,swap_thread_id,"
        "steps,step_dispatch_ms,render_helper_ms,frame_transition_ms,geometry_draws,game_tick\n";
    }
    auto& previous=writer.previous[device_];
    const auto ms=[](auto duration){return std::chrono::duration<double,std::milli>(duration).count();};
    const auto epoch=std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
    writer.file << epoch << ',' << device_ << ','
      << (previous.entry==Clock::time_point{}?0:ms(marks_[0]-previous.entry)) << ','
      << (previous.exit==Clock::time_point{}?0:ms(marks_[0]-previous.exit)) << ','
      << ms(marks_[1]-marks_[0]) << ',' << ms(marks_[3]-marks_[1]) << ','
      << ms(marks_[4]-marks_[3]);
    for(size_t index=0;index<waits.size();++index)
      writer.file << ',' << (previous.entry==Clock::time_point{} || waits[index]<previous.waits[index]
        ? 0 : double(waits[index]-previous.waits[index])/1000000.0);
    writer.file << ',' << (previous.entry==Clock::time_point{} ? 0 : extra_steps-previous.extra_steps)
      << ',' << (previous.entry==Clock::time_point{} ? 0 : double(process_cpu-previous.process_cpu)/10000.0)
      << ',' << (previous.thread_id!=thread_id ? 0 : double(thread_cpu-previous.thread_cpu)/10000.0)
      << ',' << thread_id;
    // Steps, the three phases' wall ms and the draws since the previous swap; the game clock now.
    const bool first=previous.entry==Clock::time_point{};
    writer.file << ',' << (first?0:totals[0]-previous.totals[0]);
    for(size_t index=1;index<4;++index)
      writer.file << ',' << (first?0:double(totals[index]-previous.totals[index])/1000000.0);
    writer.file << ',' << (first || totals[4]<previous.totals[4]?0:totals[4]-previous.totals[4])
      << ',' << totals[5] << '\n';
    previous={marks_[0],marks_[4],waits,extra_steps,process_cpu,thread_cpu,thread_id,totals};
    if(++writer.samples%60==0) writer.file.flush();
  }
 private:
  uint32_t device_;
  bool enabled_;
  std::array<Clock::time_point,5> marks_{};
};
// edf_native_frame_times: one sample per guest swap, entry to entry (the
// present-to-present time the player sees), with counter deltas over the same
// interval. Called with the bridge locks held, after the scene frame's submit,
// so the frame's own creations and splits are counted in it.
void RecordNativeFrameTimeLocked(std::chrono::steady_clock::time_point entry) {
  static std::optional<std::chrono::steady_clock::time_point> previous;
  static edf::native::NativeFrameTimeRecorder recorder;
  static edf::native::NativeFrameCounterDeltas deltas;
  static uint64_t window_spikes=0,suppressed=0;
  auto& state=edf::native::State();
  const auto stats=state.scene_backend?state.scene_backend->Statistics():edf::native::NativeBackendStatistics{};
  auto& events=edf::native::FrameEventCounters();
  const edf::native::NativeFrameCounter counters[]{
    {"pipelines",stats.pipeline_misses},
    {"pipeline_waits",stats.pipeline_waits},
    {"pipeline_content_hits",stats.pipeline_content_hits},
    {"shader_compiles",events.shader_compiles.load(std::memory_order_relaxed)},
    {"shader_cache_hits",events.shader_cache_hits.load(std::memory_order_relaxed)},
    {"mesh_builds",state.meshes.builds()},
    {"buffers",stats.buffers_created},
    {"buffers_committed",stats.buffers_committed},
    {"buffer_kb",stats.buffer_bytes_created/1024},
    {"textures",stats.textures_created},
    {"texture_kb",stats.texture_bytes_created/1024},
    {"declines",events.pass_declines.load(std::memory_order_relaxed)},
    {"post_fallbacks",events.post_fallbacks.load(std::memory_order_relaxed)},
    {"upload_stalls",stats.upload_stalls},
    {"descriptor_stalls",stats.descriptor_stalls},
    {"sampler_misses",stats.sampler_misses},
    {"backend_frame_waits",stats.frame_waits},
    {"frame_splits",state.scene_frame_splits}};
  deltas.Update(counters);
  // The frame's three largest hook phases, taken whether or not it spikes so
  // each frame starts from zero. Umbrella phases that contain the others are
  // left out: they would always win.
  std::array<std::pair<uint64_t,const char*>,3> top{};
  auto& phases=edf::native::FrameHookPhaseTotals();
  for(size_t index=0;index<phases.nanos.size();++index) {
    const auto nanos=phases.nanos[index].exchange(0,std::memory_order_relaxed);
    const auto phase=edf::native::HookPhase(index);
    if(!nanos || phase==edf::native::HookPhase::FrameNative || phase==edf::native::HookPhase::RenderHelper ||
       phase==edf::native::HookPhase::ResourceHelper) continue;
    if(nanos<=top.back().first) continue;
    top.back()={nanos,phases.names[index].load(std::memory_order_relaxed)};
    std::sort(top.begin(),top.end(),[](const auto& a,const auto& b) { return a.first>b.first; });
  }
  if(!previous) { previous=entry; return; }
  const double ms=std::chrono::duration<double,std::milli>(entry-*previous).count();
  previous=entry;
  const auto sample=recorder.Record(ms);
  if(sample.spike()) {
    // A loading screen can spike every frame; the window line still counts them.
    if(++window_spikes>64) ++suppressed;
    else {
      std::string largest;
      const bool timed=REXCVAR_GET(edf_native_hook_timings) || REXCVAR_GET(edf_native_load_timings);
      for(const auto& [nanos,name]:top) {
        if(!nanos || !name) continue;
        largest+=std::format("{}{}:{:.3f}",largest.empty()?"":",",name,double(nanos)/1e6);
      }
      if(largest.empty()) largest=timed?"none":"off";
      REXLOG_INFO("Native frame spike: frame={} ms={:.3f} median_ms={:.3f} reason={} {} top_phases={}",
        sample.frame,sample.ms,sample.median_ms,sample.reason(),deltas.Describe(),largest);
    }
  }
  edf::native::NativeFrameTimeWindow window;
  if(recorder.TakeReport(window)) {
    REXLOG_INFO("Native frame times: frames={} span_ms={:.1f} p50_ms={:.3f} p90_ms={:.3f} p99_ms={:.3f} p99_9_ms={:.3f} max_ms={:.3f} mean_ms={:.3f} spikes={} suppressed_spikes={} hist={}",
      window.frames,window.span_ms,window.p50_ms,window.p90_ms,window.p99_ms,window.p999_ms,window.max_ms,
      window.mean_ms,window.spikes,suppressed,edf::native::FormatNativeFrameTimeHistogram(window.histogram));
    window_spikes=0; suppressed=0;
  }
}
// edf_native_first_use_log: every record since the last swap, stamped with
// this swap's number (native_first_use.h). A record made on a thread no frame
// waits for says so, since it cannot have been a hitch.
void LogNativeFirstUse() {
  static uint64_t swaps=0;
  ++swaps;
  auto& log=edf::native::NativeFirstUseLog::Get();
  if(!log.enabled()) return;
  uint64_t dropped=0;
  const auto events=log.Take(&dropped);
  for(const auto& event:events)
    REXLOG_INFO("Native first use: swap={} kind={} ms={:.3f} thread={} key={} {}",swaps,
      edf::native::kNativeFirstUseNames[size_t(event.kind)],event.ms,event.background?"background":"caller",
      event.key,event.detail);
  if(dropped) REXLOG_INFO("Native first use: swap={} dropped={} (queue full)",swaps,dropped);
}
}
REX_EXTERN(edf_native_swap_wait) {
  using namespace edf::native;
  LogNativeFirstUse();
  const bool frame_times=REXCVAR_GET(edf_native_frame_times);
  const auto swap_entry=frame_times?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
  NativeSwapFrameTrace frame_trace(ctx.r3.u32);
  const GuestReader reader(base);
  const auto device=ctx.r3.u32;
  const auto mode=reader.Word(reader.Add(device,13220));
  if(mode!=0 && mode!=1 && mode!=2 && mode!=4)
    throw std::runtime_error("unsupported native swap interval");
  const uint32_t interval=mode==4?3:mode==2?2:1;
  const uint32_t phase_limit=(reader.Word(reader.Add(device,11580))>>23)&127;
  auto& state=State();
  // Keep later game commands behind this barrier, but let host presentation
  // and CPU worker bookkeeping take the context lock between short polls.
  std::lock_guard submission(state.submissions);
  std::unique_lock lock(state.mutex);
  if(!state.initialized)
    throw std::runtime_error("native swap requires an initialized native device");
  if(reader.Word(reader.Add(device,15120)))
    throw std::runtime_error("native swap has an unaudited vblank callback");
  // Submit before placing the frame marker. The marker bounds CPU lead;
  // resource fences separately retain their actual GPU completion semantics.
  SubmitSceneFrameLocked(state);
  frame_trace.Mark(1);
  if(frame_times) RecordNativeFrameTimeLocked(swap_entry);
  auto [it,inserted]=state.swap_clocks.try_emplace(device);
  auto& timing=it->second;
  if(inserted) timing.clock.Reset(NativePacingClock::Clock::now(),0);
  HookTiming gpu_timing(HookPhase::SwapGpuWait);
  // Bound CPU lead with presentation credits. Real guest resource fences and
  // worker callbacks retain their independent GPU completion queues.
  // edf_low_latency: one frame of GPU work in flight while the GPU is the limit, two while it
  // keeps up (NativeFrameCreditPolicy). Live; edf_native_frame_latency alone needs a restart.
  const bool low_latency=edf::latency::LowLatency();
  static NativeFrameCreditPolicy credit_policy;  // swaps are serialized by state.submissions
  const auto latency=low_latency?uint32_t(credit_policy.Limit())
                                :uint32_t(std::clamp(REXCVAR_GET(edf_native_frame_latency),1,3));
  std::unique_ptr<NativeCompletionQueue> barrier;
  if(state.scene_backend->name()=="d3d12") {
    if(!timing.flight) timing.flight=std::make_unique<NativeFrameFlight>(latency);
    timing.flight->Submit(state.scene_backend->MarkCompletion());
    timing.flight->SetLimit(latency);  // after the Submit: a lower limit only waits longer
  } else {
    barrier=CreateCompletionQueueLocked(state,1); barrier->Submit(2);
  }
  const auto gpu_entry=NativePacingClock::Clock::now();
  const auto gpu_deadline=gpu_entry+std::chrono::seconds(10);
  bool gpu_waited=false;
  while(timing.flight?!timing.flight->Ready():barrier->Poll(true)!=2) {
    gpu_waited=true;
    if(NativePacingClock::Clock::now()>=gpu_deadline)
      throw std::runtime_error("native swap GPU completion timed out");
    lock.unlock();
    // Latency 1 waits for this very frame, so a millisecond sleep would be added to each
    // frame: poll by yielding for the first few milliseconds instead.
    if(low_latency && NativePacingClock::Clock::now()-gpu_entry<std::chrono::milliseconds(4))
      std::this_thread::yield();
    else std::this_thread::sleep_for(std::chrono::milliseconds(1));
    lock.lock();
  }
  if(low_latency && timing.flight) {
    static NativePacingClock::Clock::time_point previous_swap{};
    const auto loop=previous_swap==NativePacingClock::Clock::time_point{}?0:
      std::chrono::duration_cast<std::chrono::nanoseconds>(gpu_entry-previous_swap).count();
    previous_swap=gpu_entry;
    credit_policy.Observe(gpu_waited,loop,edf::latency::DisplayRefreshNs());
  }
  gpu_timing.Finish();
  frame_trace.Mark(3);
  HookTiming refresh_timing(HookPhase::SwapRefreshWait);
  const bool audit_pacing=REXCVAR_GET(edf_native_hook_timings);
  double pacing_sleep_ms=0, pacing_lock_ms=0;
  uint32_t pacing_polls=0;
  NativeSwapPacingState pacing{
    reader.Word(reader.Add(device,15124)),reader.Word(reader.Add(device,15128)),
    reader.Word(reader.Add(device,15132)),reader.Word(reader.Add(device,15136))};
  auto now=NativePacingClock::Clock::now();
  auto ticks=timing.clock.Sample(now);
  pacing.Advance(ticks-timing.sampled);
  timing.sampled=ticks;
  const auto entry_phase=timing.clock.PhasePercent(now);
  const auto entry_ticks=pacing.ticks, entry_ack=pacing.acknowledged;
  // Movie decoding/presentation follows the title's requested swap interval.
  // The gameplay unlock must not speed up that separate playback loop. Menus
  // and loading after the movie must not stay paced, so release once draws stop.
  const bool movie_frame=state.movie_pacing.Swap(state.movie_draws,
    state.movie_pacing_active.load(std::memory_order_relaxed));
  if(!movie_frame) state.movie_pacing_active.store(false,std::memory_order_relaxed);
  bool released=REXCVAR_GET(edf_native_unlock_framerate) && !movie_frame
    ? pacing.CompleteUnpaced(interval):pacing.CompleteNative(interval);
  // Publish the callback increment once, before sleeping. A statistics reader
  // may reset this counter during the refresh wait; do not overwrite its reset.
  reader.StoreWord(reader.Add(device,15136),pacing.callbacks);
  const auto pacing_deadline=now+std::chrono::seconds(1);
  while(!released) {
    lock.unlock();
    const auto sleep_start=audit_pacing?NativePacingClock::Clock::now():NativePacingClock::Clock::time_point{};
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto sleep_end=audit_pacing?NativePacingClock::Clock::now():NativePacingClock::Clock::time_point{};
    lock.lock();
    now=NativePacingClock::Clock::now();
    if(audit_pacing) {
      pacing_sleep_ms+=std::chrono::duration<double,std::milli>(sleep_end-sleep_start).count();
      pacing_lock_ms+=std::chrono::duration<double,std::milli>(now-sleep_end).count();
      ++pacing_polls;
    }
    ticks=timing.clock.Sample(now);
    released=pacing.Advance(ticks-timing.sampled);
    timing.sampled=ticks;
    if(!released && now>=pacing_deadline)
      throw std::runtime_error("native swap pacing state failed to reach its deadline");
  }
  reader.StoreWord(reader.Add(device,15124),pacing.ticks);
  reader.StoreWord(reader.Add(device,15128),pacing.acknowledged);
  reader.StoreWord(reader.Add(device,15132),pacing.pending);
  // No GPU writeback address, callback packet or register acknowledgement.
  static std::atomic<uint64_t> calls{};
  const auto count=calls.fetch_add(1,std::memory_order_relaxed)+1;
  if(audit_pacing && count%256==0)
    REXLOG_INFO("Native swap pacing detail: count={}, mode={}, interval={}, phase_limit={}, entry_phase={}, entry_ticks={}, entry_ack={}, exit_ticks={}, polls={}, sleep_ms={}, lock_ms={}",
      count,mode,interval,phase_limit,entry_phase,entry_ticks,entry_ack,pacing.ticks,pacing_polls,pacing_sleep_ms,pacing_lock_ms);
  if(count<=4 || !(count&(count-1)))
    REXLOG_INFO("Native swap barrier: count={}, interval={}, ticks={}, acknowledged={}",
      count,interval,pacing.ticks,pacing.acknowledged);
  frame_trace.Finish(*state.scene_backend);
}
// Replace the two present profiling packets with native timestamp markers.
REX_EXTERN(__imp__sub_821390B8);
REX_HOOK_RAW(sub_821390B8) {
  if(!EDF_NATIVE_FLAG(host)) { __imp__sub_821390B8(ctx,base); return; }
  const bool finish=ctx.lr==0x8215162C;
  if(!finish && ctx.lr!=0x82151544) throw std::runtime_error("unknown native profiling marker caller");
  const edf::native::GuestReader reader(base);
  const auto device=ctx.r3.u32,sequence=reader.Word(reader.Add(device,20052));
  const auto writeback=reader.Word(reader.Add(device,10768));
  if(ctx.r4.u32!=reader.Add(writeback,64+4*((sequence+uint32_t(finish))&7)))
    throw std::runtime_error("native profiling marker slot mismatch");
  auto& state=edf::native::State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  auto& profiler=state.present_profilers[device];
  if(!profiler) {
    auto& backend=edf::native::EnsureSceneBackendLocked(state);
    if(backend.name()=="d3d12") profiler=std::make_unique<edf::native::NativePresentProfiler>(backend,
      [&state]() -> edf::native::NativeBackendRecorder& {return edf::native::SceneRecorderLocked(state);});
    else profiler=std::make_unique<edf::native::NativePresentProfiler>(*state.device.Get(),*state.context.Get());
  }
  if(finish) profiler->Finish(sequence); else profiler->Start(sequence);
}
REX_EXTERN(__imp__sub_82138158);
REX_HOOK_RAW(sub_82138158) {
  if(!EDF_NATIVE_FLAG(host) || ctx.r4.u32!=6) { __imp__sub_82138158(ctx,base); return; }
  // Retail's 0x7fffffff float is its unavailable-sample sentinel, not zero.
  ctx.f1.f64=edf::native::NativeProfileResult(std::nullopt,0,0);
  const edf::native::GuestReader reader(base);
  const auto device=ctx.r3.u32;
  auto& state=edf::native::State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  const auto found=state.present_profilers.find(device);
  if(found==state.present_profilers.end()) return;
  const auto sample=found->second->Poll();
  if(!sample) return;
  const auto consumer=reader.Word(reader.Add(device,20048));
  if(sample->tag!=consumer) throw std::runtime_error("native profiling consumer sequence mismatch");
  reader.StoreWord(reader.Add(device,20048),consumer+2);
  const auto ratio=sample->FractionAfterMiddle();
  if(!ratio) return; // First interval, disjoint data or zero-duration interval.
  const auto scale=std::bit_cast<float>(reader.Word(0x82003e3c));
  const auto minimum=std::bit_cast<float>(reader.Word(0x820009a4));
  ctx.f1.f64=edf::native::NativeProfileResult(ratio,scale,minimum);
}
REX_EXTERN(__imp__sub_82138E00);
REX_HOOK_RAW(sub_82138E00) {
  uint32_t reset_device=0;
  if(EDF_NATIVE_FLAG(host) && (ctx.r3.u32==0 ||
      ((ctx.r3.u32==16 || ctx.r3.u32==17) && ctx.r4.u32==6))) {
    const edf::native::GuestReader reader(base);
    const auto device=reader.Word(reader.Word(0x82000720));
    if(device && reader.Word(reader.Add(device,52)) && reader.DoubleWord(reader.Add(device,10752)))
      reset_device=device;
  }
  __imp__sub_82138E00(ctx,base);
  if(reset_device) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    state.present_profilers.erase(reset_device);
  }
}
// Native engine timing replaces the engine's vblank registration and spin loop,
// not the retail GPU interrupt handler (which reads Xenos MMIO).
REX_EXTERN(__imp__sub_821BEBF0);
REX_HOOK_RAW(sub_821BEBF0) {
  if(!EDF_NATIVE_FLAG(host)) { __imp__sub_821BEBF0(ctx,base); return; }
  const edf::native::GuestReader reader(base);
  const auto object=ctx.r3.u32, divisor=ctx.r4.u32;
  (void)edf::native::NativePacingSteps(0,0,divisor);
  auto& state=edf::native::PacingState();
  std::lock_guard lock(state.mutex);
  reader.StoreWord(object,0x82019918);
  reader.StoreWord(reader.Add(object,4),divisor);
  reader.StoreDoubleWord(reader.Add(object,16),1);
  state.clock.Reset(edf::native::NativePacingClock::Clock::now(),reader.DoubleWord(0x8257C300));
  state.calls=0;
  REXLOG_INFO("Native engine pacing: 60 Hz clock, divisor={}, no vblank callback registered",divisor);
}
REX_EXTERN(__imp__sub_821BEAB0);
REX_EXTERN(sub_821FAC28);
namespace {
// F1 pause menu (pause_menu.h). The engine thread stops here, at the top of a main-loop
// iteration, while the menu asks for a pause: no simulation step is dispatched and no
// guest frame is rendered, and the host keeps presenting the last scene image with the
// menu over it (its UI ticker paints at 60 Hz without guest frames). This is where the
// retail loop already blocks for the next tick, and the previous frame's render helper
// has been joined (821A4DE8 runs just before this call). The engine pacing clock is the
// only clock that has to be told about the gap: on resume it restarts at the tick and
// sub-tick fraction it had when the hold began, so the game clock (0x8257C300) does not
// advance across the pause, no catch-up steps are dispatched, and the camera and pose
// interpolation histories see consecutive ticks (no cut, nothing to reset). Guest
// threads other than the engine (streaming, audio) keep running and wait on it as they
// would across one long frame.
void HoldEngineForMenu() {
  auto& pacing=edf::native::PacingState();
  uint64_t tick=0;
  float fraction=0;
  {
    std::lock_guard lock(pacing.mutex);
    const auto now=edf::native::NativePacingClock::Clock::now();
    tick=pacing.clock.Sample(now);
    fraction=pacing.clock.Fraction(now);
  }
  const auto began=std::chrono::steady_clock::now();
  if(!edf::menu::HoldWhilePaused()) return;
  std::lock_guard lock(pacing.mutex);
  const auto now=edf::native::NativePacingClock::Clock::now();
  pacing.clock.Reset(now-std::chrono::nanoseconds(int64_t(double(fraction)*1e9/pacing.clock.rate())),tick);
  REXLOG_INFO("Native engine pacing: paused by the settings menu for {} ms; resumed at tick {} (clock rebased, no catch-up)",
    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-began).count(),tick);
}
}
namespace {
// --edf_deterministic_steps: the virtual tick the heartbeat last granted (engine thread).
uint64_t& DeterministicTick() { static uint64_t tick=0; return tick; }
}
REX_HOOK_RAW(sub_821BEAB0) {
  if(!EDF_NATIVE_FLAG(host)) { __imp__sub_821BEAB0(ctx,base); return; }
  // The main loop's heartbeat only (the call site the frame-rate unlock also keys on),
  // and not during movie playback, whose own clocks are not paused.
  if(ctx.lr==0x821A6894 && edf::menu::Engine().requested.load(std::memory_order_acquire) &&
     !edf::native::State().movie_pacing_active.load(std::memory_order_relaxed))
    HoldEngineForMenu();
  NativeLoopTrace loop_trace("heartbeat",ctx.r3.u32,ctx.lr);
  edf::native::HookTiming engine_timing(edf::native::HookPhase::EngineWait);
  const edf::native::GuestReader reader(base);
  const auto object=ctx.r3.u32;
  const bool unlocked=!edf::gate::DeterministicSteps() && REXCVAR_GET(edf_native_unlock_framerate) && ctx.lr==0x821A6894 &&
    !edf::native::State().movie_pacing_active.load(std::memory_order_relaxed);
  native_loop_budget={};
  const auto divisor=reader.Word(reader.Add(object,4));
  auto& state=edf::native::PacingState();
  std::lock_guard lock(state.mutex);
  // The console's "timescale" (native_pacing.h NativeTimeScaleRequest): 60 x scale ticks/s.
  if(const double rate=60.0*edf::native::NativeTimeScaleRequest().load(std::memory_order_relaxed); rate!=state.clock.rate())
    state.clock.SetRate(edf::native::NativePacingClock::Clock::now(),rate);
  const auto previous=reader.DoubleWord(0x8257C308);
  auto sampled_at=edf::native::NativePacingClock::Clock::now();
  auto current=state.clock.Sample(sampled_at);
  // Exactness gate (src/guest_state_hash.h): one virtual tick per heartbeat, no wall clock.
  const bool deterministic=edf::gate::DeterministicSteps();
  if(deterministic) { current=previous+divisor; DeterministicTick()=current; }
  auto steps=edf::native::NativePacingSteps(current,previous,divisor);
  edf::native::NativeFrameWaitTrace waiting(edf::native::FrameWaitKind::Engine,
    !unlocked && edf::native::NativePacingPending(steps));
  const auto wait_began=sampled_at;
  while(!deterministic && !unlocked && edf::native::NativePacingPending(steps)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    sampled_at=edf::native::NativePacingClock::Clock::now();
    current=state.clock.Sample(sampled_at);
    steps=edf::native::NativePacingSteps(current,previous,divisor);
  }
  waiting.Finish();
  // Performance overlay CPU time (frame_stats.h): the time spent waiting for the tick.
  if(sampled_at!=wait_began)
    edf::CurrentFrameStats().engine_wait_ns.fetch_add(uint64_t(
      std::chrono::duration_cast<std::chrono::nanoseconds>(sampled_at-wait_began).count()),std::memory_order_relaxed);
  reader.StoreDoubleWord(0x8257C300,current);
  // A render-only iteration must not discard fractional divisor progress.
  if(!unlocked || steps) reader.StoreDoubleWord(0x8257C308,current);
  ctx.r3.u64=reader.Add(object,8);
  sub_821FAC28(ctx,base);
  const auto simulation_steps=edf::native::NativePacingResult(steps);
  native_loop_budget={unlocked,simulation_steps,current,state.clock.Fraction(sampled_at),divisor};
  edf::latency::OnHeartbeat(current);
  // The retail outer loop treats zero as "skip all normal work". Its render
  // token stays nonzero; the actual step dispatcher receives the real budget.
  ctx.r3.u64=unlocked?1:simulation_steps;
  // Game clock for scripted input ("clock game" scripts).
  edf::SimulationTicks().fetch_add(simulation_steps,std::memory_order_relaxed);
  if(simulation_steps>1 && !REXCVAR_GET(edf_native_frame_trace).empty())
    edf::native::FrameExtraSimulationSteps().fetch_add(simulation_steps-1,std::memory_order_relaxed);
  if(++state.calls<=3)
    REXLOG_INFO("Native engine pacing: tick={}, steps={}, result={}",current,steps,ctx.r3.u32);
}
REX_EXTERN(__imp__sub_821BEB38);
REX_HOOK_RAW(sub_821BEB38) {
  if(!EDF_NATIVE_FLAG(host)) { __imp__sub_821BEB38(ctx,base); return; }
  auto& state=edf::native::PacingState();
  std::lock_guard lock(state.mutex);
  const edf::native::GuestReader reader(base);
  reader.StoreDoubleWord(0x8257C300,edf::gate::DeterministicSteps() ? DeterministicTick()
    : state.clock.Sample(edf::native::NativePacingClock::Clock::now()));
  __imp__sub_821BEB38(ctx,base);
}
REX_EXTERN(sub_8213C788);
// Engine scene begin: original sets up tile replay, then a full-frame viewport.
// Native graphics uses one full-resolution HDR surface, not the eDRAM tile
// dimensions. Sample count comes from the actual guest color surface; host
// multisample storage/resolves do not reproduce the Xbox eDRAM tile layout.
// Replace the audited full-scene viewport's CPU state in untiled mode. Other
// callers retain their original setter, with bounded clamp diagnostics.
REX_EXTERN(__imp__sub_821371D0);
REX_HOOK_RAW(sub_821371D0) {
  edf::native::HookTiming hook_timing(edf::native::HookPhase::ViewportHook);
  const auto device=ctx.r3.u32;
  const auto caller=uint32_t(ctx.lr);
  const bool audit=EDF_NATIVE_FLAG(host);
  std::array<uint32_t,6> requested{};
  if(audit) requested=edf::native::ReadGuestWords<6>(edf::native::GuestReader(base),ctx.r4.u32);
  if(audit) {
    const edf::native::GuestReader reader(base);
    auto& state=edf::native::State();
    edf::native::HookTiming lock_timing(edf::native::HookPhase::ViewportLock);
    std::lock_guard lock(state.mutex);
    lock_timing.Finish();
    // Initial caller retains its owner in r31. Later setters must target the
    // active native scene's color surface, not a smaller post-process target.
    const bool initial=caller==0x8219C828;
    const auto owner=initial?ctx.r31.u32:state.active_scene;
    const bool native_scene=initial || (owner && state.untiled_devices.contains(device) &&
      reader.Word(reader.Add(owner,8))==device &&
      reader.Word(reader.Add(device,12168))==state.scenes.at(owner).color_surface);
    if(native_scene) {
    edf::native::HookTiming read_timing(edf::native::HookPhase::ViewportRead);
    if(!state.untiled_devices.contains(device) || reader.Word(reader.Add(owner,8))!=device)
      throw std::runtime_error("native scene viewport without matching scene begin");
    const auto raw=edf::native::ReadViewportWords(reader,device);
    const auto replacement=edf::native::MakeNativeSceneViewportCpuState(requested,
      reader.Word(reader.Add(owner,84)),reader.Word(reader.Add(owner,88)),
      {raw.words[6],raw.words[7],raw.words[8],raw.words[9]},raw.scissor_enabled,
      edf::native::ReadGuestWords<2>(reader,reader.Add(device,10308)),initial);
    read_timing.Finish();
    edf::native::HookTiming write_timing(edf::native::HookPhase::ViewportWrite);
    std::array<uint32_t,6> transform_words{};
    for(uint32_t i=0;i<6;++i) transform_words[i]=std::bit_cast<uint32_t>(replacement.transform[i]);
    const auto dirty=reader.DoubleWord(reader.Add(device,24))|0xfcu;
    reader.StoreCpuWords(reader.Add(device,12376),replacement.viewport);
    reader.StoreCpuWords(reader.Add(device,10376),transform_words);
    reader.StoreCpuWords(reader.Add(device,10308),replacement.packed_scissor);
    reader.StoreCpuWords(reader.Add(device,24),std::array<uint32_t,2>{uint32_t(dirty>>32),uint32_t(dirty)});
    write_timing.Finish();
    static thread_local std::set<uint32_t> native_callers;
    if(native_callers.size()<16 && native_callers.insert(caller).second)
      REXLOG_INFO("Native untiled scene viewport: caller={:#x}, requested={}x{}, stored={}x{}, CPU transform/scissor updated without tile packets",
        caller,requested[2],requested[3],replacement.viewport[2],replacement.viewport[3]);
    return;
    }
  }
  {
    edf::native::HookTiming original_timing(edf::native::HookPhase::ViewportGuest);
    __imp__sub_821371D0(ctx,base);
  }
  if(audit) {
    const edf::native::GuestReader reader(base);
    const auto actual=edf::native::ReadViewportWords(reader,device);
    static thread_local uint32_t reports=0;
    if(reports<12 && (requested[2]!=actual.words[2] || requested[3]!=actual.words[3])) {
      ++reports;
      const auto surface=reader.Word(reader.Add(device,12168));
      REXLOG_INFO("Untiled viewport clamp: caller={:#x}, requested={}x{} at {},{}, stored={}x{} at {},{}, surface={:#x}, descriptor={:#x}, scissor={},{},{},{} enabled={}",
        caller,requested[2],requested[3],requested[0],requested[1],actual.words[2],actual.words[3],actual.words[0],actual.words[1],
        surface,surface?reader.Word(reader.Add(surface,36)):0,
        actual.words[6],actual.words[7],actual.words[8],actual.words[9],actual.scissor_enabled);
    }
  }
}
REX_HOOK_RAW(sub_821409A0) {
  edf::native::HookTiming timing(edf::native::HookPhase::TilingBegin);
  {
    if(uint32_t(ctx.lr)!=0x8219C654)
      throw std::runtime_error("untiled scene encountered an unaudited begin caller");
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    if(!state.untiled_devices.insert(ctx.r3.u32).second)
      throw std::runtime_error("untiled scene began before prior scene ended");
    // The engine caller already bound its color/depth surfaces and computed
    // the authored clear color. C7A8's native hook performs the actual clear.
    // No Xbox recording descriptors, tile replay state or tile packets.
    ctx.r3.u64=0;
    return;
  }
}
REX_HOOK_RAW(sub_82140E98) {
  edf::native::HookTiming timing(edf::native::HookPhase::TilingEnd);
  {
    if(uint32_t(ctx.lr)!=0x8219C990 && uint32_t(ctx.lr)!=0x8219C6B8)
      throw std::runtime_error("untiled scene encountered an unaudited end caller");
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    if(!state.untiled_devices.erase(ctx.r3.u32))
      throw std::runtime_error("untiled scene ended without a matching begin");
    // The enclosing native C930/C840 hooks own HDR resolve/publication. The
    // enclosing engine function restores ordinary target bindings afterward.
    ctx.r3.u64=0;
    return;
  }
}
REX_EXTERN(__imp__sub_8219C7A8);
REX_HOOK_RAW(sub_8219C7A8) {
  const bool presenter_trace=native_load_trace_presenter_thread && LoadTraceOn();
  if(presenter_trace) {
    LoadTraceState().presenter_frames.fetch_add(1,std::memory_order_relaxed);
    LoadTraceTick();
  }
  edf::native::LoadTraceScope presenter_scope(presenter_trace,edf::native::LoadTraceKind::PresenterFrame);
  edf::native::HookTiming setup_timing(edf::native::HookPhase::SceneSetup);
  const auto owner=ctx.r3.u32;
  {
    edf::native::HookTiming original_timing(edf::native::HookPhase::SceneSetupGuest);
    __imp__sub_8219C7A8(ctx,base);
  }
  edf::native::HookTiming native_timing(edf::native::HookPhase::SceneSetupNative);
  if (EDF_NATIVE_FLAG(shader_bridge) && ctx.r3.u32) {
    auto& state=edf::native::State();
    edf::native::HookTiming lock_timing(edf::native::HookPhase::SceneSetupLock);
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    lock_timing.Finish();
    try {
      const edf::native::GuestReader reader(base);
      const auto width=reader.Word(reader.Add(owner,84)),height=reader.Word(reader.Add(owner,88));
      const auto color_surface=reader.Word(reader.Add(reader.Word(reader.Add(owner,8)),12168));
      const auto& creation=state.surface_creations.at(color_surface);
      // Pin the choice for this renderer lifetime, including later scene recreation.
      // FSR on at startup (NativeFsrSceneAtStartup) and the motion vectors
      // both take the scene to 1x.
      static const bool fsr_scene=edf::native::NativeFsrSceneAtStartup();
      static const int32_t sample_override=fsr_scene || REXCVAR_GET(edf_native_motion_vectors)?1:REXCVAR_GET(edf_native_msaa);
      const uint32_t samples=edf::native::NativeSceneSamples(creation.msaa,sample_override);
      auto found=state.scenes.find(owner);
      if (found==state.scenes.end() || found->second.color.sampled.width!=width || found->second.color.sampled.height!=height || found->second.samples!=samples) {
        // Reversed-Z: the scene clears depth to 0, so that is its declared
        // optimized clear. The SRV is opt-in and single-sampled only.
        // Motion vectors and FSR read the depth through its SRV, which is
        // single-sampled only: with either on, the scene is 1x and its depth sampled.
        static const bool motion_vectors=REXCVAR_GET(edf_native_motion_vectors);
        static const bool depth_srv=REXCVAR_GET(edf_native_scene_depth_srv) || motion_vectors || fsr_scene;
        // FidelityFX availability, once, on the scene device (FSR runs there).
        static const bool ffx_logged=[&] {
          auto* raw=edf::native::EnsureSceneBackendLocked(state).D3D12Raw();
          REXLOG_INFO("{} (scene backend {}{}; edf_native_fsr={})",edf::native::NativeFsrLibrary().Describe(raw?raw->Device():nullptr),
            std::string(edf::native::EnsureSceneBackendLocked(state).name()),raw?"":", no D3D12 raw access: FSR unavailable",
            std::string(REXCVAR_GET(edf_native_fsr)));
          return true;
        }();
        (void)ffx_logged;
        edf::native::NativeScene scene{
          edf::native::CreateNativeRenderTarget(edf::native::EnsureSceneBackendLocked(state),width,height,DXGI_FORMAT_R16G16B16A16_FLOAT,samples),
          edf::native::CreateNativeDepthTarget(edf::native::EnsureSceneBackendLocked(state),width,height,DXGI_FORMAT_D32_FLOAT_S8X24_UINT,samples,
            0.0f,depth_srv)};
        scene.samples=samples;
        found=state.scenes.insert_or_assign(owner,std::move(scene)).first;
        REXLOG_INFO("Native full scene allocated: owner={:#x}, {}x{}, samples={}, guest_surface={:#x}",owner,width,height,samples,color_surface);
      }
      state.active_scene=owner;
      if(REXCVAR_GET(edf_native_hook_timings)) {
        // Sparse diagnostic samples; never wait for query readiness or invent
        // a GPU duration from CPU submission time. Includes GPU idle gaps.
        try {
          if(!state.scene_gpu_timer) {
            auto& backend=edf::native::EnsureSceneBackendLocked(state);
            if(backend.name()=="d3d12") state.scene_gpu_timer=std::make_unique<edf::native::NativeGpuTimer>(backend,
              [&state]() -> edf::native::NativeBackendRecorder& {return edf::native::SceneRecorderLocked(state);});
            else state.scene_gpu_timer=std::make_unique<edf::native::NativeGpuTimer>(*state.device.Get(),*state.context.Get());
          }
          auto& timer=*state.scene_gpu_timer;
          if(timer.active()) timer.Cancel(); // Previous scene took an alternate exit.
          state.scene_gpu_timer_owner=0;
          state.scene_gpu_timer_resolved=false;
          while(const auto sample=timer.Poll()) {
            const auto duration=sample->Milliseconds();
            if(duration && sample->middle) REXLOG_INFO("Native frame GPU span: scene={}, scene_ms={}, post_ms={}, total_ms={} (includes submission gaps; excludes presentation)",sample->tag,
              double(*sample->middle-sample->begin)*1000.0/double(sample->frequency),
              double(sample->end-*sample->middle)*1000.0/double(sample->frequency),*duration);
            else REXLOG_INFO("Native scene GPU span unreliable: scene={}",sample->tag);
          }
          if(state.scene_begins%60==0 && timer.pending()<8) {
            timer.Begin(state.scene_begins+1);
            state.scene_gpu_timer_owner=owner;
          }
        } catch(const std::exception& error) {
          state.scene_gpu_timer.reset(); state.scene_gpu_timer_owner=0;
          REXLOG_ERROR("Native scene GPU timing: {}",error.what());
        }
      }
      state.active_output=0;
      state.scene_indexed_start=state.indexed_submitted;
      state.scene_full_frame=false;
      state.visibility.clear();
      auto& scene=found->second;
      scene.color_surface=color_surface;
      // 8219C5A8 computes the float clear color at 8257BFC0 and passes it to
      // BeginTiling. Use those actual values rather than owner color guesses.
      float color[4];
      for (uint32_t i=0;i<4;++i) color[i]=std::bit_cast<float>(reader.Word(0x8257bfc0+i*4));
      {
        edf::native::HookTiming clear_timing(edf::native::HookPhase::SceneClear);
        auto& recorder=edf::native::SceneRecorderLocked(state);
        recorder.ClearColor(*scene.color.backend_surface,{color[0],color[1],color[2],color[3]});
        scene.color.content_valid=true;
        edf::native::ClearNativeDepthTarget(recorder,scene.depth,true,true,
          std::bit_cast<float>(reader.Word(0x820009a4)),0);
      }
      // The whole surface now contains defined pixels. Native post-processing
      // may sample its explicit resolve, but scene completeness is independent.
      scene.color.content_valid=true;
      scene.frame_complete=false; // Remaining producers/presentation are not covered.
      edf::native::BindActiveTarget(state);
      if(state.context) edf::native::ReadNativeDrawViewport(reader,reader.Word(reader.Add(owner,8))).Bind(*state.context.Get());
      if (++state.scene_begins<=5 || state.scene_begins%1000==0)
        REXLOG_INFO("Native full scene begin: count={}, clear_depth={}, color={},{},{},{}",state.scene_begins,
          std::bit_cast<float>(reader.Word(0x820009a4)),color[0],color[1],color[2],color[3]);
    } catch (const std::exception& error) { state.active_scene=0; REXLOG_ERROR("Native full scene begin: {}",error.what()); }
  }
}
// Ends the tiled pass and switches to the ordinary surface pair. r4==1
// resolves scene HDR to owner+104; other modes discard the tiled contents.
REX_EXTERN(__imp__sub_8219C930);
REX_HOOK_RAW(sub_8219C930) {
  const auto output_owner=ctx.r3.u32;
  if (EDF_NATIVE_FLAG(shader_bridge)) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    if (state.active_scene==ctx.r3.u32) {
      state.active_scene=0;
      edf::native::BindActiveTarget(state);
      // Only mode1 resolves to owner+104; discard mode must retain old sampled
      // contents. Do not synthesize the separate end-frame/backbuffer resolve.
      if (ctx.r4.u32==1) edf::native::ResolveScene(edf::native::GuestReader(base),state,ctx.r3.u32);
      if(state.scene_gpu_timer_owner==ctx.r3.u32 && state.scene_gpu_timer) {
        try {
          if(ctx.r4.u32==1) {
            state.scene_gpu_timer->MarkMiddle();
            state.scene_gpu_timer_resolved=true;
          } else {
            state.scene_gpu_timer->Cancel();
            state.scene_gpu_timer_owner=0;
          }
        } catch(const std::exception& error) {
          state.scene_gpu_timer.reset();
          state.scene_gpu_timer_owner=0;
          REXLOG_ERROR("Native scene GPU timing end: {}",error.what());
        }
      }
      edf::native::CaptureScene(state,ctx.r3.u32);
      if (++state.scene_ends<=5 || state.scene_ends%1000==0)
        REXLOG_INFO("Native full scene end: count={}, resolve_mode={}, frame_complete=false",state.scene_ends,ctx.r4.u32);
    }
  }
  __imp__sub_8219C930(ctx,base);
  if (EDF_NATIVE_FLAG(shader_bridge)) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    state.active_output=0;
    try {
      const edf::native::GuestReader reader(base);
      auto& scene=state.scenes.at(output_owner);
      const auto surface=reader.Word(reader.Add(output_owner,112));
      const auto& creation=state.surface_creations.at(surface);
      if (state.scene_ends<=5)
        REXLOG_INFO("Native ordinary output contract: owner={:#x}, surface={:#x}, {}x{}, format={:#x}, MSAA={}",
          output_owner,surface,creation.width,creation.height,creation.format,creation.msaa);
      if (creation.msaa || creation.width!=scene.color.sampled.width || creation.height!=scene.color.sampled.height ||
          (creation.format!=0x1a220186 && creation.format!=0x18280186))
        throw std::runtime_error("unsupported ordinary output surface contract");
      if (scene.output_surface!=surface || !scene.output.backend_surface) {
        scene.output=edf::native::CreateNativeRenderTarget(edf::native::EnsureSceneBackendLocked(state),creation.width,creation.height,DXGI_FORMAT_R8G8B8A8_UNORM);
        scene.output_surface=surface;
      }
      scene.output.content_valid=false;
      state.active_output=output_owner;
      edf::native::BindActiveTarget(state);
    } catch (const std::exception& error) {
      if (state.scene_ends<=5) REXLOG_INFO("Native ordinary output pending: {}",error.what());
    }
  }
}
// End-frame can finish the tiled pass directly without the C930 intermediate
// resolve. Close that native scope too, before any following UI/frame work.
REX_EXTERN(__imp__sub_8219C840);
REX_HOOK_RAW(sub_8219C840) {
  if (EDF_NATIVE_FLAG(shader_bridge)) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    if(REXCVAR_GET(edf_native_loading_trace)) {
      // Observe before either scope is closed. These are eligibility counts,
      // not successful publications or proof that loading UI pixels are visible.
      const auto owner=ctx.r3.u32;
      const bool output=state.active_output==owner;
      const auto found=state.scenes.find(owner);
      const bool valid=output && found!=state.scenes.end() && found->second.output.content_valid;
      const size_t route=output ? (valid?0:1) : (state.active_scene==owner?2:3);
      ++state.loading_trace_routes[route];
      const auto now=std::chrono::steady_clock::now();
      if(++state.loading_trace_frames<=8 || now-state.loading_trace_reported>=std::chrono::seconds(1)) {
        state.loading_trace_reported=now;
        REXLOG_INFO("Native loading frame: frames={}, owner={:#x}, scene={:#x}, output={:#x}, output_valid={}, handoff={}, eligible={}, invalid_output={}, direct_scene={}, no_scope={}, xui_total={}, font_total={}, movie_total={} (cumulative eligibility, not publication success or per-frame UI attribution)",
          state.loading_trace_frames,owner,state.active_scene,state.active_output,valid,
          state.presentation_frames!=nullptr,state.loading_trace_routes[0],state.loading_trace_routes[1],
          state.loading_trace_routes[2],state.loading_trace_routes[3],state.xui_draws,state.font_draws,state.movie_draws);
      }
    }
    if (state.active_output==ctx.r3.u32) {
      auto& scene=state.scenes.at(state.active_output);
      if(REXCVAR_GET(edf_native_publish_frames)) {
        if(state.presentation_frames) state.presentation_frames->Invalidate();
        if(scene.output.content_valid) {
          // Two ways to the window, and which one applies is decided by what
          // the scene is drawn on rather than by a flag. A D3D11 scene has an
          // ID3D11ShaderResourceView the compositor can take; anything else
          // publishes a shared surface through the host snapshot and compositor.
          try {
            if(scene.output.surface)
              state.presentation_frames->Publish(*scene.output.surface.Get(),edf::native::NativeFrameKind::PartialScene,
                state.display_gamma?&*state.display_gamma:nullptr);
            else edf::native::PublishSceneSharedLocked(state,scene.output);
          }
          catch(const std::exception& error) { REXLOG_ERROR("Native frame publication: {}",error.what()); }
        }
      }
      const auto prefix=REXCVAR_GET(edf_native_scene_capture);
      const bool indexed_output=edf::native::NativeOutputFrameCounts(scene.output.content_valid,state.indexed_submitted,
        state.scene_indexed_start,state.scene_full_frame);
      if(indexed_output) state.movie_pacing_active.store(false,std::memory_order_relaxed);
      if(indexed_output) ++state.indexed_output_frames;
      // Retail exposure adapts per frame. The first three outputs alone cannot
      // distinguish startup overexposure from a persistently broken post chain.
      const auto capture_interval=REXCVAR_GET(edf_native_output_capture_interval);
      if (!prefix.empty() && indexed_output && edf::native::ShouldCaptureNativeOutput(
          state.indexed_output_frames,state.output_captures,REXCVAR_GET(edf_native_output_capture_limit),
          capture_interval,REXCVAR_GET(edf_native_output_capture_start_frame))) {
        try {
          ++state.output_captures;
          const auto path=std::filesystem::path(prefix+".output."+std::to_string(state.indexed_output_frames)+".bmp");
          if (std::filesystem::exists(path)) throw std::runtime_error("native output capture already exists");
          const auto bmp=edf::native::CaptureOutputBmp(state,scene);
          std::ofstream output(path,std::ios::binary);
          output.write(reinterpret_cast<const char*>(bmp.data()),bmp.size()); output.close();
          if (!output) throw std::runtime_error("native output capture write failed");
          REXLOG_INFO("Native bloom output capture: {}, draws={}, frame_complete=false (UI and scene coverage incomplete)",path.string(),state.output_draws);
          if(REXCVAR_GET(edf_native_output_capture_scene_color)) {
            if(!scene.color.content_valid || !scene.color.backend_surface)
              throw std::runtime_error("paired scene-color capture has no valid scene surface");
            const auto scene_path=std::filesystem::path(prefix+".scene-color."+std::to_string(state.indexed_output_frames)+".bmp");
            if(std::filesystem::exists(scene_path)) throw std::runtime_error("paired scene-color capture already exists");
            const auto scene_bmp=edf::native::CaptureNativeBmp(edf::native::EnsureSceneBackendLocked(state),*scene.color.backend_surface,scene.color.format);
            std::ofstream scene_file(scene_path,std::ios::binary);
            scene_file.write(reinterpret_cast<const char*>(scene_bmp.data()),scene_bmp.size()); scene_file.close();
            if(!scene_file) throw std::runtime_error("paired scene-color capture write failed");
            REXLOG_INFO("Native paired scene color capture: {}, output={}, same indexed frame; HDR BMP is diagnostic, not display reference",
              scene_path.string(),path.string());
          }
          for (const auto& [owner,target]:state.render_targets) {
            const auto& sample=target.native.sampled;
            if (!sample.content_valid || sample.width>40 || sample.height>22) continue;
            const auto value=edf::native::ReadNativeColorPixel(edf::native::EnsureSceneBackendLocked(state),*sample.backend,sample.format,sample.width/2,sample.height/2);
            REXLOG_INFO("Native post pixel: texture={:#x}, {}x{}, center={},{},{},{}",target.texture_handle,sample.width,sample.height,value[0],value[1],value[2],value[3]);
          }
        } catch (const std::exception& error) { REXLOG_ERROR("Native output capture: {}",error.what()); }
      }
      state.active_output=0;
      edf::native::BindActiveTarget(state);
    }
    if (state.active_scene==ctx.r3.u32) {
      try {
        const edf::native::GuestReader reader(base);
        const auto owner=ctx.r3.u32;
        auto& scene=state.scenes.at(owner);
        // C840 -> C678 resolves directly to owner[(31 + buffer_index) * 4].
        // This path has no ordinary-output/post pass and must still present UI.
        const auto index=reader.Word(reader.Add(owner,140));
        if(index>=4) throw std::runtime_error("unsupported direct frame buffer index");
        const auto handle=reader.Word(reader.Add(owner,124+index*4));
        const auto& creation=state.texture_creations.at(handle);
        if(creation.width!=scene.color.sampled.width || creation.height!=scene.color.sampled.height ||
           creation.format!=0x28280106)
          throw std::runtime_error("unsupported direct frame destination format/dimensions: "+std::to_string(creation.format));
        auto& direct=scene.direct_outputs[handle];
        // Tested on the backend handle, not the D3D11 one: a target on the
        // scene's own backend has no D3D11 surface, and this would build a new
        // one every frame and hand the old one to the collector mid-flight.
        if(!direct.backend_surface)
          direct=edf::native::CreateNativeOpaqueFrameTarget(edf::native::EnsureSceneBackendLocked(state),creation.width,creation.height);
        edf::native::ResolveNativeRgba8Frame(edf::native::SceneRecorderLocked(state),scene.color,direct);
        state.textures.insert_or_assign(handle,direct.sampled);
        if(REXCVAR_GET(edf_native_publish_frames)) {
          if(state.presentation_frames) state.presentation_frames->Invalidate();
          // Two ways to the window, chosen by what the frame was drawn on; see
          // the ordinary output path, which makes the same choice.
          if(direct.sampled.content_valid) {
            if(direct.sampled.resource)
              state.presentation_frames->Publish(*direct.sampled.resource.Get(),edf::native::NativeFrameKind::PartialScene,
                state.display_gamma?&*state.display_gamma:nullptr);
            else edf::native::PublishSceneSharedLocked(state,direct);
          }
        }
      } catch(const std::exception& error) {
        static uint64_t failures=0;
        if(++failures<=8 || !(failures&(failures-1)))
          REXLOG_ERROR("Native direct frame resolve: {} (failures={})",error.what(),failures);
      }
      state.active_scene=0;
      edf::native::BindActiveTarget(state);
      edf::native::CaptureScene(state,ctx.r3.u32);
      ++state.scene_ends;
    }
  }
  __imp__sub_8219C840(ctx,base);
}
// Renderer teardown owns the full-frame native scene, unlike individual guest
// tile resources. Clear the active scope before releasing its attachments.
REX_EXTERN(__imp__sub_8219E140);
REX_HOOK_RAW(sub_8219E140) {
  if (EDF_NATIVE_FLAG(shader_bridge)) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    if (state.active_scene==ctx.r3.u32) state.active_scene=0;
    if (state.active_output==ctx.r3.u32) state.active_output=0;
    if (state.scenes.erase(ctx.r3.u32)) edf::native::BindActiveTarget(state);
  }
  __imp__sub_8219E140(ctx,base);
}
