#pragma once
// Bridge CPU timing: hook phases and their timing scopes (HookTiming), the engine-thread region probe,
// the engine/render-helper thread QoS and the timed bridge locks (BridgeMutex, BridgeGate). Moved from
// guest_shader_bridge.cpp unchanged; its variables and functions are defined in hook_timing.cpp, except
// FrameHookPhaseTotals, inline here (HookTiming::Finish calls it from every translation unit).
#include "../native_load_trace.h"
#include "native_cvars.h"
#include <rex/logging.h>
#include <windows.h>
#include <intrin.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

// edf_native_load_trace (native_load_trace.h); read by the bridge locks and every load-trace scope.
inline bool LoadTraceOn() { return REXCVAR_GET(edf_native_load_trace); }

namespace edf::native {
// Opt-in CPU wall-clock diagnostics, not GPU timestamps. Include lock waits and
// any nested work; phases from different hooks must not be added as exclusive
// frame costs. Per-thread buckets avoid adding contention to the draw path.
enum class HookPhase { ActivationGuest, ActivationNative, InstanceGuest, InstanceNative,
                       IndexedNative, IndexedGuest, ImmediateNative, ImmediateGuest,
                       SwapGpuWait, SwapRefreshWait, EngineWait, CompletionPoll,
                       SceneSetup, WorkerService, TilingBegin, TilingEnd,
                       FenceWait, SubmissionFlush, DescriptorSubmit,
                       SceneSetupGuest, SceneSetupNative, SceneSetupLock, SceneClear,
                       ColorTarget, DepthTarget, ViewportHook, ViewportLock,
                       ViewportRead, ViewportWrite, ViewportGuest,
                       IndexedMesh, IndexedBindings, MeshRanges, MeshAcquire, MeshDrawRange,
                       MeshObserve, MeshLookup, MeshCommit,
                       IndexedSubmissionWait, IndexedContextWait, ImmediateSubmissionWait,
                       ImmediateContextWait, PresentationContextWait,
                       ActivationLock, ActivationResolve, ActivationVertexParams, ActivationPixelParams,
                       ActivationTextures, ActivationBind,
                       XuiNative, XuiDecode, XuiBind, XuiDraw,
                       IndexedSetup, IndexedRecord, IndexedDraw, IndexedTail, IndexedCoverage,
                       ImmediateClassify, ImmediateUtility3D, ImmediateAcquire, ImmediateRecord, ImmediateTail,
                       ActivationSamplerWords, InstanceRead, InstancePatch,
                       SimulationDispatch, RenderHelper, FrameTransition,
                       RenderGather, RenderBuckets, RenderModel, RenderMesh, RenderOverlay,
                       RenderSceneEnd, RenderFinish, RenderPose,
                       RenderList, RenderSceneBegin, RenderChildren, RenderWorld,
                       RenderListener, RenderUiListener,
                       RenderQueued, RenderMaterialGroup,
                       RenderGatherClassify, RenderGatherVisibility, RenderGatherLod, RenderGatherPush, RenderGatherGuest,
                       QueuedEligibility, QueuedResolve, QueuedInstances, QueuedRecord,
                       QueuedHandoff, QueuedHandoffBinds, QueuedHandoffReplays,
                       FrameNative, FrameNativeBegin, FrameNativeSky, FrameNativeModels,
                       FrameNativeStaticWorld, FrameNativeEffects, FrameNativeTransparent, FrameNativePost,
                       FrameNativeEnd, FrameNativeOverlays, FrameNativePhases,
                       FrameNativeModelsVisibility, FrameNativeModelsPrograms, FrameNativeModelsResolve, FrameNativeModelsRecord,
                       FrameNativeStaticWorldSelect, FrameNativeStaticWorldBuild, FrameNativeStaticWorldRecord,
                       FrameNativeEffectActivate, FrameNativeEffectEncode,
                       SimRegistry, SimStaticWalk, SimPreloadGeometry, SimPreloadMaterial,
                       SimTrees, SimMembership, SimPublish, SimPoses, SimLockWait, SimPreloadPrecheck, SimWorldUpdate,
                       BridgeMutexWait, BridgeGateWait, GuestWait,
                       TextureSnapshot, TextureOriginal, TextureLock, TextureCreate,
                       ShaderRegistration, ShaderLock, ShaderEntry,
                       TextureAllocate, TextureUpload2D, TextureUploadVolume, TexturePrepare,
                       ResourceOneShot, ResourceCoordinator, ResourceHelper, ResourceTransition, Count };
inline constexpr const char* kHookPhaseNames[]{"activation.original","activation.native",
  "instance.original","instance.native","indexed.native","indexed.original",
  "immediate.native","immediate.original","swap.gpu_wait","swap.refresh_wait",
  "engine.wait","completion.poll","scene.setup","worker.service","tiling.begin","tiling.end",
  "fence.wait","submission.flush","descriptor.submit","scene.setup.original","scene.setup.native",
  "scene.setup.lock","scene.clear","target.color","target.depth","viewport.hook",
  "viewport.lock","viewport.read","viewport.write","viewport.original",
  "indexed.mesh","indexed.bindings","mesh.ranges","mesh.acquire","mesh.draw_range",
  "mesh.observe","mesh.lookup","mesh.commit",
  "indexed.submission_wait","indexed.context_wait","immediate.submission_wait",
  "immediate.context_wait","presentation.context_wait",
  "activation.lock","activation.resolve","activation.params_vs","activation.params_ps",
  "activation.textures","activation.bind",
  "xui.native","xui.decode","xui.bind","xui.draw",
  "indexed.setup","indexed.record","indexed.draw","indexed.tail","indexed.coverage",
  "immediate.classify","immediate.utility3d","immediate.acquire","immediate.record","immediate.tail",
  "activation.sampler_words","instance.read","instance.patch",
  "engine.simulation_dispatch","engine.render_helper","engine.frame_transition",
  "render.gather","render.buckets","render.model","render.mesh","render.overlay",
  "render.scene_end","render.finish","render.pose",
  "render.list","render.scene_begin","render.children","render.world",
  "render.listener","render.ui_listener",
  "render.queued","render.material_group",
  "render.gather.classify","render.gather.visibility","render.gather.lod","render.gather.push","render.gather.guest_dispatch",
  "render.queued.eligibility","render.queued.resolve","render.queued.instances","render.queued.record",
  "render.queued.handoff","render.queued.handoff_binds","render.queued.handoff_replays",
  "frame.native","frame.native.begin","frame.native.sky","frame.native.models",
  "frame.native.static_world","frame.native.effects","frame.native.transparent","frame.native.post",
  "frame.native.end","frame.native.view_overlays","frame.native.phases",
  "frame.native.models.visibility","frame.native.models.programs","frame.native.models.resolve","frame.native.models.record",
  "frame.native.static_world.select","frame.native.static_world.build","frame.native.static_world.record",
  "frame.native.effect_activate","frame.native.effect_encode",
  "sim.registry","sim.static_walk","sim.preload_geometry","sim.preload_material",
  "sim.trees","sim.membership","sim.publish","sim.poses","sim.lock_wait","sim.preload_precheck","sim.world_update",
  "bridge.mutex_wait","bridge.gate_wait","guest.wait",
  "load.texture.snapshot","load.texture.original","load.texture.lock","load.texture.create",
  "load.shader.registration","load.shader.lock","load.shader.entry",
  "load.texture.allocate","load.texture.upload2d","load.texture.upload_volume","load.texture.prepare",
  "load.resource.oneshot","load.resource.coordinator","load.resource.helper","load.resource.transition"};
static_assert(std::size(kHookPhaseNames)==static_cast<size_t>(HookPhase::Count));
// The thread-locals declared here are constinit (constant-initialized, trivially destructible): read from any
// file without the TLS guard check an extern thread_local otherwise costs on every access.
extern thread_local constinit uint32_t texture_loader_depth;
// This frame's inclusive totals per phase, for the edf_native_frame_times
// spike lines: every thread adds, the swap takes and clears them. Only full
// (not sampled) timings add, so they exist only with the hook or load timings
// on. Names are published by the first Finish of each phase.
struct FrameHookPhases {
  std::array<std::atomic<uint64_t>,static_cast<size_t>(HookPhase::Count)> nanos{};
  std::array<std::atomic<const char*>,static_cast<size_t>(HookPhase::Count)> names{};
};
inline FrameHookPhases& FrameHookPhaseTotals() {
  static FrameHookPhases totals;
  return totals;
}
// Engine-thread region probe (edf_native_hook_timings only; nothing runs when
// off). The step dispatch 821A4BA0 and the frame transition 821A4DE8 run guest
// code on the engine thread, and their inclusive timings say how long they
// take, not why. This splits a region's wall time into: time off the CPU
// (thread cycle time against the TSC: blocked in a wait, or preempted), the
// processor class it ran on (efficiency-class samples at entry, exit and at
// every timed hook inside it), whether the render helper ran beside it, the
// bridge-lock and guest waits inside it (bridge.*_wait, guest.wait), and every
// timed hook phase nested in it. A fixed calibration kernel run beside the
// region measures how fast the engine thread itself executes there,
// independent of guest content: a slower region with an unchanged kernel is
// more guest work; a slower kernel is a slower thread (core class, clock, SMT
// sibling, cache). Scheduling, locks and guest state are unchanged.
enum class EngineRegion : uint8_t { Dispatch, Transition, Count };
struct EngineRegionTotals {
  uint64_t calls=0,steps=0;
  double wall_ms=0,oncpu_ms=0;
  // Processor samples: [0] the highest efficiency class (performance cores;
  // every core on a non-hybrid part), [1] a lower class, [2] unknown.
  std::array<uint64_t,3> cores{};
  uint64_t migrations=0,helper_entry=0,helper_exit=0,mxcsr_stale=0;
  std::array<double,size_t(HookPhase::Count)> phase_ms{};
  std::array<uint64_t,size_t(HookPhase::Count)> phase_calls{};
  // Guest wait wrappers timed inside the region, by function.
  struct Wait { uint32_t function=0; uint64_t calls=0; double ms=0; };
  std::array<Wait,6> waits{};
  uint64_t other_waits=0; double other_wait_ms=0;
  // Calibration kernels (microseconds) and the class they ran on.
  uint64_t calibrations=0; double alu_us=0,memory_us=0;
  std::array<uint64_t,3> calibration_cores{};
  std::chrono::steady_clock::time_point reported{};
};
// The region this thread is in; null outside one (and always when timings are off).
extern thread_local constinit EngineRegionTotals* native_engine_region;
// The guest wait wrapper being timed on this thread, for the per-function split.
extern thread_local constinit uint32_t native_guest_wait_function;
// Render helper calls (821A5080) in flight, sampled at region entry and exit.
extern std::atomic<int> native_render_helper_active;
// Logical processor -> efficiency class (GetSystemCpuSetInformation), and the
// CPU set IDs of the highest class. Read once; the topology does not change.
struct NativeCpuTopology {
  struct Processor { BYTE efficiency=0; bool known=false; };
  std::vector<Processor> processors;  // index: group*64+number
  std::vector<ULONG> performance_sets;
  BYTE highest=0,lowest=0;
  size_t performance_logical=0,other_logical=0;
  static const NativeCpuTopology& Get() { static const NativeCpuTopology value=Build(); return value; }
  bool hybrid() const { return highest!=lowest; }
  // 0: highest class, 1: lower class, 2: unknown.
  size_t Classify(const PROCESSOR_NUMBER& number) const {
    const size_t index=size_t(number.Group)*64+number.Number;
    if(index>=processors.size() || !processors[index].known) return 2;
    return processors[index].efficiency==highest?0:1;
  }
  size_t Current() const { PROCESSOR_NUMBER number{}; GetCurrentProcessorNumberEx(&number); return Classify(number); }
 private:
  static NativeCpuTopology Build() {
    NativeCpuTopology result;
    ULONG length=0;
    GetSystemCpuSetInformation(nullptr,0,&length,GetCurrentProcess(),0);
    if(!length) return result;
    std::vector<uint8_t> buffer(length);
    if(!GetSystemCpuSetInformation(reinterpret_cast<SYSTEM_CPU_SET_INFORMATION*>(buffer.data()),length,&length,
         GetCurrentProcess(),0)) return result;
    struct Entry { size_t index; BYTE efficiency; ULONG id; };
    std::vector<Entry> entries;
    for(size_t offset=0;offset+offsetof(SYSTEM_CPU_SET_INFORMATION,CpuSet)<=length;) {
      const auto* entry=reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buffer.data()+offset);
      if(!entry->Size || offset+entry->Size>length) break;
      if(entry->Type==CpuSetInformation)
        entries.push_back({size_t(entry->CpuSet.Group)*64+entry->CpuSet.LogicalProcessorIndex,
          entry->CpuSet.EfficiencyClass,entry->CpuSet.Id});
      offset+=entry->Size;
    }
    if(entries.empty()) return result;
    result.highest=result.lowest=entries.front().efficiency;
    for(const auto& entry:entries) {
      result.highest=(std::max)(result.highest,entry.efficiency);
      result.lowest=(std::min)(result.lowest,entry.efficiency);
      if(entry.index>=result.processors.size()) result.processors.resize(entry.index+1);
      result.processors[entry.index]={entry.efficiency,true};
    }
    for(const auto& entry:entries) {
      if(entry.efficiency==result.highest) { result.performance_sets.push_back(entry.id); ++result.performance_logical; }
      else ++result.other_logical;
    }
    return result;
  }
};
extern thread_local constinit std::array<EngineRegionTotals,size_t(EngineRegion::Count)> native_engine_regions;
void ReportEngineRegion(EngineRegion region,EngineRegionTotals& totals);
// One engine-thread region (see EngineRegionTotals). Construct it inside the
// region's own inclusive HookTimings so they are not attributed to it.
class EngineRegionScope {
 public:
  using Clock=std::chrono::steady_clock;
  EngineRegionScope(EngineRegion region,uint32_t steps,uint32_t guest_csr)
      :region_(region),steps_(steps) {
    if(!REXCVAR_GET(edf_native_hook_timings)) return;
    auto& totals=native_engine_regions[size_t(region)];
    previous_=std::exchange(native_engine_region,&totals);
    totals_=&totals;
    // The guest's cached flush mode against the live MXCSR: a stale cache
    // runs VMX code without flush-to-zero (slow denormals) until the next
    // FPU instruction rewrites it.
    if(((_mm_getcsr()^guest_csr)&0x8040u)!=0) ++totals.mxcsr_stale;
    if(native_render_helper_active.load(std::memory_order_relaxed)>0) ++totals.helper_entry;
    GetCurrentProcessorNumberEx(&processor_);
    ++totals.cores[NativeCpuTopology::Get().Classify(processor_)];
    QueryThreadCycleTime(GetCurrentThread(),&cycles_);
    tsc_=__rdtsc();
    start_=Clock::now();
  }
  ~EngineRegionScope() { Finish(); }
  EngineRegionScope(const EngineRegionScope&)=delete;
  EngineRegionScope& operator=(const EngineRegionScope&)=delete;
  void Finish() {
    if(!totals_) return;
    auto& totals=*std::exchange(totals_,nullptr);
    const auto now=Clock::now();
    const auto tsc=__rdtsc();
    ULONG64 cycles=0;
    QueryThreadCycleTime(GetCurrentThread(),&cycles);
    native_engine_region=previous_;
    const double wall=std::chrono::duration<double,std::milli>(now-start_).count();
    const double share=tsc>tsc_?(std::min)(1.0,double(cycles-cycles_)/double(tsc-tsc_)):1.0;
    PROCESSOR_NUMBER processor{};
    GetCurrentProcessorNumberEx(&processor);
    ++totals.cores[NativeCpuTopology::Get().Classify(processor)];
    if(processor.Group!=processor_.Group || processor.Number!=processor_.Number) ++totals.migrations;
    if(native_render_helper_active.load(std::memory_order_relaxed)>0) ++totals.helper_exit;
    ++totals.calls; totals.steps+=steps_;
    totals.wall_ms+=wall; totals.oncpu_ms+=wall*share;
    if(totals.calls>=256 && (totals.reported==Clock::time_point{} || now-totals.reported>=std::chrono::seconds(5))) {
      ReportEngineRegion(region_,totals);
      totals={};
      totals.reported=now;
    }
  }
 private:
  EngineRegion region_;
  uint32_t steps_;
  EngineRegionTotals* totals_=nullptr;
  EngineRegionTotals* previous_=nullptr;
  PROCESSOR_NUMBER processor_{};
  ULONG64 cycles_=0;
  uint64_t tsc_=0;
  Clock::time_point start_{};
};
// Fixed work on the engine thread, every 32nd region call with hook timings
// on, outside every region timing: a dependent multiply-add chain (latency
// bound: the core's class and clock) and a pointer chase through 16 MiB
// (last-level cache and memory contention). About 25 us and 50-200 us.
void RunEngineCalibration(EngineRegion region);
// Quality of service for the two threads that bound a frame: the engine thread
// (step dispatch and transition) and the render helper (edf_native_thread_qos).
// Windows derives a thread's QoS from its window: a hidden or occluded one
// (automated runs launch hidden) gets low QoS, which on a hybrid CPU steers it
// towards efficiency cores and lower clocks. 1 opts these two threads (only;
// workers keep the OS default) out of execution-speed throttling (HighQoS);
// 2 also restricts them to the highest efficiency class's CPU sets
// (performance cores; a no-op on a non-hybrid part). Applied once per thread, at its first hook
// call; guest state, locks and ordering are untouched.
enum class NativeThreadRole : uint8_t { Engine, RenderHelper };
void ApplyNativeThreadQos(NativeThreadRole role);
class HookTiming {
 public:
  explicit HookTiming(HookPhase phase,bool active=true) : phase_(phase), enabled_(active && (phase>=HookPhase::TextureSnapshot ?
      REXCVAR_GET(edf_native_load_timings) : REXCVAR_GET(edf_native_hook_timings))) {
    if(active && !enabled_ && phase<HookPhase::TextureSnapshot) {
      const auto period=uint32_t(REXCVAR_GET(edf_native_hook_sample_period));
      if(period) {
        static thread_local std::array<uint64_t,static_cast<size_t>(HookPhase::Count)> calls{};
        const auto index=size_t(phase);
        // Offset phases so nested scopes do not all pay for timing on the
        // same draw. Samples remain deterministic for reproducible diagnosis.
        enabled_=(calls[index]++ + index*17)%period==0;
        sample_period_=period;
      }
    }
    if(enabled_) start_=Clock::now();
  }
  ~HookTiming() { Finish(); }
  void Finish() {
    if(!enabled_) return;
    enabled_=false;
    const auto now=Clock::now();
    const double ms=std::chrono::duration<double,std::milli>(now-start_).count();
    struct Bucket { uint64_t count=0; double total=0,maximum=0; Clock::time_point reported{}; };
    static thread_local std::array<Bucket,static_cast<size_t>(HookPhase::Count)> buckets{};
    const auto& names=kHookPhaseNames;
    const auto index=static_cast<size_t>(phase_);
    if(!sample_period_ && REXCVAR_GET(edf_native_frame_times)) {
      auto& totals=FrameHookPhaseTotals();
      totals.names[index].store(names[index],std::memory_order_relaxed);
      totals.nanos[index].fetch_add(uint64_t(ms*1e6),std::memory_order_relaxed);
    }
    auto& bucket=buckets[index];
    ++bucket.count; bucket.total+=ms; bucket.maximum=(std::max)(bucket.maximum,ms);
    if(auto* region=native_engine_region) {
      region->phase_ms[index]+=ms; ++region->phase_calls[index];
      ++region->cores[NativeCpuTopology::Get().Current()];
      if(phase_==HookPhase::GuestWait) {
        auto slot=std::find_if(region->waits.begin(),region->waits.end(),[](const EngineRegionTotals::Wait& wait) {
          return wait.function==native_guest_wait_function || !wait.function; });
        if(slot==region->waits.end()) { ++region->other_waits; region->other_wait_ms+=ms; }
        else { slot->function=native_guest_wait_function; ++slot->calls; slot->ms+=ms; }
      }
    }
    if(phase_>=HookPhase::TextureSnapshot ||
       ((sample_period_ || bucket.count>=256) && (bucket.reported==Clock::time_point{} || now-bucket.reported>=std::chrono::seconds(5)))) {
      if(sample_period_) {
        REXLOG_INFO("Native sampled hook timing: phase={} samples={} period={} total_ms={} max_ms={} (sampled inclusive CPU wall time)",
          names[index],bucket.count,sample_period_,bucket.total,bucket.maximum);
      } else {
        REXLOG_INFO("Native hook timing: phase={} calls={} total_ms={} max_ms={} (inclusive CPU wall time)",
          names[index],bucket.count,bucket.total,bucket.maximum);
      }
      bucket={};
      bucket.reported=now;
    }
  }
 private:
  using Clock=std::chrono::steady_clock;
  HookPhase phase_;
  uint32_t sample_period_=0;
  bool enabled_;
  Clock::time_point start_{};
};
// A bridge lock whose contended acquisitions are timed (bridge.mutex_wait,
// bridge.gate_wait; inside an engine region they are also attributed to it).
// try_lock first, so an uncontended acquisition is one atomic operation as
// before, and nothing is timed unless hook timings are on and the lock was
// actually held by another thread. Lockable, so lock_guard, unique_lock,
// scoped_lock and NativeLockSlices take it unchanged.
template<class Mutex,HookPhase Phase>
class NativeTimedMutex {
 public:
  void lock() {
    if(mutex_.try_lock()) return;
    HookTiming timing(Phase);
    LoadTraceScope trace(::LoadTraceOn(),LoadTraceKind::BridgeMutexWait);
    mutex_.lock();
  }
  bool try_lock() { return mutex_.try_lock(); }
  void unlock() { mutex_.unlock(); }
 private:
  Mutex mutex_;
};
using BridgeMutex=NativeTimedMutex<std::mutex,HookPhase::BridgeMutexWait>;
using BridgeGate=NativeTimedMutex<std::recursive_mutex,HookPhase::BridgeGateWait>;
}  // namespace edf::native
