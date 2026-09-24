#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "guest_shader_bridge.h"
#include "native_renderer_preset.h"
#include "../scripted_input_logic.h"
#include "../frame_stats.h"
#include "../pause_menu.h"
#include "../input_latency.h"
#include "native_backend_frame.h"
#include "native_backend_frame_queue.h"
#include "native_frame_flight.h"
#include "native_lock_slices.h"
#include "d3d11_backend.h"
#include "d3d12_backend.h"
#include "native_render_backend.h"
#include "native_constant_cache.h"
#include "native_scene_sources.h"
#include "native_scene_adapter.h"
#include "native_scene_pass_inputs.h"
#include "native_world_publication_mirror.h"
#include "native_scene_cpu_window.h"
#include "native_scene_membership.h"
#include "native_scene_geometry.h"
#include "native_recorded_reads.h"
#include "native_scene_geometry_install.h"
#include "native_material_cpu_program.h"
#include "native_scene_handoff.h"
#include "native_scene_execution.h"
#include "native_scene_world_restore.h"
#include "native_static_group_eligibility.h"
#include "native_static_world_resolve.h"
#include "native_static_world_pass.h"
#include "native_static_world_cache.h"
#include "native_queued_scene.h"
#include "native_decode_workers.h"
#include "native_d3d12_preview.h"
#include "native_host_surface.h"
#include "guest_instance_parameters.h"
#include "guest_parameter_records.h"
#include "guest_draw_state.h"
#include "guest_readable_range.h"
#include "guest_sdk_readable_range.h"
#include "guest_mesh_watch_audit.h"
#include "guest_audio_output_ranges.h"
#include "native_model_buffers.h"
#include "native_pool_backings.h"
#include "native_cache_flush.h"
#include "native_model_header.h"
#include "native_model_cleanup.h"
#include "native_index_binding.h"
#include "native_stream_binding.h"
#include "native_shader_state.h"
#include "native_shader_binding.h"
#include "native_frame_dispatch.h"
#include "native_full_frame.h"
#include "native_coverage_census.h"
#include "native_frame_times.h"
#include "native_disk_cache.h"
#include "native_gpu_pass_timings.h"
#include "native_full_frame_static_world.h"
#include "native_full_frame_models.h"
#include "native_motion_vector_pass.h"
#include "native_full_frame_sky.h"
#include "native_full_frame_effects.h"
#include "native_view_globals.h"
#include "native_bucket_dispatch.h"
#include "native_map_effects.h"
#include "native_scene_tree.h"
#include "native_scene_walk_lock.h"
#include "native_scene_tree_publication.h"
#include "native_scene_static_walk.h"
#include "native_texture_binding.h"
#include "native_render_state_snapshot.h"
#include "native_declarations.h"
#include "native_material_parameters.h"
#include "native_font_bindings.h"
#include "native_xui_bindings.h"
#include "native_display_layout.h"
#include "native_movie_bindings.h"
#include "native_generated_indices.h"
#include "native_physical_write_notify.h"
#include "native_contract_ledger.h"
#include "native_load_trace.h"
#include "utility_layout.h"
#include "triangle_strip.h"
#include "guest_fence.h"
#include "native_fence_poll.h"
#include "native_fence_records.h"
#include "native_pix_monitor.h"
#include "native_allocator_wait.h"
#include "native_submission_flush.h"
#include "native_descriptor_submit.h"
#include "native_submission_observers.h"
#include "native_submission_dispatch.h"
#include "native_ring_cursor.h"
#include "native_submission_cursors.h"
#include "native_buffer_write_frame.h"
#include "native_pacing.h"
#include "native_camera_history.h"
#include "native_model_pose_history.h"
#include "native_model_publication.h"
#include "native_render_registry.h"
#include "native_model_pass.h"
#include "native_profile_result.h"
#include "native_capture_policy.h"
#include "native_ab_alternate.h"
#include "native_shadow_render.h"
#include "native_post_finish_plan.h"
#include "native_fsr.h"
#include "native_ffx.h"
#include "native_d3d12_raw.h"
#include "native_full_frame_post.h"
#include "native_constant_ownership.h"
#include "immediate_mesh_key.h"
#include "d3d11_bindings.h"
#include "d3d11_texture.h"
#include "d3d11_quads.h"
#include "d3d11_mesh.h"
#include "d3d11_completion.h"
#include "d3d11_signals.h"
#include "d3d11_gpu_timer.h"
#include "d3d11_sampler.h"
#include "d3d11_render_state.h"
#include "movie_effect.h"
#include "xui_effect.h"
#include "native_transient_batching.h"
#include "native_reuse.h"
#include "native_immediate_classify.h"
#include "font_effect.h"
#include "bridge/native_cvars.h"
#include "bridge/bridge_state.h"
#include "bridge/bridge_helpers.h"
#include "bridge/bridge_shared.h"
#include "edf/full_frame/host.h"
#include <rex/cvar.h>
#include <rex/chrono/clock.h>
#include <rex/ppc/func.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/memory.h>
#include <rex/thread/mutex.h>
#include <cstring>
#include <format>
#include <limits>
#include <bit>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <fstream>
#include <set>
#include <cmath>
#include <chrono>
#include <thread>
#include <optional>

REXCVAR_DECLARE(std::string, edf_hud_safe_area);
REXCVAR_DECLARE(int32_t, window_width);
REXCVAR_DECLARE(int32_t, window_height);
REX_EXTERN(__imp__KeSetEvent);

namespace {
// The engine thread's kernel+user time in 100 ns units (GetThreadTimes'
// granularity is the scheduler tick; fine against 250 ms windows).
uint64_t LoadTraceEngineCpu(NativeLoadTraceState& state) {
  const HANDLE engine=state.engine.load(std::memory_order_acquire);
  FILETIME created{},exited{},kernel{},user{};
  if(!engine || !GetThreadTimes(engine,&created,&exited,&kernel,&user)) return 0;
  auto value=[](const FILETIME& time) { return (uint64_t(time.dwHighDateTime)<<32)|time.dwLowDateTime; };
  return value(kernel)+value(user);
}
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
// Categories that mean loading work (not per-frame gameplay costs): a tick
// outside a loading window is logged only if one of these moved, so the
// intro's background streaming and the scene teardown show up as ticks while
// steady gameplay stays quiet.
bool LoadTraceLoadingActivity(const edf::native::LoadTraceSample& now,const edf::native::LoadTraceSample& before) {
  using edf::native::LoadTraceKind;
  for(const auto kind:{LoadTraceKind::FileRead,LoadTraceKind::TextureCreate,LoadTraceKind::TextureGuest,
                       LoadTraceKind::ShaderRegistration,LoadTraceKind::ModelConstruct,LoadTraceKind::ModelRetire,
                       LoadTraceKind::ResourceDestroy,LoadTraceKind::MapLoad})
    if(now.calls[size_t(kind)]!=before.calls[size_t(kind)]) return true;
  return false;
}
}  // namespace (shared: bridge/bridge_shared.h)
// Phase transition: logs what happened since the previous event. `open`
// starts the loading window (ticks every 250 ms), `close` ends it and logs the
// window's totals. `detail` is event-specific (manager, mode, caller).
void LoadTraceEvent(const char* event,uint32_t detail,bool open,bool close) {
  if(!LoadTraceOn()) return;
  auto& state=LoadTraceState();
  std::lock_guard lock(state.mutex);
  const auto now=NativeLoadTraceState::Clock::now();
  const auto sample=edf::native::NativeLoadTrace::Get().Sample();
  const auto cpu=LoadTraceEngineCpu(state);
  const auto frames=state.presenter_frames.load(std::memory_order_relaxed);
  if(!state.started) {
    state.started=true; state.origin=state.last_event=state.last_tick=now;
    state.engine_cpu_event=state.engine_cpu_tick=cpu;
  }
  auto ms=[](auto duration) { return std::chrono::duration<double,std::milli>(duration).count(); };
  REXLOG_INFO("Native load trace: event={} detail={:#x} thread={} t_ms={:.1f} dt_ms={:.1f} engine_cpu_ms={:.1f} "
    "presenter_frames={} loading={} | {}",event,detail,GetCurrentThreadId(),ms(now-state.origin),ms(now-state.last_event),
    double(cpu-state.engine_cpu_event)/1e4,frames-state.presenter_frames_event,state.loading.load(),
    edf::native::NativeLoadTrace::Format(sample,state.at_event));
  state.last_event=now; state.at_event=sample; state.engine_cpu_event=cpu; state.presenter_frames_event=frames;
  if(close && state.loading.load()) {
    REXLOG_INFO("Native load trace: window closed by {} wall_ms={:.1f} engine_cpu_ms={:.1f} presenter_frames={} | {}",
      event,ms(now-state.window_start),double(cpu-state.engine_cpu_window)/1e4,frames-state.presenter_frames_window,
      edf::native::NativeLoadTrace::Format(sample,state.at_window));
    // The ten costliest mission script natives of the window (inclusive).
    std::vector<std::pair<uint64_t,size_t>> natives;
    for(size_t index=0;index<NativeLoadTraceState::kNatives;++index)
      if(const auto nanos=state.native_nanos[index].load(std::memory_order_relaxed)-state.native_nanos_window[index])
        natives.emplace_back(nanos,index);
    std::sort(natives.begin(),natives.end(),[](const auto& a,const auto& b) { return a.first>b.first; });
    std::string text;
    char item[96];
    for(size_t rank=0;rank<natives.size() && rank<10;++rank) {
      const auto index=natives[rank].second;
      std::snprintf(item,sizeof(item),"native%zu=%llu/%.1fms ",index,
        (unsigned long long)(state.native_calls[index].load(std::memory_order_relaxed)-state.native_calls_window[index]),
        double(natives[rank].first)/1e6);
      text+=item;
    }
    if(!text.empty()) REXLOG_INFO("Native load trace: window script natives (mission dispatcher, inclusive) {}",text);
    state.loading.store(false);
  }
  if(open && !state.loading.load()) {
    state.window_start=now; state.at_window=sample; state.engine_cpu_window=cpu; state.presenter_frames_window=frames;
    for(size_t index=0;index<NativeLoadTraceState::kNatives;++index) {
      state.native_calls_window[index]=state.native_calls[index].load(std::memory_order_relaxed);
      state.native_nanos_window[index]=state.native_nanos[index].load(std::memory_order_relaxed);
    }
    state.loading.store(true);
  }
  // Restart the tick clock at every event so a tick never repeats an event's span.
  state.last_tick=now; state.at_tick=sample; state.engine_cpu_tick=cpu; state.presenter_frames_tick=frames;
  state.next_tick.store((now+std::chrono::milliseconds(250)).time_since_epoch().count(),std::memory_order_relaxed);
}
// Called from frequent hooks (engine transition, presenter frame, model
// retirement): one relaxed load and a clock read unless a tick is due.
void LoadTraceTick() {
  if(!LoadTraceOn()) return;
  auto& state=LoadTraceState();
  const auto now=NativeLoadTraceState::Clock::now();
  if(now.time_since_epoch().count()<state.next_tick.load(std::memory_order_relaxed)) return;
  std::unique_lock lock(state.mutex,std::try_to_lock);
  if(!lock) return;
  if(!state.started) {
    state.started=true; state.origin=state.last_event=state.last_tick=now;
    state.at_event=state.at_tick=edf::native::NativeLoadTrace::Get().Sample();
    state.engine_cpu_event=state.engine_cpu_tick=LoadTraceEngineCpu(state);
  }
  if(now<state.last_tick+std::chrono::milliseconds(250)) return;
  const auto sample=edf::native::NativeLoadTrace::Get().Sample();
  const auto cpu=LoadTraceEngineCpu(state);
  const auto frames=state.presenter_frames.load(std::memory_order_relaxed);
  const bool loading=state.loading.load();
  if(loading || LoadTraceLoadingActivity(sample,state.at_tick)) {
    auto ms=[](auto duration) { return std::chrono::duration<double,std::milli>(duration).count(); };
    REXLOG_INFO("Native load trace: tick t_ms={:.1f} span_ms={:.1f} engine_cpu_ms={:.1f} presenter_frames={} loading={} | {}",
      ms(now-state.origin),ms(now-state.last_tick),double(cpu-state.engine_cpu_tick)/1e4,frames-state.presenter_frames_tick,
      loading,edf::native::NativeLoadTrace::Format(sample,state.at_tick));
  }
  state.last_tick=now; state.at_tick=sample; state.engine_cpu_tick=cpu; state.presenter_frames_tick=frames;
  state.next_tick.store((now+std::chrono::milliseconds(250)).time_since_epoch().count(),std::memory_order_relaxed);
}
// The loading presenter (clSatoCallback::slot6, 8216EBC0) runs on its own
// thread; its scene setups are counted as presenter frames.
thread_local constinit bool native_load_trace_presenter_thread=false;

namespace edf::native {
// Lock-free prefilters for hot simulation hooks (NativeAddressFilter): list
// headers and member nodes the static walk plans or the scene membership
// track, and owners the scene sources have seen born. A hook whose addresses
// are absent skips the bridge lock, which the full frame's passes hold for
// milliseconds on the render thread. Leaked: hooks may run during shutdown.
NativeAddressFilter& SceneAnchors() { static auto* value=new NativeAddressFilter; return *value; }
NativeAddressFilter& SceneSourceOwners() { static auto* value=new NativeAddressFilter; return *value; }
// The adapter's per-world group orders and pass animations as last written
// (native_world_publication_mirror.h): the 820B4250 post-hook's lock-free
// proof that its publication is a no-op. Leaked like the filters above.
NativeWorldPublicationMirror& WorldPublications() { static auto* value=new NativeWorldPublicationMirror; return *value; }
std::function<std::array<int32_t,2>()>& NativeDisplaySizeProvider() {
  static std::function<std::array<int32_t,2>()> provider;
  return provider;
}
void SetNativeDisplaySizeProvider(std::function<std::array<int32_t,2>()> provider) {
  NativeDisplaySizeProvider()=std::move(provider);
}
std::array<int32_t,2> NativeDisplaySize() {
  if(const auto& provider=NativeDisplaySizeProvider()) try {
    const auto size=provider();
    if(size[0]>0 && size[1]>0) return size;
  } catch(const std::exception& error) { REXLOG_WARN("Native display size provider: {}",error.what()); }
  return {REXCVAR_GET(window_width),REXCVAR_GET(window_height)};
}
// The 2D canvas layout for a target. edf_hud_safe_area is read per draw: it
// is a constant mapping, not an allocation, so it applies live.
NativeClipAffine NativeHudLayout(uint32_t width,uint32_t height) {
  if(!NativeRenderDimensions()[0]) return {};
  return NativeHudClipAffine(width,height,ParseNativeHudSafeArea(REXCVAR_GET(edf_hud_safe_area)));
}
// A scissor already in legacy canvas form (ScaleNativeCanvasScissor), mapped
// through a layout affine; identity leaves it untouched.
NativeViewportState MapNativeCanvasViewportScissor(NativeViewportState state,const NativeClipAffine& affine,
                                                   uint32_t width,uint32_t height) {
  if(affine.identity()) return state;
  const auto mapped=MapNativeCanvasPixelRect({int32_t(state.scissor.left),int32_t(state.scissor.top),
    int32_t(state.scissor.right),int32_t(state.scissor.bottom)},affine,width,height);
  state.scissor={LONG(mapped[0]),LONG(mapped[1]),LONG(mapped[2]),LONG(mapped[3])};
  return state;
}
namespace {
// Extra simulation steps folded into one engine update. This is diagnostic
// bookkeeping only; the retail result and clock writeback remain unchanged.
std::atomic<uint64_t>& FrameExtraSimulationSteps() {
  static std::atomic<uint64_t> total{0};
  return total;
}

template<class Reader>
NativeViewportState ReadDrawViewport(const Reader& reader,uint32_t device) {
  return DecodeDrawViewport(ReadViewportWords(reader,device));
}
template<class Reader>
void BindGuestRenderState(const NativeRenderState& state,ID3D11DeviceContext& context,
                          const Reader& reader,uint32_t device,uint64_t* generation=nullptr) {
  // Every path that binds render state comes through here, so this is where a
  // skip elsewhere has to be invalidated from. Same for BindActiveTarget.
  // Passed in rather than fetched, because this is a template defined before
  // the bridge state is.
  if(generation) ++*generation;
  if(!state.requires_blend_factor) { state.Bind(context); return; }
  if(!REXCVAR_GET(edf_native_owned_render_state) && !REXCVAR_GET(edf_native_render_state_audit)) {
    state.Bind(context,ReadBlendFactor(reader,device)); return;
  }
  NativeRenderStateSnapshots::BlendWords live{};
  const bool audit=REXCVAR_GET(edf_native_render_state_audit);
  if(audit) live=ReadGuestWords<4>(reader,reader.Add(device,10336));
  state.Bind(context,ResolveBlendFactorForDraw(device,audit?&live:nullptr));
}
// State().mutex for one visibility walk; see native_scene_walk_lock.h.
using BridgeWalkLock=NativeWalkLockScope<BridgeMutex>;
using BridgeGuestCall=NativeWalkGuestCall<BridgeMutex>;
// The current tree walk's camera view, shared with the list gathers it runs
// under its own scope: one read per walk, again only after a guest call.
struct BridgeWalkView {
  const BridgeWalkLock* scope; uint32_t context;
  NativeGuestCallCached<NativeSceneVisibilityView> view;
  // The walk's page admissions, shared by its node reads and every list's
  // gather. Keyed to this thread's guest call count: a guest call drops them.
  const NativeSceneCpuWindow<GuestReader>* window=nullptr;
};
inline thread_local BridgeWalkView* bridge_walk_view=nullptr;
}  // namespace (shared: bridge/bridge_shared.h)
// Called under the registry lock at consumption, never from a writer callback.
void AuditGeneratedWrites(Bridge& state,const NativeBufferWrites::Batch& batch) {
  for(size_t i=0;i<batch.writer_hit_count;++i) {
    const auto& hit=batch.writer_hits[i];
    REXLOG_INFO("Native geometry writer site: file={}, line={}, provider={:#x}, caller={:#x}, owner={:#x}, lifetime={}, calls={}, first_physical={:#x}, first_bytes={} (owner at notification; caller is pre-call LR; observed workload only)",
      hit.site.file?hit.site.file:"",hit.site.line,hit.site.provider,hit.site.caller,
      hit.owner,hit.lifetime,hit.calls,hit.first_range.address,hit.first_range.bytes);
  }
  if(batch.writer_hits_omitted)
    REXLOG_WARN("Native geometry writer inventory incomplete: omitted_hits={} (64 site/owner/lifetime keys per batch)",batch.writer_hits_omitted);
  if(!batch.generated_calls && !batch.provider_exact_owner_hits) return;
  static uint64_t batches=0,reported_overlaps=0;
  static uint64_t exact_batches=0,total_generated_hits=0,total_provider_hits=0;
  total_generated_hits+=batch.generated_exact_owner_hits;
  total_provider_hits+=batch.provider_exact_owner_hits;
  const bool exact_hit=batch.generated_exact_owner_hits || batch.provider_exact_owner_hits;
  if(exact_hit) ++exact_batches;
  size_t overlaps=0;
  for(size_t i=0;i<batch.subscribed_sample_count;++i) {
    const auto& sample=batch.subscribed_samples[i];
    state.model_buffers.VisitPhysicalOverlaps(sample.address,sample.bytes,[&](uint32_t owner,const auto& buffer) {
      ++overlaps;
      if(reported_overlaps++<16)
        REXLOG_INFO("Native generated write overlap: physical={:#x}, bytes={}, owner={:#x}, generation={}, kind={} (sampled; owner at drain, not writer PC)",
          sample.address,sample.bytes,owner,buffer.generation,buffer.kind==NativeModelBuffers::Kind::Vertex?"vertex":"index");
    });
  }
  ++batches;
  if(batches<=8 || (batches & (batches-1))==0)
    REXLOG_INFO("Native generated write audit: batch={}, generated_calls={}, provider_calls={}, samples={}, omitted={}, sample_owner_overlaps={}, all={}, pages={} (bounded samples; no completeness claim)",
      batches,batch.generated_calls,batch.provider_calls,batch.generated_sample_count,
      batch.generated_calls-batch.generated_sample_count,overlaps,batch.all,batch.pages?batch.pages->count():0);
  if(batches<=8 || (batches & (batches-1))==0)
    REXLOG_INFO("Native subscribed write audit: batch={}, generated_page_hits={}, retained={}, omitted={}, unknown={} (page subscriptions at notification; exact owners queried at drain)",
      batches,batch.generated_subscribed_calls,batch.subscribed_sample_count,
      batch.generated_subscribed_calls-batch.subscribed_sample_count,batch.subscriptions_unknown);
  if((exact_hit && (exact_batches<=16 || (exact_batches & (exact_batches-1))==0)) || batches<=8 || (batches & (batches-1))==0)
    REXLOG_INFO("Native exact write versions: batch={}, generated_owner_hits={}, provider_owner_hits={}, total_generated_hits={}, total_provider_hits={} (unsampled overlap counts at notification, not writer PCs)",
      batches,batch.generated_exact_owner_hits,batch.provider_exact_owner_hits,total_generated_hits,total_provider_hits);
}
namespace {
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
// FSR's end of an armed frame (NativeFsrBridgeState): the scene colour just
// resolved (render size, jittered), the sampled depth, motion vectors (B's,
// or zero) and the opaque-only copy go through GENERATEREACTIVEMASK and the
// upscaler, recorded here on the scene recorder, and the output texture is
// what owner+104 samples from now on. Nullopt (owner+104 keeps the plain
// resolve) when the frame is not armed for this scene or the dispatch fails.
std::optional<NativeTexture> DispatchNativeFsrLocked(Bridge& state,uint32_t owner,NativeScene& scene) {
  auto& fsr=state.fsr;
  if(!fsr.frame || fsr.owner!=owner) return std::nullopt;
  fsr.frame=false;
  if(!scene.color.sampled.content_valid || !scene.color.sampled.backend || !state.scene_backend) return std::nullopt;
  try {
    const auto width=scene.color.sampled.width,height=scene.color.sampled.height;
    const auto plan=PlanNativeFsrMotion(fsr.motion,fsr.camera);
    const auto reasons=fsr.resets.Next({fsr.helper_frame,width,height,scene.color.format,fsr.mode,NativeAbNativeSide(),
      plan.camera,plan.reset});
    const auto now=std::chrono::steady_clock::now();
    const float frame_ms=fsr.dispatched?std::chrono::duration<float,std::milli>(now-fsr.last_dispatch).count():16.6667f;
    fsr.last_dispatch=now;
    NativeFsrDispatchInputs inputs;
    inputs.color=scene.color.sampled.backend.get();
    inputs.depth=scene.depth.backend_target.get();
    inputs.motion=plan.motion;
    inputs.opaque=fsr.opaque;
    inputs.jitter=fsr.jitter;
    // Workstream B's vectors are UV offsets (current to previous position);
    // this scales them to render pixels, as FSR takes them.
    inputs.motion_scale={float(width),float(height)};
    inputs.sharpness=float(REXCVAR_GET(edf_native_fsr_sharpness));
    inputs.frame_ms=frame_ms;
    inputs.reset=reasons!=0;
    inputs.camera=plan.camera;
    fsr.upscaler.Dispatch(*state.scene_backend,SceneRecorderLocked(state),inputs);
    // The raw pass left the recorder with no bindings (EndExternal).
    ++state.bind_generation;
    state.recorded={};
    ++fsr.dispatched;
    if(inputs.reset) ++fsr.history_resets;
    // Resets other than "no motion vectors" (which is every frame until
    // workstream B lands) are worth a line each, sparsely.
    static uint64_t logged_resets=0;
    if((reasons&~kNativeFsrResetMotion) && (++logged_resets<=8 || !(logged_resets&(logged_resets-1))))
      REXLOG_INFO("FSR history reset: {} (frame={} count={})",NativeFsrResetTracker::Describe(reasons),fsr.helper_frame,logged_resets);
    if(fsr.dispatched<=4 || fsr.dispatched%1000==0)
      REXLOG_INFO("FSR dispatch: count={} {}x{} jitter=({:.4f},{:.4f})px phase={}/{} reset={} reactive={} motion={} near={} far={} fov_y={:.4f}{} sharpness={:.2f} dropped={} failures={}",
        fsr.dispatched,width,height,inputs.jitter.pixel_x,inputs.jitter.pixel_y,fsr.index,fsr.upscaler.JitterPhaseCount(),
        NativeFsrResetTracker::Describe(reasons),inputs.opaque,plan.motion?"workstream_b":"zero",plan.camera.near_plane,
        plan.camera.far_plane,plan.camera.fov_y,plan.camera.derived?"":" (defaults)",inputs.sharpness,fsr.dropped,fsr.failures);
    for(const auto& message:DrainNativeFsrMessages()) {
      static uint64_t messages=0;
      if(++messages<=32) REXLOG_WARN("FidelityFX: {}",message);
    }
    NativeTexture output;
    output.backend=fsr.upscaler.output();
    output.width=width; output.height=height; output.mip_count=1;
    output.format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    output.content_valid=true;
    return output;
  } catch(const std::exception& error) {
    fsr.resets.Forget();
    if(++fsr.failures<=8 || !(fsr.failures&(fsr.failures-1)))
      REXLOG_ERROR("FSR dispatch failed, the plain resolve is used: {} (failures={})",error.what(),fsr.failures);
    for(const auto& message:DrainNativeFsrMessages()) REXLOG_WARN("FidelityFX: {}",message);
    return std::nullopt;
  }
}
}  // namespace (shared: bridge/bridge_shared.h)
void ResolveScene(const GuestReader& reader,Bridge& state,uint32_t owner) {
  try {
    auto& scene=state.scenes.at(owner);
    const auto handle=reader.Word(reader.Add(owner,104));
    const auto creation=state.texture_creations.find(handle);
    if (!handle || creation==state.texture_creations.end() || creation->second.format!=0x1a22ab60 ||
        creation->second.width!=scene.color.sampled.width || creation->second.height!=scene.color.sampled.height)
      throw std::runtime_error("scene resolve destination is not the expected full-frame HDR texture");
    ID3D11ShaderResourceView* empty[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
    if(state.context) {
      state.context->PSSetShaderResources(0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,empty);
      state.context->VSSetShaderResources(0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,empty);
    }
    // Through the recorder, always: a converting target's resolve is a draw,
    // and on the adopted D3D11 backend the recorder issues straight to the same
    // immediate context, so the ordering against the direct paths is exact.
    ResolveNativeRenderTarget(SceneRecorderLocked(state),scene.color);
    // A converting resolve is a draw and binds its own targets, so whatever
    // this code last bound - on the context or on the recorder - is no longer
    // what is set.
    ++state.bind_generation;
    state.recorded={};
    // An FSR frame's post samples the upscaled image instead (the same
    // extent: native AA). Unarmed, this is the plain resolve as before.
    if(auto upscaled=DispatchNativeFsrLocked(state,owner,scene)) state.textures.insert_or_assign(handle,std::move(*upscaled));
    else state.textures.insert_or_assign(handle,scene.color.sampled);
    if (++state.scene_resolves<=5 || state.scene_resolves%1000==0)
      REXLOG_INFO("Native HDR scene resolve: count={}, texture={:#x}, initialized={}, frame_complete={}",
        state.scene_resolves,handle,scene.color.sampled.content_valid,scene.frame_complete);
  } catch (const std::exception& error) {
    if (++state.scene_resolve_errors<=10) REXLOG_ERROR("Native HDR scene resolve: {}",error.what());
  }
}
namespace {

namespace {
// Fill in only what is resolved: a rejection before shader or declaration
// lookup still names a real contract, just a coarser one.
edf::native::NativeContract MakeNativeContract(Bridge& state,edf::native::NativeContractPath path,
    uint32_t vertex,uint32_t pixel,uint32_t declaration,uint32_t topology,
    uint32_t stride=0,uint32_t index_width=0) {
  edf::native::NativeContract contract;
  contract.path=path; contract.topology=topology;
  contract.stride=stride; contract.index_width=index_width;
  if(const auto found=state.shaders.find(vertex);found!=state.shaders.end() && found->second.bindings)
    contract.vertex_source=found->second.bindings->shader().source_fingerprint;
  if(const auto found=state.shaders.find(pixel);found!=state.shaders.end() && found->second.bindings)
    contract.pixel_source=found->second.bindings->shader().source_fingerprint;
  if(declaration) try {
    const auto owned=state.declarations.Get(declaration);
    contract.declaration=edf::native::HashNativeDeclaration(owned->bytes());
    contract.elements=owned->count();
    // Keep the bytes, not only the identity: the disc cannot supply them.
    state.contracts.RetainDeclaration(contract.declaration,owned->bytes());
  } catch(const std::exception&) { /* Unpublished declaration stays unresolved. */ }
  return contract;
}
void ReportNativeContractRejection(Bridge& state,const edf::native::NativeContract& contract,
    std::string_view reason) {
  state.contracts.set_limit(size_t(std::clamp(REXCVAR_GET(edf_native_contract_limit),1,1<<20)));
  if(!state.contracts.RecordRejected(contract,reason)) return;
  REXLOG_WARN("Native contract rejected: path={}, VS_source={:#x}, PS_source={:#x}, declaration={:#x}, elements={}, topology={}, stride={}, index_width={}, reason={} (this draw is missing from the frame)",
    edf::native::NativeContractPathName(contract.path),contract.vertex_source,contract.pixel_source,
    contract.declaration,contract.elements,contract.topology,contract.stride,contract.index_width,reason);
}
}
template<class Reader>
void UploadParameters(const Reader& reader, const std::shared_ptr<const NativeMaterialParameters::Groups>& owned,
                      RegisteredShader& shader, uint32_t stage_offset,
                      ShaderBindings& bindings, Bridge& state, ShaderBindings* alternate=nullptr) {
  // sub_821BBAD8 constructs each stage's 36-byte group. Local named records:
  // [name, data, register_count, register_index]. Globals:
  // [value_vector, name, register_count, register_index]. The activation path
  // sub_821B8E48 dereferences value_vector+0 to obtain the live global data.
  for (bool global : {false, true}) {
    const size_t group=(stage_offset?2:0)+(global?1:0);
    const auto& plan=shader.ResolveUploads(owned,group,alternate!=nullptr);
    state.optimized_out+=plan.optimized_out;
    if(plan.parameters.empty()) continue;
    // Keep record windows and payloads live. Only shader layout decisions are
    // cached: material/global pointers may change on every activation.
    const auto record_base=owned->record_base[group];
    const auto record_bytes=owned->record_bytes[group];
    const std::array<ShaderBindings*,2> destinations{&bindings,alternate};
    const auto upload=[&](const auto& source) {
    for(const auto& entry:plan.parameters) {
      const auto& parameter=(*owned)[group][entry.index];
      const auto value=parameter.ReadValue(source,global);
      const auto& targets=entry.targets;
      const auto& sizes=entry.sizes;
      const auto maximum=entry.maximum;
      const bool canvas_xy=entry.canvas_xy;
      // Global vector capacity is live even though required native sizes are
      // immutable. Check it before reading any payload, as on the old path.
      if(global && (value.available>4096 || maximum>size_t(value.available)*16))
        throw std::runtime_error("native global constant exceeds owned vector: "+parameter.name);
      const auto* data=source.Bytes(value.data,maximum);
      if(REXCVAR_GET(edf_native_instance_motion_trace)>0 && !stage_offset && parameter.name=="g_mWorldArray") {
        // Palette storage may be shared scratch rewritten between draws. Track
        // both between-frame and within-frame changes before choosing a key.
        struct PaletteSample {
          uint64_t frame=0,hash=0,samples=0,changes=0,within_frame=0;
          uint32_t registers=0;
        };
        static std::map<std::pair<uint32_t,uint32_t>,PaletteSample> samples;
        static uint64_t start_frame=0;
        static bool finished=false;
        if(!finished) {
          if(!start_frame) start_frame=state.scene_frames;
          const auto frames=REXCVAR_GET(edf_native_instance_motion_trace);
          if(state.scene_frames-start_frame>=uint64_t(frames)) {
            for(const auto& [key,sample]:samples)
              REXLOG_INFO("Native palette motion: record={:#x} data={:#x} registers={} samples={} changes={} same_frame_changes={}",
                key.first,key.second,sample.registers,sample.samples,sample.changes,sample.within_frame);
            REXLOG_INFO("Native palette motion: complete frames={} sources={}",frames,samples.size());
            finished=true; samples.clear();
          } else {
            const auto key=std::make_pair(parameter.record,value.data);
            auto found=samples.find(key);
            if(found==samples.end() && samples.size()<64) {
              found=samples.emplace(key,PaletteSample{}).first;
              found->second.registers=parameter.registers;
              REXLOG_INFO("Native palette identity: record={:#x} data={:#x} registers={} first={} global={} bytes={}",
                parameter.record,value.data,parameter.registers,parameter.first,global,maximum);
            }
            if(found!=samples.end()) {
              auto& sample=found->second;
              uint64_t hash=14695981039346656037ull;
              for(size_t i=0;i<maximum;++i) { hash^=data[i]; hash*=1099511628211ull; }
              if(!sample.samples || sample.frame!=state.scene_frames) {
                if(sample.samples && sample.hash!=hash) ++sample.changes;
                ++sample.samples; sample.frame=state.scene_frames;
              } else if(sample.hash!=hash) ++sample.within_frame;
              sample.hash=hash;
            }
          }
        }
      }
      std::array<uint8_t,16> canvas_data{};
      if(canvas_xy && NativeRenderDimensions()[0]>0 && maximum==16) {
        // Kept raw for the Utility draw's per-target canvas layout.
        const bool offset=parameter.name=="_g_DX2DOffset";
        std::memcpy(offset?shader.canvas_offset.data():shader.canvas_scale.data(),data,16);
        shader.canvas_uploaded|=offset?2:1;
        canvas_data=ScaleNativeCanvasXY({data,16},float(NativeRenderDimensions()[0])/1280.0f,
          float(NativeRenderDimensions()[1])/720.0f);
        data=canvas_data.data();
      }
      for(size_t index=0;index<destinations.size();++index) if(targets[index]) {
        if(destinations[index]->SetGuestFloatRegisters(*targets[index],{data,sizes[index]})) ++state.parameter_uploads;
        else ++state.optimized_out;
      }
    }
    };
    // Windowing the scattered payload reads as well was measured and did not
    // pay: the pixel stage improved 14% but the vertex stage lost 5% to the
    // extra pass, and activation.native did not move outside the 5% run-to-run
    // drift of the untouched phases. Not worth a second pass and a fallback in
    // this routine.
    if(record_base && record_bytes)
      upload(edf::native::GuestReadWindow(reader,record_base,record_bytes));
    else upload(reader);
  }
}
// `samplers` reads the device's sampler block; it is windowed by the caller
// because these words lie outside the material, and reading them through the
// material's window fell back to a validated read per texture per activation.
template<class Reader,class SamplerReader>
void UploadTextures(const Reader& reader, const SamplerReader& samplers,
                    const std::shared_ptr<const NativeMaterialParameters::Groups>& owned,
                    RegisteredShader& shader, uint32_t device, ShaderBindings& bindings, Bridge& state) {
  bindings.BeginResourceUpdate();
  for (bool global : {false, true}) {
    const auto& resolved=shader.ResolveTextures(owned,global?1:0);
    size_t index=0;
    for(const auto& parameter:owned->textures[global?1:0]) {
      const auto& target=resolved[index++];
      const auto value=parameter.ReadValue(reader,global);
      const auto& name=parameter.name;
      const auto handle=value.handle;
      const auto found = state.textures.find(handle);
      const std::shared_ptr<NativeBackendTexture> missing_texture;
      const auto& texture = found == state.textures.end() || !found->second.content_valid
        ? missing_texture : found->second.backend;
      auto* view = texture.get();
      if (!bindings.TrySetTexture(target, texture)) {
        // Guest material records describe Xbox compiler usage. A native entry
        // can optimize a combined sampler out; it then has neither binding.
        // Do not let an unused record erase the material's remaining textures.
        if (bindings.TrySetSampler(target,nullptr))
          throw std::runtime_error("native combined sampler has no texture binding: " + name);
        continue;
      }
      // Read after the original activation has applied named filtering and
      // texture mip limits. Inline engine address changes are already present.
      edf::native::HookTiming sampler_timing(edf::native::HookPhase::ActivationSamplerWords);
      const auto key=NativeFilteringKey(ReadSamplerWords(samplers,device,value.slot),
                                        REXCVAR_GET(edf_native_anisotropic_filtering));
      auto cached = state.samplers.find(key);
      sampler_timing.Finish();
      if (cached == state.samplers.end()) {
        // Decoded by the shared guest decoder, not by a D3D11-shaped one, so a
        // second backend cannot filter this material differently.
        const auto desc = DecodeNativeGuestSampler(key);
        cached = state.samplers.emplace(key,&EnsureSceneBackendLocked(state).CreateSampler(desc)).first;
        REXLOG_INFO("Native sampler: cached={}, address={}/{}/{}, filter={}/{}/{}, lod={}..{}, bias={}, anisotropy={}",
                    state.samplers.size(),uint32_t(desc.u),uint32_t(desc.v),uint32_t(desc.w),
                    uint32_t(desc.min),uint32_t(desc.mag),uint32_t(desc.mip),
                    desc.min_lod,desc.max_lod,desc.mip_lod_bias,desc.max_anisotropy);
      }
      if(!bindings.TrySetSampler(target,cached->second))
        throw std::runtime_error("unknown sampler: "+name);
      ++state.sampler_bindings;
      if (handle && !view) {
        ++state.texture_missing;
        if (state.texture_missing <= 10)
          REXLOG_INFO("Native texture bridge: pending resource name={}, handle={:#x}", name, handle);
        if (state.texture_missing <= 10) {
          const auto creation = state.texture_creations.find(handle);
          if (creation != state.texture_creations.end()) {
            const auto& c = creation->second;
            REXLOG_INFO("Native texture bridge: pending allocation {}x{}x{}, levels={}, usage={:#x}, format={:#x}, pool={}, type={}, caller={:#x}",
                        c.width, c.height, c.depth, c.levels, c.usage, c.format, c.pool, c.type, c.caller);
          }
        }
      } else ++state.texture_bindings;
    }
  }
  bindings.EndResourceUpdate();
}
// Whether this shader samples the target it is drawing into.
//
// What a shader can actually sample is the resolved texture, or the surface
// itself when the target is one of the converting ones that declares it
// sampled. Both are checked; neither is a D3D11 pointer, because this used to
// compare those and a backend without them made every answer yes.
bool SamplesTarget(const ShaderBindings& shader,const NativeRenderTarget& target) {
  if(target.sampled.backend && shader.UsesTexture(*target.sampled.backend)) return true;
  if(target.backend_surface)
    if(auto* surface=target.backend_surface->texture())
      if(shader.UsesTexture(*surface)) return true;
  return false;
}
}  // namespace (shared with the full frame: bridge/bridge_helpers.h)
ActiveTargets ActiveTargetsLocked(Bridge& state) {
  ActiveTargets result;
  const auto found=state.render_targets.find(state.active_target);
  if(found==state.render_targets.end()) {
    const auto scene=state.scenes.find(state.active_scene);
    if(!state.active_target && scene!=state.scenes.end()) {
      auto& colour=scene->second.color;
      auto& depth=scene->second.depth;
      result.colors[result.count]=colour.backend_surface.get();
      result.rtv_format[result.count]=colour.format;
      ++result.count;
      result.depth=depth.backend_target.get();
      result.dsv_format=depth.format;
      result.samples=colour.samples;
      result.width=colour.sampled.width; result.height=colour.sampled.height;
      return result;
    }
    if(!state.active_target && !state.active_scene && state.scenes.contains(state.active_output)) {
      auto& output=state.scenes.at(state.active_output).output;
      result.colors[result.count]=output.backend_surface.get();
      result.rtv_format[result.count]=output.format;
      ++result.count;
      result.samples=output.samples;
      result.width=output.sampled.width; result.height=output.sampled.height;
    }
    return result;
  }
  auto& target=found->second.native;
  result.colors[result.count]=target.backend_surface.get();
  result.rtv_format[result.count]=target.format;
  ++result.count;
  result.samples=target.samples;
  result.width=target.sampled.width; result.height=target.sampled.height;
  return result;
}
namespace {
struct NativeQueuedSceneGroup {
  NativeSceneExecution execution;
  std::optional<GuestViewportWords> pass_viewport;
  NativeSceneMaterialHandoff material_handoff;
  NativeSceneGeometryHandoff geometry_handoff;
  NativeSceneGeometryInstallState geometry_install;
  bool material_compatibility_only=true;
  bool published_activation_used=false,all_native_draws=true;
  uint32_t activation_instance=0;
  std::shared_ptr<const NativeSceneGroupMaterial> published_material;
  std::vector<NativeSceneMaterialInputs::Constant> pass_constants;
  std::optional<NativeMaterialRenderPass> material_pass;
  std::array<NativeMaterialSamplerPass,16> sampler_pass{};
  bool material_audited=false;
  std::vector<std::shared_ptr<const NativeSceneInstance>> objects;
  std::shared_ptr<const NativeSceneMaterial> material;
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
  bool reverse_depth=false;
  uint32_t vertex=0,pixel=0;
  std::optional<VertexParameterRange> world_parameter;
  bool world_column_major=false,constants_clean=false,population_safe=true;
  std::optional<std::array<uint8_t,64>> pending_world;
  NativeSceneView view;
  ActiveTargets targets;
  uint32_t instance=0;
};
thread_local NativeQueuedSceneGroup* native_queued_scene_group=nullptr;
void EnterNativeSceneBoundary(NativeSceneBoundary boundary) {
  if(native_queued_scene_group)
    native_queued_scene_group->execution.Enter(boundary,REXCVAR_GET(edf_native_scene_reject_compatibility));
}
}  // namespace (shared: bridge/bridge_shared.h)
thread_local constinit NativeMaterialPassCursor* native_material_pass_cursor=nullptr;
thread_local constinit NativeSceneQueues* native_scene_queues=nullptr;
thread_local std::shared_ptr<const NativeScenePublication> native_scene_publication;
thread_local constinit std::optional<NativeScenePassCamera> native_scene_pass_camera;
thread_local std::shared_ptr<const NativeScenePassCameras> native_scene_pass_cameras;
thread_local constinit std::optional<NativeScenePassAnimation> native_scene_pass_animation;
thread_local std::shared_ptr<const NativeSceneAdapter::WorldAnimations> native_scene_pass_animations;
thread_local constinit uint32_t native_scene_animation_owner=0;
// FSR (native_fsr.h): while a full-frame view is jittered, the pass camera
// with the jitter in its projection and view*projection, for the draws that
// take their camera constants from a NativeScenePassCamera (the effect and
// map-effect activations). native_scene_pass_camera itself stays unjittered:
// the static world's reuse keys, per-group camera constants and camera plan,
// the culls, the effects' eye and the guest view globals all read it. The
// renderer-drawn passes get the same jitter through
// NativeSceneRenderer::SetClipJitter. Null (no jitter) outside such a view.
thread_local constinit std::optional<NativeScenePassCamera> native_scene_draw_camera;
thread_local constinit NativeFsrJitter native_scene_view_jitter;
NativeFsrMode NativeFsrRequestedMode() {
  const std::string text=REXCVAR_GET(edf_native_fsr);
  if(const auto mode=ParseNativeFsrMode(text)) return *mode;
  static std::atomic<bool> reported=false;
  if(!reported.exchange(true))
    REXLOG_WARN("edf_native_fsr={} is not a mode (off, native_aa, quality, balanced, performance, ultra_performance); FSR stays off",text);
  return NativeFsrMode::Off;
}
namespace {
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
}  // namespace (shared with the full frame: bridge/bridge_helpers.h)
// Arms this helper call's frame for FSR on `renderer`'s scene (its first
// accepted view, locks held) and picks the frame's jitter: false, and no
// jitter anywhere, when FSR is off, stood aside, or cannot run.
bool ArmNativeFsrFrameLocked(Bridge& state,uint32_t renderer,NativeScene& scene,uint64_t helper_frame) {
  auto& fsr=state.fsr;
  if(fsr.frame) { ++fsr.dropped; fsr.frame=false; }
  fsr.opaque=false; fsr.motion={};
  const auto mode=NativeFsrRequestedMode();
  if(mode==NativeFsrMode::Off) {
    if(fsr.mode!=NativeFsrMode::Off) {
      REXLOG_INFO("FSR off (dispatched={} dropped={} failures={})",fsr.dispatched,fsr.dropped,fsr.failures);
      fsr.resets.Forget();
    }
    fsr.mode=NativeFsrMode::Off;
    return false;
  }
  if(const auto* excluded=NativeFsrExcludedBy(NativeFsrCurrentExclusions())) {
    static std::atomic<bool> reported=false;
    if(!reported.exchange(true)) REXLOG_WARN("FSR disabled: {} is set (validation runs compare unjittered frames)",excluded);
    return false;
  }
  if(scene.samples!=1 || !scene.depth.backend_target || !scene.depth.backend_target->texture()) {
    static std::atomic<bool> reported=false;
    if(!reported.exchange(true))
      REXLOG_WARN("FSR needs a restart: the scene has {} sample(s) and {} depth; edf_native_fsr on at startup makes them 1x and sampled",
        scene.samples,scene.depth.backend_target && scene.depth.backend_target->texture()?"sampled":"unsampled");
    return false;
  }
  if(!state.scene_backend) return false;
  if(NativeFsrEffectiveMode(mode)!=mode) {
    static std::atomic<bool> reported=false;
    if(!reported.exchange(true))
      REXLOG_WARN("edf_native_fsr={} runs as native_aa: only native AA is implemented",NativeFsrModeName(mode));
  }
  const auto width=scene.color.sampled.width,height=scene.color.sampled.height;
  std::string error;
  if(!fsr.upscaler.Prepare(*state.scene_backend,width,height,scene.color.format,&error)) {
    static uint64_t failures=0;
    if(++failures<=4 || !(failures&(failures-1))) REXLOG_WARN("FSR unavailable: {} (count={})",error,failures);
    for(const auto& message:DrainNativeFsrMessages()) REXLOG_WARN("FidelityFX: {}",message);
    return false;
  }
  if(fsr.upscaler.recreated())
    REXLOG_INFO("FSR context: {}x{} provider={} flags=HDR|DEPTH_INVERTED|AUTO_EXPOSURE jitter_phases={} ({}) gpu_memory={:.1f}MB contexts={} retired={}",
      width,height,fsr.upscaler.provider().empty()?"?":fsr.upscaler.provider(),fsr.upscaler.JitterPhaseCount(),
      fsr.upscaler.jitter_from_ffx()?"ffx":"local",double(fsr.upscaler.stats().context_bytes)/1048576.0,fsr.upscaler.stats().contexts,fsr.upscaler.stats().retired);
  const auto phases=(std::max)(1,fsr.upscaler.JitterPhaseCount());
  fsr.index%=phases;
  fsr.jitter=MakeNativeFsrJitter(fsr.upscaler.JitterOffset(fsr.index),width,height);
  fsr.index=(fsr.index+1)%phases;
  fsr.mode=mode; fsr.frame=true; fsr.owner=renderer; fsr.helper_frame=helper_frame;
  ++fsr.armed;
  return true;
}
// An armed frame nothing resolved (a direct-frame publication, a failed post):
// dropped, so no later resolve dispatches with its jitter.
void DisarmNativeFsrLocked(Bridge& state) {
  if(!state.fsr.frame) return;
  state.fsr.frame=false;
  if(const auto dropped=++state.fsr.dropped;dropped<=4 || !(dropped&(dropped-1)))
    REXLOG_INFO("FSR frame not dispatched: nothing resolved the scene to owner+104 (direct frame or post failure; dropped={})",dropped);
}
const NativeSceneSources& NativeSceneSourcesForPass(const Bridge& state) {
  if(EDF_NATIVE_FLAG(scene_sources_owned) && native_scene_publication && native_scene_publication->sources) {
    static uint64_t reads=0;
    if(++reads<=4 || reads%100000==0)
      REXLOG_INFO("Native published source reads: count={} tick={}",reads,native_scene_publication->snapshot->tick);
    return *native_scene_publication->sources;
  }
  return state.scene_sources;
}
namespace {
// The immutable source generation this pass reads, or null when the pass reads
// the producer's current sources (under state.mutex) instead. Taken once per
// walk: a published generation needs no lock, and cannot change within a pass.
std::shared_ptr<const NativeSceneSources> PublishedNativeSceneSourcesForPass(const Bridge& state) {
  if(!EDF_NATIVE_FLAG(scene_sources_owned) || !native_scene_publication || !native_scene_publication->sources) return {};
  NativeSceneSourcesForPass(state);
  return native_scene_publication->sources;
}
// source is sources.Find(instance), already looked up by the caller.
template<class Reader>
void ReadNativeSceneInstanceParameters(const Reader& reader,const NativeSceneSources::Source* source,
    uint32_t instance,std::vector<InstanceParameter>& parameters) {
  if(!REXCVAR_GET(edf_native_scene_instance_owned) || !source || !source->world_first) {
    ReadInstanceParameters(reader,instance,parameters); return;
  }
  parameters.assign(1,InstanceParameter{source->world_data,*source->world_first,4});
  if(REXCVAR_GET(edf_native_scene_transform_audit)) {
    const auto actual=ReadInstanceParameters(reader,instance);
    if(actual!=parameters) {
      REXLOG_ERROR("Native instance metadata mismatch: instance={:#x} owner={:#x}",instance,source->owner);
      throw std::runtime_error("published world-only instance metadata changed without a source event");
    }
  }
  static uint64_t count=0;
  if(++count<=4 || count%100000==0)
    REXLOG_INFO("Native owned instance metadata: reads={} audited={}",count,REXCVAR_GET(edf_native_scene_transform_audit));
}
template<class Reader>
void ReadNativeSceneInstanceParameters(const Reader& reader,const NativeSceneSources& sources,
    uint32_t instance,std::vector<InstanceParameter>& parameters) {
  ReadNativeSceneInstanceParameters(reader,sources.Find(instance),instance,parameters);
}
// source is sources.Find(instance). Owned metadata without the transform
// audit is exactly the source generation's (ReadNativeSceneInstanceParameters
// would only copy it), so it is decided from the source without building the
// parameter list; anything else reads the parameters as before.
template<class Reader>
bool NativeStaticWorldOnly(const Reader& reader,const NativeSceneSources::Source& source,
                          uint32_t instance,uint32_t first,uint32_t device) {
  if(!source.world_data) return false;
  if(REXCVAR_GET(edf_native_scene_instance_owned) && source.world_first && !REXCVAR_GET(edf_native_scene_transform_audit))
    return NativeStaticWorldOnlySource(source,first,device);
  static thread_local std::vector<InstanceParameter> parameters;
  ReadNativeSceneInstanceParameters(reader,&source,instance,parameters);
  return parameters.size()==1 && parameters[0].count==4 && parameters[0].first==first &&
    parameters[0].data==source.world_data &&
    !(uint64_t(parameters[0].data)<uint64_t(device)+5888 && uint64_t(parameters[0].data)+64>uint64_t(device)+1792);
}
template<class Reader>
bool NativeStaticWorldOnly(const Reader& reader,const NativeSceneSources& sources,
                          uint32_t instance,uint32_t first,uint32_t device) {
  const auto* source=sources.Find(instance);
  return source && NativeStaticWorldOnly(reader,*source,instance,first,device);
}
std::shared_ptr<const NativeSceneInstance> SelectNativeSceneInstanceLocked(Bridge& state,uint64_t id) {
  const auto current=state.scene_adapter.SelectOne(id);
  const auto published=native_scene_publication?native_scene_publication->Find(id):nullptr;
  if(published && published->object==current->object) {
    ++state.scene_published_selections;
    return published;
  }
  ++state.scene_current_selections;
  return current;
}
bool TryAppendNativeQueuedSceneInstance(uint8_t* base,uint32_t device,uint32_t instance,NativeQueuedSceneGroup& group);
bool TryAppendPublishedNativeSceneInstance(uint8_t* base,uint32_t device,uint32_t address,uint32_t instance,uint32_t count,NativeQueuedSceneGroup& group);
void SynchronizeNativeQueuedSceneInstance(uint8_t* base,uint32_t device,NativeQueuedSceneGroup& group);
void ConfigureNativeQueuedWorldLocked(Bridge& state,NativeQueuedSceneGroup& group) {
  group.world_parameter.reset(); group.constants_clean=false;
  size_t matrices=0;
  for(const auto& constant:group.material->constants()) for(const auto& matrix:constant.matrices)
    if(matrix.source==NativeSceneMatrixSource::World) {
      if(constant.stage!=NativeBackendStage::Vertex) return;
      ++matrices; group.world_column_major=matrix.column_major;
    }
  if(matrices!=1) return;
  if(group.published_material) {
    const auto& definition=group.published_material->program->inputs;
    const auto first=definition.WorldRegisterFirst();
    if(!first) return;
    auto& shader=state.shaders.at(definition.vertex);
    if(!shader.reversed_bindings) return;
    auto normal=shader.bindings->ResolveFloatRegisters("g_mWorld");
    auto reversed=shader.reversed_bindings->ResolveFloatRegisters("g_mWorld");
    if(normal.bytes()!=64 || reversed.bytes()!=64) return;
    group.world_parameter=VertexParameterRange{"g_mWorld",*first,4,std::move(normal),std::move(reversed)};
    return;
  }
  if(!state.active_vertex_parameters) return;
  for(const auto& parameter:*state.active_vertex_parameters)
    if(parameter.name=="g_mWorld" && parameter.count==4 && parameter.normal.bytes()==64 && parameter.reversed.bytes()==64) {
      for(const auto& other:*state.active_vertex_parameters)
        if(&other!=&parameter && other.first<parameter.first+4 && other.first+other.count>parameter.first) return;
      group.world_parameter=parameter; return;
    }
}
void FlushNativeQueuedSceneLocked(Bridge& state,NativeQueuedSceneGroup& group) {
  if(group.objects.empty()) return;
  auto& recorder=SceneRecorderLocked(state);
  recorder.SetRenderTargets({group.targets.colors.data(),group.targets.count},group.targets.depth);
  const auto statistics=group.execution.RecordBatch([&] {
    // Keep pending objects until the complete batch has been recorded. A
    // partial GPU recording cannot be rolled back or safely repeated.
    state.scene_recorded_snapshots.push_back(NativeSceneSnapshot{0,group.objects});
    return state.scene_renderer.Render(*state.scene_backend,state.scene_recorded_snapshots.back(),group.view,1);
  });
  state.scene_native_objects+=statistics.visible; state.scene_native_draws+=statistics.draws;
  group.objects.clear();
  // The batch bound its own targets and pipelines, like a resolve: neither the
  // recorder cache nor the indexed skip may compare against what came before.
  ++state.bind_generation;
  state.recorded={};
}
// render_states[key], created on first use, as the UI draw paths look it up:
// a std::map search of array keys per draw, where consecutive draws almost
// always ask for the key they asked for last.
NativeRenderState& RenderStateLocked(Bridge& state,const RenderStateWords& key) {
  auto& memo=state.render_state_memo;
  // Reuse off (native_reuse.h): the map is searched (the memo still records).
  if(NativeReuseAllowed()) {
    if(memo[0].value && memo[0].key==key) return *memo[0].value;
    if(memo[1].value && memo[1].key==key) { std::swap(memo[0],memo[1]); return *memo[0].value; }
  }
  auto found=state.render_states.find(key);
  if(found==state.render_states.end())
    found=state.render_states.emplace(key,CreateNativeRenderState(state.device.Get(),key)).first;
  memo[1]=memo[0]; memo[0]={key,&found->second};
  return found->second;
}
}  // namespace (shared: bridge/bridge_shared.h)
// samplers[key], created on the scene backend on first use; memoized likewise.
NativeBackendSampler* SamplerLocked(Bridge& state,const SamplerStateWords& key) {
  auto& memo=state.sampler_memo;
  if(NativeReuseAllowed()) {
    if(memo[0].value && memo[0].key==key) return memo[0].value;
    if(memo[1].value && memo[1].key==key) { std::swap(memo[0],memo[1]); return memo[0].value; }
  }
  auto found=state.samplers.find(key);
  if(found==state.samplers.end()) {
    const auto desc=DecodeNativeGuestSampler(key);
    found=state.samplers.emplace(key,&EnsureSceneBackendLocked(state).CreateSampler(desc)).first;
  }
  memo[1]=memo[0]; memo[0]={key,found->second};
  return found->second;
}
namespace {
// The immediate hook's pair classification, memoized (native_immediate_classify.h).
const NativeImmediatePairMemo<Bridge::ImmediatePairShaders>::Entry&
ClassifyImmediatePairLocked(Bridge& state,const GuestShaderPair& pair) {
  return state.immediate_pairs.Get(pair.vertex,pair.pixel,state.shader_registry_generation,
    state.embedded_shader_generation,[&](NativeImmediatePairClass& kind,Bridge::ImmediatePairShaders& shaders) {
      const auto vertex_embedded=state.embedded_shaders.find(pair.vertex);
      const auto pixel_embedded=state.embedded_shaders.find(pair.pixel);
      const auto vertex=state.shaders.find(pair.vertex),pixel=state.shaders.find(pair.pixel);
      NativeEmbeddedIdentity vertex_identity,pixel_identity;
      NativeSourceIdentity vertex_source,pixel_source;
      const bool embedded=vertex_embedded!=state.embedded_shaders.end() &&
                          pixel_embedded!=state.embedded_shaders.end();
      if(embedded) {
        vertex_identity={vertex_embedded->second.source,vertex_embedded->second.pixel};
        pixel_identity={pixel_embedded->second.source,pixel_embedded->second.pixel};
      }
      const bool registered=vertex!=state.shaders.end() && pixel!=state.shaders.end();
      if(registered) {
        const auto& vs=vertex->second.bindings->shader();
        const auto& ps=pixel->second.bindings->shader();
        vertex_source={vs.source_fingerprint,vs.entry.name};
        pixel_source={ps.source_fingerprint,ps.entry.name};
      }
      kind=ClassifyNativeImmediatePair(embedded?&vertex_identity:nullptr,embedded?&pixel_identity:nullptr,
        registered?&vertex_source:nullptr,registered?&pixel_source:nullptr);
      shaders={registered?&vertex->second:nullptr,registered?&pixel->second:nullptr};
    });
}
}  // namespace (shared with the full frame: bridge/bridge_helpers.h)
void BindActiveTarget(Bridge& state) {
  ++state.bind_generation;
  if(!state.context) return; // Recorded draws bind backend targets explicitly.
  const auto found = state.render_targets.find(state.active_target);
  if (found == state.render_targets.end()) {
    const auto scene=state.scenes.find(state.active_scene);
    // An unsupported nested post-process scope must not fall through and
    // overwrite the scene. Only the outer scene scope may bind this fallback.
    if (!state.active_target && scene!=state.scenes.end()) {
      auto* view=scene->second.color.target.Get();
      state.context->OMSetRenderTargets(1,&view,scene->second.depth.target.Get());
    } else state.context->OMSetRenderTargets(0,nullptr,nullptr);
    if (!state.active_target && !state.active_scene && state.scenes.contains(state.active_output)) {
      auto* output=state.scenes.at(state.active_output).output.target.Get();
      state.context->OMSetRenderTargets(1,&output,nullptr);
    }
    return;
  }
  auto& target = found->second.native;
  auto* view = target.target.Get();
  state.context->OMSetRenderTargets(1,&view,nullptr);
  const D3D11_VIEWPORT viewport{0,0,float(target.sampled.width),float(target.sampled.height),0,1};
  state.context->RSSetViewports(1,&viewport);
}
namespace {
// The live value of each global g_mWorld of published's constants, as 821B94E8
// would upload it now. The preload keeps the published copy stable
// (NativeSceneObjectWorldConstant); the guest path's pass constants, which its
// published activation binds to the live shader bindings, take it from here.
// Throws when the group's load no longer matches the publication's program.
void ReadNativeSceneLiveWorldLocked(Bridge& state,const GuestReader& reader,const NativeSceneGroupMaterial& published,
    std::vector<NativeSceneMaterialInputs::Constant>& constants) {
  const auto found=state.scene_material_loads.find(published.group);
  if(found==state.scene_material_loads.end() || !found->second.published || !found->second.schema ||
     found->second.published->program!=published.program || found->second.constants.size()!=constants.size())
    throw std::runtime_error("native scene material load does not match its publication");
  const auto& load=found->second;
  for(size_t i=0;i<constants.size();++i) {
    const auto& slot=load.constants[i];
    if(!slot.object_world) continue;
    if(!constants[i].global || constants[i].name!="g_mWorld") throw std::runtime_error("native scene material world slot moved");
    const auto* data=ReadNativeSceneMaterialConstant(reader,*load.schema,slot);
    constants[i].registers.assign(data,data+slot.bytes);
  }
}
bool ObservePublishedActivation(uint32_t instance) {
  auto* group=native_queued_scene_group;
  if(!group || group->activation_instance!=instance || !group->published_material || !group->material_pass)
    return false;
  auto& state=State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  const auto& published=*group->published_material;
  const auto latest=state.scene_adapter.GroupMaterial(published.group,published.revision);
  const auto* membership=NativeSceneSourcesForPass(state).FindGroup(published.group);
  if(!latest || latest->program!=published.program || !membership || membership->revision!=published.revision)
    return false;
  const auto& program=*published.program;
  const auto vertex=program.inputs.vertex,pixel=program.inputs.pixel;
  if(!state.shaders.contains(vertex) || !state.shaders.contains(pixel) || program.backend!=state.scene_backend)
    return false;
  auto& shader=state.shaders.at(vertex);
  auto& vs=*shader.bindings;
  auto& ps=*state.shaders.at(pixel).bindings;
  if(!shader.reversed_bindings || vs.shader().bytecode!=program.vertex.bytecode ||
     shader.reversed_bindings->shader().bytecode!=program.reversed_vertex.bytecode ||
     ps.shader().bytecode!=program.pixel.bytecode) return false;
  auto& reversed=*shader.reversed_bindings;
  const std::pair<uint32_t,uint32_t> link{vertex,pixel};
  if(!state.validated_links.contains(link)) {
    ValidateNativeShaderLink(vs.shader(),ps.shader());
    ValidateNativeShaderLink(reversed.shader(),ps.shader());
    state.validated_links.insert(link);
  }
  auto ranges=shader.ResolvePublishedVertexRanges(published.program);
  const auto resolved=program.ResolveSamplers(group->sampler_pass);
  std::vector<NativeBackendSampler*> samplers;
  for(const auto& texture:program.inputs.textures) {
    if(texture.slot>=resolved.size()) throw std::runtime_error("invalid published activation sampler slot");
    const auto key=NativeFilteringKey(resolved[texture.slot].words,REXCVAR_GET(edf_native_anisotropic_filtering));
    auto cached=state.samplers.find(key);
    if(cached==state.samplers.end())
      cached=state.samplers.emplace(key,&program.backend->CreateSampler(DecodeNativeGuestSampler(key))).first;
    samplers.push_back(cached->second);
  }
  state.active_vertex=0;
  state.active_vertex_parameters.reset();
  program.ApplyBindings(vs,ps,group->pass_constants,samplers);
  if(!shader.reversed_mirrors) program.ApplyBindings(reversed,ps,group->pass_constants,samplers);
  if(state.context) { vs.Bind(*state.context.Get()); ps.Bind(*state.context.Get()); }
  state.linked_vertex=vertex; state.linked_pixel=pixel;
  state.active_vertex_parameters=std::move(ranges);
  state.active_vertex=vertex;
  group->published_activation_used=true;
  ++state.activations;
  static uint64_t count=0;
  if(++count<=4 || count%100000==0)
    REXLOG_INFO("Native published activation: count={} (no live material constants or texture reads)",count);
  return true;
}
void ObserveActivation(const GuestReader& backing, uint32_t instance, uint32_t device) {
  // Per-stage vector headers, texture vectors and pass pointer share this
  // material object. Validate once, retaining no window beyond activation.
  const GuestReadWindow reader(backing,instance,112);
  // The device's sixteen sampler records, validated once for the activation.
  const GuestReadWindow sampler_reader(backing,backing.Add(device,1024),16*24);
  const auto pass = reader.Word(reader.Add(instance, 108));
  const auto vertex = reader.Word(reader.Word(pass));
  const auto pixel = reader.Word(reader.Add(reader.Word(reader.Add(pass, 4)), 4));
  auto& state = State();
  // Sub-phases: activation is the most expensive per-call hook in gameplay and
  // nobody has measured which part of it that is. Lock wait is separated from
  // work because both scale differently with thread contention.
  edf::native::HookTiming lock_timing(edf::native::HookPhase::ActivationLock);
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  lock_timing.Finish();
  edf::native::HookTiming resolve_timing(edf::native::HookPhase::ActivationResolve);
  ++state.activations;
  // Repeat activations of one material with unchanged shaders are the cheapest
  // thing to skip if they dominate, so count them before optimising anything.
  if(instance==state.last_activation_instance && vertex==state.last_activation_vertex &&
     pixel==state.last_activation_pixel) ++state.repeat_activations;
  state.last_activation_instance=instance;
  state.last_activation_vertex=vertex; state.last_activation_pixel=pixel;
  if(state.activations%250000==0)
    REXLOG_INFO("Native activation repeats: activations={}, repeats_of_previous={} ({:.1f}% identical instance and shader pair as the immediately preceding activation)",
      state.activations,state.repeat_activations,100.0*double(state.repeat_activations)/double(state.activations));
  // One lookup per shader, kept: nothing below inserts into or erases from the
  // shader registry, so both entries stay where they were found.
  const auto vertex_entry=state.shaders.find(vertex),pixel_entry=state.shaders.find(pixel);
  const bool found = vertex_entry!=state.shaders.end() && pixel_entry!=state.shaders.end();
  state.active_vertex = 0;
  state.active_vertex_parameters.reset();
  if (!found) ++state.misses;
  if (found) {
    try {
      auto& vertex_shader=vertex_entry->second;
      auto& pixel_shader=pixel_entry->second;
      const auto material=state.material_parameters.Get(instance);
      auto& vs = *vertex_shader.bindings;
      auto& reversed = *vertex_shader.reversed_bindings;
      auto& ps = *pixel_shader.bindings;
      if (state.linked_vertex != vertex || state.linked_pixel != pixel) {
        const std::pair<uint32_t,uint32_t> link{vertex,pixel};
        if(!state.validated_links.contains(link)) {
          ValidateNativeShaderLink(vs.shader(),ps.shader());
          ValidateNativeShaderLink(reversed.shader(),ps.shader());
          state.validated_links.insert(link); // After both passed, never before.
        }
        state.linked_vertex = vertex; state.linked_pixel = pixel;
      }
      const auto vertex_ranges=vertex_shader.ResolveVertexRanges(material);
      // A stage whose native shader consumes a Common.fx global that this
      // material never lists for that stage keeps the value our own HLSL
      // compilation baked in. Distinguish that from an upload the bridge drops:
      // the guest's own records decide a stage's parameter list.
      // Audit the first activation of each distinct shader pair only. The
      // per-stage parameter list belongs to the material's shaders, not to the
      // draw, so one sample per pair is complete, and gameplay activates
      // hundreds of thousands of times a second.
      if(const auto audit=REXCVAR_GET(edf_native_shared_constant_audit);
         audit>0 && state.shared_constant_pairs.size()<uint64_t(audit) &&
         state.shared_constant_pairs.insert({vertex,pixel}).second) {
        ++state.shared_constant_activations;
        const auto owned=state.material_parameters.Get(instance);
        const auto supplied=[&](size_t stage,const std::string& name) {
          for(size_t group=stage*2;group<stage*2+2;++group)
            for(const auto& parameter:(*owned)[group]) if(parameter.name==name) return true;
          return false;
        };
        // Where both stages are supplied, compare the storage each one names.
        // Identical source bytes with different shader values would be an
        // upload defect; different source addresses are the guest's own layout.
        const auto global_source=[&](size_t stage,const std::string& name)
            ->std::optional<std::array<uint32_t,3>> {
          for(const auto& parameter:(*owned)[stage*2+1]) {
            if(parameter.name!=name) continue;
            const auto node=GuestBlockWord(reader.Bytes(parameter.record,4));
            const auto value=parameter.ReadValue(reader,true);
            return std::array<uint32_t,3>{parameter.record,node,value.data};
          }
          return std::nullopt;
        };
        for(const auto* name:{"g_LightVector","g_LightDiffuse","g_HemiSphereVector",
            "g_HemiSphereColor1","g_HemiSphereColor2","g_FogParam","g_FogColor"}) {
          // Declaring a constant is not reading it: the reflection lists every
          // Common.fx global in the buffer, so a binding's existence says
          // nothing about whether this stage's pixels depend on the value.
          const bool vertex_used=vs.ConsumesConstant(name);
          const bool pixel_used=ps.ConsumesConstant(name);
          const bool vertex_supplied=supplied(0,name),pixel_supplied=supplied(1,name);
          if(vertex_supplied || pixel_supplied) {
            const auto vertex_source=global_source(0,name),pixel_source=global_source(1,name);
            if(vertex_used && pixel_used && vertex_supplied && pixel_supplied)
              ++state.shared_constant_both_supplied;
            if(vertex_source || pixel_source) {
              const std::array<uint32_t,3> storage_identity{vertex,pixel,uint32_t(name[2])*7+uint32_t(name[3])};
              if(state.shared_constant_storage_reported.size()<64 &&
                 state.shared_constant_storage_reported.insert(storage_identity).second) {
                const auto first=[&](uint32_t data)->float {
                  return data ? std::bit_cast<float>(GuestBlockWord(reader.Bytes(data,4))) : 0.f;
                };
                const auto vertex_data=vertex_source?(*vertex_source)[2]:0u;
                const auto pixel_data=pixel_source?(*pixel_source)[2]:0u;
                REXLOG_INFO("Native shared constant storage: name={}, VS={:#x} {} uses={} data={:#x} first={}, PS={:#x} {} uses={} data={:#x} first={}, same_data={}",
                  name,vertex,vs.shader().entry.name,vertex_used,vertex_data,first(vertex_data),
                  pixel,ps.shader().entry.name,pixel_used,pixel_data,first(pixel_data),
                  vertex_data && pixel_data && vertex_data==pixel_data);
              }
              if(vertex_source && pixel_source && (*vertex_source)[2]!=(*pixel_source)[2])
                ++state.shared_constant_split_storage;
            }
          }
          if((vertex_used && !vertex_supplied)||(pixel_used && !pixel_supplied)) {
            ++state.shared_constant_unsupplied;
            const std::array<uint32_t,3> identity{vertex,pixel,uint32_t(std::string_view(name).size()*131+name[2])};
            if(state.shared_constant_reported.size()<64 &&
               state.shared_constant_reported.insert(identity).second)
              REXLOG_WARN("Native shared constant unsupplied: name={}, VS={:#x} {} uses={} supplied={}, PS={:#x} {} uses={} supplied={}, source={:#x} (an unsupplied stage keeps the compiled source default)",
                name,vertex,vs.shader().entry.name,vertex_used,vertex_supplied,
                pixel,ps.shader().entry.name,pixel_used,pixel_supplied,
                vs.shader().source_fingerprint);
          }
        }
        const auto seen=state.shared_constant_activations;
        if(seen<=4 || (seen&(seen-1))==0)
          REXLOG_INFO("Native shared constant audit: pairs={}, unsupplied_uses={}, distinct_unsupplied={}, both_supplied={}, split_storage={} (one sample per distinct shader pair; split_storage counts pairs whose two stages name different guest value storage)",
            seen,state.shared_constant_unsupplied,state.shared_constant_reported.size(),
            state.shared_constant_both_supplied,state.shared_constant_split_storage);
      }
      resolve_timing.Finish();
      { edf::native::HookTiming vertex_params(edf::native::HookPhase::ActivationVertexParams);
        UploadParameters(reader, material, vertex_shader, 0, vs, state,
                         vertex_shader.reversed_mirrors?nullptr:&reversed); }
      { edf::native::HookTiming pixel_params(edf::native::HookPhase::ActivationPixelParams);
        UploadParameters(reader, material, pixel_shader, 36, ps, state); }
      edf::native::HookTiming texture_timing(edf::native::HookPhase::ActivationTextures);
      try { UploadTextures(reader, sampler_reader, material, pixel_shader, device, ps, state); }
      catch (const std::exception& error) {
        ++state.texture_binding_errors;
        if (state.texture_binding_errors <= 10)
          REXLOG_ERROR("Native texture binding: {} (instance={:#x})", error.what(), instance);
        ps.ClearTextures();
        ps.ClearSamplers();
      }
      texture_timing.Finish();
      if(state.context) { edf::native::HookTiming bind_timing(edf::native::HookPhase::ActivationBind);
        vs.Bind(*state.context.Get());
        ps.Bind(*state.context.Get()); }
      state.active_vertex_parameters=vertex_ranges;
      state.active_vertex = vertex;
    } catch (const std::exception& error) {
      ++state.parameter_errors;
      if (state.parameter_errors <= 10)
        REXLOG_ERROR("Native parameter bridge: {} (instance={:#x})", error.what(), instance);
    }
  }
  if (state.activations <= 5 || state.activations % 10000 == 0)
    REXLOG_INFO("Native shader bridge: activation={}, VS={:#x}, PS={:#x}, resolved={}, misses={}, uploads={}, optimized_out={}, parameter_errors={}, texture_bindings={}, texture_missing={}, texture_binding_errors={}, sampler_bindings={}, sampler_states={}",
                state.activations, vertex, pixel, found, state.misses, state.parameter_uploads,
                state.optimized_out, state.parameter_errors, state.texture_bindings,
                state.texture_missing, state.texture_binding_errors,state.sampler_bindings,state.samplers.size());
}
}
void LogNativeCoverageCensusFinal() {
  static std::atomic<bool> logged=false;
  if(!REXCVAR_GET(edf_native_coverage_census) || !CoverageCensus().Totals().frames || logged.exchange(true)) return;
  for(const auto& line:CoverageCensus().Summary("final")) REXLOG_INFO("{}",line);
}
void SetNativeMeshWatchAudit(std::weak_ptr<GuestMeshWatchAudit> audit) {
  auto& state=State();
  std::lock_guard lock(state.mutex);
  state.mesh_watch_audit=std::move(audit);
}
std::array<float,4> ResolveBlendFactorForDraw(uint32_t device,
    const NativeRenderStateSnapshots::BlendWords* live) {
  auto& snapshots=State().render_state_snapshots;
  if(live) {
    const auto result=snapshots.CheckBlend(device,*live);
    const auto& count=snapshots.blend_counters();
    if(count.checks<=8 || !(count.checks&(count.checks-1)) ||
        (result!=NativeRenderStateSnapshots::Result::Match && count.missing+count.mismatches<=8))
      REXLOG_INFO("Native blend-factor audit: device={:#x}, checks={}, missing={}, mismatches={}",
        device,count.checks,count.missing,count.mismatches);
    if(REXCVAR_GET(edf_native_owned_render_state) && result!=NativeRenderStateSnapshots::Result::Match)
      throw std::runtime_error("native blend-factor audit mismatch");
  }
  const auto words=REXCVAR_GET(edf_native_owned_render_state)?snapshots.RequireBlend(device):*live;
  return {std::bit_cast<float>(words[0]),std::bit_cast<float>(words[1]),
          std::bit_cast<float>(words[2]),std::bit_cast<float>(words[3])};
}
void PublishNativeRenderState(uint8_t* base,uint32_t device,uint32_t producer) {
  PublishNativeRenderStateWith(device,producer,[&] { return GuestReader(base); });
}
// Draw consumers already hold the bridge state lock. This diagnostic never
// repairs ownership; opt-in native consumption additionally rejects mismatches.
void AuditNativeRenderState(uint32_t device,const NativeRenderStateSnapshots::Words& live) {
  if(!REXCVAR_GET(edf_native_render_state_audit)) return;
  auto& snapshots=State().render_state_snapshots;
  const auto result=snapshots.Check(device,live);
  const auto& count=snapshots.counters();
  if(count.checks<=8 || !(count.checks&(count.checks-1)) ||
      (result==NativeRenderStateSnapshots::Result::Mismatch && count.mismatches<=8)) {
    const auto* saved=snapshots.Find(device);
    REXLOG_INFO("Native render-state ownership audit: device={:#x}, publications={}, checks={}, missing={}, mismatches={}, producer={:#x}, revision={}",
      device,count.publications,count.checks,count.missing,count.mismatches,saved?saved->producer:0,saved?saved->revision:0);
    if(result==NativeRenderStateSnapshots::Result::Mismatch && count.mismatches<=8)
      for(size_t field=0;field<live.size();++field) if(live[field]!=saved->words[field])
        REXLOG_INFO("Native render-state mismatch: field={}, owned={:#x}, live={:#x}, producer={:#x}",field,saved->words[field],live[field],saved->producers[field]);
    if(result==NativeRenderStateSnapshots::Result::Missing)
      REXLOG_INFO("Native render-state missing fields: valid_mask={:#x}",saved?saved->valid_fields:0);
  }
}
template <typename Reader>
GuestXuiDeviceWords ReadAuditedXuiDeviceWords(const Reader& reader,uint32_t device) {
  if(REXCVAR_GET(edf_native_owned_render_state))
    return ReadXuiDeviceWords(reader,device,ReadAuditedRenderStateWords(reader,device));
  auto snapshot=ReadXuiDeviceWords(reader,device);
  AuditNativeRenderState(device,snapshot.render);
  return snapshot;
}
namespace {
// The persistent cache root from --edf_native_cache_dir, applied before the
// first shader compile and the first D3D12 device. Idempotent.
void ApplyNativeCacheDirectory() {
  static std::once_flag once;
  std::call_once(once,[] {
    const std::string setting=REXCVAR_GET(edf_native_cache_dir);
    std::filesystem::path directory;
    if(setting!="off") directory=setting.empty()?edf::native::NativeDefaultCacheDirectory():std::filesystem::path(setting);
    edf::native::SetNativeCacheDirectory(directory);
    edf::native::SetNativeD3D12SceneCacheDirectory(directory);
    REXLOG_INFO("Native persistent caches: {}",directory.empty()?std::string("off"):directory.string());
  });
}
// The D3D12 options the cvars carry, applied before anything can build a D3D12
// device. Every site that registers the backend goes through here, because the
// debug layer is a process-wide switch that only the first device gets to throw
// and the presenter's device is usually the first: setting it later - which is
// what the scene backend used to do - removes the device that already exists.
void RegisterD3D12BackendLocked() {
  ApplyNativeCacheDirectory();
  edf::native::SetNativeD3D12DebugLayer(REXCVAR_GET(edf_native_d3d12_debug_layer));
  // Sized from a measured frame, not from a guess; see the cvar.
  edf::native::SetNativeD3D12UploadMegabytes(
    uint32_t((std::max)(16,REXCVAR_GET(edf_native_upload_megabytes))));
  edf::native::RegisterNativeD3D12Backend();
}
// Builds the backend named by --edf_native_backend, once, with the state lock
// already held. One place knows how to do this; the public accessor below only
// adds the lock.
//
// Refused on an unknown name rather than falling back to whatever exists: an
// A/B run silently comparing a backend against itself would be worse than a
// failure to start.
edf::native::NativeRenderBackend& EnsureBackendLocked(Bridge& state) {
  if(state.backend) return *state.backend;
  const std::string name=REXCVAR_GET(edf_native_backend);
  if(name.empty()) throw std::runtime_error("a render backend was asked for but --edf_native_backend is empty");
  if(!state.initialized) throw std::runtime_error("a render backend was asked for before the renderer had a device");
  edf::native::RegisterNativeD3D11Backend();
  RegisterD3D12BackendLocked();
  try {
    // "d3d11" means the device this renderer already has, not a second one: a
    // separate device could not share a texture or a target with the paths
    // that still draw through D3D11 directly, which is what lets the port
    // proceed one path at a time.
    state.backend=name=="d3d11"
      ? edf::native::AdoptNativeD3D11Backend(*state.device.Get(),*state.context.Get())
      : edf::native::CreateNativeRenderBackend(name);
  } catch(const std::exception& error) {
    std::string known;
    for(const auto& candidate:edf::native::NativeRenderBackendNames())
      known+=(known.empty()?"":", ")+candidate;
    REXLOG_ERROR("Native render backend: --edf_native_backend={} was refused: {}. Available: {}",
      name,error.what(),known.empty()?std::string("none"):known);
    throw;
  }
  REXLOG_INFO("Native render backend ready: name={}, recorders={}, parallel_recording={}; selected by --edf_native_backend={}. {}",
    std::string(state.backend->name()),state.backend->RecorderCount(),
    state.backend->SupportsParallelRecording(),name,
    name=="d3d11"?"Sharing this renderer's own device, so ported and unported paths draw into the same targets."
                 :"On its own device, so it cannot share targets with the paths that still draw through D3D11 directly.");
  for(const auto& message:state.backend->DrainValidationMessages())
    REXLOG_WARN("Native render backend validation: {}",message);
  return *state.backend;
}
}  // namespace (shared with the full frame: bridge/bridge_helpers.h)
// The backend the scene's own resources are created on.
//
// Separate from EnsureBackendLocked because during the port these are not the
// same backend: a texture created on a second device cannot be sampled by the
// draw paths that are still direct D3D11, so the scene's resources stay on the
// adopted backend - this renderer's own device - until the last path has moved.
edf::native::NativeRenderBackend& EnsureSceneBackendLocked(Bridge& state) {
  if(state.scene_backend) return *state.scene_backend;
  const std::string& name=state.scene_backend_name;
  if(name.empty()) throw std::runtime_error("the scene's resources were asked for but --edf_native_scene_backend is empty");
  if(!state.initialized) throw std::runtime_error("the scene's resources were asked for before the renderer had a device");
  edf::native::RegisterNativeD3D11Backend();
  RegisterD3D12BackendLocked();
  state.scene_backend=name=="d3d11"
    ? edf::native::AdoptNativeD3D11Backend(*state.device.Get(),*state.context.Get())
    : (name=="d3d12" || name=="d3d12-warp")
      ? edf::native::CreateNativeD3D12SceneBackend(name=="d3d12-warp",
          uint32_t(std::clamp(REXCVAR_GET(edf_native_geometry_workers),0,32)))
      : edf::native::CreateNativeRenderBackend(name);
  REXLOG_INFO("Native scene backend ready: name={}; selected by --edf_native_scene_backend={}. {}",
    std::string(state.scene_backend->name()),name,
    name=="d3d11"?"This renderer's own device, so ported and unported draw paths share the same resources."
                 :"A separate device: every draw path that samples a scene resource must already be ported, or it will have nothing to bind.");
  if(const auto caches=state.scene_backend->DescribeCaches();!caches.empty())
    REXLOG_INFO("Native scene caches: {}",caches);
  for(const auto& message:state.scene_backend->DrainValidationMessages())
    REXLOG_WARN("Native scene backend validation: {}",message);
  return *state.scene_backend;
}
namespace {
std::unique_ptr<NativeCompletionQueue> CreateCompletionQueueLocked(Bridge& state,size_t capacity=4096) {
  auto& backend=EnsureSceneBackendLocked(state);
  if(backend.name()=="d3d12") return std::make_unique<NativeCompletionQueue>(backend,capacity);
  return std::make_unique<NativeCompletionQueue>(*state.device.Get(),*state.context.Get(),capacity);
}
std::unique_ptr<NativeSignalQueue> CreateSignalQueueLocked(Bridge& state) {
  auto& backend=EnsureSceneBackendLocked(state);
  if(backend.name()=="d3d12") return std::make_unique<NativeSignalQueue>(backend);
  return std::make_unique<NativeSignalQueue>(*state.device.Get(),*state.context.Get());
}
}  // namespace (shared with the full frame: bridge/bridge_helpers.h)
edf::native::NativeBackendRecorder& SceneRecorderLocked(Bridge& state) {
  auto& backend=EnsureSceneBackendLocked(state);
  // Submitted and reopened when it has carried enough, so the work between two
  // guest swaps is not one unbounded command list. Everything recorded is
  // already ordered by submission order, so splitting a frame changes when the
  // GPU sees the work and nothing about what it sees.
  const auto limit=REXCVAR_GET(edf_native_frame_operations);
  if(state.scene_frame_open && limit>0 &&
     state.scene_frame_operations>=uint64_t(limit)) {
    ++state.scene_frame_splits;
    SubmitSceneFrameLocked(state);
  }
  if(!state.scene_frame_open) {
    backend.BeginFrame();
    state.scene_frame_open=true;
    state.scene_frame_operations=0;
    ++state.scene_frames;
  }
  ++state.scene_frame_operations;
  // One producer captures immutable draw packets. The D3D12 scene backend
  // distributes contiguous packet ranges to its recording workers.
  return backend.Recorder();  // Recorder(0), or the shadow render's draw-list tap wrapping it.
}
void PublishSceneSharedLocked(Bridge& state,const NativeRenderTarget& output,NativeFrameKind kind) {
  if(state.scene_shared_refused || !state.scene_backend || !output.backend_surface) return;
  // The compositor already carries a D3D11 scene to the window, with the
  // letterboxing and the display gamma it applies. Sharing one as well would
  // be a second copy of a frame that already arrived.
  if(state.scene_backend->name()=="d3d11") return;
  const auto width=output.sampled.width,height=output.sampled.height;
  if(!width || !height) return;
  // A converting target keeps its HDR working surface separately from the
  // resolved RGBA8 image. Publish the latter, as the D3D11 path does.
  auto* published=output.converted_target?output.converted_target.get():output.backend_surface.get();
  const auto format=output.converted_target?output.sampled.format:output.format;
  if(format!=DXGI_FORMAT_R8G8B8A8_UNORM)
    throw std::runtime_error("native scene publication requires resolved RGBA8 output");
  if(!state.scene_shared_slot) {
    NativeFrameWaitTrace waiting(FrameWaitKind::SharedSlot);
    state.scene_shared_slot=state.scene_frame_queue.Reserve(
      std::chrono::steady_clock::now()+std::chrono::seconds(10));
    waiting.Finish();
    state.scene_shared=state.scene_shared_slots[*state.scene_shared_slot];
  }
  if(!state.scene_shared || state.scene_shared->width()!=width || state.scene_shared->height()!=height ||
     state.scene_shared->format()!=format) {
    edf::native::NativeBackendTextureDesc desc{};
    desc.width=width; desc.height=height; desc.levels=1;
    desc.format=format;
    state.scene_shared=state.scene_backend->CreateSharedSurface(desc);
    if(!state.scene_shared) {
      // Said once: a backend that cannot share is a fact about the backend,
      // not a per-frame event worth repeating sixty times a second.
      state.scene_shared_refused=true;
      REXLOG_WARN("Native scene frame sharing: the {} backend cannot create a shared surface, so its frames cannot reach the window",
        std::string(state.scene_backend->name()));
      return;
    }
    state.scene_shared_slots[*state.scene_shared_slot]=state.scene_shared;
    state.scene_shared_slot_generations[*state.scene_shared_slot]=++state.scene_shared_next_generation;
    state.scene_shared_width=width; state.scene_shared_height=height;
    REXLOG_INFO("Native scene frame sharing: {}x{} format={} on the {} backend; the window opens this rather than a copy through system memory",
      width,height,format,std::string(state.scene_backend->name()));
  }
  state.scene_shared_generation=state.scene_shared_slot_generations[*state.scene_shared_slot];
  SceneRecorderLocked(state).CopyToShared(*state.scene_shared,*published);
  state.scene_shared_pending=true;
  state.scene_shared_kind=kind;
}
void SubmitSceneFrameLocked(Bridge& state) {
  if(!state.scene_frame_open) return;
  const auto operations=state.scene_frame_operations;
  state.scene_frame_open=false;
  // Nothing survives a frame boundary: the recorder's own tracked state is
  // reset when the next frame opens, so what this believed was still bound is
  // no longer true.
  ++state.bind_generation;
  state.recorded={};
  try {
    state.scene_backend->Submit();
    state.scene_recorded_snapshots.clear();
    state.scene_recorded_frames.clear();
  } catch(const std::exception& error) {
    // A frame that cannot be submitted is lost either way; what must not
    // happen is the next frame finding one still open and refusing to start.
    REXLOG_ERROR("Native scene frame submit: {} (frame {})",error.what(),state.scene_frames);
    if(state.scene_shared_slot) state.scene_frame_queue.Fail(std::current_exception());
    state.scene_shared_pending=false;
    throw;
  }
  // After the submit, never inside the frame: on a backend with a queue this
  // is a queue signal, and signalling before the work is submitted would tell
  // the consumer a frame is ready that has not been recorded yet.
  if(state.scene_shared_pending) {
    state.scene_shared_pending=false;
    try {
      state.scene_backend->SignalShared(*state.scene_shared);
      ++state.scene_shared_sequence;
      state.scene_shared_gamma=state.display_gamma;
      const auto& queued=*state.scene_shared;
      state.scene_frame_queue.Publish(state.scene_shared_slot.value(),
        {queued.texture_handle(),queued.fence_handle(),queued.value(),state.scene_shared_sequence,
         state.scene_shared_generation,queued.width(),queued.height(),queued.format(),state.scene_shared_gamma},
        state.scene_shared);
      edf::latency::OnFrameSubmitted(state.scene_shared_sequence);
      state.scene_shared_slot.reset();
      if(state.presentation_frames && !REXCVAR_GET(edf_native_scene_backend).starts_with("d3d12")) {
        const auto& source=*state.scene_shared;
        state.presentation_frames->PublishShared(
          {source.texture_handle(),source.fence_handle(),source.value(),
           source.width(),source.height(),source.format()},
          state.scene_shared_kind,state.display_gamma?&*state.display_gamma:nullptr);
        const auto copied=state.presentation_frames->Shared();
        if(!state.scene_backend->WaitSharedFence(copied.fence,copied.value))
          throw std::runtime_error("scene backend cannot wait for presentation snapshot copy");
      }
    } catch(const std::exception& error) {
      state.scene_frame_queue.Fail(std::current_exception());
      REXLOG_ERROR("Native scene frame sharing: {}",error.what());
      throw;
    }
  }
  for(const auto& message:state.scene_backend->DrainValidationMessages())
    REXLOG_WARN("Native scene backend validation: {}",message);
  // Periodically, what the frame actually cost the backend. A frame time has
  // to be attributable to something: a stall for upload memory, a pipeline
  // built during gameplay, a sampler cache thrashing, a frame split into more
  // command lists than it should need. Without these the only thing that can
  // be said about a slow frame is that it was slow.
  if(state.scene_frames<=3 || state.scene_frames%600==0) {
    const auto counts=state.scene_backend->Statistics();
    REXLOG_INFO("Native geometry workers: draws={}, batches={}, worker_mask={:#x}, max_concurrent={}, recording_cpu_ms={}, wait_ms={}, serial_draws={}, serial_flushes={} (serial draws were replayed on the producer thread because a flush found fewer packets than the worker minimum)",
      counts.geometry_draws,counts.geometry_batches,counts.geometry_worker_mask,counts.geometry_max_concurrent,
      counts.geometry_record_ns/1000000.0,counts.geometry_wait_ns/1000000.0,
      counts.geometry_serial_draws,counts.geometry_serial_flushes);
    REXLOG_INFO("Native world instancing: groups={}, folded_draws={} (consecutive queued draws with identical non-world state)",
      counts.geometry_instanced_draws,counts.geometry_folded_draws);
    REXLOG_INFO("Native transient batching: appended_draws={} (UI/immediate list draws recorded as the continuation of the identical-state draw before them)",
      counts.geometry_transient_appends);
    REXLOG_INFO("Native world constants: reused={}, snapshot_bytes={} (full immutable constant images copied by the producer)",
      counts.geometry_world_constant_reuses,counts.geometry_constant_snapshot_bytes);
    REXLOG_INFO("Native constant images: interned={}, interned_bytes={}, uploads={}, upload_reuses={}, streamed_jobs={} (binds of an image the frame already held and the bytes not copied; images staged once per submission and binds of a staged image; worker ranges handed over before their flush)",
      counts.geometry_constant_interned,counts.geometry_constant_interned_bytes,
      counts.geometry_constant_uploads,counts.geometry_constant_upload_reuses,counts.geometry_streamed_jobs);
    REXLOG_INFO("Native scene backend spend: frames={}, splits={}, operations_last_frame={}, "
      "upload_stalls={}, descriptor_stalls={}, pipelines={} (hits={}, misses={}), "
      "sampler_tables={} (hits={}, misses={}, evictions={}), retiring={}, "
      "frame_waits={} averaging {}us",
      state.scene_frames,state.scene_frame_splits,operations,
      counts.upload_stalls,counts.descriptor_stalls,counts.pipelines,counts.pipeline_hits,
      counts.pipeline_misses,counts.sampler_tables,counts.sampler_hits,counts.sampler_misses,
      counts.sampler_evictions,counts.retiring,counts.frame_waits,
      counts.frame_waits?counts.frame_wait_ns/counts.frame_waits/1000:0);
    const auto shaders=edf::native::GetNativeShaderCacheStatistics();
    REXLOG_INFO("Native first-use caches: pipelines prebuilt={} content_hits={} waits={} ({}us) manifest_entries={}; "
      "sampler_tables_prewarmed={}; buffers committed={} placed={} heaps={} ({} MB); "
      "shaders compiled={} memory_hits={} disk_hits={} disk_stores={} disk_rejects={}",
      counts.pipeline_prebuilt,counts.pipeline_content_hits,counts.pipeline_waits,counts.pipeline_wait_ns/1000,
      counts.pipeline_manifest_entries,counts.sampler_prewarmed,counts.buffers_committed,counts.buffers_placed,
      counts.buffer_heaps,counts.buffer_heap_bytes>>20,shaders.compiles,shaders.memory_hits,shaders.disk_hits,
      shaders.disk_stores,shaders.disk_rejects);
    if(state.scene_frames%6000==0)
      if(const auto caches=state.scene_backend->DescribeCaches();!caches.empty())
        REXLOG_INFO("Native scene caches: {}",caches);
  }
}

void InitializeGuestShaderBridge(const std::filesystem::path& game_root) {
  // Before anything can compile a shader.
  ApplyNativeCacheDirectory();
  REXLOG_INFO("Native render-state consumption: owned={}, audit={}",
    REXCVAR_GET(edf_native_owned_render_state),REXCVAR_GET(edf_native_render_state_audit));
  if(REXCVAR_GET(edf_native_preview_window) && !REXCVAR_GET(edf_native_publish_frames))
    throw std::runtime_error("native preview requires native frame publication");
  if(REXCVAR_GET(edf_native_publish_frames) && !EDF_NATIVE_FLAG(shader_bridge))
    throw std::runtime_error("native frame publication requires native shader bridge");
  // Said here rather than discovered per draw. A scene on its own device with
  // the direct path still in use fails at the first mesh - "this mesh is not
  // on a D3D11 backend and cannot be drawn through a context" - once per draw,
  // for the rest of the run, which is a worse way to learn it.
  if(REXCVAR_GET(edf_native_scene_backend)!="d3d11" && !EDF_NATIVE_FLAG(seam_draws))
    throw std::runtime_error("--edf_native_scene_backend="+REXCVAR_GET(edf_native_scene_backend)+
      " needs --edf_native_seam_draws=true: a draw issued straight to the D3D11 context cannot "
      "bind a resource that lives on another device");
  if(REXCVAR_GET(edf_native_validate_wait) &&
     (!EDF_NATIVE_FLAG(shader_bridge) || !REXCVAR_GET(edf_native_fence_probe)))
    throw std::runtime_error("native wait validation requires native shader bridge and fence probe");
  if (!EDF_NATIVE_FLAG(shader_bridge)) return;
  auto& state = State();
  std::lock_guard lock(state.mutex);
  state.root = game_root;
  // Native overlays may initialize the device before a game path/runtime
  // exists. The later OnPostSetup call supplies the root without replacing
  // a device already referenced by the host window and font textures.
  if(state.initialized) return;
  state.scene_backend_name=REXCVAR_GET(edf_native_scene_backend);
  if(state.scene_backend_name=="d3d11") {
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &level, 1,
                              D3D11_SDK_VERSION, &state.device, nullptr, &state.context)))
    throw std::runtime_error("native shader bridge: D3D11 device creation failed");
  REXLOG_INFO("Native shader bridge: initialized hardware D3D11 fallback device");
  }
  state.initialized=true;
  RegisterD3D12BackendLocked();
  REXLOG_INFO("Native shader bridge: initialized; scene={}, D3D11_device={}",
    REXCVAR_GET(edf_native_scene_backend),bool(state.device));
  // The host window asks for a backend through this rather than calling into
  // the bridge, so it can still be built standalone by its own test.
  // A backend of its own for presentation, never the bridge's. The bridge's
  // may be the adopted D3D11 one, which shares this renderer's immediate
  // context - and presentation runs on the window thread outside the context
  // lock, so sharing it would drive that context from two threads.
  //
  // The flag is read here rather than in the host surface, which is also
  // built standalone by its own test and cannot see the bridge's cvars.
  edf::native::SetNativeHostBackendFactory([]() -> std::unique_ptr<edf::native::NativeRenderBackend> {
    if(!REXCVAR_GET(edf_native_backend_present)) return nullptr;
    const std::string name=REXCVAR_GET(edf_native_backend);
    if(name.empty()) return nullptr;
    edf::native::RegisterNativeD3D11Backend();
    RegisterD3D12BackendLocked();
    return edf::native::CreateNativeRenderBackend(name);
  });
  REXLOG_INFO("Native render backend: --edf_native_backend={}; built on first use, so selecting one costs nothing until something draws through it. Scene draws use the backend interface; the scene backend is selected independently",
    REXCVAR_GET(edf_native_backend).empty()?std::string("none"):REXCVAR_GET(edf_native_backend));
  if(REXCVAR_GET(edf_native_backend_preview)) {
    if(!REXCVAR_GET(edf_native_publish_frames))
      throw std::runtime_error("--edf_native_backend_preview needs --edf_native_publish_frames: it draws the frames the renderer publishes");
    // Built here, under the lock already held, through the same creator the
    // lazy accessor uses - so there is one place that knows how to build the
    // selected backend rather than two that can drift.
    edf::native::RegisterNativeD3D11Backend();
    RegisterD3D12BackendLocked();
    state.backend_preview=std::make_unique<edf::native::NativeD3D12Preview>(
      REXCVAR_GET(edf_native_backend));
  }
  if(REXCVAR_GET(edf_native_publish_frames) && !REXCVAR_GET(edf_native_scene_backend).starts_with("d3d12"))
    state.presentation_frames=std::make_unique<NativeFrameHandoff>(*state.device.Get(),*state.context.Get());
}
NativeRenderBackend* EnsureNativeRenderBackend() {
  auto& state=State();
  std::lock_guard lock(state.mutex);
  if(state.backend) return state.backend.get();
  if(REXCVAR_GET(edf_native_backend).empty() || !state.initialized) return nullptr;
  return &EnsureBackendLocked(state);
}

bool VisitNativeBackendFrame(uint64_t after_sequence,
    const std::function<NativeBackendFrameCopied(const NativeBackendPublishedFrame&)>& copy,
    NativeBackendFrameVisitTiming* timing) {
  return State().scene_frame_queue.Visit(after_sequence,copy,timing,edf::latency::LowLatency());
}
bool VisitNativeBackendFrameMirror(uint64_t after_sequence,
    const std::function<NativeBackendFrameCopied(const NativeBackendPublishedFrame&)>& copy) {
  return State().scene_frame_queue.VisitMirror(after_sequence,copy);
}
void SetNativeBackendFrameConsumerActive(bool active) {
  State().scene_frame_queue.SetActive(active);
}
void SetNativeBackendFrameReadyCallback(std::function<void()> callback) {
  State().scene_frame_queue.SetReadyCallback(std::move(callback));
}
bool NativeFramerateUnlockActive() {
  return REXCVAR_GET(edf_native_unlock_framerate) &&
    !State().movie_pacing_active.load(std::memory_order_relaxed);
}

bool VisitNativePresentationSharedFrame(NativeFrameHandoff::SharedFrame& shared,uint64_t& sequence) {
  auto& state=State();
  std::lock_guard lock(state.mutex);
  if(!state.presentation_frames) return false;
  shared=state.presentation_frames->Shared();
  sequence=state.presentation_frames->SharedSequence();
  return bool(shared);
}

bool VisitNativePresentationFrame(const NativeFrameHandoff::Consumer& consumer) {
  auto& state=State();
  HookTiming wait(HookPhase::PresentationContextWait);
  std::lock_guard lock(state.mutex);
  wait.Finish();
  return state.presentation_frames && state.presentation_frames->Visit(consumer);
}
bool VisitNativePresentationContext(
    const std::function<void(ID3D11Device&,ID3D11DeviceContext&)>& consumer) {
  auto& state=State();
  HookTiming wait(HookPhase::PresentationContextWait);
  std::lock_guard lock(state.mutex);
  wait.Finish();
  if(!state.presentation_frames) return false;
  state.presentation_frames->VisitContext(consumer);
  return true;
}
// Native mode is the only path permitted to publish guest GPU completion.
// Cursor and fence come from the same completed native event, never from the
// current issued value or an unsubmitted target. Caller holds the bridge lock.
void PublishNativeCompletion(const GuestReader& reader,Bridge& state,uint32_t device,bool allow_flush) {
  const auto found=state.completion_queues.find(device);
  if(found==state.completion_queues.end() || !found->second)
    throw std::runtime_error("native completion has no submitted queue");
  const auto completed=found->second->Poll(allow_flush);
  if(!completed) return;
  const auto published=state.native_published_completions.find(device);
  if(published!=state.native_published_completions.end() && published->second==*completed) return;
  const auto writeback=reader.Word(reader.Add(device,10768));
  reader.StoreWord(reader.Add(writeback,4),found->second->completed_cursor().value());
  reader.StoreWord(writeback,*completed);
  state.native_published_completions.insert_or_assign(device,*completed);
}
// Queries may finish while C410 is returning into C868, before the latter's
// busy increment. Defer dispatch across the entire enclosing CPU transaction.
class NativeSignalSubmissionScope {
 public:
  explicit NativeSignalSubmissionScope(uint32_t device):device_(device) {
    auto global=rex::thread::global_critical_region::AcquireDirect();
    auto& state=State();
    std::lock_guard lock(state.signal_delivery_mutex);
    state.signal_deliveries[device_].BeginSubmission();
  }
  ~NativeSignalSubmissionScope() {
    auto& state=State();
    std::lock_guard lock(state.signal_delivery_mutex);
    state.signal_deliveries.at(device_).EndSubmission();
  }
  NativeSignalSubmissionScope(const NativeSignalSubmissionScope&)=delete;
  NativeSignalSubmissionScope& operator=(const NativeSignalSubmissionScope&)=delete;
 private:
  uint32_t device_;
};
void PollNativeWorkerSignals(PPCContext& ctx,uint8_t* base,uint32_t device,bool allow_flush=false) {
  // Match interrupt-side serialization, without entering the Xbox GPU ISR or
  // borrowing/modifying a guest thread's PCR CPU/TLS state. Only the audited
  // EBA0 event contract is implemented here, not arbitrary guest callbacks.
  auto& state=State();
  bool needs_gpu_poll=true;
  {
    std::lock_guard delivery_lock(state.signal_delivery_mutex);
    const auto found=state.signal_deliveries.find(device);
    if(found!=state.signal_deliveries.end()) {
      if(found->second.submitting()) return;
      needs_gpu_poll=!found->second.pending();
    }
  }
  // An occupied CPU delivery slot needs no immediate-context access. Release
  // its lock before acquiring the renderer lock on the GPU polling path.
  if(needs_gpu_poll) {
    std::lock_guard lock(state.mutex);
    std::lock_guard delivery_lock(state.signal_delivery_mutex);
    auto& delivery=state.signal_deliveries[device];
    if(delivery.submitting()) return;
    if(!delivery.pending()) {
      const auto found=state.signal_queues.find(device);
      if(found==state.signal_queues.end()) return;
      const auto ready=found->second->PeekCompleted(1,allow_flush);
      if(ready.empty()) return;
      delivery.Enqueue(ready.front()); // Nonallocating handoff before retiring GPU ownership.
      found->second->AcknowledgeCompleted(1);
    }
  }
  // No renderer lock is held while acquiring the SDK lock or waking workers.
  auto global=rex::thread::global_critical_region::AcquireDirect();
  std::lock_guard delivery_lock(state.signal_delivery_mutex);
  auto& delivery=state.signal_deliveries.at(device);
  if(delivery.submitting() || !delivery.pending()) return;
  const GuestReader reader(base);
  // Keep ownership if the guest's single publication slot is occupied.
  if(reader.Word(reader.Add(device,10900))) return;
  if(reader.Word(reader.Word(0x82000720))!=device)
    throw std::runtime_error("native worker signal device no longer matches callback global");
  NativeSignal delivered{};
  const bool completed=delivery.Deliver([&](const NativeSignal& signal,uint32_t cpu) {
    if(signal.callback!=0x8214EBA0) throw std::runtime_error("unsupported native worker callback");
      // EBA0 publishes its command-list argument before waking the selected
      // worker. The worker, not this producer, executes jobs and retires busy.
      reader.StoreWord(reader.Add(device,10900),signal.argument);
      auto event_context=ctx;
      event_context.r3.u64=reader.Add(device,11228+cpu*56);
      event_context.r4.u64=1;
      event_context.r5.u64=0;
      __imp__KeSetEvent(event_context,base);
      delivered=signal;
  });
  if(completed) {
    static std::atomic<uint64_t> dispatched{0};
    const auto count=dispatched.fetch_add(1,std::memory_order_relaxed)+1;
    if(count<=8 || !(count&(count-1)))
      REXLOG_INFO("Native worker signal completed: count={}, device={:#x}, argument={:#x}, CPUs={:#x}, busy={}",
        count,device,delivered.argument,delivered.cpu_mask,reader.Word(reader.Add(device,10868)));
  }
}
}

REX_EXTERN(__imp__sub_821A4BA0);
thread_local constinit NativeLoopBudget native_loop_budget;
// Leaked: guest frees (820B2510) may still arrive during static destruction.
edf::native::NativeModelPublications& ModelPublications() { static auto* value=new edf::native::NativeModelPublications; return *value; }
// Pose vectors rebuilt by 821C9478 during this thread's 821A4DE8 dirty walk
// (slot +8 after the helper join); null outside that walk.
thread_local constinit std::vector<uint32_t>* native_model_dirty_poses=nullptr;
// World owners whose tree the 820B4250 post-hook published (current at the
// time) since this thread's last 821A4DE8 publication, which skips them while
// they are still current instead of comparing every tree byte a second time.
thread_local std::vector<uint32_t> native_step_trees;
thread_local constinit NativeLoopBudget native_render_budget;
thread_local constinit uint64_t native_render_publication=0;
// Whether the guest render helper running on this thread (the 821A5080 hook's
// guest-helper and frame-dispatch routes) is its tick's advancing render
// (edf::native::NativeTickGate over native_render_budget). True outside a
// helper and on every locked render, so guest code there is unchanged; false
// only on an unlocked render that dispatched no simulation step. The
// clEffectEtc02 slot 4 hook (8217C4A0) keeps +612 on such a render, and the
// native post loop of the 820B0B80 hook drops the DownsampleTone pass
// (PostIssueWithoutPass) so the tone history blends once per tick. The full
// frame reads NativeFrameInputs::tick_frame instead.
thread_local constinit bool native_render_tick_frame=true;
// The shadow render's guest side (edf_native_shadow_render,
// native_shadow_render.h) on this thread; null outside it, always null with
// the cvar off. Counts what its guest route held back.
thread_local constinit NativeShadowGuest* native_shadow_guest=nullptr;
namespace {
struct NativeModelRenderContext {
  uint32_t source=0;
  const std::vector<edf::native::NativePoseMatrix>* poses=nullptr;
};
thread_local const NativeModelRenderContext* native_model_render_context=nullptr;
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
  {
    edf::native::HookTiming timing(edf::native::HookPhase::ResourceCoordinator);
    edf::native::HookTiming engine_timing(edf::native::HookPhase::SimulationDispatch);
    const edf::native::EngineRegionScope region(edf::native::EngineRegion::Dispatch,ctx.r4.u32,ctx.fpscr.csr);
    __imp__sub_821A4BA0(ctx,base);
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
REX_EXTERN(__imp__sub_821C9478);
REX_HOOK_RAW(sub_821C9478) {
  edf::native::HookTiming timing(edf::native::HookPhase::RenderPose);
  // r4 is the output pose vector; recording its address keeps the tick O(dirty).
  const auto vector=ctx.r4.u32;
  __imp__sub_821C9478(ctx,base);
  if(native_model_dirty_poses) native_model_dirty_poses->push_back(vector);
}
REX_EXTERN(sub_821B94E8);
namespace edf::native {
thread_local constinit PostFinishRecorder* post_finish_recorder=nullptr;
}
namespace edf::native {
void RetireGroupOrder(uint32_t owner) {
  // Destructor path of every world: stay off the bridge lock unless orders exist.
  if(!EDF_NATIVE_FLAG(scene_group_order) && !REXCVAR_GET(edf_native_scene_group_order_audit)) return;
  auto& state=State();
  std::lock_guard lock(state.mutex);
  state.scene_adapter.RetireGroupOrder(owner);
  WorldPublications().RetiredOrder(owner);
}
// Only the 820B4038 static walk and its audit consume plans. The full frame
// reads its route words live at selection (NativeFullFrameLiveRoutes), for the
// objects culling keeps, so it publishes nothing per step for them.
bool NativeStaticWalkPlansEnabled() {
  return REXCVAR_GET(edf_native_scene_static_walk) || REXCVAR_GET(edf_native_scene_static_walk_audit);
}
void RetireStaticWalkPlans(uint32_t owner) {
  if(!NativeStaticWalkPlansEnabled()) return;
  auto& state=State();
  std::lock_guard lock(state.mutex);
  state.static_walk_plans.Retire(owner);
}
// After a guest link/unlink: each anchor is a list header or a member node,
// so the list it belongs to (by the plan's node index) loses its plan. The
// caller holds the bridge lock.
void TouchStaticWalkPlansLocked(Bridge& state,std::initializer_list<uint32_t> anchors) {
  if(!NativeStaticWalkPlansEnabled()) return;
  for(const auto anchor:anchors) state.static_walk_plans.Touch(anchor);
}
void TouchStaticWalkPlans(std::initializer_list<uint32_t> anchors) {
  if(!NativeStaticWalkPlansEnabled()) return;
  auto& state=State();
  std::lock_guard lock(state.mutex);
  TouchStaticWalkPlansLocked(state,anchors);
}
// 820B4250 post-hook: the world's plan, published after its tree, under the
// bridge lock (plans read the scene sources); with plans reused that is two
// header words per list through a page window.
void PublishStaticWalkPlans(uint8_t* base,uint32_t owner) {
  if(!NativeStaticWalkPlansEnabled()) return;
  HookTiming timing(HookPhase::SimStaticWalk);
  auto& state=State();
  const auto epoch=TreePublications().Epoch();
  const GuestReader backing(base);
  const NativeSceneCpuWindow reader(backing);
  HookTiming wait(HookPhase::SimLockWait);
  std::lock_guard lock(state.mutex);
  wait.Finish();
  try {
    state.static_walk_plans.SetAnchorFilter(&SceneAnchors());
    const auto& sources=state.scene_sources;
    state.static_walk_plans.Publish(reader,owner,epoch,sources.CandidateRevision(),
      [&](uint32_t object) { return sources.FindCandidate(object); });
    const auto& stats=state.static_walk_plans.stats();
    if(stats.publications<=4 || stats.publications%1000==0)
      REXLOG_INFO("Native static walk plan: owner={:#x} publications={} collections={} builds={} reuses={} refreshes={} touches={} lists={} nodes={}",
        owner,stats.publications,stats.collections,stats.builds,stats.reuses,stats.refreshes,stats.touches,
        state.static_walk_plans.lists(),state.static_walk_plans.nodes());
  } catch(const std::exception& error) {
    state.static_walk_plans.Retire(owner);
    static std::set<std::string> reported;
    static std::mutex reported_mutex;
    std::lock_guard reported_lock(reported_mutex);
    if(reported.insert(error.what()).second) REXLOG_INFO("Native static walk plan deferred: {}",error.what());
  }
}
}
#define EDF_TREE_MUTATION(address) \
  REX_EXTERN(__imp__sub_##address); \
  REX_HOOK_RAW(sub_##address) { \
    edf::native::TreePublications().Invalidate(); \
    __imp__sub_##address(ctx,base); \
  }
EDF_TREE_MUTATION(821C7740)
EDF_TREE_MUTATION(821C5730)
EDF_TREE_MUTATION(821C5488)
EDF_TREE_MUTATION(821C49A0)
#undef EDF_TREE_MUTATION
REX_EXTERN(__imp__sub_820B5F38);
REX_HOOK_RAW(sub_820B5F38) {
  edf::native::TreePublications().Retire(ctx.r3.u32);
  edf::native::RetireGroupOrder(ctx.r3.u32);
  edf::native::RetireStaticWalkPlans(ctx.r3.u32);
  __imp__sub_820B5F38(ctx,base);
}
REX_EXTERN(__imp__sub_821C61D8);
REX_EXTERN(__imp__sub_821C3070);
REX_EXTERN(__imp__sub_821C3178);
REX_EXTERN(sub_820B4038);
REX_HOOK_RAW(sub_821C61D8) {
  edf::native::HookTiming timing(edf::native::HookPhase::RenderChildren);
  const edf::native::GuestReader reader(base);
  const auto manager=ctx.r3.u32,context=ctx.r4.u32;
  if(!EDF_NATIVE_FLAG(scene_tree) || !EDF_NATIVE_FLAG(shader_bridge) ||
     *reader.Bytes(reader.Add(manager,108),1)) {
    __imp__sub_821C61D8(ctx,base); return;
  }
  auto work=ctx;
  if(work.r1.u32<320) throw std::runtime_error("invalid native tree stack");
  const auto stack=work.r1.u32-320;
  reader.StoreWord(stack,work.r1.u32); work.r1.u64=stack;
  uint64_t checks=0;
  auto& publications=edf::native::TreePublications();
  std::shared_ptr<const edf::native::NativeSceneTreeImage> hierarchy;
  if(EDF_NATIVE_FLAG(scene_tree_published)) {
    if(EDF_NATIVE_FLAG(scene_membership_owned) && edf::native::native_scene_publication) {
      const auto& trees=edf::native::native_scene_publication->trees;
      const auto found=trees.find(manager);
      if(found!=trees.end()) hierarchy=found->second;
    } else hierarchy=publications.Acquire(manager);
  }
  const bool audit=REXCVAR_GET(edf_native_scene_visibility_audit);
  // Live node reads, the manager+100 counter and every gather's owner headers
  // go through one page window for the walk. Each admission is an SDK region
  // query (heap lock plus a scan of the region's page entries), so a word read
  // through the bare reader costs one; admissions stay valid until this thread
  // makes a guest call, which drops them all.
  const edf::native::NativeSceneCpuWindow window(reader,&edf::native::BridgeWalkLock::guest_calls);
  edf::native::NativeSceneTreeReader tree_reader(window,publications,std::move(hierarchy),audit);
  // One bridge lock scope for the walk: each leaf list's gather reuses it,
  // every guest call below releases it and so does the end of every list, so
  // the hold never spans the traversal. The view is read once and again only
  // after a guest call, which alone can change camera data; the gathers share
  // it, and releasing the lock does not invalidate it.
  edf::native::BridgeWalkLock walk_lock(edf::native::State().mutex);
  edf::native::BridgeWalkView walk_view{&walk_lock,context,{},&window};
  const auto outer_view=std::exchange(edf::native::bridge_walk_view,&walk_view);
  struct RestoreWalkView {
    edf::native::BridgeWalkView* outer;
    ~RestoreWalkView() { edf::native::bridge_walk_view=outer; }
  } restore_view{outer_view};
  edf::native::TraverseNativeSceneTree(tree_reader,manager,[&](uint32_t node) {
    const auto& view=walk_view.view.Get(edf::native::BridgeWalkLock::guest_calls,
      [&] { return edf::native::ReadNativeSceneVisibilityView(window,context); });
    const auto node_class=edf::native::ClassifyNativeSceneTreeNode(tree_reader,node,view);
    const auto& transformed=node_class.transformed;
    const auto radius=node_class.radius;
    const auto sphere=node_class.sphere,result=node_class.result;
    if(audit) {
      const auto camera=reader.Word(reader.Add(context,16));
      for(uint32_t i=0;i<4;++i) reader.StoreWord(reader.Add(stack,80+i*4),std::bit_cast<uint32_t>(transformed[i]));
      work.r3.u64=reader.Add(camera,288); work.r4.u64=reader.Add(stack,80); work.f1.f64=radius;
      work.lr=0x821C6024;
      { edf::native::BridgeGuestCall guest; __imp__sub_821C3070(work,base); }
      const auto original_sphere=work.r3.u32;
      auto original=original_sphere;
      if(original_sphere==2) {
        work.r3.u64=reader.Add(camera,288); work.r4.u64=reader.Add(camera,96);
        work.r5.u64=reader.Add(node,32); work.r6.u64=reader.Add(node,48);
        work.lr=0x821C605C;
        { edf::native::BridgeGuestCall guest; __imp__sub_821C3178(work,base); }
        original=work.r3.u32;
      }
      if(sphere!=original_sphere || result!=original) throw std::runtime_error("native tree classification differs from original");
      ++checks;
    }
    return result;
  },[&](uint32_t list) {
    work.r3.u64=manager; work.r4.u64=list; work.r5.u64=context; work.lr=0x821C56F8;
    sub_820B4038(work,base);
    walk_lock.EndList();
  });
  walk_lock.Release();
  static std::atomic<uint64_t> traversals=0,audited=0,owned_reads=0,live_reads=0;
  audited+=checks;
  owned_reads+=tree_reader.owned_reads; live_reads+=tree_reader.live_reads;
  const auto count=++traversals;
  if(count<=4 || count%1000==0) REXLOG_INFO("Native scene tree: traversals={} classification_checks={} mismatches=0 owned_reads={} live_reads={}",
    count,audited.load(),owned_reads.load(),live_reads.load());
}
REX_EXTERN(__imp__sub_820B4250);
REX_HOOK_RAW(sub_820B4250) {
  edf::native::HookTiming timing(edf::native::HookPhase::RenderWorld);
  const auto owner=ctx.r3.u32;
  std::optional<edf::native::NativeScenePassAnimation> published;
  if(REXCVAR_GET(edf_native_scene_material_audit) || EDF_NATIVE_FLAG(scene_material_owned)) {
    const edf::native::GuestReader reader(base);
    published=edf::native::NativeScenePassAnimation{
      reader.Word(reader.Add(owner,356)),reader.Word(reader.Add(owner,364))};
  }
  __imp__sub_820B4250(ctx,base);
  if(EDF_NATIVE_FLAG(scene_tree_published)) {
    edf::native::HookTiming trees(edf::native::HookPhase::SimTrees);
    try {
      // Node and region reads go through one page window: each bare read is a
      // committed-page query, and an unchanged tree is every region compared.
      const edf::native::GuestReader backing(base);
      const edf::native::NativeSceneCpuWindow reader(backing);
      if(edf::native::TreePublications().Publish(reader,owner)) {
        native_step_trees.push_back(owner);
        static std::atomic<uint64_t> publications=0;
        const auto count=++publications;
        if(count<=4 || count%1000==0) REXLOG_INFO("Native tree producer publication: owner={:#x} completed={}",owner,count);
      }
    } catch(const std::exception& error) {
      edf::native::TreePublications().Retire(owner);
      static std::set<std::string> reported;
      static std::mutex reported_mutex;
      std::lock_guard lock(reported_mutex);
      if(reported.insert(error.what()).second) REXLOG_INFO("Native tree publication deferred: {}",error.what());
    }
  }
  edf::native::PublishStaticWalkPlans(base,owner);
  // Both publications below are no-ops for an unchanged value, which is the
  // common case at every step. WorldPublications mirrors what the adapter
  // holds, so an unchanged value is proven without the bridge mutex, which the
  // full frame's passes hold in long slices on the render thread (a locked
  // no-op here cost 0.26 ms a call in the intro, 0.02 ms on the guest path).
  if(EDF_NATIVE_FLAG(scene_group_order) || REXCVAR_GET(edf_native_scene_group_order_audit)) {
    static thread_local std::vector<uint32_t> order;
    try {
      const edf::native::GuestReader reader(base);
      edf::native::CaptureNativeSceneGroupOrder(reader,reader.Add(owner,240),order);
      auto& state=edf::native::State();
      edf::native::WorldPublications().PublishOrder(owner,order,[&] { return std::unique_lock(state.mutex); },
        [&](std::span<const uint32_t> value) {
          if(!state.scene_adapter.PublishGroupOrder(owner,value)) return;
          static std::atomic<uint64_t> changes=0;
          const auto count=++changes;
          if(count<=4 || count%1000==0) REXLOG_INFO("Native group order publication: owner={:#x} groups={} changes={}",owner,value.size(),count);
        });
    } catch(const std::exception& error) {
      edf::native::RetireGroupOrder(owner);
      static std::set<std::string> reported;
      static std::mutex reported_mutex;
      std::lock_guard lock(reported_mutex);
      if(reported.insert(error.what()).second) REXLOG_INFO("Native group order publication deferred: {}",error.what());
    }
  }
  if(published) {
    auto& state=edf::native::State();
    edf::native::WorldPublications().PublishAnimation(owner,*published,[&] { return std::unique_lock(state.mutex); },
      [&](const edf::native::NativeScenePassAnimation& value) { state.scene_adapter.PublishWorldAnimation(owner,value); });
  }
}
REX_EXTERN(__imp__sub_820B4310);
REX_EXTERN(sub_821C61D8);
REX_EXTERN(sub_821C3BB8);
REX_HOOK_RAW(sub_820B4310) {
  if(REXCVAR_GET(edf_native_scene_group_order_audit)) {
    static thread_local std::vector<uint32_t> live;
    const edf::native::GuestReader reader(base);
    bool captured=true;
    try { edf::native::CaptureNativeSceneGroupOrder(reader,reader.Add(ctx.r3.u32,240),live); }
    catch(const std::exception& error) {
      // A torn list is an audit finding, never an exception through guest code.
      static uint64_t failures=0;
      if(++failures<=8) REXLOG_INFO("Native group order audit: owner={:#x} live walk failed: {}",ctx.r3.u32,error.what());
      captured=false;
    }
    if(captured) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    const bool matched=state.scene_adapter.AuditGroupOrder(ctx.r3.u32,live);
    const auto& audit=state.scene_adapter.group_order_audit();
    if(audit.checks<=4 || audit.checks%1000==0 || (!matched && audit.mismatches+audit.missing<=64))
      REXLOG_INFO("Native group order audit: owner={:#x} groups={} checks={} mismatches={} missing={}",
        ctx.r3.u32,live.size(),audit.checks,audit.mismatches,audit.missing);
    }
  }
  if(REXCVAR_GET(edf_native_scene_material_audit) || EDF_NATIVE_FLAG(scene_material_owned)) {
    edf::native::native_scene_animation_owner=ctx.r3.u32;
    edf::native::native_scene_pass_animation.reset();
    // Select completed producer inputs at world-pass entry. The outer helper
    // can begin before this world's update has published its next generation.
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    edf::native::native_scene_pass_animations=state.scene_adapter.AcquireWorldAnimations();
    if(const auto publication=edf::native::native_scene_pass_animations) {
      const auto found=publication->find(ctx.r3.u32);
      if(found!=publication->end()) edf::native::native_scene_pass_animation=found->second;
    }
  }
  if(EDF_NATIVE_FLAG(scene_tree) && EDF_NATIVE_FLAG(shader_bridge)) {
    const edf::native::GuestReader reader(base);
    const auto owner=ctx.r3.u32,context=ctx.r4.u32;
    auto work=ctx;
    if(work.r1.u32<112) throw std::runtime_error("invalid native world dispatch stack");
    const auto stack=work.r1.u32-112;
    reader.StoreWord(stack,work.r1.u32); work.r1.u64=stack;
    work.r3.u64=owner; work.r4.u64=reader.Add(owner,372); work.r5.u64=context;
    work.lr=0x820B4338; sub_820B4038(work,base);
    work.r3.u64=owner; work.r4.u64=context; work.lr=0x820B4344; sub_821C61D8(work,base);
    // The static opaque world pass: native groups in published order, guest
    // 821D96D8 per unsupported group, original 821C3BB8 when no order applies.
    // edf_native_ab_alternate latches a guest side on alternate frames for image A/B.
    // A throw must never unwind through guest code: the pass is switched off for
    // the rest of the run and this frame's groups go to the original walk.
    static std::atomic<bool> world_pass_failed=false;
    bool drawn=false;
    if(!world_pass_failed.load(std::memory_order_relaxed) &&
       edf::native::NativeStaticWorldPassEnabled() && edf::native::NativeAbNativeSide()) {
      try { edf::native::RenderNativeStaticWorldPass(work,base,owner); drawn=true; }
      catch(const std::exception& error) {
        if(!world_pass_failed.exchange(true))
          REXLOG_ERROR("Native static world pass disabled after failure: {}",error.what());
      }
    }
    if(!drawn) { work.r3.u64=reader.Add(owner,240); work.lr=0x820B434C; sub_821C3BB8(work,base); }
  } else __imp__sub_820B4310(ctx,base);
}
REX_EXTERN(__imp__sub_820B5FA8);
REX_HOOK_RAW(sub_820B5FA8) {
  edf::native::TreePublications().Retire(ctx.r3.u32);
  {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    state.scene_adapter.RetireWorldAnimation(ctx.r3.u32);
    state.scene_adapter.RetireGroupOrder(ctx.r3.u32);
    edf::native::WorldPublications().RetiredAnimation(ctx.r3.u32);
    edf::native::WorldPublications().RetiredOrder(ctx.r3.u32);
    state.static_walk_plans.Retire(ctx.r3.u32);
  }
  __imp__sub_820B5FA8(ctx,base);
}
EDF_RENDER_PHASE(8216DA80, RenderListener)
EDF_RENDER_PHASE(820A6978, RenderUiListener)
REX_EXTERN(__imp__sub_821C3BB8);
REX_EXTERN(sub_821D96D8);
REX_HOOK_RAW(sub_821C3BB8) {
  edf::native::HookTiming timing(edf::native::HookPhase::RenderQueued);
  if(!EDF_NATIVE_FLAG(scene_tree) || !EDF_NATIVE_FLAG(shader_bridge)) {
    __imp__sub_821C3BB8(ctx,base); return;
  }
  const edf::native::GuestReader reader(base);
  auto work=ctx;
  if(work.r1.u32<112) throw std::runtime_error("invalid native group traversal stack");
  const auto stack=work.r1.u32-112;
  reader.StoreWord(stack,work.r1.u32); work.r1.u64=stack;
  edf::native::NativeMaterialPassCursor material_pass;
  struct RestoreMaterialPass {
    edf::native::NativeMaterialPassCursor* previous=edf::native::native_material_pass_cursor;
    ~RestoreMaterialPass() { edf::native::native_material_pass_cursor=previous; }
  } restore_material_pass;
  edf::native::native_material_pass_cursor=REXCVAR_GET(edf_native_scene_pass_owned)?&material_pass:nullptr;
  edf::native::DispatchNativeSceneGroups(reader,ctx.r3.u32,[&](uint32_t group) {
    work.r3.u64=group; work.lr=0x821C3C04; sub_821D96D8(work,base);
  });
}
#undef EDF_RENDER_PHASE
// Render helper entries (sub_821A5080), one per frame; census periods count these.
std::atomic<uint64_t> native_render_frames{0};
// Render helper entries whose model slot 4s ran as guest code (the guest
// helper or frame dispatch, e.g. A/B alternate frames): their 821A1730 stores
// are in the guest effect pool, which the full-frame models' pool carry then
// takes again (NativeFullFrameModelPass::guest_frames).
std::atomic<uint64_t> native_guest_slot4_frames{0};
REX_EXTERN(sub_821C0C00);
REX_EXTERN(__imp__sub_821D96D8);
REX_EXTERN(__imp__sub_821BEE68);
REX_HOOK_RAW(sub_821BEE68) {
  auto* queues=edf::native::native_scene_queues;
  if(queues && queues->enabled) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    const edf::native::GuestReader reader(base);
    const auto parts=edf::native::NativeSceneSourcesForPass(state).LodParts(ctx.r3.u32);
    bool supported=parts.has_value();
    if(parts) for(const auto& part:*parts) {
      if(!part.group || (!queues->Contains(part.group) &&
         reader.Word(reader.Add(part.group,4))!=reader.Word(reader.Add(part.group,8)))) { supported=false; break; }
    }
    if(supported) {
      for(const auto& part:*parts) queues->Push(part.group,part.instance);
      return;
    }
    // Restore all earlier selections before this unknown producer executes, so
    // mixed groups retain the exact original head-insertion order.
    queues->Materialize(reader);
    if(++state.scene_queue_fallbacks<=8)
      REXLOG_INFO("Native scene queue fallback: descriptor={:#x} known_lod={}",ctx.r3.u32,parts.has_value());
  }
  __imp__sub_821BEE68(ctx,base);
}
REX_EXTERN(sub_82137410);
REX_EXTERN(sub_82149A90);
REX_EXTERN(sub_821375C0);
REX_EXTERN(sub_821B94E8);
REX_EXTERN(sub_821D9600);
REX_EXTERN(sub_821FE358);
REX_EXTERN(__imp__edf_native_indexed_cpu_tail);
namespace {
void InstallNativeStaticGeometry(PPCContext&,uint8_t*,uint32_t,const edf::native::NativeSceneGeometrySource&,edf::native::NativeSceneGeometryInstallState&);
void RestoreNativeStaticMaterial(PPCContext&,uint8_t*,uint32_t,uint32_t);
}
REX_HOOK_RAW(sub_821D96D8) {
  edf::native::HookTiming timing(edf::native::HookPhase::RenderMaterialGroup);
  edf::native::NativeQueuedSceneGroup group;
  const auto group_address=ctx.r3.u32;
  struct Restore {
    edf::native::NativeQueuedSceneGroup& group;
    uint32_t address;
    edf::native::NativeQueuedSceneGroup* previous=edf::native::native_queued_scene_group;
    ~Restore() {
      edf::native::native_queued_scene_group=previous;
      // Report aborted groups too. Logging must never replace their exception.
      try {
        static std::array<std::atomic<uint64_t>,size_t(1)<<uint32_t(edf::native::NativeSceneBoundary::Count)> shapes{};
        const auto& run=group.execution;
        const auto count=++shapes.at(run.mask());
        if(!run.complete() || count<=4 || !(count&(count-1)))
          REXLOG_INFO("Native static group execution: group={:#x} completed={} recorded_batches={} compatibility_calls={} boundary_mask={:#x} occurrences={}",
            address,run.complete(),run.recordings(),run.Total(),run.mask(),count);
      } catch(...) {}
    }
  } restore{group,group_address};
  edf::native::native_queued_scene_group=&group;
  if(!EDF_NATIVE_FLAG(scene_queued)) {
    // No queued group without the queued scene: the indexed draw hook treats a
    // set group as a queued instance, and every guest draw then threw and
    // recorded its setup twice (about 7 ms/frame in Mission 1 gameplay).
    edf::native::native_queued_scene_group=nullptr;
    if(edf::native::native_material_pass_cursor) edf::native::native_material_pass_cursor->reset();
    __imp__sub_821D96D8(ctx,base); group.execution.Complete(); return;
  }
  size_t native_queue_size=0;
  uint32_t group_device=0;
  if(EDF_NATIVE_FLAG(host) && EDF_NATIVE_FLAG(shader_bridge) && EDF_NATIVE_FLAG(seam_draws)) {
    const edf::native::GuestReader reader(base);
    const auto address=ctx.r3.u32;
    auto instances=edf::native::native_scene_queues?edf::native::native_scene_queues->Take(address):std::vector<uint32_t>{};
    native_queue_size=instances.size();
    if(native_queue_size && reader.Word(reader.Add(address,4))!=reader.Word(reader.Add(address,8)))
      throw std::runtime_error("untracked guest producer modified a native scene queue");
    auto work=ctx;
    if(work.r1.u32<128) throw std::runtime_error("invalid native scene group stack");
    work.r1.u64=work.r1.u32-128;
    reader.StoreWord(work.r1.u32,ctx.r1.u32);
    uint32_t device=0,index_count=0;
    const auto install_geometry=[&](const edf::native::NativeSceneGeometrySource& input) {
      InstallNativeStaticGeometry(work,base,device,input,group.geometry_install);
    };
    const auto finish_geometry=[&] {
      if(!group.geometry_handoff.pending()) return;
      const auto restore_material=[&] {
        if(group.material_handoff.pending()) {
          // Activation writes material defaults into the register bank. The
          // final instance world must be restored afterwards, never before.
          group.material_handoff.Finish([&] {
            RestoreNativeStaticMaterial(work,base,group.activation_instance,device);
          },[&] { edf::native::SynchronizeNativeQueuedSceneInstance(base,device,group); });
          static uint64_t count=0;
          if(++count<=4 || count%100000==0)
            REXLOG_INFO("Native material handoff: groups={} (CPU activation follows native submission; final instance restored)",count);
        }
      };
      edf::native::FinishNativeSceneGroupHandoff(group.geometry_handoff,
        [&] { edf::native::SynchronizeNativeQueuedSceneInstance(base,device,group); },
        [&] {
          auto& state=edf::native::State();
          std::lock_guard submission(state.submissions);
          std::lock_guard lock(state.mutex);
          edf::native::FlushNativeQueuedSceneLocked(state,group);
        },install_geometry,restore_material,[&] {
        work.r3.u64=device; work.r4.u64=4; work.r5.u64=0; work.r6.u64=0; work.r7.u64=index_count;
        work.lr=0x821D97E8; __imp__edf_native_indexed_cpu_tail(work,base);
      },[&] { group.constants_clean=false; });
      if(group.geometry) {
        static uint64_t count=0;
        if(++count<=4 || count%100000==0)
          REXLOG_INFO("Native geometry handoff: groups={} (native submission precedes CPU buffer/declaration setup and draw tail)",count);
      }
    };
    const auto prepare=[&] {
      std::optional<edf::native::NativeSceneGeometrySource> published_setup;
      {
        auto& state=edf::native::State();
        std::lock_guard lock(state.mutex);
        group.material=state.scene_adapter.PreviousGroupMaterial(group_address);
        if(EDF_NATIVE_FLAG(scene_geometry_owned) && edf::native::native_scene_publication) {
          const auto* source=edf::native::NativeSceneSourcesForPass(state).FindGroup(group_address);
          if(source) {
            const auto latest=state.scene_adapter.GroupGeometry(group_address,source->revision);
            for(const auto& published:edf::native::native_scene_publication->group_geometry)
              if(published==latest && published->group==group_address && published->setup &&
                 published->geometry->backend()==state.scene_backend.get()) {
                published_setup=published->setup; break;
              }
          }
        }
      }
      device=reader.Word(reader.Add(reader.Word(0x8257BFB4),8));
      group_device=device;
      if(published_setup) {
        const auto& input=*published_setup;
        const auto eligibility=edf::native::AssessNativeStaticGroup(reader,device,work.r1.u32,input);
        group.material_compatibility_only=eligibility!=edf::native::NativeStaticGroupEligibility::Supported;
        if(group.material_compatibility_only) {
          edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::UnsupportedConfiguration);
          static std::array<std::atomic<uint64_t>,7> reasons{};
          const auto count=++reasons.at(size_t(eligibility));
          if(count<=4 || !(count&(count-1)))
            REXLOG_INFO("Native static group compatibility eligibility: reason={} count={}",unsigned(eligibility),count);
        }
        if(native_queue_size && REXCVAR_GET(edf_native_scene_geometry_deferred) &&
           REXCVAR_GET(edf_native_scene_activation_owned) && EDF_NATIVE_FLAG(scene_material_owned) &&
           !REXCVAR_GET(edf_native_scene_material_audit) && !group.material_compatibility_only) group.geometry_handoff.Defer(input);
        else install_geometry(input);
        index_count=input.count; work.r3.u64=input.material;
        static uint64_t count=0;
        if(++count<=4 || count%100000==0)
          REXLOG_INFO("Native published geometry setup: groups={} (owned descriptor inputs; CPU binding mirrors retained)",count);
      } else {
      edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::GeometrySetup);
      auto descriptor=reader.Word(reader.Add(address,12));
      work.r3.u64=device; work.r4.u64=0; work.r5.u64=reader.Add(descriptor,4);
      work.r6.u64=0; work.r7.u64=reader.Word(reader.Add(descriptor,60)); work.r8.u64=4096;
      work.lr=0x821D9730; sub_82137410(work,base);
      descriptor=reader.Word(reader.Add(address,12));
      const auto container=reader.Word(reader.Add(descriptor,72));
      const auto node=reader.Word(reader.Add(descriptor,76));
      if(!container || node==reader.Word(reader.Add(container,4)))
        throw std::runtime_error("native scene group has no index resource");
      work.r3.u64=device; work.r4.u64=reader.Word(reader.Add(node,28));
      work.lr=0x821D9764; sub_82149A90(work,base);
      descriptor=reader.Word(reader.Add(address,12));
      work.r3.u64=device; work.r4.u64=reader.Add(descriptor,84);
      work.lr=0x821D9774; sub_821375C0(work,base);
      descriptor=reader.Word(reader.Add(address,12));
      index_count=uint32_t((int32_t(reader.Word(reader.Add(descriptor,140)))/3)*3);
      work.r3.u64=reader.Word(reader.Add(reader.Word(descriptor),16));
      }
      group.activation_instance=work.r3.u32;
      if(REXCVAR_GET(edf_native_scene_material_audit) || EDF_NATIVE_FLAG(scene_material_owned)) {
        const auto publication=edf::native::native_scene_publication;
        if(publication) for(const auto& published:publication->group_materials)
          if(published->group==group_address) { group.published_material=published; break; }
        if(group.published_material) {
          try {
          if(!edf::native::native_scene_pass_camera) throw std::runtime_error("native material pass has no camera");
          group.pass_constants=group.published_material->constants;
          {
            // The published g_mWorld is not refreshed; the activation binds the live one.
            auto& state=edf::native::State();
            std::lock_guard lock(state.mutex);
            edf::native::ReadNativeSceneLiveWorldLocked(state,reader,*group.published_material,group.pass_constants);
          }
          for(auto& constant:group.pass_constants) {
            if(edf::native::native_scene_pass_camera->Apply(constant)) continue;
            if(!constant.global || (constant.name!="m_WaterTime" && constant.name!="g_SignalBrightness")) continue;
            if(!edf::native::native_scene_pass_animation) throw std::runtime_error("missing native animation pass");
            edf::native::native_scene_pass_animation->Apply(constant);
          }
          static std::set<std::string> changed_inputs;
          for(const auto& constant:group.pass_constants) {
            if(constant.name=="g_mWorld" || edf::native::NativeScenePassOwnedConstant(constant.global,constant.name)) continue;
            const auto& published=group.published_material->constants;
            const auto old=std::find_if(published.begin(),published.end(),[&](const auto& input) {
              return input.pixel==constant.pixel && input.global==constant.global && input.name==constant.name;
            });
            if(old!=published.end() && old->registers==constant.registers) continue;
            const auto key=std::string(constant.pixel?"PS ":"VS ")+(constant.global?"global ":"local ")+constant.name;
            if(changed_inputs.size()<64 && changed_inputs.insert(key).second)
              REXLOG_INFO("Native scene pass input changed since publication: {}",key);
          }
          const auto* cursor=edf::native::native_material_pass_cursor;
          if(cursor && *cursor && (*cursor)->device==device) {
            group.material_pass=(*cursor)->material.render;
            group.sampler_pass=(*cursor)->material.samplers;
            if(REXCVAR_GET(edf_native_scene_view_owned)) {
              group.pass_viewport=(*cursor)->viewport;
              group.targets=(*cursor)->targets;
            }
            static uint64_t reads=0;
            if(++reads<=4 || reads%100000==0)
              REXLOG_INFO("Native inherited pass inputs: reads={} (no live render-state or sampler reads)",reads);
          } else if(cursor) {
            group.material_pass=edf::native::ReadNativeMaterialRenderPass(reader,device);
            for(uint32_t slot=0;slot<16;++slot)
              group.sampler_pass[slot]=edf::native::ReadNativeMaterialSamplerPass(reader,device,slot);
          } else {
          group.material_pass=edf::native::ReadNativeMaterialRenderPass(reader,device);
          std::array<bool,16> read{};
          for(const auto& operation:group.published_material->program->sampler_operations) {
            if(operation.slot>=read.size()) throw std::runtime_error("invalid published sampler slot");
            if(!read[operation.slot]) {
              group.sampler_pass[operation.slot]=edf::native::ReadNativeMaterialSamplerPass(reader,device,operation.slot);
              read[operation.slot]=true;
            }
          }
          }
          } catch(const std::exception& error) {
            static std::set<std::string> errors;
            if(errors.size()<32 && errors.insert(error.what()).second)
              REXLOG_ERROR("Native scene pass input construction failed: {}",error.what());
            group.published_material.reset();
            group.material_pass.reset();
          }
        }
      }
      if(edf::native::native_material_pass_cursor) edf::native::native_material_pass_cursor->reset();
      if(REXCVAR_GET(edf_native_scene_material_deferred) && group.geometry_handoff.pending() &&
         group.published_material && group.material_pass &&
         group.published_material->program->CanDeferCpuActivation() &&
         edf::native::ObservePublishedActivation(group.activation_instance)) {
        group.material_handoff.Defer();
        return;
      }
      edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::MaterialActivation);
      work.lr=0x821D979C; sub_821B94E8(work,base);
      if(!group.published_activation_used) finish_geometry();
    };
    const auto draw=[&](uint32_t instance) {
      if(edf::native::TryAppendNativeQueuedSceneInstance(base,device,instance,group)) return;
      edf::native::SynchronizeNativeQueuedSceneInstance(base,device,group);
      if(edf::native::TryAppendPublishedNativeSceneInstance(base,device,address,instance,index_count,group)) {
        if(group.geometry_handoff.pending()) group.geometry_handoff.DrawAccepted();
        else {
          work.r3.u64=device; work.r4.u64=4; work.r5.u64=0; work.r6.u64=0; work.r7.u64=index_count;
          work.lr=0x821D97E8;
          __imp__edf_native_indexed_cpu_tail(work,base);
        }
        return;
      }
      finish_geometry();
      group.all_native_draws=false;
      edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::InstanceSetup);
      work.r3.u64=instance; work.r4.u64=device;
      work.lr=0x821D97D0; sub_821D9600(work,base);
      work.r3.u64=device; work.r4.u64=4; work.r5.u64=0; work.r6.u64=0; work.r7.u64=index_count;
      work.lr=0x821D97E8;
      edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::IndexedDraw);
      sub_821FE358(work,base);
      // A non-world override would contaminate the shared capture for unseen
      // parts. Certify every full-path instance as well as direct instances.
      if(group.population_safe) {
        auto& state=edf::native::State();
        std::lock_guard lock(state.mutex);
        try {
          group.population_safe=group.world_parameter && edf::native::NativeStaticWorldOnly(
            reader,state.scene_sources,instance,group.world_parameter->first,device);
        } catch(const std::exception&) { group.population_safe=false; }
      }
    };
    if(native_queue_size) {
      prepare();
      for(const auto instance:instances) draw(instance);
      reader.StoreWord(reader.Add(address,4),0);
    } else edf::native::VisitNativeQueuedScene(reader,address,prepare,draw);
    edf::native::SynchronizeNativeQueuedSceneInstance(base,device,group);
    finish_geometry();
    if(auto* cursor=edf::native::native_material_pass_cursor;
       cursor && group.published_activation_used && group.all_native_draws && group.material_pass) {
      const edf::native::NativeSceneMaterialPassState incoming{*group.material_pass,group.sampler_pass};
      auto next=incoming.After(*group.published_material->program);
      if(REXCVAR_GET(edf_native_material_state_audit) || REXCVAR_GET(edf_native_material_sampler_audit)) {
        edf::native::NativeSceneMaterialPassState actual;
        actual.render=edf::native::ReadNativeMaterialRenderPass(reader,device);
        for(uint32_t slot=0;slot<16;++slot)
          actual.samplers[slot]=edf::native::ReadNativeMaterialSamplerPass(reader,device,slot);
        actual=actual.Inputs();
        if(next!=actual) {
          REXLOG_ERROR("Native inherited pass mismatch: group={:#x} render_equal={} samplers_equal={}",
            group_address,next.render==actual.render,next.samplers==actual.samplers);
          throw std::runtime_error("native inherited material pass differs from CPU mirrors");
        }
      }
      *cursor=edf::native::NativeScenePassCursorState{device,std::move(next),group.pass_viewport,group.targets};
      static uint64_t count=0;
      if(++count<=4 || count%100000==0)
        REXLOG_INFO("Native inherited material pass: groups={} (owned render state and all 16 sampler slots)",count);
    }
  } else {
    if(edf::native::native_material_pass_cursor) edf::native::native_material_pass_cursor->reset();
    edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::OriginalGroup);
    __imp__sub_821D96D8(ctx,base);
  }
  auto& state=edf::native::State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  edf::native::FlushNativeQueuedSceneLocked(state,group);
  if(native_queue_size && group.population_safe && group.world_parameter && group.geometry && group.material &&
     !edf::native::BufferWrites().Pending()) {
    try {
      const edf::native::GuestReader reader(base);
      const auto populated=state.scene_adapter.PopulateGroup(state.scene_sources,group_address,group.geometry,
        {group.material,edf::native::kNativeSceneIdentity,group.view},[&](const auto& part) {
          try { return edf::native::NativeStaticWorldOnly(reader,state.scene_sources,part.instance,
                                                        group.world_parameter->first,group_device); }
          catch(const std::exception&) { return false; }
        });
      state.scene_asset_examined+=populated.examined;
      state.scene_asset_created+=populated.created;
      state.scene_asset_rejected+=populated.rejected;
    } catch(const std::exception& error) {
      if(state.scene_native_reasons.size()<32 && state.scene_native_reasons.insert(error.what()).second)
        REXLOG_INFO("Native scene group population deferred: {}",error.what());
    }
  }
  if(group.geometry && group.material) state.scene_adapter.RememberGroupMaterial(group_address,group.material);
  group.execution.Complete();
  if(native_queue_size) {
    const auto previous=state.scene_queue_instances;
    state.scene_queue_instances+=native_queue_size; ++state.scene_queue_groups;
    if(previous/1000000!=state.scene_queue_instances/1000000) {
      REXLOG_INFO("Native scene queues: instances={} groups={} fallback_batches={}",
        state.scene_queue_instances,state.scene_queue_groups,state.scene_queue_fallbacks);
      REXLOG_INFO("Native scene retained materials: captures={} reused={}",state.scene_group_material_captures,state.scene_group_material_reused);
      REXLOG_INFO("Native scene group assets: examined={} created_before_draw={} rejected={}",
        state.scene_asset_examined,state.scene_asset_created,state.scene_asset_rejected);
      REXLOG_INFO("Native scene publications: tick={} published_selections={} current_selections={}",
        state.scene_publication_tick,state.scene_published_selections,state.scene_current_selections);
    }
  }
}
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
}  // namespace (shared: bridge/bridge_shared.h)
// Host identities behind a published material program: backend, schema,
// shader variants and ready textures. Guest bytes are the caller's reads.
bool NativeSceneMaterialHostCurrent(Bridge& state,const NativeSceneMaterialProgram& program,uint32_t material,
    const std::shared_ptr<const NativeMaterialParameters::Groups>& schema) {
  if(program.backend!=state.scene_backend) return false;
  try { if(state.material_parameters.Get(material)!=schema) return false; }
  catch(const std::exception&) { return false; }
  const auto vs=state.shaders.find(program.inputs.vertex),ps=state.shaders.find(program.inputs.pixel);
  if(vs==state.shaders.end() || ps==state.shaders.end() || !vs->second.bindings || !vs->second.reversed_bindings ||
     !ps->second.bindings || !(program.vertex.bytecode==vs->second.bindings->shader().bytecode) ||
     !(program.reversed_vertex.bytecode==vs->second.reversed_bindings->shader().bytecode) ||
     !(program.pixel.bytecode==ps->second.bindings->shader().bytecode)) return false;
  if(program.textures.size()!=program.inputs.textures.size()) return false;
  for(size_t i=0;i<program.textures.size();++i) {
    const auto handle=program.inputs.textures[i].handle;
    if(!handle) { if(program.textures[i]) return false; continue; }
    const auto texture=state.textures.find(handle);
    if(texture==state.textures.end() || !texture->second.content_valid ||
       texture->second.backend!=program.textures[i]) return false;
  }
  return true;
}
namespace {
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
void PublishStaticScenePartsLocked(Bridge& state,const GuestReader& reader,uint32_t owner) {
  if(!state.scene_sources.HasOwner(owner)) return; // Nested initial model load precedes completed construction.
  // LOD owners (clMapArtifact_Base) or a fixed-record owner (clRock).
  const bool fixed=state.scene_sources.Fixed(owner);
  const auto parts=ReadNativeSceneOwnerParts(reader,owner,fixed);
  state.scene_sources.Observe(owner,parts);
  state.scene_sources.PublishWorld(owner,ReadNativeStaticWorld(reader,owner));
  state.scene_sources.PublishVisibility(owner,ReadNativeSceneOwnerVisibility(reader,owner,fixed));
  // A model replacement can remove parts/LODs as well as replace their assets.
  // Previously selected snapshots retain old native objects through submission.
  state.scene_adapter.Retire(owner);
  if(++state.scene_source_publications<=4 || state.scene_source_publications%1024==0)
    REXLOG_INFO("Native scene source events: publications={} owners={} parts={}",
      state.scene_source_publications,state.scene_sources.owners(),state.scene_sources.parts());
}
}
}
REX_EXTERN(__imp__sub_820B33B0);
REX_HOOK_RAW(sub_820B33B0) {
  const auto object=ctx.r3.u32;
  const bool scene=REXCVAR_GET(edf_native_scene_adapter_audit) || EDF_NATIVE_FLAG(scene_queued);
  if(scene) {
    auto& state=edf::native::State();
    // The destructor hook normally retired this address already. Only this
    // constructor can Born it, so an absent owner stays absent and the loader
    // need not wait for submission order to retire nothing.
    bool stale;
    { std::lock_guard lock(state.mutex);
      stale=state.scene_sources.HasOwner(object) || state.scene_adapter.HasOwner(object); }
    if(stale) {
      std::lock_guard submission(state.submissions);
      std::lock_guard lock(state.mutex);
      state.scene_adapter.Retire(object);
      state.scene_sources.Retire(object);
    }
  }
  __imp__sub_820B33B0(ctx,base);
  if(scene) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    edf::native::SceneSourceOwners().Add(object);  // Before the owner exists: see NativeAddressFilter.
    state.scene_sources.Born(object);
    edf::native::PublishStaticScenePartsLocked(state,edf::native::GuestReader(base),object);
  }
}
REX_EXTERN(__imp__sub_820B2870);
REX_HOOK_RAW(sub_820B2870) {
  if(REXCVAR_GET(edf_native_scene_adapter_audit) || EDF_NATIVE_FLAG(scene_queued)) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    state.scene_adapter.Retire(ctx.r3.u32);
    state.scene_sources.Retire(ctx.r3.u32);
  }
  __imp__sub_820B2870(ctx,base);
}
// clRock (NativeSceneFixedRecord): the scene source of its one +396 record,
// born after its constructor 820BAF98 returns (the record is loaded, its bound
// at +288 and world registers written; nothing changes them later: its slot 2
// is empty) and retired by its slot-1 destructor 820BB208, as 820B33B0/820B2870
// do for the map artifacts. The full frame's static world draws it
// (NativeFullFrameStaticRoute::fixed).
REX_EXTERN(__imp__sub_820BAF98);
REX_HOOK_RAW(sub_820BAF98) {
  const auto object=ctx.r3.u32;
  const bool scene=REXCVAR_GET(edf_native_scene_adapter_audit) || EDF_NATIVE_FLAG(scene_queued);
  if(scene) {
    auto& state=edf::native::State();
    bool stale;
    { std::lock_guard lock(state.mutex);
      stale=state.scene_sources.HasOwner(object) || state.scene_adapter.HasOwner(object); }
    if(stale) {
      std::lock_guard submission(state.submissions);
      std::lock_guard lock(state.mutex);
      state.scene_adapter.Retire(object);
      state.scene_sources.Retire(object);
    }
  }
  __imp__sub_820BAF98(ctx,base);
  if(scene) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    edf::native::SceneSourceOwners().Add(object);
    state.scene_sources.Born(object,true);
    try { edf::native::PublishStaticScenePartsLocked(state,edf::native::GuestReader(base),object); }
    catch(const std::exception& error) {
      // An unreadable record leaves the rock unpublished (the static world counts it).
      state.scene_sources.Retire(object);
      static std::atomic<uint32_t> failures{0};
      if(failures.fetch_add(1,std::memory_order_relaxed)<8) REXLOG_INFO("Native scene source: clRock {:#x} not published: {}",object,error.what());
    }
  }
}
REX_EXTERN(__imp__sub_820BB208);
REX_HOOK_RAW(sub_820BB208) {
  if(REXCVAR_GET(edf_native_scene_adapter_audit) || EDF_NATIVE_FLAG(scene_queued)) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    state.scene_adapter.Retire(ctx.r3.u32);
    state.scene_sources.Retire(ctx.r3.u32);
  }
  __imp__sub_820BB208(ctx,base);
}
REX_EXTERN(__imp__sub_820B2AC0);
REX_HOOK_RAW(sub_820B2AC0) {
  const auto owner=ctx.r3.u32;
  __imp__sub_820B2AC0(ctx,base);
  if(REXCVAR_GET(edf_native_scene_adapter_audit) || EDF_NATIVE_FLAG(scene_queued)) {
    auto& state=edf::native::State();
    // Nested in construction the owner is not yet born and cannot become so
    // on another thread; skip the submission wait for a publication of nothing.
    if(!edf::native::SceneSourceOwners().MayContain(owner)) return;
    { std::lock_guard lock(state.mutex); if(!state.scene_sources.HasOwner(owner)) return; }
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    edf::native::PublishStaticScenePartsLocked(state,edf::native::GuestReader(base),owner);
  }
}
REX_EXTERN(__imp__sub_820B2DF8);
REX_HOOK_RAW(sub_820B2DF8) {
  const auto owner=ctx.r3.u32;
  __imp__sub_820B2DF8(ctx,base);
  // Objects the scene sources never saw born have no generation: skip both locks.
  if(EDF_NATIVE_FLAG(scene_queued) && edf::native::SceneSourceOwners().MayContain(owner)) {
    // Timed with its lock wait: the other per-step bridge-mutex user inside the
    // simulation dispatch, next to the 820B4250 post-hook.
    edf::native::HookTiming timing(edf::native::HookPhase::SimWorldUpdate);
    auto& state=edf::native::State();
    // The bridge mutex alone, as the step's publication: a world update submits
    // nothing, and the submission gate would make it wait out the swap's pacing.
    std::lock_guard lock(state.mutex);
    if(const auto generation=state.scene_sources.Generation(owner)) {
      const auto world=edf::native::ReadNativeStaticWorld(edf::native::GuestReader(base),owner);
      state.scene_sources.PublishWorld(owner,world);
      state.scene_sources.PublishVisibility(owner,edf::native::ReadNativeSceneOwnerVisibility(edf::native::GuestReader(base),owner,
        state.scene_sources.Fixed(owner)));
      state.scene_adapter.UpdateWorld(owner,generation,world);
      ++state.scene_world_publications;
    }
  }
}
REX_EXTERN(__imp__sub_821C0C00);
// Per-object tail of sub_821C0C00. Only sort modes 1/2 are native; hidden,
// mode 0 (virtual render) and unknown modes (uninitialized stack key) remain
// the original routine. Shared by the hook and the native visibility walk.
// True when a sort-mode 1/2 object was inserted natively and no guest code
// ran; false leaves the object to the original routine.
static bool TryNativeBucketInsert(PPCContext& ctx,uint8_t* base) {
  if(!REXCVAR_GET(edf_native_bucket_dispatch) || REXCVAR_GET(edf_native_bucket_dispatch_audit) ||
     !edf::native::NativeAbNativeSide()) return false;
  using namespace edf::native;
  const GuestReader reader(base);
  const auto object=ctx.r3.u32,context=ctx.r4.u32;
  static std::atomic<uint64_t> inserts=0,failures=0;
  NativeBucketDispatch kind=NativeBucketDispatch::Unknown;
  try { kind=ClassifyNativeBucket(reader,object); } catch(const std::exception&) {}
  if(kind!=NativeBucketDispatch::Bucket) return false;
  // The original clears flush-to-zero before its first lfs.
  ctx.fpscr.disableFlushMode();
  try {
    InsertNativeBucket(reader,context,object);
    const auto count=++inserts;
    if(count==1 || count%1000000==0) REXLOG_INFO("Native bucket dispatch: inserts={} failures={}",count,failures.load());
    return true;
  } catch(const std::exception& e) {
    // Every read precedes the first store and the stores are the original's
    // own values in its order, so the original can redo a partial insert.
    if(++failures<=8) REXLOG_ERROR("Native bucket dispatch fell back: object={:#x} error={}",object,e.what());
  }
  return false;
}
// `native_tried`: the caller already ran TryNativeBucketInsert and it declined.
static void DispatchNativeBucketObject(PPCContext& ctx,uint8_t* base,bool native_tried=false) {
  // The audit always runs the original; only the native insert follows the A/B side.
  const bool audit=REXCVAR_GET(edf_native_bucket_dispatch_audit),
    native=REXCVAR_GET(edf_native_bucket_dispatch) && edf::native::NativeAbNativeSide();
  if(!audit && !native) { __imp__sub_821C0C00(ctx,base); return; }
  if(!audit) { if(native_tried || !TryNativeBucketInsert(ctx,base)) __imp__sub_821C0C00(ctx,base); return; }
  using namespace edf::native;
  const GuestReader reader(base);
  const auto object=ctx.r3.u32,context=ctx.r4.u32;
  static std::atomic<uint64_t> checks=0,mismatches=0,failures=0;
  NativeBucketDispatch kind=NativeBucketDispatch::Unknown;
  try { kind=ClassifyNativeBucket(reader,object); } catch(const std::exception&) {}
  if(kind!=NativeBucketDispatch::Bucket) { __imp__sub_821C0C00(ctx,base); return; }
  // The original clears flush-to-zero before its first lfs.
  ctx.fpscr.disableFlushMode();
  // Audit: shadow the native plan, run the original, compare what it wrote.
  std::optional<NativeBucketInsert> shadow;
  try { shadow=PlanNativeBucket(reader,context,object); }
  catch(const std::exception& e) { if(++failures<=8) REXLOG_ERROR("Native bucket audit plan failed: object={:#x} error={}",object,e.what()); }
  __imp__sub_821C0C00(ctx,base);
  if(!shadow) return;
  const auto* bytes=reader.Bytes(reader.Add(object,40),2);
  const uint8_t low=bytes[0],high=bytes[1];
  const auto depth=reader.Word(reader.Add(object,44)),link=reader.Word(reader.Add(object,60));
  const auto head=reader.Word(shadow->slot);
  const auto count=++checks;
  if(low!=shadow->key.low || high!=shadow->key.high || depth!=shadow->key.depth_bits ||
     link!=shadow->previous || head!=object) {
    if(++mismatches<=16) REXLOG_ERROR("Native bucket mismatch: object={:#x} mode={} key={:#x} bytes={:#x},{:#x}/{:#x},{:#x} depth={:#x}/{:#x} link={:#x}/{:#x} slot={:#x} head={:#x}/{:#x}",
      object,reader.Word(reader.Add(object,52)),shadow->key.key,shadow->key.low,shadow->key.high,low,high,
      shadow->key.depth_bits,depth,shadow->previous,link,shadow->slot,object,head);
  }
  if(count==1 || count%1000000==0) REXLOG_INFO("Native bucket audit: checks={} mismatches={}",count,mismatches.load());
}
REX_EXTERN(__imp__sub_821C0B88);
REX_HOOK_RAW(sub_821C0B88) {
  const auto owner=ctx.r3.u32;
  __imp__sub_821C0B88(ctx,base);
  if(EDF_NATIVE_FLAG(scene_queued) && edf::native::SceneSourceOwners().MayContain(owner)) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    if(state.scene_sources.HasOwner(owner)) state.scene_sources.PublishVisibility(owner,
      edf::native::ReadNativeSceneOwnerVisibility(edf::native::GuestReader(base),owner,state.scene_sources.Fixed(owner)));
  }
}
REX_EXTERN(__imp__sub_821BEF10);
REX_HOOK_RAW(sub_821BEF10) {
  const auto destination=ctx.r4.u32;
  __imp__sub_821BEF10(ctx,base);
  if(EDF_NATIVE_FLAG(scene_queued) && destination>=288 && edf::native::SceneSourceOwners().MayContain(destination-288)) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    const auto owner=destination-288;
    if(state.scene_sources.HasOwner(owner)) state.scene_sources.PublishVisibility(owner,
      edf::native::ReadNativeSceneOwnerVisibility(edf::native::GuestReader(base),owner,state.scene_sources.Fixed(owner)));
  }
}
REX_EXTERN(__imp__sub_820B4038);
REX_EXTERN(__imp__sub_821C4EB8);
REX_HOOK_RAW(sub_821C4EB8) {
  edf::native::TreePublications().Invalidate();
  const auto node=ctx.r3.u32;
  __imp__sub_821C4EB8(ctx,base);
  edf::native::TouchStaticWalkPlans({node+120});
  if(EDF_NATIVE_FLAG(scene_queued) && EDF_NATIVE_FLAG(scene_visibility)) {
    auto& state=edf::native::State(); std::lock_guard lock(state.mutex);
    edf::native::SceneAnchors().Add(node+120);
    state.scene_membership.Born(node+120); ++state.scene_membership_events;
  }
}
REX_EXTERN(__imp__sub_821C5D28);
REX_HOOK_RAW(sub_821C5D28) {
  edf::native::TreePublications().Invalidate();
  const auto node=ctx.r3.u32;
  __imp__sub_821C5D28(ctx,base);
  edf::native::TouchStaticWalkPlans({node+120});
  if(EDF_NATIVE_FLAG(scene_queued) && EDF_NATIVE_FLAG(scene_visibility)) {
    auto& state=edf::native::State(); std::lock_guard lock(state.mutex);
    edf::native::SceneAnchors().Add(node+120);
    state.scene_membership.Born(node+120,edf::native::GuestReader(base).Word(node+132));
    ++state.scene_membership_events;
  }
}
// 821A1628 / 821A1678 link and unlink every guest list node (object lists,
// update subscriptions, leaf lists). Only anchors the static walk plans or
// the scene membership track matter here; SceneAnchors() holds every one of
// them (added before tracking starts), so any other list's link skips the
// bridge lock. Tracked links take it once, not once per consumer.
REX_EXTERN(__imp__sub_821A1628);
REX_HOOK_RAW(sub_821A1628) {
  const auto anchor=ctx.r3.u32,node=ctx.r4.u32;
  const auto& anchors=edf::native::SceneAnchors();
  const bool tracked=anchors.MayContain(anchor) || anchors.MayContain(node);
  if(tracked && EDF_NATIVE_FLAG(scene_tree_published)) {
    auto& state=edf::native::State(); std::lock_guard lock(state.mutex);
    if(state.scene_membership.HasAnchor(anchor) || state.scene_membership.HasAnchor(node))
      edf::native::TreePublications().Invalidate();
  }
  __imp__sub_821A1628(ctx,base);
  if(!tracked) return;
  auto& state=edf::native::State(); std::lock_guard lock(state.mutex);
  // After the link: the destination by its anchor, the source list by the node.
  edf::native::TouchStaticWalkPlansLocked(state,{anchor,node});
  if(EDF_NATIVE_FLAG(scene_queued) && EDF_NATIVE_FLAG(scene_visibility)) {
    if(state.scene_membership.HasAnchor(anchor)) {
      edf::native::SceneAnchors().Add(node);
      state.scene_membership.InsertAfter(anchor,node,edf::native::GuestReader(base).Word(node+8));
      ++state.scene_membership_events;
    } else if(state.scene_membership.Remove(node)) ++state.scene_membership_events;
  }
}
REX_EXTERN(__imp__sub_821A1678);
REX_HOOK_RAW(sub_821A1678) {
  const auto node=ctx.r3.u32;
  const bool tracked=edf::native::SceneAnchors().MayContain(node);
  if(tracked && EDF_NATIVE_FLAG(scene_tree_published)) {
    auto& state=edf::native::State(); std::lock_guard lock(state.mutex);
    if(state.scene_membership.HasAnchor(node)) edf::native::TreePublications().Invalidate();
  }
  __imp__sub_821A1678(ctx,base);
  if(!tracked) return;
  auto& state=edf::native::State(); std::lock_guard lock(state.mutex);
  edf::native::TouchStaticWalkPlansLocked(state,{node});
  if(EDF_NATIVE_FLAG(scene_queued) && EDF_NATIVE_FLAG(scene_visibility))
    if(state.scene_membership.Remove(node)) ++state.scene_membership_events;
}
REX_EXTERN(__imp__sub_821B0198);
REX_EXTERN(__imp__sub_821C3070);
REX_EXTERN(__imp__sub_821C33E8);
REX_HOOK_RAW(sub_820B4038) {
  edf::native::HookTiming timing(edf::native::HookPhase::RenderGather);
  auto* queues=edf::native::native_scene_queues;
  if(!queues || !queues->enabled || !EDF_NATIVE_FLAG(scene_visibility)) {
    edf::native::BridgeGuestCall guest; __imp__sub_820B4038(ctx,base); return;
  }
  using namespace edf::native;
  // Inside a tree walk (821C61D8) the walk's lock scope is current and the
  // walk releases it when this list ends; standalone gathers own a scope for
  // this list. Either way a hold covers at most this list and kObjectBudget
  // candidates, and every guest call below releases it.
  std::optional<BridgeWalkLock> own_lock;
  auto* bridge=BridgeWalkLock::current;
  if(!bridge) bridge=&own_lock.emplace(State().mutex);
  auto& state=State();
  const GuestReader reader(base);
  const auto context=ctx.r5.u32;
  // The walk's view and page window when this list runs under the walk's own
  // scope; a standalone gather has its own. Every guest word below (context,
  // stack, list header, owner headers, vtable slots, group links) goes through
  // the window: one SDK region query per page per guest-call epoch instead of
  // one per word, and none per list for the pages the walk already admitted.
  auto* walk_view=bridge_walk_view && bridge_walk_view->scope==bridge && bridge_walk_view->context==context?bridge_walk_view:nullptr;
  std::optional<NativeSceneCpuWindow<GuestReader>> own_window;
  const auto& cpu=walk_view && walk_view->window?*walk_view->window:own_window.emplace(reader,&BridgeWalkLock::guest_calls);
  // Per-candidate sub-phases (render.gather.*): read once per list, and free
  // when timings are off.
  const bool phase_timing=REXCVAR_GET(edf_native_hook_timings);
  uint32_t end=0;
  const auto generation=cpu.Word(cpu.Add(context,12));
  uint32_t cursor=0;
  // The camera view is current until this thread makes a guest call; after a
  // callback it is re-read at the next candidate that needs it, not eagerly.
  NativeGuestCallCached<NativeSceneVisibilityView> own_view;
  auto& view_cache=walk_view?walk_view->view:own_view;
  const auto current_view=[&]() -> const NativeSceneVisibilityView& {
    return view_cache.Get(BridgeWalkLock::guest_calls,[&] { return ReadNativeSceneVisibilityView(cpu,context); });
  };
  current_view();
  auto* center_destination=const_cast<uint8_t*>(cpu.WritableBytes(cpu.Add(context,32),16,4));
  auto work=ctx;
  if(work.r1.u32<160) throw std::runtime_error("invalid native visibility stack");
  work.r1.u64=work.r1.u32-160;
  cpu.StoreWord(work.r1.u32,ctx.r1.u32);
  const bool audit=REXCVAR_GET(edf_native_scene_visibility_audit);
  std::shared_ptr<const NativeSceneMembership::Snapshot> membership;
  // Resolve the pass's source generation once per list, not per candidate:
  // each candidate then costs one owner lookup and no further lock.
  std::shared_ptr<const NativeSceneSources> pass_sources;
  {
    bridge->Hold();
    bool published=false;
    if(EDF_NATIVE_FLAG(scene_membership_owned) && native_scene_publication && native_scene_publication->membership) {
      const auto& lists=*native_scene_publication->membership;
      if(state.scene_membership.Current(lists)) {
        if(const auto* found=lists.lists.Find(ctx.r4.u32)) { membership=*found; published=true; }
      }
      if(!published) {
        static uint64_t fallbacks=0;
        if(++fallbacks<=4 || fallbacks%10000==0)
          REXLOG_INFO("Native published membership fallback: count={} list={:#x}",fallbacks,ctx.r4.u32);
      }
    }
    if(published) {
      end=membership->end;
      cursor=membership->members.empty()?end:membership->members.front().node;
      static uint64_t reads=0;
      if(++reads<=4 || reads%10000==0)
        REXLOG_INFO("Native published membership: lists={} revision={}",reads,native_scene_publication->membership->revision);
    } else membership=state.scene_membership.Acquire(ctx.r4.u32);
    if(!published || audit) {
      end=cpu.Word(cpu.Add(ctx.r4.u32,12)); cursor=cpu.Word(ctx.r4.u32);
    }
    if(membership && !published && !audit && (membership->end!=end ||
       (membership->members.empty()?end:membership->members.front().node)!=cursor)) {
      ++state.scene_membership_mismatches;
      if(state.scene_membership_mismatches<=8)
        REXLOG_ERROR("Native scene membership header changed outside tracked events: list={:#x}",ctx.r4.u32);
      membership.reset();
    }
    if(membership && audit) {
      std::vector<NativeSceneMembership::Member> live;
      for(auto at=cursor;at!=end;) {
        if(live.size()>=1000000) throw std::runtime_error("native membership audit list cycle");
        const auto node=ReadGuestWords<3>(reader,at);
        live.push_back({at,node[2]}); at=node[0];
      }
      ++state.scene_membership_checks;
      if(membership->end!=end || membership->members!=live) {
        ++state.scene_membership_mismatches;
        if(state.scene_membership_mismatches<=8)
          REXLOG_ERROR("Native scene membership mismatch: list={:#x} native={} guest={}",ctx.r4.u32,membership->members.size(),live.size());
        membership.reset();
      }
    }
    if(membership) ++state.scene_membership_lists;
  }
  size_t member_index=0;
  if(membership) cursor=membership->members.empty()?end:membership->members.front().node;
  pass_sources=PublishedNativeSceneSourcesForPass(state);
  // The step's static walk plan for this list (native_scene_static_walk.h).
  // Driving: membership, the vtable+16 test and source candidates come from
  // the plan; the header words stay live. Audit: the walk stays live and the
  // plan's classification is compared with it.
  const bool static_walk=REXCVAR_GET(edf_native_scene_static_walk),static_audit=REXCVAR_GET(edf_native_scene_static_walk_audit);
  std::shared_ptr<const NativeStaticWalkList> plan;
  bool plan_drives=false,plan_sources=false;
  size_t plan_index=0;
  uint64_t plan_touches=0,plan_members=0,direct_reuses=0,source_reuses=0,bucket_native=0,plan_abandoned=0;
  NativeStaticWalkAudit plan_audit;
  if(static_walk || static_audit) {
    bridge->Hold();
    plan_touches=state.static_walk_plans.Touches();
    plan=state.static_walk_plans.Acquire(ctx.r4.u32);
    // A list header rewritten outside the membership hooks drops the plan.
    if(plan && (plan->world!=ctx.r3.u32 || plan->Head()!=cpu.Word(ctx.r4.u32) || plan->end!=cpu.Word(reader.Add(ctx.r4.u32,12)))) {
      ++state.static_walk_stale; plan.reset();
    }
    if(plan) {
      ++state.static_walk_lists;
      plan_sources=plan->sources_revision==(pass_sources?pass_sources->CandidateRevision():state.scene_sources.CandidateRevision());
      plan_drives=static_walk && !static_audit;
      if(plan_drives) { membership.reset(); cursor=plan->Head(); end=plan->end; }
      else ++plan_audit.lists;
    } else ++state.static_walk_misses;
  }
  uint64_t candidates=0,retained=0,selected=0,checks=0,mismatches=0;
  uint64_t native_members=0;
  size_t visited=0;
  while(cursor!=end) {
    if(++visited>1000000) throw std::runtime_error("native visibility list cycle");
    // classify: the next member, the header (+48 marker, route words) and the
    // source candidate. Ends before any visibility math.
    HookTiming classify_timing(HookPhase::RenderGatherClassify,phase_timing);
    std::array<uint32_t,3> node;
    const NativeStaticWalkMember* planned=nullptr;
    if(plan_drives) {
      planned=&plan->members.at(plan_index);
      node={plan->Next(plan_index),0,planned->owner}; ++plan_index; ++plan_members;
    } else if(membership) {
      const auto& member=membership->members.at(member_index++);
      node={member_index<membership->members.size()?membership->members[member_index].node:end,0,member.owner};
      ++native_members;
    } else node=ReadGuestWords<3>(cpu,cursor);
    // Audit: the plan's member at the same position against the live node.
    if(plan && !plan_drives) {
      if(AuditNativeStaticWalkMember(*plan,plan_index,cursor,node[0],node[2],plan_audit)) planned=&plan->members[plan_index];
      else plan.reset();  // Positions no longer align; later members are not comparable.
      ++plan_index;
    }
    const auto owner=node[2];
    bool callback=false;
    uint32_t hidden=0,mode=0,table=0;
    bool unseen=false;
    {
      // The guest performs ordinary CPU stores here, not atomic publication.
      // Validate one complete header and do not retain its window over a call.
      auto* header=const_cast<uint8_t*>(cpu.WritableBytes(owner,80,4));
      unseen=GuestBlockWord(header+48)!=generation;
      if(unseen) {
        StoreGuestCpuWords(std::span<uint8_t>{header+48,4},std::array<uint32_t,1>{generation});
        hidden=GuestBlockWord(header+64)>>16; mode=GuestBlockWord(header+52); table=GuestBlockWord(header);
      }
    }
    if(unseen) {
      // A view of the candidate, not a retaining copy: a published generation
      // is immutable and held by pass_sources for the whole list, the plan by
      // `plan` until after the last use below, and the producer's answer by
      // `looked_up`.
      NativeSceneSources::Candidate looked_up;
      NativeSceneSources::CandidateView source;
      if(planned && plan_drives && plan_sources) { source=NativeSceneSources::View(planned->source); ++source_reuses; }
      else if(pass_sources) source=pass_sources->FindCandidateView(owner);
      else { bridge->Hold(); looked_up=state.scene_sources.FindCandidate(owner); source=NativeSceneSources::View(looked_up); }
      if(planned && !plan_drives) {
        const bool live_direct=!hidden && !mode && cpu.Word(cpu.Add(table,16))==kNativeStaticDirectRender;
        AuditNativeStaticWalkClassification(*planned,table,mode,hidden,live_direct,plan_sources?&source:nullptr,plan_audit);
      }
      // vtable+16 from the plan while the live vtable is the one it was read
      // through (vtables are image data); otherwise the live slot.
      const auto direct=[&] {
        if(planned && plan_drives && planned->vtable==table) { ++direct_reuses; return planned->direct; }
        return cpu.Word(cpu.Add(table,16))==kNativeStaticDirectRender;
      };
      const auto* published=source.visibility;
      const auto read_live=[&](bool lods) {
        const GuestReadWindow window(cpu,owner,lods?540:356);
        return ReadNativeSceneVisibility(window,owner,lods);
      };
      auto object=published?*published:read_live(false);
      ++candidates; retained+=bool(published);
      classify_timing.Finish();
      // visibility: transform, the context+32 center store, depth, sphere/box.
      HookTiming visibility_timing(HookPhase::RenderGatherVisibility,phase_timing);
      const auto& view=current_view();
      if(audit && published) {
        const auto live=read_live(true);
        if(live!=object) {
          static std::atomic<uint32_t> reports=0;
          if(reports++<16) REXLOG_ERROR("Native visibility source mismatch: owner={:#x} box={} radius={}/{} distance={}/{} lod_count={}/{} lod_thresholds={}",
            owner,live.box!=object.box,object.radius,live.radius,object.distance,live.distance,
            object.lod_count,live.lod_count,live.lod_thresholds!=object.lod_thresholds);
          ++mismatches; object=live;
        }
      }
      auto center=NativeVisibilityCenter(view,object);
      if(audit) {
        const auto camera=reader.Word(reader.Add(context,16));
        work.r3.u64=work.r1.u32+80; work.r4.u64=reader.Add(owner,288); work.r5.u64=reader.Add(camera,96);
        work.lr=0x820B40AC;
        { BridgeGuestCall guest; __imp__sub_821B0198(work,base); }
        const auto original=ReadNativeVisibilityFloats<4>(reader,work.r1.u32+80);
        if(!NativeVisibilityBitsEqual(original,center)) {
          static std::atomic<uint32_t> reports=0;
          if(reports++<16) REXLOG_ERROR("Native visibility transform mismatch: owner={:#x} native_bits={:#x},{:#x},{:#x},{:#x} original_bits={:#x},{:#x},{:#x},{:#x}",
            owner,std::bit_cast<uint32_t>(center[0]),std::bit_cast<uint32_t>(center[1]),std::bit_cast<uint32_t>(center[2]),std::bit_cast<uint32_t>(center[3]),
            std::bit_cast<uint32_t>(original[0]),std::bit_cast<uint32_t>(original[1]),std::bit_cast<uint32_t>(original[2]),std::bit_cast<uint32_t>(original[3]));
          ++mismatches; center=original;
        } else if(std::any_of(center.begin(),center.end(),[](float value){return std::isnan(value);})) {
          static std::atomic<uint64_t> nan_matches=0;
          const auto count=++nan_matches;
          if(count<=4 || count%10000==0)
            REXLOG_INFO("Native visibility NaN parity: checks={} owner={:#x} all_register_bits_match=true",count,owner);
        }
      }
      std::array<uint32_t,4> encoded_center;
      for(size_t i=0;i<4;++i) encoded_center[i]=std::bit_cast<uint32_t>(center[i]);
      // Re-admitted here, not after the callback that dropped it.
      if(!center_destination) center_destination=const_cast<uint8_t*>(cpu.WritableBytes(cpu.Add(context,32),16,4));
      StoreGuestCpuWords(std::span<uint8_t>{center_destination,16},encoded_center);
      // The shared decision (native_scene_visibility.h); the audit below may
      // still replace its classification with the original's.
      const auto selection=SelectNativeVisibility(view,object,center);
      bool visible=selection.in_range;
      if(visible) {
        auto sphere=selection.sphere;
        auto box=selection.box;
        if(audit) {
          const auto camera=reader.Word(reader.Add(context,16));
          work.r3.u64=reader.Add(camera,288); work.r4.u64=reader.Add(context,32); work.f1.f64=object.radius;
          work.lr=0x820B40D8;
          { BridgeGuestCall guest; __imp__sub_821C3070(work,base); }
          const auto original_sphere=work.r3.u32;
          uint32_t original_box=original_sphere;
          if(original_sphere==2) {
            work.r3.u64=reader.Add(camera,288); work.r4.u64=reader.Add(camera,96); work.r5.u64=reader.Add(owner,288);
            work.lr=0x820B40F8;
            { BridgeGuestCall guest; __imp__sub_821C33E8(work,base); }
            original_box=work.r3.u32;
          }
          ++checks;
          if(sphere!=original_sphere || box!=original_box) {
            static std::atomic<uint32_t> reports=0;
            if(reports++<16) REXLOG_ERROR("Native visibility classification mismatch: owner={:#x} sphere={}/{} box={}/{}",owner,sphere,original_sphere,box,original_box);
            ++mismatches; box=original_box;
          }
        }
        visible=box!=0;
      }
      visibility_timing.Finish();
      bool native_selected=false;
      // Preserve the guest hidden flag and nonzero sorting modes. Only the
      // audited static direct-dispatch method may bypass the virtual callback.
      if(visible && published && object.lod_count && queues->enabled &&
         hidden==0 && mode==0) {
        // lod: the direct-render test, the LOD's parts and their groups.
        HookTiming lod_timing(HookPhase::RenderGatherLod,phase_timing);
        if(direct()) {
          const auto parts=source.Lod(selection.lod);
          native_selected=parts.has_value();
          if(parts) for(const auto& part:*parts) {
            if(!part.group || (!queues->Contains(part.group) &&
               cpu.Word(cpu.Add(part.group,4))!=cpu.Word(cpu.Add(part.group,8)))) { native_selected=false; break; }
          }
          lod_timing.Finish();
          if(native_selected) {
            HookTiming push_timing(HookPhase::RenderGatherPush,phase_timing);
            for(const auto& part:*parts) queues->Push(part.group,part.instance);
            ++selected;
          }
        }
      }
      if(visible && !native_selected) {
        // guest_dispatch: the bucket route, including any original routine it
        // runs (inclusive of guest time).
        HookTiming guest_timing(HookPhase::RenderGatherGuest,phase_timing);
        // Classify first. Sort modes 1/2 go through the native bucket insert
        // when it is enabled: it touches guest memory only, so it is not a
        // guest call, keeps the hold, and membership, the view and the plan
        // stay valid. Only a route into the original routine is a guest call.
        const bool try_native=hidden==0 && (int32_t(mode)==1 || int32_t(mode)==2);
        work.r3.u64=owner; work.r4.u64=context; work.lr=0x820B410C;
        callback=NativeWalkBucketDispatch<BridgeMutex>(try_native,[&] { return TryNativeBucketInsert(work,base); },[&] {
          // Unported callbacks may change membership or node values. Continue
          // from the original post-callback link rather than an older snapshot.
          membership.reset();
          // No page admission, center pointer or view survives the call: the
          // window and view are keyed to the guest call count, and the center
          // is re-admitted at the next candidate that stores one.
          cpu.Invalidate(); center_destination=nullptr;
          // Hierarchy writer hooks invalidate tree images if this callback
          // changes membership, bounds or topology. Unrelated callback activity
          // must not discard every completed producer publication.
          DispatchNativeBucketObject(work,base,try_native);
        });
        bucket_native+=try_native && !callback;
      }
    }
    // Pure native math/queue selection cannot mutate membership. A remaining
    // guest dispatcher can, so only that route must reacquire the next link.
    if(callback && plan && state.static_walk_plans.Touches()!=plan_touches) {
      // A hook touched some plan during the callback: keep this one only while
      // its membership is still the list's.
      bridge->Hold();
      plan_touches=state.static_walk_plans.Touches();
      if(!state.static_walk_plans.Current(ctx.r4.u32,*plan)) { plan.reset(); plan_drives=false; ++plan_abandoned; }
    }
    if(callback) {
      const auto next=cpu.Word(cursor);
      // A driving plan continues only onto the node it expects next.
      if(plan_drives && next!=node[0]) { plan.reset(); plan_drives=false; ++plan_abandoned; }
      cursor=next;
    } else cursor=node[0];
    bridge->Advance();
  }
  if(plan && !plan_drives && plan_index!=plan->members.size()) ++plan_audit.membership;  // The plan holds more members.
  bridge->Hold();
  if(static_walk || static_audit) {
    state.static_walk_members+=plan_members; state.static_walk_direct_reuses+=direct_reuses;
    state.static_walk_source_reuses+=source_reuses; state.static_walk_abandoned+=plan_abandoned;
    state.static_walk_bucket_native+=bucket_native;
    auto& total=state.static_walk_audit;
    total.lists+=plan_audit.lists; total.members+=plan_audit.members; total.classified+=plan_audit.classified;
    total.membership+=plan_audit.membership; total.routes+=plan_audit.routes; total.sources+=plan_audit.sources;
    total.drift+=plan_audit.drift; total.abandoned+=static_audit?plan_abandoned:0;
    if(plan_audit.mismatches()) {
      static uint64_t reports=0;
      if(++reports<=16) REXLOG_ERROR("Native static walk audit mismatch: list={:#x} membership={} routes={} sources={}",
        ctx.r4.u32,plan_audit.membership,plan_audit.routes,plan_audit.sources);
    }
    static uint64_t walks=0;
    if(++walks<=4 || walks%100000==0)
      REXLOG_INFO("Native static walk: lists={} misses={} stale={} members={} direct_reuses={} source_reuses={} abandoned={} bucket_native={} audit_lists={} audit_members={} audit_classified={} mismatches={} (membership={} routes={} sources={}) drift={}",
        state.static_walk_lists,state.static_walk_misses,state.static_walk_stale,state.static_walk_members,
        state.static_walk_direct_reuses,state.static_walk_source_reuses,state.static_walk_abandoned,state.static_walk_bucket_native,
        total.lists,total.members,total.classified,total.mismatches(),total.membership,total.routes,total.sources,total.drift);
  }
  const auto previous=state.scene_visibility_candidates;
  state.scene_visibility_candidates+=candidates; state.scene_visibility_retained+=retained;
  state.scene_visibility_selected+=selected; state.scene_visibility_checks+=checks;
  state.scene_visibility_mismatches+=mismatches;
  state.scene_membership_nodes+=native_members;
  if(mismatches) REXLOG_ERROR("Native scene visibility mismatch: candidates={} mismatches={}",candidates,mismatches);
  if(previous/1000000!=state.scene_visibility_candidates/1000000) {
    REXLOG_INFO("Native scene visibility: candidates={} retained={} selected={} checks={} mismatches={}",
      state.scene_visibility_candidates,state.scene_visibility_retained,state.scene_visibility_selected,
      state.scene_visibility_checks,state.scene_visibility_mismatches);
    REXLOG_INFO("Native scene membership: events={} lists={} nodes={} checks={} mismatches={} registered={} live_nodes={}",
      state.scene_membership_events,state.scene_membership_lists,state.scene_membership_nodes,
      state.scene_membership_checks,state.scene_membership_mismatches,state.scene_membership.lists(),state.scene_membership.nodes());
  }
}
REX_HOOK_RAW(sub_821C0C00) {
  if(REXCVAR_GET(edf_native_scene_adapter_audit)) {
    const edf::native::GuestReader reader(base);
    const auto object=ctx.r3.u32;
    const auto table=reader.Word(object);
    const auto method=reader.Word(reader.Add(table,16));
    static std::mutex audit_mutex;
    static std::map<uint32_t,std::vector<uint32_t>> samples;
    std::lock_guard lock(audit_mutex);
    if(samples.contains(method) || samples.size()<32) {
      auto& objects=samples[method];
      if(objects.size()<4 && std::find(objects.begin(),objects.end(),object)==objects.end()) {
        objects.push_back(object);
        REXLOG_INFO("Native scene source: object={:#x} vtable={:#x} render={:#x} owner={:#x} mode={} parameter={:#x}",
          object,table,method,reader.Word(reader.Add(object,32)),reader.Word(reader.Add(object,52)),ctx.r4.u32);
      }
    }
  }
  DispatchNativeBucketObject(ctx,base);
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
}
namespace {
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
REX_EXTERN(__imp__sub_821BE8D0);
REX_HOOK_RAW(sub_821BE8D0) {
  edf::native::HookTiming timing(edf::native::HookPhase::RenderSceneBegin);
  if(REXCVAR_GET(edf_native_scene_material_audit) || EDF_NATIVE_FLAG(scene_material_owned)) {
    const edf::native::GuestReader reader(base);
    const auto scene=ctx.r4.u32;
    const auto& cameras=edf::native::native_scene_pass_cameras;
    const auto found=cameras?cameras->find(scene):edf::native::NativeScenePassCameras::const_iterator{};
    if(cameras && found!=cameras->end()) {
      edf::native::native_scene_pass_camera=found->second;
      if(REXCVAR_GET(edf_native_scene_transform_audit) &&
         found->second!=edf::native::ReadNativeScenePassCamera(reader,scene)) {
        REXLOG_ERROR("Native published camera mismatch: scene={:#x}",scene);
        throw std::runtime_error("native camera changed after producer publication");
      }
      static uint64_t reads=0;
      if(++reads<=4 || reads%1000==0)
        REXLOG_INFO("Native published camera: reads={} audited={}",reads,REXCVAR_GET(edf_native_scene_transform_audit));
    } else {
      edf::native::native_scene_pass_camera=edf::native::ReadNativeScenePassCamera(reader,scene);
      if(cameras) {
        static uint64_t misses=0;
        if(++misses<=4 || misses%1000==0)
          REXLOG_INFO("Native camera publication fallback: scene={:#x} misses={}",scene,misses);
      }
    }
  }
  // This backend callback consumes scene+32/+96 before render-list traversal.
  // Observe its inputs, not guest state after another simulation tick. A new
  // submission with unchanged matrices is not evidence of interpolated motion.
  const auto limit=REXCVAR_GET(edf_native_motion_trace);
  if(limit>0) {
    static std::atomic<uint64_t> calls{0};
    const auto sequence=calls.fetch_add(1,std::memory_order_relaxed)+1;
    if(sequence<=uint64_t(limit)) {
      const edf::native::GuestReader reader(base);
      const auto scene=ctx.r4.u32;
      const auto fingerprint=[&](uint32_t offset) {
        const auto* bytes=reader.Bytes(reader.Add(scene,offset),64);
        uint64_t hash=14695981039346656037ull;
        for(size_t i=0;i<64;++i) { hash^=bytes[i]; hash*=1099511628211ull; }
        return hash;
      };
      const auto us=std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
      REXLOG_INFO("Native motion trace: seq={} us={} scene={:#x} viewport={} matrix32={:#x} matrix96={:#x}",
        sequence,us,scene,ctx.r5.u32,fingerprint(32),fingerprint(96));
    }
  }
  __imp__sub_821BE8D0(ctx,base);
}
REX_EXTERN(__imp__sub_821CDDF8);
REX_HOOK_RAW(sub_821CDDF8) {
  static thread_local std::unordered_map<uint32_t,edf::native::NativeCameraHistory> histories;
  const auto scene=ctx.r3.u32;
  const edf::native::GuestReader reader(base);
  // Vert+ (native_display_layout.h): 821CDDF8 derives the projection
  // (821C82C0) and the frustum (821C26C0) from the vertical angle at +480 and
  // the viewport +496/+500, so a render narrower than 16:9 would crop the
  // sides. For the main view (the viewport the renderer size gave it,
  // clSgsCoreRender::slot7) the angle it consumes is widened for the duration
  // of the call, so projection, frustum and every copy agree; +480 is restored
  // after, and the camera state the game reads keeps its own angle.
  const float fov_scale=[&] {
    static const auto mode=edf::native::ParseNativeAspectMode(REXCVAR_GET(edf_aspect));
    const auto& dimensions=edf::native::NativeRenderDimensions();
    if(!dimensions[0]) return 1.0f;
    const auto size=edf::native::ReadGuestWords<2>(reader,reader.Add(scene,496));
    const float width=std::bit_cast<float>(size[0]),height=std::bit_cast<float>(size[1]);
    if(width!=float(dimensions[0]) || height!=float(dimensions[1])) return 1.0f;
    return edf::native::NativeVerticalFovScale(width,height,mode);
  }();
  const auto derive=[&](float fov) {
    if(fov_scale==1.0f) { __imp__sub_821CDDF8(ctx,base); return; }
    auto* destination=const_cast<uint8_t*>(reader.WritableBytes(reader.Add(scene,480),4,4));
    struct Restore {
      uint8_t* destination;
      std::array<uint8_t,4> bytes;
      ~Restore() { std::memcpy(destination,bytes.data(),bytes.size()); }
    } restore{destination,{}};
    std::memcpy(restore.bytes.data(),destination,restore.bytes.size());
    const std::array<uint32_t,1> word{std::bit_cast<uint32_t>(edf::native::NativeAdjustedVerticalFov(fov,fov_scale))};
    edf::native::StoreGuestCpuWords(std::span<uint8_t>{destination,4},word);
    __imp__sub_821CDDF8(ctx,base);
  };
  const auto current_fov=[&] { return std::bit_cast<float>(reader.Word(reader.Add(scene,480))); };
  // Publication follows the render-helper join. Constructors and other callers
  // must initialize their real matrices and invalidate any reused address.
  if(ctx.lr!=0x821A4EB0 || !native_loop_budget.unlocked || native_loop_budget.divisor!=1 ||
     !REXCVAR_GET(edf_native_camera_interpolation)) {
    histories.erase(scene);
    derive(current_fov()); return;
  }
  const auto source=reader.Add(scene,416);
  const auto words=edf::native::ReadGuestWords<17>(reader,source);
  edf::native::NativeCameraPose pose;
  for(size_t i=0;i<16;++i) pose.world[i]=std::bit_cast<float>(words[i]);
  pose.fov=std::bit_cast<float>(words[16]);
  pose.viewport=edf::native::ReadGuestWords<4>(reader,reader.Add(scene,488));
  if(histories.size()>=16 && !histories.contains(scene)) histories.clear();
  auto& history=histories[scene];
  if(native_loop_budget.steps>1) history.Reset();
  const auto rendered=history.Sample(pose,native_loop_budget.tick,native_loop_budget.fraction);
  if(rendered==pose) { derive(pose.fov); return; }
  // Rebuild every derived camera matrix and its frustum from the same pose.
  // Restore authoritative simulation inputs even if the guest call throws.
  auto* destination=const_cast<uint8_t*>(reader.WritableBytes(source,68,4));
  struct Restore {
    uint8_t* destination;
    std::array<uint8_t,68> bytes;
    ~Restore() { std::memcpy(destination,bytes.data(),bytes.size()); }
  } restore{destination,{}};
  std::memcpy(restore.bytes.data(),destination,restore.bytes.size());
  std::array<uint32_t,17> interpolated;
  for(size_t i=0;i<16;++i) interpolated[i]=std::bit_cast<uint32_t>(rendered.world[i]);
  interpolated[16]=std::bit_cast<uint32_t>(edf::native::NativeAdjustedVerticalFov(rendered.fov,fov_scale));
  edf::native::StoreGuestCpuWords(std::span<uint8_t>{destination,68},interpolated);
  __imp__sub_821CDDF8(ctx,base);
}
// Render registry feeds (native_render_registry.h). Off: one cvar read each.
REX_EXTERN(__imp__sub_821C2090);
REX_HOOK_RAW(sub_821C2090) {
  const uint32_t object=ctx.r3.u32;
  __imp__sub_821C2090(ctx,base);
  if(REXCVAR_GET(edf_native_render_registry) || EDF_NATIVE_FLAG(full_frame)) edf::native::RenderRegistry().Born(object);
}
REX_EXTERN(__imp__sub_821C1FE8);
REX_HOOK_RAW(sub_821C1FE8) {
  if(REXCVAR_GET(edf_native_render_registry) || EDF_NATIVE_FLAG(full_frame)) edf::native::RenderRegistry().Died(ctx.r3.u32);
  __imp__sub_821C1FE8(ctx,base);
}
REX_EXTERN(__imp__sub_821C0D70);
REX_HOOK_RAW(sub_821C0D70) {
  // r4==1 links obj+120 into scene+100; any other value unlinks it.
  const uint32_t object=ctx.r3.u32,flag=ctx.r4.u32;
  __imp__sub_821C0D70(ctx,base);
  if(REXCVAR_GET(edf_native_render_registry) || EDF_NATIVE_FLAG(full_frame)) edf::native::RenderRegistry().Subscribed(object,flag==1);
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
REX_EXTERN(__imp__sub_821B8E48);
REX_EXTERN(sub_821498C8);
REX_EXTERN(sub_82149608);
REX_EXTERN(sub_82149248);
REX_EXTERN(sub_82149358);
REX_EXTERN(sub_8213BA98);
namespace {
template<class Reader> void BindNativeShaderResource(PPCContext&,uint8_t*,bool,const Reader&);
template<class Reader> void BindNativeTextureResource(PPCContext&,uint8_t*,const Reader&);
bool NativeTextureBindingOwned();
void PublishNativeShaderBinding(uint32_t,uint32_t,bool);
// The guest device block the activation reads and writes: sampler records at
// +1024, constant registers from +1792, render-state mirrors to +12188, the
// texture and shader bindings at +12272..+12423; the extent the static world
// handoff's device overlay covers (NativeStaticWorldDeviceOverlay::kBytes).
constexpr uint32_t kNativeActivationDeviceBytes=13520;
// states=false runs the activation without its state operations (the static
// world handoff writes those combined): shader binds with defaults, constant
// uploads and texture binds with their retirements and sampler words.
//
// Its guest accesses go through two windows proven once per activation - the
// device block (read and written: dirty halves, constant registers, sampler
// and state mirrors, bindings) and the material's 112-byte header - rather
// than a separately validated access each: a HUD Utility activation made a
// few hundred of those (each state override alone re-read the render pass,
// twenty-odd words, and published its fields through a fresh reader), and the
// HUD phase loop activates ~130 times a frame. The windows are live, so every
// value is the one the per-access reads saw; payloads, records and the stack
// keep their own validated accesses, and so does everything when the device
// window cannot be proven. The operation lists go into one program per thread
// (a nested activation, should a guest call ever make one, uses its own).
void ActivateNativeMaterial(PPCContext& ctx,uint8_t* base,uint32_t instance,uint32_t device,bool states=true) {
  const edf::native::GuestReader backing(base);
  const edf::native::GuestWritableWindow reader(backing,device,kNativeActivationDeviceBytes);
  static thread_local edf::native::NativeMaterialCpuProgram reused;
  static thread_local bool reused_busy=false;
  std::optional<edf::native::NativeMaterialCpuProgram> nested;
  auto& program=reused_busy?nested.emplace():reused;
  struct Busy {
    bool& flag; bool owner;
    explicit Busy(bool& flag):flag(flag),owner(!flag) { flag=true; }
    ~Busy() { if(owner) flag=false; }
  } busy(reused_busy);
  edf::native::ReadNativeMaterialCpuProgram(edf::native::GuestReadWindow(reader,instance,112),instance,device,program);
  auto work=ctx;
  if(work.r1.u32<144) throw std::runtime_error("native activation stack");
  work.r1.u64-=144;
  reader.StoreWord(work.r1.u32,ctx.r1.u32);
  edf::native::ExecuteNativeMaterialCpuProgram(program,[&](uint32_t shader,bool pixel) {
    work.r3.u64=device; work.r4.u64=shader; work.lr=pixel?0x821B8E84:0x821B8E70;
    BindNativeShaderResource(work,base,pixel,reader);
    PublishNativeShaderBinding(device,shader,pixel);
  },[&](const auto& operation) {
    const auto first=operation.first/4,last=(operation.first+operation.count-1)/4;
    const uint64_t mask=(~uint64_t(0)>>(first))&(~uint64_t(0)<<(63-last));
    work.r3.u64=device; work.r4.u64=operation.first; work.r5.u64=operation.data;
    work.r6.u64=operation.count; work.r7.u64=mask;
    work.lr=operation.pixel?0x821B8FBC:0x821B8ED8;
    const auto destination=reader.Add(device,(operation.first+(operation.pixel?368:112))*16);
    const size_t bytes=size_t(operation.count)*16;
    const bool disjoint=uint64_t(operation.data)+bytes<=destination || uint64_t(destination)+bytes<=operation.data;
    if(!(destination&15) && disjoint) {
      // The audited upload setters only copy these raw float4 bytes and OR
      // the supplied stage mask. Preserve their stack scratch stores as well.
      reader.StoreWord(work.r1.u32-32,device);
      reader.StoreWord(reader.Add(work.r1.u32,48),uint32_t(mask>>32));
      reader.StoreWord(reader.Add(work.r1.u32,52),uint32_t(mask));
      // The destination and the incoming dirty mask are only the audit
      // oracle's: the upload validates the destination and reads the mask
      // itself, so an unaudited activation does not read them twice.
      const bool audit=REXCVAR_GET(edf_native_scene_material_audit);
      uint8_t* target=nullptr;
      uint32_t dirty_address=0;
      uint64_t dirty=0;
      if(audit) {
        target=const_cast<uint8_t*>(reader.WritableBytes(destination,bytes,4));
        dirty_address=reader.Add(device,operation.pixel?8:0);
        dirty=(uint64_t(reader.Word(dirty_address))<<32)|reader.Word(reader.Add(dirty_address,4));
      }
      if(!edf::native::UploadNativeMaterialConstant(reader,device,operation,mask))
        throw std::runtime_error("native constant eligibility changed during activation");
      if(audit) {
        static std::mutex audit_mutex;
        static std::set<std::tuple<bool,uint32_t,uint32_t,uint32_t>> shapes;
        bool sample=false;
        {
          std::lock_guard lock(audit_mutex);
          if(shapes.size()<4096) sample=shapes.emplace(operation.pixel,operation.first,operation.count,operation.data&15).second;
        }
        if(sample) {
          const std::vector<uint8_t> expected(target,target+bytes);
          // Re-run the original with the same incoming flags. The disjoint
          // source is unchanged, so this is an independent byte-copy oracle.
          reader.StoreWord(dirty_address,uint32_t(dirty>>32));
          reader.StoreWord(reader.Add(dirty_address,4),uint32_t(dirty));
          edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::AuditOracle);
          if(operation.pixel) sub_82149358(work,base); else sub_82149248(work,base);
          const auto actual=(uint64_t(reader.Word(dirty_address))<<32)|reader.Word(reader.Add(dirty_address,4));
          if(std::memcmp(target,expected.data(),bytes) || actual!=(dirty|mask))
            throw std::runtime_error("native activation CPU constant upload differs from original");
          REXLOG_INFO("Native activation constant audit: pixel={} first={} count={} source_alignment={} bytes_and_dirty_match=true",
            operation.pixel,operation.first,operation.count,operation.data&15);
        }
      }
    } else {
      // Preserve the original vector load/store ordering for aliased uploads.
      edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::AliasedConstant);
      if(operation.pixel) sub_82149358(work,base); else sub_82149248(work,base);
    }
  },[&](const auto& operation) {
    const uint64_t mask=uint64_t(1)<<(43-operation.slot);
    work.r3.u64=device; work.r4.u64=operation.slot; work.r5.u64=operation.handle;
    work.r6.u64=mask; work.lr=0x821B9090;
    // 8213BA98's hook, through the device window when its native branch owns
    // the bind; the hook itself (and so the guest setter) otherwise.
    if(NativeTextureBindingOwned()) BindNativeTextureResource(work,base,reader);
    else sub_8213BA98(work,base);
    const auto inherited=edf::native::ReadNativeMaterialSamplerPass(reader,device,operation.slot);
    const auto resolved=edf::native::ApplyNativeMaterialSampler(inherited,operation.sampler);
    reader.StoreWord(reader.Add(device,1036+operation.slot*24),resolved.words[1]);
    reader.StoreWord(reader.Add(device,1040+operation.slot*24),resolved.words[2]);
    const auto dirty=(uint64_t(reader.Word(reader.Add(device,16)))<<32)|reader.Word(reader.Add(device,20));
    reader.StoreWord(reader.Add(device,16),uint32_t((dirty|mask)>>32));
    reader.StoreWord(reader.Add(device,20),uint32_t(dirty|mask));
  },[&](const auto& operation) {
    if(!states) return;
    work.r3.u64=device; work.r4.u64=operation.value; work.ctr.u64=operation.setter; work.lr=0x821B920C;
    const auto dirty=[&](uint32_t offset) {
      return (uint64_t(reader.Word(reader.Add(device,offset)))<<32)|reader.Word(reader.Add(device,offset+4));
    };
    const auto writes=edf::native::NativeMaterialStateCpuWrites(
      edf::native::ReadNativeMaterialRenderPass(reader,device),operation.offset,operation.value,dirty(16),dirty(24));
    if(!writes) {
      edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::StateCallback);
      rex::runtime::ResolveIndirectFunction(operation.setter)(work,base); return;
    }
    bool sample=false;
    if(REXCVAR_GET(edf_native_material_state_audit)) {
      static std::mutex audit_mutex;
      static std::set<std::pair<uint32_t,uint32_t>> cases;
      std::lock_guard lock(audit_mutex);
      if(cases.size()<4096) sample=cases.emplace(operation.offset,operation.value).second;
    }
    std::vector<uint32_t> previous;
    if(sample) for(const auto& [offset,value]:*writes) previous.push_back(reader.Word(reader.Add(device,offset)));
    for(const auto& [offset,value]:*writes) reader.StoreWord(reader.Add(device,offset),value);
    if(sample) {
      const auto expected=edf::native::ReadNativeMaterialRenderPass(reader,device);
      for(size_t i=0;i<writes->size();++i) reader.StoreWord(reader.Add(device,(*writes)[i].first),previous[i]);
      edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::AuditOracle);
      rex::runtime::ResolveIndirectFunction(operation.setter)(work,base);
      for(const auto& [offset,value]:*writes) if(reader.Word(reader.Add(device,offset))!=value)
        throw std::runtime_error("native material CPU state word differs from original at "+std::to_string(offset));
      if(edf::native::ReadNativeMaterialRenderPass(reader,device)!=expected)
        throw std::runtime_error("native material CPU state differs from original");
      REXLOG_INFO("Native activation state oracle: offset={:#x} value={:#x} words_and_dirty_match=true",operation.offset,operation.value);
    }
    // Replacing the setter also replaces its ownership-publication hook.
    // Draws consume these snapshots even when the CPU words already match.
    if(operation.offset==0x44) {
      auto& state=edf::native::State();
      std::lock_guard lock(state.mutex);
      state.render_state_snapshots.PublishBlend(device,
        edf::native::ReadGuestWords<4>(reader,reader.Add(device,10336)),operation.setter);
    } else if(operation.offset!=0x64) {
      edf::native::PublishNativeRenderStateWith(device,operation.setter,[&]() -> const auto& { return reader; });
    }
  });
  static std::atomic<uint64_t> activations=0;
  const auto count=++activations;
  if(count<=4 || count%100000==0)
    REXLOG_INFO("Native material activation: count={} (native traversal/shaders/constants/samplers/state; aliased uploads, texture retirement and scissor callbacks retained)",count);
}
void RestoreNativeStaticMaterial(PPCContext& ctx,uint8_t* base,uint32_t instance,uint32_t device) {
  // Called only while the group's activation is pending. The activation hook
  // would skip ObserveActivation in this state; retain its audit path when asked.
  static std::atomic<uint64_t> native_count=0,fallback_count=0;
  if(EDF_NATIVE_FLAG(material_activation) && !REXCVAR_GET(edf_native_material_state_audit) &&
     !REXCVAR_GET(edf_native_material_sampler_audit)) {
    ActivateNativeMaterial(ctx,base,instance,device);
    ++native_count;
  } else {
    ++fallback_count;
    edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::MaterialActivation);
    ctx.r3.u64=instance; ctx.lr=0x821D979C; sub_821B94E8(ctx,base);
  }
  const auto count=native_count.load()+fallback_count.load();
  if(count<=4 || count%100000==0)
    REXLOG_INFO("Native static material handoff: native={} compatibility={}",native_count.load(),fallback_count.load());
}
}
REX_HOOK_RAW(sub_821B8E48) {
  const auto instance = ctx.r3.u32, device = ctx.r4.u32;
  // Audit state only when audited: the library's std::map allocates its head
  // node when constructed, and this hook runs for every activation.
  const bool sampler_audit=REXCVAR_GET(edf_native_material_sampler_audit);
  std::optional<std::map<uint32_t,edf::native::NativeMaterialSamplerPass>> audited_samplers;
  if(sampler_audit) audited_samplers.emplace();
  std::optional<edf::native::NativeMaterialRenderPass> expected_state;
  if(REXCVAR_GET(edf_native_material_state_audit)) {
    const edf::native::GuestReader reader(base);
    expected_state=edf::native::ReadNativeMaterialRenderPass(reader,device);
    const auto states=reader.Word(reader.Add(instance,96)),count=reader.Word(reader.Add(instance,104));
    if(count>4096) throw std::runtime_error("invalid native material state audit count");
    for(uint32_t i=0;i<count;++i) {
      const auto operation=edf::native::ReadGuestWords<2>(reader,reader.Add(states,i*8));
      try {
        const auto setter=edf::native::NativeMaterialStateSetter(operation[0]);
        if(reader.Word(reader.Add(device,56+operation[0]))!=setter)
          throw std::runtime_error("native material state setter identity changed");
        edf::native::ApplyNativeMaterialState(*expected_state,operation[0],operation[1]);
      } catch(const std::exception& error) {
        REXLOG_ERROR("Native material state audit: material={:#x} offset={:#x} value={:#x}: {}",
          instance,operation[0],operation[1],error.what());
        throw;
      }
    }
  }
  if(sampler_audit) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    const edf::native::GuestReader reader(base);
    const auto schema=state.material_parameters.Get(instance);
    const auto operations=edf::native::ReadNativeMaterialSamplerOperations(reader,*schema);
    for(const auto& operation:operations) {
      auto found=audited_samplers->find(operation.slot);
      if(found==audited_samplers->end()) found=audited_samplers->emplace(operation.slot,
        edf::native::ReadNativeMaterialSamplerPass(reader,device,operation.slot)).first;
      found->second=edf::native::ApplyNativeMaterialSampler(found->second,operation);
    }
  }
  {
    edf::native::HookTiming timing(edf::native::HookPhase::ActivationGuest);
    if(EDF_NATIVE_FLAG(shader_bridge) && EDF_NATIVE_FLAG(material_activation) &&
       (!edf::native::native_queued_scene_group || !edf::native::native_queued_scene_group->material_compatibility_only))
      ActivateNativeMaterial(ctx,base,instance,device);
    else {
      edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::MaterialActivation);
      __imp__sub_821B8E48(ctx, base);
    }
  }
  if(expected_state) {
    const auto actual=edf::native::ReadNativeMaterialRenderPass(edf::native::GuestReader(base),device);
    if(actual!=*expected_state) {
      REXLOG_ERROR("Native material state mismatch: material={:#x} expected={:#x}/{:#x}/{:#x}/{:#x}/{:#x}/{:#x} actual={:#x}/{:#x}/{:#x}/{:#x}/{:#x}/{:#x}",
        instance,expected_state->words[0],expected_state->words[1],expected_state->words[2],expected_state->words[3],expected_state->words[4],expected_state->words[5],
        actual.words[0],actual.words[1],actual.words[2],actual.words[3],actual.words[4],actual.words[5]);
      throw std::runtime_error("native material render state differs from guest activation");
    }
    static std::atomic<uint64_t> checked=0;
    const auto count=++checked;
    if(count<=4 || count%100000==0) REXLOG_INFO("Native material state audit: checked={} mismatches=0",count);
  }
  if(sampler_audit) {
    const edf::native::GuestReader reader(base);
    static std::atomic<uint64_t> checked=0;
    for(const auto& [slot,expected]:*audited_samplers) {
      const auto actual=edf::native::ReadSamplerWords(reader,device,slot);
      if(edf::native::SamplerStateKey(actual)!=edf::native::SamplerStateKey(expected.words) ||
         (actual[2]&3)!=(expected.words[2]&3)) {
        REXLOG_ERROR("Native material sampler mismatch: material={:#x} slot={} expected={:#x}/{:#x}/{:#x}/{:#x} actual={:#x}/{:#x}/{:#x}/{:#x}",
          instance,slot,expected.words[0],expected.words[1],expected.words[2],expected.words[3],actual[0],actual[1],actual[2],actual[3]);
        throw std::runtime_error("native material sampler program differs from guest activation");
      }
      const auto count=++checked;
      if(count<=4 || count%100000==0)
        REXLOG_INFO("Native material sampler audit: checked={} mismatches=0",count);
    }
  }
  if (EDF_NATIVE_FLAG(shader_bridge)) {
    edf::native::HookTiming timing(edf::native::HookPhase::ActivationNative);
    if(const auto* group=edf::native::native_queued_scene_group;
       group && group->material_handoff.activation_pending() && group->activation_instance==instance) return;
    try {
      if(!REXCVAR_GET(edf_native_scene_activation_owned) || !EDF_NATIVE_FLAG(scene_material_owned) ||
         REXCVAR_GET(edf_native_scene_material_audit) || !edf::native::ObservePublishedActivation(instance))
        edf::native::ObserveActivation(edf::native::GuestReader(base), instance, device);
    }
    catch (const std::exception& error) { REXLOG_ERROR("Native shader bridge: {}", error.what()); }
  }
}

// The instanced mesh loop at 821D97C4 uploads these overrides after material
// activation, immediately before each indexed draw. Preserve that ordering.
namespace edf::native {
namespace {
void ApplyNativeInstanceParametersLocked(Bridge& state,const GuestReader& reader,uint32_t list,uint32_t device) {
    if(!state.active_vertex || !state.active_vertex_parameters || state.shader_bindings.Vertex(device)!=state.active_vertex) return;
    auto& shader=state.shaders.at(state.active_vertex);
    // A single instanced shader variant can only serve a run whose instances
    // all patch the same register range. Fingerprint the shape (not the data)
    // so the indexed hook can tell whether a collapsible run is also uniform.
    edf::native::HookTiming read_timing(edf::native::HookPhase::InstanceRead);
    static thread_local std::vector<edf::native::InstanceParameter> parameters;
    edf::native::ReadInstanceParameters(reader,list,parameters);
    read_timing.Finish();
    edf::native::HookTiming patch_timing(edf::native::HookPhase::InstancePatch);
    if(REXCVAR_GET(edf_native_batch_audit)) {
      uint64_t shape=1469598103934665603ull;
      for(const auto& entry:parameters) for(const auto word:{entry.first,entry.count}) {
        shape^=word; shape*=1099511628211ull;
      }
      state.instance_shape=shape;
    }
    for(const auto& [data,first,count]:parameters) {
      for(const auto& parameter:*state.active_vertex_parameters) {
        const auto low=(std::max)(first,parameter.first),high=(std::min)(first+count,parameter.first+parameter.count);
        if(low>=high) continue;
        const size_t bytes=size_t(high-low)*16;
        const auto* source=reader.Bytes(reader.Add(data,(low-first)*16),bytes);
        if(const auto frames=REXCVAR_GET(edf_native_instance_motion_trace); frames>0) {
          // Diagnostic only. Addresses identify observations, not certified
          // object lifetimes; do not use this key for interpolation yet.
          struct Entry {
            uint32_t list=0,data=0,first=0,count=0;
            std::string name;
            uint64_t last_frame=0,hash=0,samples=0,changes=0,same_frame_changes=0;
          };
          static uint64_t start_frame=0;
          static bool finished=false;
          static std::map<std::tuple<uint32_t,uint32_t,uint32_t,uint32_t>,Entry> entries;
          if(!finished) {
            if(!start_frame) start_frame=state.scene_frames;
            if(state.scene_frames-start_frame>=uint64_t(frames)) {
              for(const auto& [key,entry]:entries)
                REXLOG_INFO("Native instance motion: list={:#x} data={:#x} first={} count={} name={} samples={} changes={} same_frame_changes={}",
                  entry.list,entry.data,entry.first,entry.count,entry.name,entry.samples,entry.changes,entry.same_frame_changes);
              REXLOG_INFO("Native instance motion: complete frames={} sources={}",frames,entries.size());
              finished=true; entries.clear();
            } else {
              const auto key=std::make_tuple(list,data,low,high-low);
              auto found=entries.find(key);
              if(found==entries.end() && entries.size()<256) {
                found=entries.emplace(key,Entry{list,data,low,high-low,parameter.name}).first;
                const auto header=edf::native::ReadGuestWords<3>(reader,list);
                REXLOG_INFO("Native instance identity: list={:#x} data={:#x} first={} count={} name={} header={:#x},{:#x},{:#x}",
                  list,data,low,high-low,parameter.name,header[0],header[1],header[2]);
              }
              if(found!=entries.end()) {
                auto& entry=found->second;
                uint64_t hash=14695981039346656037ull;
                for(size_t i=0;i<bytes;++i) { hash^=source[i]; hash*=1099511628211ull; }
                if(!entry.samples || entry.last_frame!=state.scene_frames) {
                  if(entry.samples && entry.hash!=hash) ++entry.changes;
                  ++entry.samples; entry.last_frame=state.scene_frames;
                } else if(entry.hash!=hash) ++entry.same_frame_changes;
                entry.hash=hash;
              }
            }
          }
        }
        shader.bindings->PatchGuestFloatRegisters(parameter.normal,low-parameter.first,{source,bytes});
        // A mirroring reversed variant picks these up at the draw that uses it.
        if(!shader.reversed_mirrors)
          shader.reversed_bindings->PatchGuestFloatRegisters(parameter.reversed,low-parameter.first,{source,bytes});
        ++state.instance_parameter_updates;
        if(state.instance_parameter_updates<=12)
          REXLOG_INFO("Native instance constant: name={}, register={}, count={}",parameter.name,low,high-low);
      }
    }
}
void SynchronizeNativeQueuedSceneInstanceLocked(Bridge& state,const GuestReader& reader,uint32_t device,NativeQueuedSceneGroup& group) {
  if(!group.pending_world) return;
  const auto& parameter=*group.world_parameter;
  auto& shader=state.shaders.at(group.vertex);
  const auto& bytes=*group.pending_world;
  // There have been no guest callbacks since the last consumed draw. All
  // skipped draws only overwrite this complete matrix, so only the final value
  // is observable by the next guest callback. Keep the source bytes owned.
  RestoreNativeSceneWorld(reader,device,parameter.first,bytes,[&](const auto& restored) {
    shader.bindings->PatchGuestFloatRegisters(parameter.normal,0,restored);
    if(!shader.reversed_mirrors) shader.reversed_bindings->PatchGuestFloatRegisters(parameter.reversed,0,restored);
  });
  group.pending_world.reset();
}
void SynchronizeNativeQueuedSceneInstance(uint8_t* base,uint32_t device,NativeQueuedSceneGroup& group) {
  if(group.material_handoff.activation_pending()) return;
  if(!group.pending_world) return;
  auto& state=State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  SynchronizeNativeQueuedSceneInstanceLocked(state,GuestReader(base),device,group);
}
struct NativeStaticPassView {
  GuestViewportWords viewport; ActiveTargets targets;
  bool operator==(const NativeStaticPassView&) const=default;
};
// Publication inputs of one static group. deferred_geometry is the identity a
// pending geometry handoff will bind instead of the draw's live bindings.
struct NativeStaticGroupInputs {
  uint32_t address=0,count=0;
  const std::shared_ptr<const NativeSceneGroupMaterial>& material;
  const std::optional<NativeSceneGeometrySource>& deferred_geometry;
};
// Pass constants, render-state and sampler pass, and the owned pass view. With
// no view the draw's live view (NativeStaticDrawBindings::view) is used.
struct NativeStaticPassInputs {
  uint32_t device=0;
  const std::vector<NativeSceneMaterialInputs::Constant>& constants;
  const NativeMaterialRenderPass& render;
  const std::array<NativeMaterialSamplerPass,16>& samplers;
  std::optional<NativeStaticPassView> view;
};
// What a guest draw reaching this instance has bound. Only the guest-draw path
// supplies these; a native pass has no bound state. Each runs at the point the
// resolve reaches it, so reads, audits and throws keep their order.
struct NativeStaticDrawBindings {
  std::function<bool(const NativeSceneGeometrySource&)> geometry;
  std::function<bool(uint32_t vertex,uint32_t pixel)> program;
  std::function<NativeStaticPassView()> view;
  std::function<void(const NativeStaticPassView&)> audit_view;
};
struct NativeStaticInstanceResolution {
  NativeStaticWorldDecline decline=NativeStaticWorldDecline::None;
  std::shared_ptr<const NativeSceneInstance> object;
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
  std::shared_ptr<const NativeSceneMaterial> material;
  uint32_t vertex=0,pixel=0;
  NativeSceneView view;
  NativeStaticPassView pass{};
  bool reverse_depth=false;
  std::optional<VertexParameterRange> world_parameter;
  bool world_column_major=false;
  std::array<uint8_t,64> world{};
  explicit operator bool() const { return decline==NativeStaticWorldDecline::None; }
};
// A group's material under one pass. program.Resolve reads the group's program,
// its retained geometry's layout, the pass view and the pass constants, render
// state and samplers - never the instance - and the capture zeroes g_mWorld out
// of the material image. So every instance of a group shares one interned
// material and differs only in the world applied to its copy of the capture,
// which is what lets the renderer instance them (it batches by material identity).
// Identified by the program, not the published group material: the preload
// republishes that object whenever a constant moves, and the resolve reads it
// only as program and constants.
struct NativeStaticGroupMaterial {
  const NativeSceneMaterialProgram* program=nullptr;
  const NativeIndexedMesh::RetainedDraw* geometry=nullptr;
  NativeStaticPassView pass{};
  NativeSceneResolvedMaterial resolved;
  std::optional<VertexParameterRange> world_parameter;
  bool world_column_major=false;
};
// The per-group half of a published static resolve: what every instance of the
// group shares under one pass. material points into the caller's slot.
struct NativeStaticGroupResolution {
  NativeStaticWorldDecline decline=NativeStaticWorldDecline::None;
  const NativeSceneSources* sources=nullptr;
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
  const NativeStaticGroupMaterial* material=nullptr;
  NativeStaticPassView pass{};
  NativeSceneView view;
  bool reverse_depth=false;
  explicit operator bool() const { return decline==NativeStaticWorldDecline::None; }
};
// The per-instance half: its publication object and its g_mWorld registers.
struct NativeStaticInstanceWorld {
  NativeStaticWorldDecline decline=NativeStaticWorldDecline::None;
  std::shared_ptr<const NativeSceneInstance> object;
  std::array<uint8_t,64> world{};
  explicit operator bool() const { return decline==NativeStaticWorldDecline::None; }
};
// Resolve one published static group from publication and pass inputs only,
// once for all of instances (each must still be a member of the group).
// Never reads the live bound shaders, streams, index bindings or view; those
// checks belong to the caller, through draw. Exceptions propagate. slot
// carries the group's material between resolves against one
// NativeStaticPassInputs (or constants the caller proved resolve to the same
// material: NativeStaticWorldGroupCache::Current); a different program,
// geometry or view resolves afresh (under the QueuedResolve timing when timed).
NativeStaticGroupResolution ResolveNativePublishedStaticGroupLocked(Bridge& state,const GuestReader& reader,
    const NativeScenePublication& publication,const NativeStaticGroupInputs& group,std::span<const uint32_t> instances,
    const NativeStaticPassInputs& pass,const NativeStaticDrawBindings* draw,std::optional<NativeStaticGroupMaterial>& slot,bool timed) {
  using D=NativeStaticWorldDecline;
  NativeStaticGroupResolution result;
  const auto decline=[&](D reason) { result.decline=reason; return result; };
  if(BufferWrites().Pending()) return decline(D::PendingWrites);
  const auto& sources=NativeSceneSourcesForPass(state);
  result.sources=&sources;
  const auto* membership=sources.FindGroup(group.address);
  const auto cached=state.scene_geometry_loads.find(group.address);
  if(!membership || !std::ranges::all_of(instances,[&](uint32_t instance) { return membership->parts.contains(instance); }) ||
     cached==state.scene_geometry_loads.end() ||
     cached->second.revision!=membership->revision || group.material->revision!=membership->revision) return decline(D::Revision);
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
  for(const auto& published:publication.group_geometry)
    if(published->group==group.address && published->revision==membership->revision) { geometry=published->geometry; break; }
  const auto latest=state.scene_adapter.GroupGeometry(group.address,membership->revision);
  if(!geometry || !latest || latest->geometry!=geometry || geometry->backend()!=state.scene_backend.get()) return decline(D::GeometryPublication);
  const auto& source_geometry=cached->second.source;
  if(group.count!=source_geometry.count) return decline(D::GeometryCount);
  if(group.deferred_geometry) {
    if(*group.deferred_geometry!=source_geometry) return decline(D::DeferredGeometry);
  } else if(draw && draw->geometry && !draw->geometry(source_geometry)) return decline(D::GeometryBinding);
  const auto* vb=state.model_buffers.Find(source_geometry.vertex,NativeModelBuffers::Kind::Vertex);
  const auto* ib=state.model_buffers.Find(source_geometry.index,NativeModelBuffers::Kind::Index);
  if(!vb || !ib || !vb->physical || !ib->physical || vb->generation!=cached->second.vertex_generation ||
     ib->generation!=cached->second.index_generation) return decline(D::BufferGeneration);
  const auto index_contents=ib->index_contents?ib->index_contents:(ib->index_storage?ib->index_storage->SourceSnapshot():nullptr);
  using Identity=NativeBufferWrites::SnapshotIdentityView;
  NativeBufferWrites::SnapshotPolicy policy{};
  policy.audit_revisions=REXCVAR_GET(edf_native_retirement_audit);
  if(!policy.audit_revisions && state.mesh_watch_audit.expired()) {
    policy.verify_interval=uint64_t(std::clamp(REXCVAR_GET(edf_native_geometry_verify_interval),0,1<<20));
    policy.verify_initial=uint64_t(std::clamp(REXCVAR_GET(edf_native_geometry_verify_initial),0,1<<16));
  }
  const auto versions=BufferWrites().TryValidateObservedSet(std::array<Identity,2>{{
    {source_geometry.vertex,*vb->physical,vb->bytes,&vb->vertex_contents},
    {source_geometry.index,*ib->physical,ib->bytes,&index_contents}}},policy);
  if(versions) {
    for(size_t i=0;i<2;++i) if((*versions)[i].lifetime!=cached->second.versions[i].lifetime ||
      (*versions)[i].revision!=cached->second.versions[i].revision) return decline(D::VersionChanged);
  } else {
    // A due sampled comparison runs here, bounded to this group's VB and IB,
    // rather than turning the group back to the guest path until the next
    // preload tick. It consumes the due observation, so the preload then finds
    // the group unchanged; anything but an exact match leaves the re-load to it.
    using C=NativeStaticComparison;
    using Source=NativeBufferWrites::SnapshotSource;
    const auto compared=CompareNativeStaticGeometry(BufferWrites(),std::array<Source,2>{{
      {source_geometry.vertex,*vb->physical,{reader.Bytes(vb->address,vb->bytes),vb->bytes},vb->vertex_contents},
      {source_geometry.index,*ib->physical,{reader.Bytes(ib->address,ib->bytes),ib->bytes},index_contents}}},
      cached->second.versions,policy);
    if(compared==C::UnreportedChange)
      REXLOG_WARN("Native published geometry draw detected an unreported geometry write: group={:#x}; the preload re-loads it",group.address);
    if(compared==C::Unavailable) return decline(D::VersionsUnavailable);
    if(compared!=C::Confirmed) return decline(D::ComparisonChanged);
    ++state.scene_geometry_draw_verified;
  }
  const auto& program=*group.material->program;
  if(draw && draw->program && !draw->program(program.inputs.vertex,program.inputs.pixel)) return decline(D::ProgramBinding);
  if(pass.view) {
    result.pass=*pass.view;
    if(draw && draw->audit_view) draw->audit_view(result.pass);
    static uint64_t count=0;
    if(++count<=4 || count%100000==0)
      REXLOG_INFO("Native owned pass view: reads={} (viewport/scissor/targets from native pass)",count);
  } else {
    if(!draw || !draw->view) throw std::runtime_error("native static instance has no pass view");
    result.pass=draw->view();
  }
  const auto& targets=result.pass.targets;
  const auto viewport=DecodeDrawViewport(result.pass.viewport);
  if(!targets.count) return decline(D::Targets);
  if(!slot || slot->program!=&program || slot->geometry!=geometry.get() ||
     slot->pass.viewport!=result.pass.viewport || slot->pass.targets!=targets) {
    // Only the world pass shares a slot across resolves; its cache misses land here.
    HookTiming resolve_timing(HookPhase::QueuedResolve,timed);
    NativeBackendPipelineDesc desc;
    desc.vertex_id=(uint64_t(program.inputs.vertex)<<1)|uint64_t(viewport.reverse_depth);
    desc.pixel_id=program.inputs.pixel;
    desc.input_layout=geometry->input_layout().elements(); desc.input_layout_id=geometry->input_layout().fingerprint();
    desc.render_targets=targets.count; desc.rtv_format=targets.rtv_format;
    desc.dsv_format=targets.dsv_format; desc.sample_count=targets.samples;
    auto resolved=program.Resolve(desc,viewport.reverse_depth,pass.constants,pass.render,pass.samplers,
      REXCVAR_GET(edf_native_anisotropic_filtering));
    // Equal materials share one object: the renderer instances only identical
    // material pointers, and the publication then keeps an unchanged instance.
    resolved.capture.material=state.scene_adapter.InternMaterial(std::move(resolved.capture.material));
    NativeQueuedSceneGroup candidate;
    candidate.material=resolved.capture.material;
    candidate.published_material=group.material;
    ConfigureNativeQueuedWorldLocked(state,candidate);
    slot=NativeStaticGroupMaterial{&program,geometry.get(),result.pass,std::move(resolved),
      std::move(candidate.world_parameter),candidate.world_column_major};
  }
  if(!slot->world_parameter) return decline(D::WorldParameter);
  result.material=&*slot;
  // Every instance's view: the shared capture's camera in the pass viewport.
  result.view=NativeStaticInstanceView(slot->resolved.capture.camera,viewport,slot->resolved.render.words[5]!=0);
  result.reverse_depth=viewport.reverse_depth;
  result.geometry=std::move(geometry);
  return result;
}
// One instance of a resolved group: its source, its world and its object. The
// group's world binding is its single vertex g_mWorld, so the decode is
// ApplyNativeScenePublishedWorld's. reuse, when given, lets an instance
// resolved to the same material and world as before keep its object.
NativeStaticInstanceWorld ResolveNativePublishedStaticInstanceWorldLocked(Bridge& state,const GuestReader& reader,
    const NativeScenePublication& publication,const NativeStaticGroupResolution& group,uint32_t instance,uint32_t device,
    NativeSceneInstanceReuse* reuse) {
  using D=NativeStaticWorldDecline;
  NativeStaticInstanceWorld result;
  const auto decline=[&](D reason) { result.decline=reason; return result; };
  const auto& sources=*group.sources;
  const auto& material=*group.material;
  const auto* source=sources.Find(instance);
  if(!source || !NativeStaticWorldOnly(reader,*source,instance,material.world_parameter->first,device)) return decline(D::WorldParameter);
  const auto world=sources.WorldRegisters(*source,source->world_data);
  if(!world) return decline(D::WorldRegisters);
  if(EDF_NATIVE_FLAG(scene_sources_owned) && REXCVAR_GET(edf_native_scene_transform_audit)) {
    ++state.scene_world_checks;
    if(std::memcmp(world->data(),reader.Bytes(source->world_data,64),64)) {
      ++state.scene_world_mismatches;
      REXLOG_ERROR("Native published first world mismatch: owner={:#x} instance={:#x}",source->owner,instance);
      return decline(D::WorldMismatch);
    }
  }
  const auto& capture=material.resolved.capture;
  const auto matrix=DecodeNativeQueuedWorld(*world,material.world_column_major);
  if(EDF_NATIVE_FLAG(scene_selection_owned)) {
    result.object=publication.Resolve(*source,group.geometry,capture.material,matrix,reuse);
    if(!result.object) return decline(D::Lifetime);
  } else {
    const auto id=state.scene_adapter.Observe(*source,group.geometry,NativeSceneMaterialCapture{capture.material,matrix,capture.camera});
    result.object=SelectNativeSceneInstanceLocked(state,id);
  }
  result.world=*world;
  return result;
}
// Both halves for one instance, as the guest draw reaching it resolves it.
NativeStaticInstanceResolution ResolveNativePublishedStaticInstanceLocked(Bridge& state,const GuestReader& reader,
    const NativeScenePublication& publication,const NativeStaticGroupInputs& group,uint32_t instance,
    const NativeStaticPassInputs& pass,const NativeStaticDrawBindings* draw=nullptr) {
  NativeStaticInstanceResolution result;
  std::optional<NativeStaticGroupMaterial> slot;
  const auto resolved=ResolveNativePublishedStaticGroupLocked(state,reader,publication,group,{&instance,1},pass,draw,slot,false);
  if(!resolved) { result.decline=resolved.decline; return result; }
  auto world=ResolveNativePublishedStaticInstanceWorldLocked(state,reader,publication,resolved,instance,pass.device,nullptr);
  if(!world) { result.decline=world.decline; return result; }
  const auto& material=*resolved.material;
  const auto& program=*group.material->program;
  result.object=std::move(world.object); result.geometry=resolved.geometry; result.material=material.resolved.capture.material;
  result.vertex=program.inputs.vertex; result.pixel=program.inputs.pixel;
  result.view=resolved.view; result.pass=resolved.pass; result.reverse_depth=resolved.reverse_depth;
  result.world_parameter=material.world_parameter; result.world_column_major=material.world_column_major;
  result.world=world.world;
  return result;
}
bool TryAppendPublishedNativeSceneInstance(uint8_t* base,uint32_t device,uint32_t address,uint32_t instance,uint32_t count,NativeQueuedSceneGroup& group) {
  if(!EDF_NATIVE_FLAG(scene_geometry_owned) || !EDF_NATIVE_FLAG(scene_material_owned) ||
     REXCVAR_GET(edf_native_scene_material_audit) || group.geometry || !group.objects.empty() ||
     !group.published_material || !group.material_pass || !native_scene_publication) return false;
  auto& state=State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  const auto defer=[](const char* reason) {
    static std::set<std::string> reported;
    if(reported.insert(reason).second) REXLOG_INFO("Native published geometry draw declined: {}",reason);
    return false;
  };
  if(BufferWrites().Pending()) return defer("pending resource writes");
  try {
    const GuestReader reader(base);
    // The live guest bindings. The resolve calls each only where it reaches
    // that check, as the inline reads did.
    NativeStaticDrawBindings draw;
    draw.geometry=[&](const NativeSceneGeometrySource& source) {
      return NativeBoundGeometryMatches(source,[&]() -> const GuestStream& { return state.streams.at({device,0}); },
        [&] { return state.index_bindings.at(device); },[&] { return state.declaration_bindings.at(device); });
    };
    draw.program=[&](uint32_t vertex,uint32_t pixel) {
      return state.active_vertex==vertex && state.linked_pixel==pixel && state.active_vertex_parameters &&
        (group.material_handoff.activation_pending() || state.shader_bindings.Vertex(device)==vertex);
    };
    draw.view=[&] { const auto viewport=ReadNativeDrawViewportWords(reader,device); return NativeStaticPassView{viewport,ActiveTargetsLocked(state)}; };
    draw.audit_view=[&](const NativeStaticPassView& view) {
      if(REXCVAR_GET(edf_native_render_state_audit) &&
         (view.viewport!=ReadNativeDrawViewportWords(reader,device) || view.targets!=ActiveTargetsLocked(state))) {
        REXLOG_ERROR("Native pass view mismatch: group={:#x}",address);
        throw std::runtime_error("native inherited viewport or targets changed without a pass boundary");
      }
    };
    const bool owned_view=REXCVAR_GET(edf_native_scene_view_owned) &&
      group.material_handoff.activation_pending() && group.pass_viewport.has_value();
    NativeStaticPassInputs pass{device,group.pass_constants,*group.material_pass,group.sampler_pass};
    if(owned_view) pass.view=NativeStaticPassView{*group.pass_viewport,group.targets};
    auto resolved=ResolveNativePublishedStaticInstanceLocked(state,reader,*native_scene_publication,
      {address,count,group.published_material,group.geometry_handoff.pending()},instance,pass,&draw);
    if(!resolved) {
      if(const auto* reason=NativeStaticWorldDeclineReason(resolved.decline)) return defer(reason);
      return false;
    }
    group.objects.push_back(std::move(resolved.object));
    group.geometry=std::move(resolved.geometry); group.material=std::move(resolved.material);
    group.view=resolved.view; group.targets=resolved.pass.targets; group.reverse_depth=resolved.reverse_depth;
    group.pass_viewport=resolved.pass.viewport;
    group.vertex=resolved.vertex; group.pixel=resolved.pixel;
    group.world_parameter=std::move(resolved.world_parameter); group.world_column_major=resolved.world_column_major;
    // The accepted instance only replaces g_mWorld. Preserve its final CPU
    // value before the next guest callback, as for subsequent queued instances.
    group.pending_world=resolved.world;
    group.instance=instance;
    group.constants_clean=group.geometry_handoff.pending().has_value();
    if(++state.scene_geometry_draw_bypasses<=4 || state.scene_geometry_draw_bypasses%10000==0)
      REXLOG_INFO("Native scene published geometry draws: bypassed={} draw_verified={} (no instance callback, indexed mesh acquisition or live binding setup)",
        state.scene_geometry_draw_bypasses,state.scene_geometry_draw_verified);
    return true;
  } catch(const std::exception& error) {
    if(state.scene_native_reasons.size()<32 && state.scene_native_reasons.insert(error.what()).second)
      REXLOG_INFO("Native published geometry draw deferred: {}",error.what());
    return false;
  }
}
bool TryAppendNativeQueuedSceneInstance(uint8_t* base,uint32_t device,uint32_t instance,NativeQueuedSceneGroup& group) {
  if(!group.geometry || !group.material) return false;
  auto& state=State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  // A pending resource write must pass the full draw's revision/geometry check.
  if(BufferWrites().Pending() || state.active_vertex!=group.vertex || state.linked_pixel!=group.pixel ||
     !state.active_vertex_parameters || (!group.material_handoff.activation_pending() && state.shader_bindings.Vertex(device)!=group.vertex)) return false;
  const GuestReader reader(base);
  std::optional<NativeSceneMaterialCapture> capture;
  std::optional<std::array<uint8_t,64>> world_registers;
  std::shared_ptr<const NativeSceneInstance> published_selection;
  uint64_t id=0;
  try {
    const auto& sources=NativeSceneSourcesForPass(state);
    const auto* source=sources.Find(instance);
    if(!source) return false;
    static thread_local std::vector<InstanceParameter> parameters;
    ReadNativeSceneInstanceParameters(reader,source,instance,parameters);
    const bool world_only=group.world_parameter && parameters.size()==1 && parameters[0].count==4 &&
      parameters[0].first==group.world_parameter->first &&
      !(uint64_t(parameters[0].data)<uint64_t(device)+5888 && uint64_t(parameters[0].data)+64>uint64_t(device)+1792);
    if(world_only) {
      if(parameters[0].data!=source->world_data) group.population_safe=false;
      if(!group.constants_clean) {
        if(!NativeQueuedConstantsClean(reader,device)) return false;
        group.constants_clean=true;
      }
      world_registers.emplace();
      if(const auto published=sources.WorldRegisters(*source,parameters[0].data)) {
        *world_registers=*published; ++state.scene_world_reused;
        if(REXCVAR_GET(edf_native_scene_transform_audit)) {
          ++state.scene_world_checks;
          const auto* actual=reader.Bytes(parameters[0].data,64);
          if(std::memcmp(actual,published->data(),64)) {
            group.population_safe=false;
            ++state.scene_world_mismatches;
            std::memcpy(world_registers->data(),actual,64);
            if(state.scene_world_mismatches<=8)
              REXLOG_ERROR("Native scene transform mismatch: owner={:#x} lod={} part={} data={:#x}",source->owner,source->lod,source->part,parameters[0].data);
          }
        }
      } else {
        std::memcpy(world_registers->data(),reader.Bytes(parameters[0].data,64),64); ++state.scene_world_reads;
      }
      capture=NativeSceneMaterialCapture{group.material,DecodeNativeQueuedWorld(*world_registers,group.world_column_major),group.view};
    } else {
      if(group.geometry_handoff.pending()) return false;
      group.population_safe=false;
      SynchronizeNativeQueuedSceneInstanceLocked(state,reader,device,group);
      if(!NativeQueuedConstantsConsumed(reader,device,parameters)) return false;
      ApplyNativeInstanceParametersLocked(state,reader,instance,device);
      auto& vertex=VertexBindingsForDraw(state.shaders.at(group.vertex),group.reverse_depth);
      capture=CaptureNativeSceneMaterial(state.scene_backend,*group.material->pipeline(),vertex,
        *state.shaders.at(group.pixel).bindings,group.material->blend_factor(),group.material);
    }
    if(EDF_NATIVE_FLAG(scene_selection_owned) && world_only && native_scene_publication) {
      published_selection=native_scene_publication->Resolve(*source,group.geometry,*capture);
      if(!published_selection) return false;
      static uint64_t selections=0;
      if(++selections<=4 || selections%100000==0)
        REXLOG_INFO("Native immutable instance selection: count={} (no current scene database mutation)",selections);
    } else id=state.scene_adapter.Observe(*source,group.geometry,*capture);
  } catch(const std::exception& error) {
    ++state.scene_native_direct_retries;
    if(state.scene_native_reasons.size()<32 && state.scene_native_reasons.insert(error.what()).second)
      REXLOG_INFO("Native scene direct retry: {}",error.what());
    // CPU constant writes and native patches are idempotent for the nonaliasing
    // sources accepted above. The regular path reapplies every override.
    return false;
  }
  const auto& camera=capture->camera;
  if(group.view.view!=camera.view || group.view.projection!=camera.projection || group.view.view_projection!=camera.view_projection)
    FlushNativeQueuedSceneLocked(state,group);
  group.view.view=camera.view; group.view.projection=camera.projection; group.view.view_projection=camera.view_projection;
  group.objects.push_back(published_selection?std::move(published_selection):SelectNativeSceneInstanceLocked(state,id));
  group.material=group.objects.back()->object.material;
  if(world_registers) { group.pending_world=std::move(world_registers); ++state.scene_native_direct_worlds; }
  if(++state.scene_native_direct_instances%100000==0) {
    REXLOG_INFO("Native scene direct: instances={} retries={} worlds={} objects={} rendered={} draws={}",
      state.scene_native_direct_instances,state.scene_native_direct_retries,state.scene_native_direct_worlds,state.scene_adapter.objects(),
      state.scene_native_objects,state.scene_native_draws);
    REXLOG_INFO("Native scene transforms: updates={} retained={} guest_reads={} checks={} mismatches={}",
      state.scene_world_publications,state.scene_world_reused,state.scene_world_reads,state.scene_world_checks,state.scene_world_mismatches);
  }
  return true;
}
}
}
REX_EXTERN(__imp__sub_821D9600);
REX_HOOK_RAW(sub_821D9600) {
  const auto list=ctx.r3.u32, device=ctx.r4.u32;
  if(edf::native::native_queued_scene_group) edf::native::native_queued_scene_group->instance=list;
  {
    edf::native::HookTiming timing(edf::native::HookPhase::InstanceGuest);
    __imp__sub_821D9600(ctx,base);
  }
  if(!EDF_NATIVE_FLAG(shader_bridge)) return;
  edf::native::HookTiming timing(edf::native::HookPhase::InstanceNative);
  auto& state=edf::native::State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  if(REXCVAR_GET(edf_native_scene_adapter_audit)) {
    if(state.scene_sources.Find(list)) ++state.scene_source_draws;
    else ++state.scene_source_misses;
    const auto total=state.scene_source_draws+state.scene_source_misses;
    if(total%100000==0)
      REXLOG_INFO("Native scene ownership: owners={} parts={} mapped_draws={} unmapped_draws={}",
        state.scene_sources.owners(),state.scene_sources.parts(),state.scene_source_draws,state.scene_source_misses);
  }
  try {
    edf::native::ApplyNativeInstanceParametersLocked(state,edf::native::GuestReader(base),list,device);
  } catch(const std::exception& error) {
    ++state.instance_parameter_errors;
    state.active_vertex=0; // Never submit a draw with partially applied overrides.
    state.active_vertex_parameters.reset();
    if(state.instance_parameter_errors<=10) REXLOG_ERROR("Native instance constant: {}",error.what());
  }
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
REX_EXTERN(__imp__sub_8213D298);
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
    struct Writer {
      std::mutex mutex;
      std::ofstream file;
      struct Previous { Clock::time_point entry{},exit{}; std::array<uint64_t,4> waits{}; uint64_t extra_steps=0,process_cpu=0,thread_cpu=0; DWORD thread_id=0; };
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
      writer.file << "epoch_ms,device,interval_ms,between_swaps_ms,submit_ms,gpu_wait_ms,pacing_ms,engine_wait_ms,guest_fence_sleep_ms,shared_slot_wait_ms,backend_frame_wait_ms,engine_extra_steps,process_cpu_ms,swap_thread_cpu_ms,swap_thread_id\n";
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
      << ',' << thread_id << '\n';
    previous={marks_[0],marks_[4],waits,extra_steps,process_cpu,thread_cpu,thread_id};
    if(++writer.samples%60==0) writer.file.flush();
  }
 private:
  uint32_t device_;
  bool enabled_;
  std::array<Clock::time_point,5> marks_{};
};
}
namespace {
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
}
REX_EXTERN(edf_native_swap_wait) {
  using namespace edf::native;
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
REX_EXTERN(__imp__sub_8213C9F0);
REX_EXTERN(__imp__edf_native_worker_signal_cpu_tail);
REX_HOOK_RAW(sub_8213C9F0) {
  const auto device=ctx.r3.u32,begin=ctx.r4.u32,callback=ctx.r6.u32,argument=ctx.r7.u32,flags=ctx.r5.u32;
  // The port has no Xbox command consumer. Unknown callbacks must not silently
  // emit a GPU interrupt packet and leave their CPU waiter permanently pending.
  // The former 51248 caller is replaced at 512D8 by native swap pacing.
  if(EDF_NATIVE_FLAG(host) && callback!=0x8214EBA0) {
    REXLOG_ERROR("Unsupported native signal callback: caller={:#x}, callback={:#x}, device={:#x}, argument={:#x}; no Xbox packet fallback",
      uint32_t(ctx.lr),callback,device,argument);
    throw std::runtime_error("native signal callback has no CPU completion implementation");
  }
  if(EDF_NATIVE_FLAG(host) && REXCVAR_GET(edf_native_hook_timings)) {
    const auto caller=uint32_t(ctx.lr);
    const size_t bucket=caller==0x8213D288?0:caller==0x8214ED64?1:caller==0x821513F4?2:3;
    static std::array<std::atomic<uint64_t>,4> calls{};
    const auto count=calls[bucket].fetch_add(1,std::memory_order_relaxed)+1;
    if(count<=8 || !(count&(count-1))) {
      // Observe the CPU callback contract before replacing its GPU signal packet.
      // Never execute the callback here: the enclosing submit has not run yet.
      REXLOG_INFO("Native pending signal audit: calls={}, caller={:#x}, device={:#x}, flags={:#x}, callback={:#x}, argument={:#x}, guest_tls={:#x}",
        count,uint32_t(ctx.lr),ctx.r3.u32,ctx.r5.u32,ctx.r6.u32,ctx.r7.u32,ctx.r13.u32);
    }
  }
  if(EDF_NATIVE_FLAG(host) && callback==0x8214EBA0)
    __imp__edf_native_worker_signal_cpu_tail(ctx,base);
  else __imp__sub_8213C9F0(ctx,base);
  if(EDF_NATIVE_FLAG(host) && callback==0x8214EBA0) {
    const uint32_t bytes=ctx.r3.u32-begin;
    const auto physical=edf::native::NativeSignalCommandAddress(begin+4);
    if(uint64_t(begin)+bytes+4>(uint64_t(1)<<32) || !bytes)
      throw std::runtime_error("invalid native worker signal packet span");
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    auto& queue=state.signal_queues[device];
    if(!queue) queue=edf::native::CreateSignalQueueLocked(state);
    std::lock_guard delivery_lock(state.signal_delivery_mutex);
    const auto delivery=state.signal_deliveries.find(device);
    if(queue->pending()+size_t(delivery!=state.signal_deliveries.end() && delivery->second.pending())>=4096)
      throw std::runtime_error("native signal capacity includes undelivered CPU work");
    try {
      queue->Capture(physical,bytes,{callback,argument,edf::native::NativeSignalCpuMask(flags)});
    } catch(const std::exception& error) {
      REXLOG_ERROR("Native signal capture failed: begin={:#x}, bytes={}, argument={:#x}, pending={}, unsubmitted={}: {}",
        physical,bytes,argument,queue->pending(),queue->unsubmitted(),error.what());
      throw;
    }
  }
}
REX_EXTERN(__imp__sub_8213C868);
REX_EXTERN(__imp__sub_8214E8E0);
REX_EXTERN(__imp__edf_native_worker_audit);
REX_EXTERN(__imp__edf_native_worker_commands_audit);
REX_HOOK_RAW(sub_8214E640) {
  __imp__edf_native_worker_commands_audit(ctx,base);
}
namespace {
struct NativeWorkerCommandTrace { uint32_t worker,cursor,word,continuation,list; };
thread_local std::array<NativeWorkerCommandTrace,32> native_worker_commands{};
thread_local uint64_t native_worker_command_count=0;
void DumpNativeWorkerCommands() {
  const auto first=native_worker_command_count>32?native_worker_command_count-32:0;
  for(auto i=first;i<native_worker_command_count;++i) {
    const auto& entry=native_worker_commands[i%32];
    REXLOG_ERROR("Native worker history: step={}, worker={:#x}, cursor={:#x}, word={:#x}, continuation={:#x}, list={:#x}",
      i,entry.worker,entry.cursor,entry.word,entry.continuation,entry.list);
  }
}
}
void edf_native_audit_worker_command(PPCContext& ctx,uint8_t* base,uint32_t worker,uint32_t cursor) {
  if(!REXCVAR_GET(edf_native_hook_timings)) return;
  const edf::native::GuestReader reader(base);
  if(cursor<0x10000) {
    DumpNativeWorkerCommands();
    REXLOG_ERROR("Native invalid worker cursor: LR={:#x}, worker={:#x}, cursor={:#x}, continuation={:#x}, list={:#x}",
      uint32_t(ctx.lr),worker,cursor,reader.Word(worker+80),reader.Word(worker+84));
    return; // Preserve the original failing read; diagnostics must not skip work.
  }
  const auto word=reader.Word(cursor);
  native_worker_commands[native_worker_command_count++%32]={worker,cursor,word,reader.Word(worker+80),reader.Word(worker+84)};
  // Count actual consumer visits, not static producer sites. Tiling commands
  // remain in retail bodies that native entry hooks may already bypass.
  const auto opcode=word>>24;
  const unsigned kind=!(word&0x80000000u)?0:
    word==0xc0000000u?14:opcode>=0x80 && opcode<=0x8c?1+opcode-0x80:15;
  static std::array<std::atomic<uint64_t>,16> visits{};
  const auto count=visits[kind].fetch_add(1,std::memory_order_relaxed)+1;
  if(count<=8 || !(count&(count-1)))
    REXLOG_INFO("Native worker command coverage: kind={}, visits={}, worker={:#x}, cursor={:#x}, word={:#x}",
      kind,count,worker,cursor,word);
}
REX_EXTERN(edf_native_null_worker_job) {
  DumpNativeWorkerCommands();
  const edf::native::GuestReader reader(base);
  const auto worker=ctx.r31.u32;
  REXLOG_ERROR("Native null worker job: LR={:#x}, worker={:#x}, callback={:#x}, job_argument={:#x}, index={}, count={}, data={:#x}, cursor={:#x}, continuation={:#x}, list={:#x}, published={:#x}",
    uint32_t(ctx.lr),worker,reader.Word(worker+16),reader.Word(worker+20),reader.Word(worker+24),reader.Word(worker+28),
    reader.Word(worker+32),reader.Word(worker+36),reader.Word(worker+80),reader.Word(worker+84),reader.Word(worker+88));
  for(const auto offset:{32u,36u,80u,84u}) {
    const auto address=reader.Word(worker+offset);
    if(!address) continue;
    for(uint32_t i=0;i<8;++i)
      REXLOG_ERROR("Native null worker memory: field={}, address={:#x}, word={:#x}",offset,address+i*4,reader.Word(reader.Add(address,i*4)));
  }
}
REX_HOOK_RAW(sub_8214E8E0) {
  const auto worker_arg=ctx.r3.u32;
  const bool audit=EDF_NATIVE_FLAG(host) && REXCVAR_GET(edf_native_hook_timings);
  static std::atomic<uint64_t> calls{0};
  const auto count=audit?calls.fetch_add(1,std::memory_order_relaxed)+1:0;
  const bool sample=count && (count<=8 || !(count&(count-1)));
  const edf::native::GuestReader reader(base);
  const auto worker=sample?reader.Word(worker_arg):0;
  if(sample)
    REXLOG_INFO("Native CPU worker entered: count={}, worker={:#x}, CPU={}, busy={}, nesting={}, argument={:#x}, continuation={:#x}",
      count,worker,reader.Word(worker_arg+4),reader.Word(worker+56),reader.Word(worker+48),reader.Word(worker+88),reader.Word(worker+80));
  if(EDF_NATIVE_FLAG(host)) __imp__edf_native_worker_audit(ctx,base);
  else __imp__sub_8214E8E0(ctx,base);
  if(EDF_NATIVE_FLAG(host)) {
    const auto actual_worker=reader.Word(worker_arg);
    edf::native::HookTiming service_timing(edf::native::HookPhase::WorkerService);
    const auto device=actual_worker-10812;
    // A worker may itself submit the query that resumes its saved continuation.
    // Service that native completion before it returns to an indefinite event
    // wait. Never execute the continuation here or change its busy counter.
    for(;;) {
      edf::native::PollNativeWorkerSignals(ctx,base,device,true);
      if(reader.Word(actual_worker+88)) break;
      bool in_flight=false;
      {
        auto& state=edf::native::State();
        std::lock_guard lock(state.mutex);
        const auto found=state.signal_queues.find(device);
        in_flight=found!=state.signal_queues.end() && found->second->pending()>found->second->unsubmitted();
        std::lock_guard delivery_lock(state.signal_delivery_mutex);
        const auto delivery=state.signal_deliveries.find(device);
        in_flight|=delivery!=state.signal_deliveries.end() && delivery->second.pending();
      }
      if(!in_flight) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  if(sample)
    REXLOG_INFO("Native CPU worker returned: count={}, worker={:#x}, busy={}, nesting={}, argument={:#x}",
      count,worker,reader.Word(worker+56),reader.Word(worker+48),reader.Word(worker+88));
}
namespace {
void SubmitOwnedNativeDescriptors(PPCContext& ctx,uint8_t* base,uint32_t device,
    std::span<const edf::native::NativeSubmissionDescriptor> descriptors);
}
REX_EXTERN(__imp__KfAcquireSpinLock);
REX_EXTERN(__imp__KfReleaseSpinLock);
REX_EXTERN(sub_8213C018);
REX_HOOK_RAW(sub_8213C868) {
  const auto device=ctx.r3.u32;
  if(EDF_NATIVE_FLAG(host) && REXCVAR_GET(edf_native_hook_timings)) {
    const auto caller=uint32_t(ctx.lr);
    const size_t path=caller==0x8213CEF8?0:caller==0x8213CFD8?1:caller==0x8214EDF8?2:caller==0x8214EE44?3:4;
    static std::array<std::atomic<uint64_t>,10> calls{};
    const auto count=calls[path*2+unsigned(ctx.r7.u32!=0)].fetch_add(1,std::memory_order_relaxed)+1;
    if(count<=8 || !(count&(count-1))) {
      try {
        const edf::native::GuestReader reader(base);
        const auto device=ctx.r3.u32;
        REXLOG_INFO("Native pending list audit: calls={}, caller={:#x}, device={:#x}, busy={}, increment={}, words={}, list={:#x}, recording={}, mode={:#x}",
          count,uint32_t(ctx.lr),device,reader.Word(reader.Add(device,10868)),ctx.r7.u32,ctx.r6.u32,
          ctx.r8.u32,reader.Word(reader.Add(device,12944)),reader.Bytes(reader.Add(device,10809),1)[0]);
      } catch(const std::exception& error) {
        REXLOG_WARN("Native pending list audit read failed: {}",error.what());
      }
    }
  }
  if(EDF_NATIVE_FLAG(host)) {
    {
      edf::native::NativeSignalSubmissionScope submission(device);
      const edf::native::GuestReader reader(base);
      const auto cursor=ctx.r4.u64,list=ctx.r8.u64;
      const auto increment=ctx.r7.u32;
      const edf::native::NativeSubmissionDescriptor descriptor{ctx.r6.u32,ctx.r5.u32};
      auto work=ctx;
      if(work.r1.u32<176) throw std::runtime_error("invalid native submission dispatch stack");
      work.r1.u64=work.r1.u32-176u;
      ctx.r3.u64=edf::native::DispatchNativeSubmission(reader,device,cursor,descriptor,increment,
        [&] {
          work.r3.u64=reader.Add(device,10872); work.lr=0x8213C8A8;
          __imp__KfAcquireSpinLock(work,base); return work.r3.u64;
        },
        [&](uint64_t token) {
          work.r3.u64=reader.Add(device,10872); work.r4.u64=token; work.lr=0x8213C8EC;
          __imp__KfReleaseSpinLock(work,base);
        },
        [&](uint64_t next,edf::native::NativeSubmissionDescriptor range) {
          work.r3.u64=list; work.r4.u64=next; work.r5.u64=range.words; work.r6.u64=range.address;
          work.lr=0x8213C8CC; sub_8213C018(work,base); return work.r3.u64;
        },
        [&](const edf::native::NativeSubmissionDescriptor& range) {
          work.r3.u64=device; work.lr=0x8213C918;
          SubmitOwnedNativeDescriptors(work,base,device,std::span(&range,1));
        });
    }
    // Enqueue is not submission: only the owned submission service arms ranges.
    edf::native::PollNativeWorkerSignals(ctx,base,device);
  } else {
    __imp__sub_8213C868(ctx,base);
  }
}
REX_HOOK_RAW(sub_8213BD90) {
  const edf::native::GuestReader reader(base);
  const auto device=ctx.r3.u32,words=ctx.r5.u32;
  auto& state=edf::native::State();
  std::lock_guard order(state.submissions);
  std::lock_guard lock(state.mutex);
  const auto snapshot=state.submission_cursors.Get(device);
  const auto cursor=snapshot.cursor,mask=snapshot.mask;
  const auto next=edf::native::AdvanceNativeRingCursor(cursor,words,mask);
  state.submission_cursors.Publish(device,snapshot,next);
  reader.StoreWord(reader.Add(device,10820),next);
  // The extracted native tail returned the omitted reservation's masked end,
  // which can differ from the repeated-mask cursor for non-contiguous masks.
  ctx.r3.u64=(cursor+words)&mask;
}
REX_HOOK_RAW(sub_8213C410) {
  {
    std::lock_guard command_order(edf::native::State().submissions);
    const auto device=ctx.r3.u32,descriptors=ctx.r4.u32,count=ctx.r5.u32;
    edf::native::NativeSignalSubmissionScope snapshot_scope(device);
    const edf::native::GuestReader reader(base);
    // C410 accepts CPU descriptors, not Xbox packets: [word count, address].
    // Snapshot before its observer callbacks can touch the descriptor storage.
    std::vector<edf::native::NativeSubmissionDescriptor> owned_descriptors;
    if(count>UINT32_MAX/8) throw std::runtime_error("native signal descriptor count overflow");
    owned_descriptors.reserve(count);
    for(uint32_t i=0;i<count;++i) {
      const auto descriptor=reader.Add(descriptors,i*8);
      const auto words=reader.Word(descriptor);
      if(words>UINT32_MAX/4) throw std::runtime_error("native signal submission range overflow");
      const auto address=reader.Word(reader.Add(descriptor,4));
      owned_descriptors.push_back({words,address});
    }
    SubmitOwnedNativeDescriptors(ctx,base,device,owned_descriptors);
  }
}
namespace {
void SubmitOwnedNativeDescriptors(PPCContext& ctx,uint8_t* base,uint32_t device,
    std::span<const edf::native::NativeSubmissionDescriptor> owned_descriptors) {
    std::lock_guard command_order(edf::native::State().submissions);
    edf::native::NativeSignalSubmissionScope submission(device);
    const edf::native::GuestReader reader(base);
    for(const auto& range:owned_descriptors)
      if(range.words>UINT32_MAX/4) throw std::runtime_error("native signal submission range overflow");
    auto work=ctx;
    if(work.r1.u32<176) throw std::runtime_error("invalid native submission observer stack");
    work.r1.u64=work.r1.u32-176u;
    auto& state=edf::native::State();
    edf::native::NativeSubmissionCursors::Snapshot snapshot;
    { std::lock_guard lock(state.mutex); snapshot=state.submission_cursors.Get(device); }
    const edf::native::NativeSubmissionCursorAccess owned_reader(reader,device,snapshot,[&](uint32_t cursor) {
      std::lock_guard lock(state.mutex);
      state.submission_cursors.Publish(device,snapshot,cursor);
      reader.StoreWord(reader.Add(device,10820),cursor);
    });
    edf::native::SubmitNativeObservers(owned_reader,device,owned_descriptors,
      [&](uint32_t target,uint32_t slot,uint64_t address,uint32_t words,uint32_t kind,uint32_t caller) {
        const auto function=slot?reader.Word(reader.Add(reader.Word(target),slot)):target;
        work.r3.u64=slot?target:kind;
        if(slot!=28) {
          // Retail subtracts in 64 bits; preserve the negative observer address.
          work.r4.u64=kind==2?0:address;
          work.r5.u64=words; work.r6.u64=kind==2?0:1;
        }
        work.lr=caller; work.ctr.u64=function; work.last_indirect_target=function;
        rex::runtime::ResolveIndirectFunction(function)(work,base);
        // The submission gate is recursive: an observer can reset this device.
        // Reject before reading its replacement ABI state or invoking another observer.
        std::lock_guard lock(state.mutex);
        state.submission_cursors.Validate(device,snapshot);
      });
    ctx.r3=work.r3;
    std::lock_guard lock(state.mutex);
    // Special-mode submission need not publish a cursor. Validate every path
    // under the same lock as queue lookup, before arming any completion ranges.
    state.submission_cursors.Validate(device,snapshot);
    edf::native::SubmitSceneFrameLocked(state);
    const auto found=state.signal_queues.find(device);
    if(found!=state.signal_queues.end())
      for(const auto& range:owned_descriptors) if(range.words) found->second->SubmitRange(range.address,range.words*4);
    const auto fences=state.completion_queues.find(device);
    if(fences!=state.completion_queues.end())
      for(const auto& range:owned_descriptors) if(range.words) fences->second->SubmitRange(range.address,range.words*4);
    return;
}
}
REX_EXTERN(__imp__sub_8213C5F0);
REX_EXTERN(__imp__edf_native_cache_range_cpu_tail);
REX_EXTERN(__imp__edf_native_cache_reservation_cpu_tail);
REX_HOOK_RAW(sub_8213C5F0) {
  // CF60 skips only the cache packet enqueue when word count is zero; its
  // following CDC0 native fence submission and optional CPU wait still run.
  // ECD8 consumes the reserved address as fallback CPU storage: retain its
  // reservation/failure contract, but do not encode or enqueue cache packets.
  if(EDF_NATIVE_FLAG(host) && uint32_t(ctx.lr)==0x8213CF9C) {
    __imp__edf_native_cache_range_cpu_tail(ctx,base); return;
  }
  if(EDF_NATIVE_FLAG(host) && uint32_t(ctx.lr)==0x8214ED04) {
    __imp__edf_native_cache_reservation_cpu_tail(ctx,base); return;
  }
  if(EDF_NATIVE_FLAG(host)) {
    REXLOG_ERROR("Unsupported native cache-range caller={:#x}; no Xbox packet fallback",uint32_t(ctx.lr));
    throw std::runtime_error("native cache range caller has no CPU reservation contract");
  }
  __imp__sub_8213C5F0(ctx,base);
}
REX_EXTERN(__imp__edf_native_device_reset);
REX_EXTERN(__imp__sub_8213D1C8);
REX_EXTERN(__imp__edf_native_device_drain);
REX_HOOK_RAW(sub_8213D1C8) {
  if(EDF_NATIVE_FLAG(host)) {
    __imp__edf_native_device_drain(ctx,base); return;
  }
  __imp__sub_8213D1C8(ctx,base);
}
REX_EXTERN(edf_native_reset_completion_tracking) {
  // D298 has drained old work, but has not yet freed/replaced writeback storage.
  // Its saved r31 still holds the device. Do not touch guest registers/memory.
  auto& state=edf::native::State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  const auto signals=state.signal_queues.find(ctx.r31.u32);
  std::lock_guard delivery_lock(state.signal_delivery_mutex);
  const auto delivery=state.signal_deliveries.find(ctx.r31.u32);
  if(delivery!=state.signal_deliveries.end() && delivery->second.pending())
    throw std::runtime_error("native reset attempted to discard undelivered worker signal");
  if(signals!=state.signal_queues.end() && signals->second->pending())
    throw std::runtime_error("native reset attempted to discard pending worker signals");
  state.signal_queues.erase(ctx.r31.u32);
  const auto fences=state.completion_queues.find(ctx.r31.u32);
  if(fences!=state.completion_queues.end() && fences->second->unsubmitted())
    throw std::runtime_error("native reset attempted to discard unsubmitted fences");
  state.completion_queues.erase(ctx.r31.u32);
  state.swap_clocks.erase(ctx.r31.u32);
  // D298 replaces command/completion storage, not the device's render words.
  // Keep their ownership; allocation/final Release delimit that lifetime.
  state.native_published_completions.erase(ctx.r31.u32);
  state.submission_cursors.Retire(ctx.r31.u32);
  state.completion_faults.erase(ctx.r31.u32);
  if(state.display_gamma_device==ctx.r31.u32) {
    state.display_gamma.reset(); state.display_gamma_device=0;
    if(state.presentation_frames) state.presentation_frames->Invalidate();
  }
  REXLOG_INFO("Native device reset: retired old completion tracking after drain, before storage replacement");
}
REX_EXTERN(edf_native_drain_worker_signals) {
  const auto device=ctx.r31.u32;
  for(;;) {
    edf::native::PollNativeWorkerSignals(ctx,base,device,true);
    size_t pending=0,unsubmitted=0;
    {
      auto& state=edf::native::State();
      std::lock_guard lock(state.mutex);
      const auto found=state.signal_queues.find(device);
      if(found!=state.signal_queues.end()) {
        pending=found->second->pending(); unsubmitted=found->second->unsubmitted();
      }
      std::lock_guard delivery_lock(state.signal_delivery_mutex);
      const auto delivery=state.signal_deliveries.find(device);
      pending+=size_t(delivery!=state.signal_deliveries.end() && delivery->second.pending());
    }
    if(unsubmitted) throw std::runtime_error("native drain has unsubmitted worker signals");
    if(!pending) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const edf::native::GuestReader reader(base);
  if(reader.Word(reader.Add(device,10868))) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
REX_HOOK_RAW(sub_8213D298) {
  if(EDF_NATIVE_FLAG(host)) {
    const auto device=ctx.r3.u32,configuration=ctx.r4.u32;
    __imp__edf_native_device_reset(ctx,base);
    if(configuration && ctx.r3.u32==0) {
      auto& state=edf::native::State();
      std::lock_guard order(state.submissions);
      std::lock_guard lock(state.mutex);
      const edf::native::GuestReader reader(base);
      state.submission_cursors.Initialize(device,reader.Word(reader.Add(device,13480)));
    }
    return;
  }
  if(EDF_NATIVE_FLAG(shader_bridge) && REXCVAR_GET(edf_native_fence_probe)) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    state.completion_queues.erase(ctx.r3.u32);
    state.native_published_completions.erase(ctx.r3.u32);
    state.completion_faults.erase(ctx.r3.u32);
  }
  __imp__sub_8213D298(ctx,base);
}

REX_EXTERN(__imp__edf_native_worker_init);
REX_HOOK_RAW(sub_8214EE50) {
  const edf::native::GuestReader reader(base);
  const auto flags=reader.Word(reader.Add(ctx.r3.u32,20416));
  __imp__edf_native_worker_init(ctx,base);
  REXLOG_INFO("Native worker initialization: retained CPU/thread setup; omitted Xbox setup packets; result={}, flags={:#x}, requested_mask={:#x}, skipped={}",
    ctx.r3.u32,flags,(flags>>24)&63,bool(flags&0x100));
}

REX_EXTERN(__imp__edf_native_device_defaults);
REX_HOOK_RAW(sub_8214EFF8) {
  const auto device=ctx.r3.u32;
  __imp__edf_native_device_defaults(ctx,base);
  edf::native::PublishNativeRenderState(base,device,0x8214eff8);
  REXLOG_INFO("Native device defaults: retained CPU descriptors/state; omitted Xbox setup packet tail");
}

// Preserve retail setter/dirty-mask/return semantics, then publish completed CPU
// state. Diagnostic-only until indirect, reset and concurrent writers are proven.
#define EDF_RENDER_STATE_SETTER(name,pc) \
  REX_EXTERN(__imp__##name); \
  REX_HOOK_RAW(name) { \
    const auto device=ctx.r3.u32; \
    __imp__##name(ctx,base); \
    edf::native::PublishNativeRenderState(base,device,pc); \
  }
EDF_RENDER_STATE_SETTER(sub_82134EE8,0x82134ee8)
EDF_RENDER_STATE_SETTER(sub_82134EB8,0x82134eb8)
EDF_RENDER_STATE_SETTER(sub_82134F18,0x82134f18)
EDF_RENDER_STATE_SETTER(sub_82134F58,0x82134f58)
EDF_RENDER_STATE_SETTER(sub_82134FE8,0x82134fe8)
EDF_RENDER_STATE_SETTER(sub_82135078,0x82135078)
EDF_RENDER_STATE_SETTER(sub_82135108,0x82135108)
EDF_RENDER_STATE_SETTER(sub_82135198,0x82135198)
EDF_RENDER_STATE_SETTER(sub_82135208,0x82135208)
EDF_RENDER_STATE_SETTER(sub_82135278,0x82135278)
EDF_RENDER_STATE_SETTER(sub_821352E8,0x821352e8)
EDF_RENDER_STATE_SETTER(sub_821353E8,0x821353e8)
EDF_RENDER_STATE_SETTER(sub_82135530,0x82135530)
EDF_RENDER_STATE_SETTER(sub_82135578,0x82135578)
EDF_RENDER_STATE_SETTER(sub_821355A8,0x821355a8)
EDF_RENDER_STATE_SETTER(sub_821355E8,0x821355e8)
EDF_RENDER_STATE_SETTER(sub_82135630,0x82135630)
EDF_RENDER_STATE_SETTER(sub_82135670,0x82135670)
EDF_RENDER_STATE_SETTER(sub_821356A0,0x821356a0)
EDF_RENDER_STATE_SETTER(sub_821356E0,0x821356e0)
EDF_RENDER_STATE_SETTER(sub_82135720,0x82135720)
EDF_RENDER_STATE_SETTER(sub_82135750,0x82135750)
EDF_RENDER_STATE_SETTER(sub_82135780,0x82135780)
EDF_RENDER_STATE_SETTER(sub_821357C0,0x821357c0)
EDF_RENDER_STATE_SETTER(sub_82135800,0x82135800)
EDF_RENDER_STATE_SETTER(sub_82135948,0x82135948)
EDF_RENDER_STATE_SETTER(sub_82135A10,0x82135a10)
EDF_RENDER_STATE_SETTER(sub_82135AB8,0x82135ab8)
EDF_RENDER_STATE_SETTER(sub_82135B08,0x82135b08)
EDF_RENDER_STATE_SETTER(sub_82135B40,0x82135b40)
EDF_RENDER_STATE_SETTER(sub_82135B78,0x82135b78)
EDF_RENDER_STATE_SETTER(sub_82135BB0,0x82135bb0)
EDF_RENDER_STATE_SETTER(sub_82136478,0x82136478)
EDF_RENDER_STATE_SETTER(sub_821364C8,0x821364c8)
EDF_RENDER_STATE_SETTER(sub_821364F8,0x821364f8)
EDF_RENDER_STATE_SETTER(sub_82137978,0x82137978)
EDF_RENDER_STATE_SETTER(sub_82137988,0x82137988)
#undef EDF_RENDER_STATE_SETTER

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

namespace {
thread_local edf::native::NativeFenceRecords native_fence_records;
template<class Reader>
void AuditNativeFenceRecord(const Reader& reader,uint32_t record,
    uint32_t tls,uint32_t caller,unsigned phase) noexcept {
  if(!REXCVAR_GET(edf_native_retirement_audit)) return;
  static std::atomic<uint64_t> counts[3]{};
  const auto count=counts[phase].fetch_add(1,std::memory_order_relaxed)+1;
  if(count>8 && (count&(count-1))) return;
  try {
    const auto device=reader.Word(record);
    const auto now=reader.Word(reader.Add(reader.Word(reader.Add(tls,256)),88));
    REXLOG_INFO("Native fence record: phase={} count={} record={:08X} device={:08X} kind={} kernel_start={} kernel_now={} callback={:08X} caller={:08X}",
      phase,count,record,device,reader.Word(reader.Add(record,4)),reader.Word(reader.Add(record,16)),now,
      reader.Word(reader.Add(device,13068)),caller);
  } catch(const std::exception& error) {
    REXLOG_WARN("Native fence record audit failed: {}",error.what());
  }
}
}
REX_EXTERN(__imp__sub_821394D8);
REX_HOOK_RAW(sub_821394D8) {
  if(!EDF_NATIVE_FLAG(host)) { __imp__sub_821394D8(ctx,base); return; }
  const edf::native::GuestReader guest(base);
  edf::native::NativeFenceRecord owned{};
  const edf::native::NativeFenceRecordAccess reader(guest,ctx.r3.u32,owned);
  edf::native::BeginNativeFenceRecord(reader,ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r13.u32,
    [] { return rex::chrono::Clock::QueryGuestTickCount(); });
  native_fence_records.Publish(ctx.r13.u32,ctx.r3.u32,owned);
  AuditNativeFenceRecord(reader,ctx.r3.u32,ctx.r13.u32,uint32_t(ctx.lr),0);
}
edf::native::NativeBufferWrites::WriterScope edf_native_begin_fence_counter_write(uint8_t*,uint32_t);
void edf_native_complete_fence_counter_write(uint8_t*,uint32_t);
REX_EXTERN(__imp__sub_82139508);
REX_HOOK_RAW(sub_82139508) {
  if(!EDF_NATIVE_FLAG(host)) { __imp__sub_82139508(ctx,base); return; }
  const edf::native::GuestReader guest(base);
  const auto record=ctx.r3.u32;
  // Detach before accounting/callbacks, allowing reentrant reuse without a
  // dangling map reference. Exceptions cannot leave a completed record active.
  auto owned=native_fence_records.Take(ctx.r13.u32,record);
  const edf::native::NativeFenceRecordAccess reader(guest,record,owned);
  edf::native::EndNativeFenceRecord(reader,ctx.r3.u32,ctx.r13.u32,
    [] { return rex::chrono::Clock::QueryGuestTickCount(); },
    [&](uint32_t callback,uint32_t device,uint32_t kind,uint32_t ticks,uint64_t elapsed) {
      auto work=ctx;
      if(work.r1.u32<96) throw std::runtime_error("invalid native fence accounting stack");
      work.r1.u64=work.r1.u32-96u;
      work.r3.u64=0; work.r4.u64=kind; work.r6.u64=elapsed;
      PPCRegister scale{},units{};
      work.fpscr.disableFlushMode();
      scale.u32=reader.Word(reader.Add(device,20020)); units.u32=reader.Word(0x82002170);
      // Match the two separately rounded retail single-precision products.
      const float scaled=float(double(scale.f32)*double(float(ticks)));
      work.f1.f64=double(float(double(scaled)*double(units.f32)));
      work.lr=0x821395B8;
      work.ctr.u64=callback; work.last_indirect_target=callback;
      rex::runtime::ResolveIndirectFunction(callback)(work,base);
      ctx.r3=work.r3;
    },[&](uint32_t total,uint32_t ticks) {
      const auto writer=edf_native_begin_fence_counter_write(base,total);
      reader.StoreDoubleWord(total,reader.DoubleWord(total)+ticks);
      edf_native_complete_fence_counter_write(base,total);
    });
  AuditNativeFenceRecord(reader,record,ctx.r13.u32,uint32_t(ctx.lr),2);
}

// Native stall policy uses host steady time. Failure unwinds, never fabricates
// guest completion through the retail device-error handler.
REX_EXTERN(__imp__sub_82139688);
REX_HOOK_RAW(sub_82139688) {
  edf::native::HookTiming poll_timing(edf::native::HookPhase::CompletionPoll);
  const bool native=EDF_NATIVE_FLAG(host);
  if(native) {
    const auto record_token=ctx.r3.u32,tls=ctx.r13.u32;
    try {
    const edf::native::GuestReader guest(base);
    auto& owned=native_fence_records.Find(ctx.r13.u32,ctx.r3.u32);
    const edf::native::NativeFenceRecordAccess reader(guest,ctx.r3.u32,owned);
    const auto device=reader.Word(ctx.r3.u32);
    AuditNativeFenceRecord(reader,ctx.r3.u32,ctx.r13.u32,uint32_t(ctx.lr),1);
    // A queued fence may depend on a CPU worker that has not consumed its
    // completed wake signal yet. Keep both completion domains progressing.
    edf::native::PollNativeWorkerSignals(ctx,base,device,true);
    {
      auto& state=edf::native::State();
      std::lock_guard lock(state.mutex);
      edf::native::PublishNativeCompletion(guest,state,device,true);
    }
    const auto limit=REXCVAR_GET(edf_native_wait_stall_ms);
    if(limit<1 || limit>600000) throw std::runtime_error("invalid native wait stall deadline");
    const bool again=edf::native::PollHostFenceProgress(reader,record_token,tls,owned.progress,
      edf::native::NativeWaitProgress::Clock::now(),std::chrono::milliseconds(limit),
      [&](uint32_t failed_device,bool stalled) {
        REXLOG_ERROR("Native wait failed: device={:08X} record={:08X} stalled={} limit_ms={}; no completion fabricated",
          failed_device,record_token,stalled,limit);
        throw std::runtime_error(stalled?"native GPU/worker wait made no progress":"native wait encountered device failure");
      });
    ctx.r3.u64=again?1:0;
    if(again) {
      edf::native::NativeFrameWaitTrace waiting(edf::native::FrameWaitKind::GuestFence);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return;
    } catch(...) {
      native_fence_records.Discard(tls,record_token);
      throw;
    }
  }
  __imp__sub_82139688(ctx,base);
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
  pacing.clock.Reset(now-std::chrono::nanoseconds(int64_t(double(fraction)*1e9/60.0)),tick);
  REXLOG_INFO("Native engine pacing: paused by the settings menu for {} ms; resumed at tick {} (clock rebased, no catch-up)",
    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-began).count(),tick);
}
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
  const bool unlocked=REXCVAR_GET(edf_native_unlock_framerate) && ctx.lr==0x821A6894 &&
    !edf::native::State().movie_pacing_active.load(std::memory_order_relaxed);
  native_loop_budget={};
  const auto divisor=reader.Word(reader.Add(object,4));
  auto& state=edf::native::PacingState();
  std::lock_guard lock(state.mutex);
  const auto previous=reader.DoubleWord(0x8257C308);
  auto sampled_at=edf::native::NativePacingClock::Clock::now();
  auto current=state.clock.Sample(sampled_at);
  auto steps=edf::native::NativePacingSteps(current,previous,divisor);
  edf::native::NativeFrameWaitTrace waiting(edf::native::FrameWaitKind::Engine,
    !unlocked && edf::native::NativePacingPending(steps));
  const auto wait_began=sampled_at;
  while(!unlocked && edf::native::NativePacingPending(steps)) {
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
  reader.StoreDoubleWord(0x8257C300,state.clock.Sample(edf::native::NativePacingClock::Clock::now()));
  __imp__sub_821BEB38(ctx,base);
}

// Observe the retained recording/direct-submission transitions independently
// of scene setup. No packet, queue, fence or scheduling behavior is changed.
REX_EXTERN(__imp__sub_8213CF60);
REX_EXTERN(sub_8213C5F0);
REX_EXTERN(sub_8213CDC0);
REX_EXTERN(sub_8213C928);
REX_HOOK_RAW(sub_8213CF60) {
  edf::native::HookTiming timing(edf::native::HookPhase::SubmissionFlush);
  if(!EDF_NATIVE_FLAG(host)) { __imp__sub_8213CF60(ctx,base); return; }
  const edf::native::GuestReader reader(base);
  const auto device=ctx.r3.u32;
  auto work=ctx;
  if(work.r1.u32<112) throw std::runtime_error("invalid native submission flush stack");
  work.r1.u64=work.r1.u32-112u;
  ctx.r3.u64=edf::native::FlushNativeSubmission(reader,device,
    [&] {
      work.r3.u64=device; work.r4.u64=reader.Add(work.r1.u32,84); work.r5.u64=reader.Add(work.r1.u32,80);
      work.lr=0x8213CF9C; sub_8213C5F0(work,base);
      if(reader.Word(reader.Add(work.r1.u32,80))) throw std::runtime_error("native cache collection returned GPU packet words");
    },
    [&] { work.r3.u64=device; work.lr=0x8213CFE0; sub_8213CDC0(work,base); },
    [&] { return reader.Word(0x82578D08)!=0; },
    [&](uint32_t target) {
      work.r3.u64=device; work.r4.u64=target; work.r5.u64=0; work.r6.u64=0;
      work.lr=0x8213D020; sub_8213C928(work,base);
    });
}
REX_EXTERN(__imp__sub_8213CDC0);
REX_EXTERN(sub_82141500);
REX_EXTERN(sub_8213CB30);
REX_EXTERN(sub_8213C788);
REX_EXTERN(sub_8213C868);
REX_EXTERN(sub_8213CC20);
REX_HOOK_RAW(sub_8213CDC0) {
  edf::native::HookTiming timing(edf::native::HookPhase::DescriptorSubmit);
  if(!EDF_NATIVE_FLAG(host)) { __imp__sub_8213CDC0(ctx,base); return; }
  const edf::native::GuestReader reader(base);
  const auto device=ctx.r3.u32;
  auto work=ctx;
  if(work.r1.u32<128) throw std::runtime_error("invalid native descriptor submission stack");
  work.r1.u64=work.r1.u32-128u;
  edf::native::SubmitNativeDescriptor(reader,device,
    [&] { work.r3.u64=device; work.lr=0x8213CE44; sub_82141500(work,base); return work.r3.u32; },
    [&] { work.r3.u64=reader.Add(device,13000); work.lr=0x8213CEA0; sub_8213CB30(work,base); return work.r3.u32; },
    [&](uint32_t next) {
      work.r3.u64=device; work.r4.u64=next; work.lr=0x8213CEC8;
      sub_8213C788(work,base); return work.r3.u32;
    },
    [&](uint32_t captured,uint32_t address,uint32_t words) {
      work.r3.u64=device; work.r4.u64=captured; work.r5.u64=address; work.r6.s64=int32_t(words);
      work.r7.u64=0; work.r8.u64=reader.Add(device,13000); work.lr=0x8213CEF8;
      sub_8213C868(work,base); return work.r3.u32;
    },
    [&] { work.r3.u64=device; work.r4.u64=0; work.lr=0x8213CF24; sub_8213CC20(work,base); });
  ctx.r3=work.r3;
}
REX_EXTERN(sub_821394D8);
REX_EXTERN(sub_82139688);
REX_EXTERN(sub_82139508);
namespace {
void RunNativeAllocatorWait(PPCContext& ctx,uint8_t* base,bool generation) {
  const edf::native::GuestReader reader(base);
  const auto device=ctx.r3.u32,first=ctx.r4.u32,second=ctx.r5.u32;
  auto work=ctx;
  if(work.r1.u32<144) throw std::runtime_error("invalid native allocator wait stack");
  work.r1.u64=work.r1.u32-144u;
  const auto token=reader.Add(work.r1.u32,80);
  auto begin=[&] {
    work.r3.u64=token; work.r4.u64=device; work.r5.u64=generation?2:1;
    work.lr=generation?0x8213BC98:0x8213BD3C; sub_821394D8(work,base);
  };
  auto poll=[&] {
    work.r3.u64=token; work.lr=generation?0x8213BCA0:0x8213BD44;
    sub_82139688(work,base); return work.r3.u32!=0;
  };
  auto end=[&] {
    work.r3.u64=token; work.lr=generation?0x8213BCD8:0x8213BD80; sub_82139508(work,base);
  };
  if(generation) {
    edf::native::WaitNativeAllocationGeneration(reader,device,first,second,begin,poll,end);
    ctx.r3=work.r3;
  } else {
    ctx.r3.u64=edf::native::WaitNativeRingRange(reader,device,first,second,begin,poll,end);
  }
}
}
REX_EXTERN(__imp__sub_8213BC48);
REX_HOOK_RAW(sub_8213BC48) {
  if(!EDF_NATIVE_FLAG(host)) { __imp__sub_8213BC48(ctx,base); return; }
  RunNativeAllocatorWait(ctx,base,true);
}
REX_EXTERN(__imp__sub_8213BCE0);
REX_HOOK_RAW(sub_8213BCE0) {
  if(!EDF_NATIVE_FLAG(host)) { __imp__sub_8213BCE0(ctx,base); return; }
  RunNativeAllocatorWait(ctx,base,false);
}
REX_EXTERN(__imp__sub_8213C928);
REX_EXTERN(__imp__sub_8214E5B8);
REX_HOOK_RAW(sub_8214E5B8) {
  if(!EDF_NATIVE_FLAG(host)) { __imp__sub_8214E5B8(ctx,base); return; }
  const edf::native::GuestReader reader(base);
  const auto device=reader.Word(reader.Word(0x82000720));
  auto work=ctx;
  if(work.r1.u32<144) throw std::runtime_error("invalid native worker wait stack");
  work.r1.u64=work.r1.u32-144u;
  const auto token=reader.Add(work.r1.u32,80);
  edf::native::WaitNativeWorkerSlots(reader,device,
    [&] { work.r3.u64=token; work.r4.u64=device; work.r5.u64=0;
      work.lr=0x8214E600; sub_821394D8(work,base); },
    [&] { work.r3.u64=token; work.lr=0x8214E608; sub_82139688(work,base); return work.r3.u32!=0; },
    [&] { work.r3.u64=token; work.lr=0x8214E628; sub_82139508(work,base); });
  ctx.r3=work.r3;
}
// Native mode owns wait control flow. Submission, wait-record accounting and
// error policy remain retained services; completion polling publishes native
// D3D results. Optional diagnostics below run only after this boundary returns.
REX_EXTERN(sub_8213CF60);
REX_EXTERN(sub_821394D8);
REX_EXTERN(sub_82139688);
REX_EXTERN(sub_82139508);
REX_HOOK_RAW(sub_8213C928) {
  edf::native::HookTiming wait_timing(edf::native::HookPhase::FenceWait);
  const auto device=ctx.r3.u32,target=ctx.r4.u32;
  if(EDF_NATIVE_FLAG(host)) {
    const edf::native::GuestReader reader(base);
    const auto wait_device=ctx.r3.u32,target=ctx.r4.u32,kind=ctx.r5.u32;
    auto work=ctx;
    // Preserve the caller frame for retained submission/error services. The
    // old record address is now an opaque key; its fields live in native storage.
    if(work.r1.u32<144) throw std::runtime_error("invalid native fence wait stack");
    work.r1.u64=work.r1.u32-144u;
    const auto record=reader.Add(work.r1.u32,80);
    edf::native::WaitNativeResourceFence(reader,wait_device,target,
      [&] { work.r3.u64=wait_device; work.lr=0x8213C97C; sub_8213CF60(work,base); },
      [&] { work.r3.u64=record; work.r4.u64=wait_device; work.r5.u64=kind;
        work.lr=0x8213C9A8; sub_821394D8(work,base); },
      [&] { work.r3.u64=record; work.lr=0x8213C9B4; sub_82139688(work,base); return work.r3.u32!=0; },
      [&] { work.r3.u64=record; work.lr=0x8213C9E8; sub_82139508(work,base); });
    ctx.r3=work.r3;
  } else {
    __imp__sub_8213C928(ctx,base);
  }
  wait_timing.Finish(); // Exclude optional post-wait diagnostic validation.
  if(!EDF_NATIVE_FLAG(shader_bridge) || !REXCVAR_GET(edf_native_fence_probe)) return;
  auto& state=edf::native::State();
  std::lock_guard lock(state.mutex);
  if(state.completion_faults.contains(device)) return;
  try {
    const edf::native::GuestReader reader(base);
    const auto issued=reader.Word(reader.Add(device,10780));
    const auto guest_completed=reader.Word(reader.Word(reader.Add(device,10768)));
    std::optional<uint32_t> native_completed;
    const auto queue=state.completion_queues.find(device);
    if(queue!=state.completion_queues.end() && queue->second) native_completed=queue->second->Poll();
    const bool native_pending=native_completed && edf::native::GuestFencePending(issued,target,*native_completed);
    ++state.completion_waits;
    if(native_pending) ++state.completion_waits_native_pending;
    if(state.completion_waits<=16 || state.completion_waits%1000==0 ||
       (native_pending && state.completion_waits_native_pending<=8))
      REXLOG_INFO("Native fence wait probe: device={:#x}, target={}, issued={}, guest_completed={}, guest_pending={}, native_known={}, native_completed={}, native_pending={}, waits={}, native_pending_waits={}; observational only",
        device,target,issued,guest_completed,edf::native::GuestFencePending(issued,target,guest_completed),
        native_completed.has_value(),native_completed.value_or(0),native_pending,state.completion_waits,
        state.completion_waits_native_pending);
    if(REXCVAR_GET(edf_native_validate_wait) && target) {
      if(queue==state.completion_queues.end() || !queue->second)
        throw std::runtime_error("native wait validation has no submitted event queue");
      const auto started=std::chrono::steady_clock::now();
      // A diagnostic deadline, not a guessed conversion of the retail tick
      // timeout. This does not replace guest error handling or its counters.
      const auto result=queue->second->WaitUntil(issued,target,started+std::chrono::seconds(2));
      if(result!=edf::native::NativeWaitResult::Complete)
        throw std::runtime_error(result==edf::native::NativeWaitResult::Unsubmitted ?
          "native wait validation target was not submitted" :
          result==edf::native::NativeWaitResult::TimedOut ? "native wait validation timed out" :
          "native wait validation cancelled");
      if(++state.completion_validated_waits<=8 || state.completion_validated_waits%1000==0)
        REXLOG_INFO("Native wait validated: device={:#x}, target={}, completed={}, elapsed_ms={}, count={}; guest counters unchanged",
          device,target,queue->second->completed().value_or(0),
          std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count(),
          state.completion_validated_waits);
    }
  } catch(const std::exception& error) {
    state.completion_faults.insert(device);
    REXLOG_ERROR("Native fence wait probe disabled for device {:#x}: {}",device,error.what());
  }
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

// Whole-pool destruction has no per-owner release callbacks. Resolve all
// backing allocations before taking the registry lock, and retire before the
// original routine detaches/frees its nodes. Never hold bridge locks over free.
namespace {
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

// The model loader copies into owner+48 storage before associating it with
// its embedded resource. Publish after the complete creator, not mid-copy.
namespace {
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

// Bulk writes can run under a native submission lock. Queue completed physical
// writes without entering renderer state; the next indexed draw retires affected
// storage. Scalar/inline stores and other native providers are not covered here.
edf::native::NativeBufferWrites* NativeBufferWriteQueue(uint8_t* base,uint32_t destination,uint32_t bytes,
    std::optional<edf::native::NativeBufferWrites::Range>* range) {
  if(range) range->reset();
  auto* memory=REX_KERNEL_MEMORY();
  if(!EDF_NATIVE_FLAG(shader_bridge) || !memory || memory->virtual_membase()!=base) return nullptr;
  const auto extent=edf::native::MapCompletedNativePhysicalWrite(*memory,destination,bytes);
  if(!extent) return nullptr;
  if(range && !extent->all) *range=edf::native::NativeBufferWrites::Range{extent->address,extent->bytes};
  return &edf::native::BufferWrites();
}
void NotifyCompletedNativeBufferWrite(uint8_t* base,uint32_t destination,uint32_t bytes,bool notify_versions,bool generated,
    edf::native::NativeBufferWrites::WriterSite site) {
  if(!EDF_NATIVE_FLAG(shader_bridge) || !bytes || destination<0xa0000000u) return;
  auto* memory=REX_KERNEL_MEMORY();
  if(!memory || memory->virtual_membase()!=base) return;
  const auto extent=edf::native::MapCompletedNativePhysicalWrite(*memory,destination,bytes);
  if(!extent) return;
  if(site.provider && !REXCVAR_GET(edf_native_retirement_audit)) site={};
  edf::native::BufferWrites().Record(extent->address,extent->bytes,generated,site);
  if(!extent->all && notify_versions && REXCVAR_GET(edf_native_mesh_watch_audit))
    edf::native::NotifyPhysicalProviderWrite(*memory,extent->address,extent->bytes);
}
edf::native::NativeBufferWrites::WriterScope edf_native_begin_fence_counter_write(uint8_t* base,uint32_t total) {
  return BeginNativeBufferWrite(base,total,8,true);
}
void edf_native_complete_fence_counter_write(uint8_t* base,uint32_t total) {
  NotifyCompletedNativeBufferWrite(base,total,8);
}
edf::native::NativeBufferWrites::WriterScope edf_native_begin_counter_reset_write(uint8_t* base,uint32_t device) {
  return BeginNativeBufferWrite(base,edf::native::GuestReader(base).Add(device,20000),48,true);
}
void edf_native_complete_counter_reset_write(uint8_t* base,uint32_t device) {
  NotifyCompletedNativeBufferWrite(base,edf::native::GuestReader(base).Add(device,20000),48);
}
void edf_native_counter_store_word(uint8_t* base,uint32_t address,uint32_t value) {
  const auto writer=BeginNativeBufferWrite(base,address,4,true);
  edf::native::GuestReader(base).StoreWord(address,value);
  NotifyCompletedNativeBufferWrite(base,address,4);
}
REX_EXTERN(__imp__sub_82139228);
REX_EXTERN(__imp__sub_82138858);
REX_HOOK_RAW(sub_82138858) {
  if(EDF_NATIVE_FLAG(host)) {
    const edf::native::GuestReader reader(base);
    if(edf::native::RunNativePixIdle(ctx,reader,[&](uint32_t callback,auto& work) {
      rex::runtime::ResolveIndirectFunction(callback)(work,base);
    })) return;
  }
  __imp__sub_82138858(ctx,base);
}
REX_EXTERN(__imp__edf_native_counter_reset_cpu_tail);
REX_HOOK_RAW(sub_82139228) {
  if(EDF_NATIVE_FLAG(host)) __imp__edf_native_counter_reset_cpu_tail(ctx,base);
  else __imp__sub_82139228(ctx,base);
}

// Clear's native flags are low four color-target bits, bit 4 depth, bit 5
// stencil (verified in 821334E8), not desktop D3D9's 1/2/4 flag values.
REX_EXTERN(__imp__sub_821340D0);
REX_HOOK_RAW(sub_821340D0) {
  if (EDF_NATIVE_FLAG(shader_bridge) && (ctx.r6.u32&15)) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    try {
      const edf::native::GuestReader reader(base);
      auto word=[&](uint32_t offset) { return reader.Word(reader.Add(ctx.r3.u32,offset)); };
      // 82133D20 reads the four actual color surfaces at device+12168..12180.
      // Match resource identity, not the last high-level Begin scope. Never
      // interpret r7 as an address: 821340D0 unpacks that packed ARGB value.
      for (uint32_t slot=0;slot<4;++slot) {
        if (!(ctx.r6.u32&(1u<<slot))) continue;
        const auto surface=word(12168+slot*4);
        for (auto& [owner,registered]:state.render_targets) {
          if (!surface || registered.surface_handle!=surface) continue;
          auto& target=registered.native;
          // Partial/tiled clears need their own rectangle mapping. Retain no
          // claim of current initialized contents when that write is skipped.
          if (ctx.r5.u32 || word(11584) || word(12376) || word(12380) ||
              word(12384)!=target.sampled.width || word(12388)!=target.sampled.height ||
              (*reader.Bytes(reader.Add(ctx.r3.u32,10808),1)&0x30)) {
            target.content_valid=false;
            if (++state.color_clear_skips<=5)
              REXLOG_INFO("Native color clear skipped: surface={:#x}, rectangles={:#x}, viewport={}x{}, target={}x{}",
                surface,ctx.r5.u32,word(12384),word(12388),target.sampled.width,target.sampled.height);
            continue;
          }
          edf::native::ClearNativeColorTarget(edf::native::SceneRecorderLocked(state),target,ctx.r7.u32);
          if (++state.color_clears<=5)
            REXLOG_INFO("Native color clear: surface={:#x}, slot={}, ARGB={:#x}, initialized=true",surface,slot,ctx.r7.u32);
        }
      }
    } catch (const std::exception& error) {
      REXLOG_ERROR("Native color clear: {}",error.what());
    }
  }
  if (EDF_NATIVE_FLAG(shader_bridge) && (ctx.r6.u32&0x30)) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    try {
      const edf::native::GuestReader reader(base);
      auto word=[&](uint32_t offset) { return reader.Word(reader.Add(ctx.r3.u32,offset)); };
      const auto found=state.depth_targets.find(word(12184));
      if (found!=state.depth_targets.end()) {
        auto& target=found->second;
        // Guest clears intersect viewport/scissor and may carry explicit
        // rectangles. D3D11 ClearDepthStencilView clears the whole resource.
        // Never expand an unimplemented partial clear into a whole-surface one.
        if (ctx.r5.u32 || word(11584) || word(12376) || word(12380) ||
            word(12384)!=target.width || word(12388)!=target.height) {
          ++state.depth_clear_skips;
          if (ctx.r6.u32&0x10) target.depth_valid=false;
          if (ctx.r6.u32&0x20) target.stencil_valid=false;
        } else {
          edf::native::ClearNativeDepthTarget(edf::native::SceneRecorderLocked(state),target,
            (ctx.r6.u32&0x10)!=0,(ctx.r6.u32&0x20)!=0,float(ctx.f1.f64),uint8_t(ctx.r9.u32));
          ++state.depth_clears;
          if (state.depth_clears<=5 || state.depth_clears%1000==0)
            REXLOG_INFO("Native depth clear: cleared={}, skipped={}, flags={:#x}, depth={}, stencil={}",
              state.depth_clears,state.depth_clear_skips,ctx.r6.u32,ctx.f1.f64,ctx.r9.u32);
        }
      }
    } catch (const std::exception& error) {
      if (++state.depth_errors<=10) REXLOG_ERROR("Native depth clear: {}",error.what());
    }
  }
  __imp__sub_821340D0(ctx,base);
}

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
REX_EXTERN(__imp__sub_82137F98);
REX_HOOK_RAW(sub_82137F98) {
  edf::native::HookTiming timing(edf::native::HookPhase::ColorTarget);
  __imp__sub_82137F98(ctx,base);
}
REX_EXTERN(__imp__sub_82137CB8);
REX_HOOK_RAW(sub_82137CB8) {
  edf::native::HookTiming timing(edf::native::HookPhase::DepthTarget);
  const auto device=ctx.r3.u32;
  __imp__sub_82137CB8(ctx,base);
  edf::native::PublishNativeRenderState(base,device,0x82137cb8);
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

// CPU SetStreamSource: retain the original byte offset and full stride. The
// device's fetch descriptor encodes these and cannot serve as a native API.
namespace {
void AuditNativeRetirement(const edf::native::GuestReader& reader,uint32_t device,bool index,
    edf::native::NativeRetirementPath path,uint32_t previous,uint32_t value,uint32_t cursor) noexcept {
  if(!REXCVAR_GET(edf_native_retirement_audit)) return;
  static std::atomic<uint64_t> counts[2][4]{};
  const auto branch=static_cast<unsigned>(path);
  const auto count=counts[index?1:0][branch].fetch_add(1,std::memory_order_relaxed)+1;
  if(count>8 && (count&(count-1))) return;
  try {
    const auto descriptor=reader.Word(reader.Add(device,13140));
    REXLOG_INFO("Native retirement: index={} branch={} count={} device={:08X} previous={:08X} value={:08X} cursor={:08X} descriptor={:08X}",
      index,branch,count,device,previous,value,cursor,descriptor);
  } catch(const std::exception& error) {
    REXLOG_WARN("Native retirement audit read failed: {}",error.what());
  }
}
}
REX_EXTERN(__imp__sub_82137410);
REX_EXTERN(sub_82141440);
namespace {
void InstallNativeStaticGeometry(PPCContext& ctx,uint8_t* base,uint32_t device,
    const edf::native::NativeSceneGeometrySource& input,edf::native::NativeSceneGeometryInstallState& progress) {
  const edf::native::GuestReader reader(base);
  edf::native::InstallNativeSceneGeometry(reader,device,input,
    [&](bool index) {
      auto work=ctx;
      const uint32_t frame=index?128:144;
      if(work.r1.u32<frame) throw std::runtime_error("invalid static geometry retirement stack");
      work.r1.u64=work.r1.u32-frame; work.r3.u64=device;
      work.lr=index?0x8213761C:0x821374D0;
      // Queue growth is an explicit remaining allocator boundary, not a setter.
      edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::RetirementAllocation);
      sub_82141440(work,base); return work.r3.u32;
    },[&](bool index) {
      const uint32_t offset=index?48:64;
      if(ctx.r1.u32<offset) throw std::runtime_error("invalid static geometry retirement tag");
      return reader.Word(ctx.r1.u32-offset);
    },[&](bool index,auto path,auto previous,auto value,auto cursor) {
      AuditNativeRetirement(reader,device,index,path,previous,value,cursor);
    },[&](edf::native::NativeGeometryBinding binding) {
      auto& state=edf::native::State();
      std::lock_guard submission(state.submissions);
      std::lock_guard lock(state.mutex);
      switch(binding) {
        case edf::native::NativeGeometryBinding::Stream:
          state.streams.insert_or_assign({device,0},edf::native::GuestStream{input.vertex,0,input.stride}); break;
        case edf::native::NativeGeometryBinding::Declaration:
          state.declaration_bindings.insert_or_assign(device,input.declaration); break;
        case edf::native::NativeGeometryBinding::Index:
          state.index_bindings.insert_or_assign(device,input.index); break;
      }
    },&progress);
}
}
REX_HOOK_RAW(sub_82137410) {
  const auto device=ctx.r3.u32, stream=ctx.r4.u32;
  const edf::native::GuestStream binding{ctx.r5.u32,ctx.r6.u32,ctx.r7.u32};
  if(!EDF_NATIVE_FLAG(shader_bridge)) { __imp__sub_82137410(ctx,base); return; }
  const edf::native::GuestReader reader(base);
  edf::native::SetNativeStreamResource(reader,device,stream,binding.resource,binding.offset,binding.stride,ctx.r8.u64,
    [&] {
      auto work=ctx;
      if(work.r1.u32<144) throw std::runtime_error("invalid stream setter guest stack");
      work.r1.u64=work.r1.u32-144u; work.r3.u64=device; work.lr=0x821374D0;
      edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::RetirementAllocation);
      sub_82141440(work,base);
      return work.r3.u32;
    },
    [&] {
      if(ctx.r1.u32<64) throw std::runtime_error("invalid stream setter tag address");
      return reader.Word(ctx.r1.u32-64u);
    },[&](auto path,uint32_t previous,uint32_t value,uint32_t cursor) {
      AuditNativeRetirement(reader,device,false,path,previous,value,cursor);
    });
  if (EDF_NATIVE_FLAG(shader_bridge)) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    state.streams.insert_or_assign({device,stream},binding);
  }
}

// CPU SetIndices: own the binding at its producer, rather than re-reading the
// guest device slot for each native draw. Keep the original old-resource fence
// and deferred-retirement bookkeeping; a null binding is an explicit unbind.
REX_EXTERN(__imp__sub_821375C0);
REX_EXTERN(sub_82141440);
REX_HOOK_RAW(sub_821375C0) {
  const auto device=ctx.r3.u32,resource=ctx.r4.u32;
  if(!EDF_NATIVE_FLAG(shader_bridge)) { __imp__sub_821375C0(ctx,base); return; }
  const edf::native::GuestReader reader(base);
  edf::native::SetNativeIndexResource(reader,device,resource,
    [&] {
      auto work=ctx;
      // Match the original caller frame while invoking the retained queue
      // allocator. In particular, do not overwrite the legacy tag at SP-48.
      if(work.r1.u32<128) throw std::runtime_error("invalid index setter guest stack");
      work.r1.u64=work.r1.u32-128u; work.r3.u64=device; work.lr=0x8213761C;
      edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::RetirementAllocation);
      sub_82141440(work,base);
      return work.r3.u32;
    },
    [&] {
      if(ctx.r1.u32<48) throw std::runtime_error("invalid index setter tag address");
      return reader.Word(ctx.r1.u32-48u);
    },[&](auto path,uint32_t previous,uint32_t value,uint32_t cursor) {
      AuditNativeRetirement(reader,device,true,path,previous,value,cursor);
    });
  if(EDF_NATIVE_FLAG(shader_bridge)) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    state.index_bindings.insert_or_assign(device,resource);
  }
}

namespace {
// reader is what the device, shader and stack words go through: the material
// activation passes its device window, the setter hooks a fresh reader.
template<class Reader>
void BindNativeShaderResource(PPCContext& ctx,uint8_t* base,bool pixel,const Reader& reader) {
  const auto device=ctx.r3.u32,shader=ctx.r4.u32;
  edf::native::SetNativeShaderResource(reader,device,shader,pixel,[&] {
    auto work=ctx;
    if(work.r1.u32<128) throw std::runtime_error("invalid native shader setter stack");
    work.r1.u64=work.r1.u32-128; work.r3.u64=device;
    work.lr=pixel?0x82149664:0x82149924;
    edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::RetirementAllocation);
    sub_82141440(work,base);
    return work.r3.u32;
  },[&] {
    if(ctx.r1.u32<48) throw std::runtime_error("invalid native shader retirement tag");
    return reader.Word(ctx.r1.u32-48);
  });
}
void BindNativeShaderResource(PPCContext& ctx,uint8_t* base,bool pixel) {
  BindNativeShaderResource(ctx,base,pixel,edf::native::GuestReader(base));
}
// 8213BA98's native branch: SetTexture's CPU words and retirement. Owned
// under the same flags as the shader setters; otherwise the guest setter runs.
bool NativeTextureBindingOwned() {
  return EDF_NATIVE_FLAG(shader_bridge) && EDF_NATIVE_FLAG(material_activation) &&
    !(edf::native::native_queued_scene_group && edf::native::native_queued_scene_group->material_compatibility_only);
}
template<class Reader>
void BindNativeTextureResource(PPCContext& ctx,uint8_t* base,const Reader& reader) {
  const auto device=ctx.r3.u32;
  edf::native::SetNativeTextureResource(reader,device,ctx.r4.u32,ctx.r5.u32,ctx.r6.u64,[&] {
    auto work=ctx;
    if(work.r1.u32<160) throw std::runtime_error("invalid native texture setter stack");
    work.r1.u64=work.r1.u32-160; work.r3.u64=device; work.lr=0x8213BBE0;
    edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::RetirementAllocation);
    sub_82141440(work,base); return work.r3.u32;
  },[&] {
    if(ctx.r1.u32<80) throw std::runtime_error("invalid native texture retirement tag");
    return reader.Word(ctx.r1.u32-80);
  });
}
void PublishNativeShaderBinding(uint32_t device,uint32_t shader,bool pixel) {
  if(!EDF_NATIVE_FLAG(shader_bridge)) return;
  auto& state=edf::native::State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  state.shader_bindings.Set(device,shader,pixel);
}
void PublishNativeMaterialParameters(uint8_t* base,uint32_t instance) {
  if(!EDF_NATIVE_FLAG(shader_bridge)) return;
  auto& state=edf::native::State();
  // CPU-owned metadata publication is serialized by the registry mutex.
  std::lock_guard lock(state.mutex);
  state.material_parameters.Publish(edf::native::GuestReader(base),instance);
}
void RetireNativeMaterialParameters(uint8_t* base,uint32_t instance,bool array) {
  if(!EDF_NATIVE_FLAG(shader_bridge)) return;
  auto& state=edf::native::State();
  // Retained metadata snapshots keep their lifetime independently of this map.
  std::lock_guard lock(state.mutex);
  if(array) {
    if(instance<4) throw std::runtime_error("invalid material array header");
    const edf::native::GuestReader reader(base);
    state.material_parameters.RetireArray(instance,reader.Word(instance-4));
  } else state.material_parameters.Retire(instance);
}
void PublishNativeDeclarationContents(uint8_t* base,uint32_t handle) {
  if(!EDF_NATIVE_FLAG(shader_bridge) || !handle) return;
  auto& state=edf::native::State();
  // Immutable declaration snapshots issue no immediate-context commands.
  std::lock_guard lock(state.mutex);
  const edf::native::GuestReader reader(base);
  const auto count=reader.Word(reader.Add(handle,24));
  if(count>64) throw std::runtime_error("invalid published declaration count");
  const size_t bytes=size_t(count)*12;
  state.declarations.Publish(handle,bytes?std::span<const uint8_t>{reader.Bytes(reader.Add(handle,52),bytes),bytes}:
    std::span<const uint8_t>{});
}
void PublishNativeDeclarationBinding(uint32_t device,uint32_t declaration) {
  if(!EDF_NATIVE_FLAG(shader_bridge)) return;
  auto& state=edf::native::State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  state.declaration_bindings.insert_or_assign(device,declaration);
}
}
REX_EXTERN(__imp__sub_82149AB0);
REX_EXTERN(__imp__sub_821BC230);
REX_HOOK_RAW(sub_821BC230) {
  const auto instance=ctx.r3.u32;
  RetireNativeMaterialParameters(base,instance,false);
  __imp__sub_821BC230(ctx,base);
  if(ctx.r3.u32&255) PublishNativeMaterialParameters(base,instance);
}
REX_EXTERN(__imp__sub_821BD110);
REX_HOOK_RAW(sub_821BD110) {
  const auto instance=ctx.r3.u32;
  RetireNativeMaterialParameters(base,instance,false);
  __imp__sub_821BD110(ctx,base);
  PublishNativeMaterialParameters(base,instance);
}
REX_EXTERN(__imp__sub_820ABE88);
REX_HOOK_RAW(sub_820ABE88) {
  RetireNativeMaterialParameters(base,ctx.r3.u32,false);
  __imp__sub_820ABE88(ctx,base);
}
REX_EXTERN(__imp__sub_820ABF20);
REX_HOOK_RAW(sub_820ABF20) {
  RetireNativeMaterialParameters(base,ctx.r3.u32,(ctx.r4.u32&2)!=0);
  __imp__sub_820ABF20(ctx,base);
}
REX_HOOK_RAW(sub_82149AB0) {
  __imp__sub_82149AB0(ctx,base);
  PublishNativeDeclarationContents(base,ctx.r3.u32);
}
REX_EXTERN(__imp__sub_82147BA0);
REX_HOOK_RAW(sub_82147BA0) {
  const auto handle=ctx.r4.u32;
  __imp__sub_82147BA0(ctx,base);
  PublishNativeDeclarationContents(base,handle);
}
REX_EXTERN(__imp__sub_821498C8);
REX_EXTERN(__imp__sub_8213BA98);
REX_HOOK_RAW(sub_8213BA98) {
  if(!NativeTextureBindingOwned()) {
    edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::TextureBinding);
    __imp__sub_8213BA98(ctx,base); return;
  }
  BindNativeTextureResource(ctx,base,edf::native::GuestReader(base));
}
REX_HOOK_RAW(sub_821498C8) {
  const auto device=ctx.r3.u32,shader=ctx.r4.u32;
  if(EDF_NATIVE_FLAG(shader_bridge) && EDF_NATIVE_FLAG(material_activation) &&
     (!edf::native::native_queued_scene_group || !edf::native::native_queued_scene_group->material_compatibility_only)) BindNativeShaderResource(ctx,base,false);
  else {
    edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::ShaderBinding);
    __imp__sub_821498C8(ctx,base);
  }
  PublishNativeShaderBinding(device,shader,false);
}
REX_EXTERN(__imp__sub_82149608);
REX_HOOK_RAW(sub_82149608) {
  const auto device=ctx.r3.u32,shader=ctx.r4.u32;
  if(EDF_NATIVE_FLAG(shader_bridge) && EDF_NATIVE_FLAG(material_activation) &&
     (!edf::native::native_queued_scene_group || !edf::native::native_queued_scene_group->material_compatibility_only)) BindNativeShaderResource(ctx,base,true);
  else {
    edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::ShaderBinding);
    __imp__sub_82149608(ctx,base);
  }
  PublishNativeShaderBinding(device,shader,true);
}
// Both retail writers of device+11536: the explicit declaration setter and
// the FVF setter. Keep CPU dirty flags and FVF conversion; draws consume the
// native binding. B0A0 returns the converted declaration in volatile r4, not
// its input FVF value (verified by its 47BA0 call and final store).
REX_EXTERN(__imp__sub_82149A90);
REX_HOOK_RAW(sub_82149A90) {
  const auto device=ctx.r3.u32,declaration=ctx.r4.u32;
  if(EDF_NATIVE_FLAG(shader_bridge)) {
    const edf::native::GuestReader reader(base);
    reader.StoreWord(reader.Add(device,11536),declaration);
    ctx.r12.u64=uint64_t(1)<<51;
    ctx.r11.u64=(uint64_t(reader.Word(reader.Add(device,16)))<<32)|reader.Word(reader.Add(device,20));
    ctx.r11.u64|=ctx.r12.u64;
    reader.StoreWord(reader.Add(device,16),uint32_t(ctx.r11.u64>>32));
    reader.StoreWord(reader.Add(device,20),uint32_t(ctx.r11.u64));
  } else __imp__sub_82149A90(ctx,base);
  PublishNativeDeclarationBinding(device,declaration);
}
REX_EXTERN(__imp__sub_8214B0A0);
REX_HOOK_RAW(sub_8214B0A0) {
  const auto device=ctx.r3.u32;
  __imp__sub_8214B0A0(ctx,base);
  PublishNativeDeclarationBinding(device,ctx.r4.u32);
}

// DrawIndexedVertices(device, primitive, baseVertex, firstIndex, indexCount).
// The model producer 821B2C28 uses triangle list (4). Inspect actual bound
// buffers before the original helper emits packets. Submit only inside the
// verified full-frame scene scope, never into an unrelated post-process target.
REX_EXTERN(__imp__sub_821FE358);
REX_EXTERN(__imp__edf_native_indexed_cpu_tail);
REX_HOOK_RAW(sub_821FE358) {
  bool native_submitted=false;
  bool native_geometry_rejected=false;
  // The stride and index width belong to the draw, not the declaration, and the
  // coverage catalog needs both to replay a layout at the size it was actually
  // drawn with. Resolved inside the draw, reported after it.
  uint32_t drawn_stride=0,drawn_index_width=0;
  edf::native::HookTiming native_timing(edf::native::HookPhase::IndexedNative);
  if (EDF_NATIVE_FLAG(shader_bridge)) {
    auto& state=edf::native::State();
    edf::native::HookTiming submission_wait(edf::native::HookPhase::IndexedSubmissionWait);
    std::lock_guard submission(state.submissions);
    submission_wait.Finish();
    edf::native::HookTiming context_wait(edf::native::HookPhase::IndexedContextWait);
    std::lock_guard lock(state.mutex);
    context_wait.Finish();
    ++state.indexed_draws;
    if(edf::native::BufferWrites().Pending()) {
      const auto writes=edf::native::BufferWrites().Drain();
      edf::native::AuditGeneratedWrites(state,writes);
      size_t affected=0;
      state.model_buffers.ApplyWrites(writes,[&](uint32_t owner) { state.meshes.Invalidate(owner); ++affected; });
      if(writes.all) ++state.buffer_write_all_batches;
      const bool report_all=writes.all && (state.buffer_write_all_batches & (state.buffer_write_all_batches-1))==0;
      if(++state.buffer_write_batches<=8 || report_all || writes.pages)
        REXLOG_INFO("Native buffer write batch: ranges={}, all={}, affected={}, all_batches={}, pages={} (deferred notifications)",
          writes.count,writes.all,affected,state.buffer_write_all_batches,writes.pages?writes.pages->count():0);
    }
    const bool scene_draw=state.active_scene && !state.active_target && state.scenes.contains(state.active_scene);
    if (!scene_draw) {
      if (++state.indexed_outside_scene<=10)
        REXLOG_INFO("Native indexed outside scene: draw={}, scene={:#x}, target={:#x}, caller={:#x}, primitive={}, count={}",
          state.indexed_draws,state.active_scene,state.active_target,uint32_t(ctx.lr),ctx.r4.u32,ctx.r7.u32);
    }
    if (state.indexed_draws<=20 || scene_draw) {
      try {
        edf::native::HookTiming setup_timing(edf::native::HookPhase::IndexedSetup);
        const edf::native::GuestReader backing(base);
        const edf::native::GuestReadWindow reader(backing,backing.Add(ctx.r3.u32,1024),12416-1024);
        // Material activation isn't necessarily the last shader change. Consult
        // native setter-owned identity before reusing its prepared parameters.
        const auto guest_shaders=state.shader_bindings.Pair(ctx.r3.u32);
        const auto guest_vertex=guest_shaders.vertex;
        const auto guest_pixel=guest_shaders.pixel;
        if (guest_vertex!=state.active_vertex || guest_pixel!=state.linked_pixel)
          throw std::runtime_error("indexed shader changed outside material activation: guestVS="+
            std::to_string(guest_vertex)+" nativeVS="+std::to_string(state.active_vertex)+
            " guestPS="+std::to_string(guest_pixel)+" nativePS="+std::to_string(state.linked_pixel));
        const auto stream=state.streams.at({ctx.r3.u32,0});
        const auto decl=state.declaration_bindings.at(ctx.r3.u32);
        const auto native_declaration=state.declarations.Get(decl);
        const auto elements=uint32_t(native_declaration->bytes().size()/12);
        const auto ib=state.index_bindings.at(ctx.r3.u32);
        const auto* native_vb=state.model_buffers.Find(stream.resource,edf::native::NativeModelBuffers::Kind::Vertex);
        const auto* native_ib=state.model_buffers.Find(ib,edf::native::NativeModelBuffers::Kind::Index);
        const unsigned ownership=(native_vb?1u:0u)|(native_ib?2u:0u);
        const auto ownership_count=++state.indexed_ownership[ownership];
        const auto ownership_attempt=++state.indexed_ownership_attempts;
        if(native_vb && native_ib && native_vb->physical && native_ib->physical) ++state.indexed_physical_pairs;
        if(ownership!=3 && (ownership_count<=8 || (ownership_count&(ownership_count-1))==0))
          REXLOG_INFO("Native indexed guest-header fallback: registered_vb={}, registered_ib={}, VB={:#x}, IB={:#x}, caller={:#x}, VS={:#x}, category_count={} (before header read; missing creator/lifetime ownership)",
            native_vb!=nullptr,native_ib!=nullptr,stream.resource,ib,uint32_t(ctx.lr),state.active_vertex,ownership_count);
        if(ownership_attempt<=8 || (ownership_attempt&(ownership_attempt-1))==0)
          REXLOG_INFO("Native indexed ownership coverage: attempts={}, both={}, vb_only={}, ib_only={}, neither={}, physical_pairs={} (resolved indexed attempts, not submitted draws; registration is not writer completeness)",
            ownership_attempt,state.indexed_ownership[3],state.indexed_ownership[1],state.indexed_ownership[2],
            state.indexed_ownership[0],state.indexed_physical_pairs);
        uint32_t vertex_address,vertex_bytes,index_address,index_bytes,index_width;
        if(native_vb) { vertex_address=native_vb->address; vertex_bytes=native_vb->bytes; }
        else {
          const auto header=edf::native::ReadGuestWords<2>(reader,reader.Add(stream.resource,24));
          vertex_address=header[0]&~3u; vertex_bytes=header[1]&0x03fffffcu;
        }
        if(native_ib) { index_address=native_ib->address; index_bytes=native_ib->bytes; index_width=native_ib->stride; }
        else {
          const auto header=edf::native::ReadGuestWords<8>(reader,ib);
          index_address=header[6]; index_bytes=header[7]; index_width=(header[0]&0x80000000u)?4:2;
        }
        drawn_stride=stream.stride; drawn_index_width=index_width;
        if (state.indexed_draws<=20) REXLOG_INFO("Native indexed input: draw={}, primitive={}, base={}, first={}, count={}, stride={}, vertex_bytes={}, index_bytes={}, index_width={}, elements={}, target={:#x}, native_vb={}, native_ib={}",
          state.indexed_draws,ctx.r4.u32,ctx.r5.s32,ctx.r6.u32,ctx.r7.u32,stream.stride,
          vertex_bytes,index_bytes,index_width,elements,state.active_target,native_vb!=nullptr,native_ib!=nullptr);
        if (ctx.r4.u32!=4 || !elements || elements>64 || stream.offset>=vertex_bytes ||
            vertex_bytes>128*1024*1024 || index_bytes>128*1024*1024)
          throw std::runtime_error("unsupported indexed geometry bounds/topology");
        auto& shader=state.shaders.at(state.active_vertex);
        const auto viewport=edf::native::ReadNativeDrawViewport(reader,ctx.r3.u32);
        auto& bindings=edf::native::VertexBindingsForDraw(shader,viewport.reverse_depth);
        setup_timing.Finish();
        edf::native::HookTiming mesh_timing(edf::native::HookPhase::IndexedMesh);
        const auto declaration_bytes=native_declaration->bytes();
        std::span<const uint8_t> vertices_bytes,indices_bytes;
        const auto read_live_ranges=[&] {
          edf::native::HookTiming ranges_timing(edf::native::HookPhase::MeshRanges);
          vertices_bytes={reader.Bytes(reader.Add(vertex_address,stream.offset),vertex_bytes-stream.offset),vertex_bytes-stream.offset};
          indices_bytes={reader.Bytes(index_address,index_bytes),index_bytes};
        };
        auto mesh_watch_audit=state.mesh_watch_audit.lock();
        edf::native::HookTiming acquire_timing(edf::native::HookPhase::MeshAcquire);
        edf::native::HookTiming observe_timing(edf::native::HookPhase::MeshObserve);
        std::optional<edf::native::NativeBufferWrites::ObservedVersion> vertex_version,index_version;
        using GeometrySnapshots=std::array<edf::native::NativeBufferWrites::ObservedSnapshot,2>;
        std::optional<GeometrySnapshots> observed_geometry;
        auto vertex_contents=native_vb?native_vb->vertex_contents:nullptr;
        const bool prepare_queued=REXCVAR_GET(edf_native_prepared_geometry) &&
          EDF_NATIVE_FLAG(seam_draws) && uint32_t(ctx.lr)==0x821D97E8;
        edf::native::NativeIndexedMesh* preacquired_mesh=nullptr;
        const edf::native::NativeIndexedMesh::PreparedDraw* prepared_draw=nullptr;
        bool prepared_geometry=false;
        static uint64_t prepared_geometry_hits=0;
        if(native_vb && native_ib && native_vb->physical && native_ib->physical) {
          using Source=edf::native::NativeBufferWrites::SnapshotSource;
          edf::native::NativeBufferWrites::SnapshotFailure failure;
          const bool revision_audit=REXCVAR_GET(edf_native_retirement_audit);
          // Revision/writer-watch audits are the comparison's own oracles, so
          // they always read the source. Otherwise the comparison is sampled:
          // see NativeBufferWrites::SnapshotPolicy for the fail-closed rules.
          edf::native::NativeBufferWrites::SnapshotPolicy policy{};
          policy.audit_revisions=revision_audit;
          if(!revision_audit && !mesh_watch_audit) {
            policy.verify_interval=uint64_t(std::clamp(REXCVAR_GET(edf_native_geometry_verify_interval),0,1<<20));
            policy.verify_initial=uint64_t(std::clamp(REXCVAR_GET(edf_native_geometry_verify_initial),0,1<<16));
          }
          const auto index_contents=native_ib->index_contents?native_ib->index_contents:
            (native_ib->index_storage?native_ib->index_storage->SourceSnapshot():nullptr);
          using Identity=edf::native::NativeBufferWrites::SnapshotIdentity;
          if(prepare_queued) {
            using View=edf::native::NativeBufferWrites::SnapshotIdentityView;
            const auto versions=edf::native::BufferWrites().TryValidateObservedSet(std::array<View,2>{{
              {stream.resource,*native_vb->physical,vertex_bytes,&vertex_contents},
              {ib,*native_ib->physical,index_bytes,&index_contents}}},policy);
            if(versions) {
              vertices_bytes=std::span<const uint8_t>(*vertex_contents).subspan(stream.offset);
              indices_bytes=*index_contents;
              preacquired_mesh=state.meshes.TryAcquireOwned(edf::native::EnsureSceneBackendLocked(state),bindings.shader(),
                {stream.resource,ib,decl,state.active_vertex,uint32_t(viewport.reverse_depth)},
                *native_declaration,stream.stride,vertices_bytes,indices_bytes,index_width);
              if(preacquired_mesh) prepared_draw=preacquired_mesh->FindPreparedDraw(ctx.r6.u32,ctx.r7.u32,ctx.r5.s32);
              // Reuse attachment only if the registry still owns this exact
              // mesh's storage. Other shader/layout variants use normal commit.
              prepared_geometry=prepared_draw && native_vb->vertex_storage==preacquired_mesh->VertexStorage() &&
                native_ib->index_storage==preacquired_mesh->IndexStorage();
              if(prepared_geometry) ++prepared_geometry_hits;
              else observed_geometry=GeometrySnapshots{{
                {(*versions)[0],vertex_contents,false,true,false},
                {(*versions)[1],index_contents,false,true,false}}};
            }
          }
          if(!prepared_geometry) {
            if(!prepare_queued) observed_geometry=edf::native::BufferWrites().TryReuseObservedSet(std::array<Identity,2>{{
              {stream.resource,*native_vb->physical,vertex_bytes,vertex_contents},
              {ib,*native_ib->physical,index_bytes,index_contents}}},policy);
            std::optional<edf::native::GuestMeshWatchAudit::Observation> vertex_watch,index_watch;
            if(!observed_geometry) {
              // Only a real source comparison needs mapped guest ranges. The
              // registry rechecks writers/revisions after this validation; no
              // SDK heap lock is acquired while holding its writer mutex.
              read_live_ranges();
              const std::span<const uint8_t> full_vertices=stream.offset==0?vertices_bytes:
                std::span<const uint8_t>{reader.Bytes(vertex_address,vertex_bytes),vertex_bytes};
              if(mesh_watch_audit) {
                vertex_watch=mesh_watch_audit->Begin(reader.Add(vertex_address,stream.offset),vertices_bytes.size());
                index_watch=mesh_watch_audit->Begin(index_address,indices_bytes.size());
              }
              observed_geometry=edf::native::BufferWrites().CopyObservedSet(std::array<Source,2>{{
                {stream.resource,*native_vb->physical,full_vertices,vertex_contents},
                {ib,*native_ib->physical,indices_bytes,index_contents}}},&failure,policy);
            }
            if(mesh_watch_audit && observed_geometry) {
              if(vertex_watch) mesh_watch_audit->Finish(*vertex_watch,
                std::span<const uint8_t>(*(*observed_geometry)[0].contents).subspan(stream.offset));
              if(index_watch) mesh_watch_audit->Finish(*index_watch,*(*observed_geometry)[1].contents);
            }
            static std::array<uint64_t,2> revision_checked{},revision_without_baseline{},revision_missed{};
            if(revision_audit && observed_geometry) for(size_t slot=0;slot<2;++slot) {
              const auto& observation=(*observed_geometry)[slot];
              if(observation.revision_audited) ++revision_checked[slot];
              else ++revision_without_baseline[slot];
              if(observation.unreported_change) ++revision_missed[slot];
            }
            if(observed_geometry) for(size_t slot=0;slot<2;++slot) if((*observed_geometry)[slot].unreported_change)
              REXLOG_WARN("Native geometry revision audit: unreported change, owner={:#x}, index={}, lifetime={}, revision={} (live comparison repaired snapshot; sampled verification is now permanently disabled for this run)",
                slot?ib:stream.resource,slot==1,(*observed_geometry)[slot].version.lifetime,(*observed_geometry)[slot].version.revision);
            static uint64_t guarded=0,unavailable=0;
            static std::array<uint64_t,5> rejections{};
            if(observed_geometry) {
              ++guarded;
              vertex_contents=(*observed_geometry)[0].contents;
              vertices_bytes=std::span<const uint8_t>(*vertex_contents).subspan(stream.offset);
              indices_bytes=*(*observed_geometry)[1].contents;
              vertex_version=(*observed_geometry)[0].version; index_version=(*observed_geometry)[1].version;
            } else {
              ++unavailable; ++rejections.at(size_t(failure.reason));
              if(unavailable<=8 || (unavailable & (unavailable-1))==0)
                REXLOG_INFO("Native geometry snapshot rejection: unavailable={}, unknown={}, active={}, missing={}, extent={}, owner={:#x}, active_writers={} (classification at acquisition; no writer completeness claim)",
                  unavailable,rejections[1],rejections[2],rejections[3],rejections[4],failure.owner,failure.active_writers);
              if(unavailable<=8 || (unavailable & (unavailable-1))==0)
                REXLOG_INFO("Native geometry active write ranges: overlapping={}, unknown={}, same_thread={}, allocation_release={} (same-thread scopes cannot be waited out by this draw)",
                  failure.overlapping_writers,failure.unknown_writers,failure.same_thread_writers,failure.releasing_writers);
              if(unavailable<=8 || (unavailable & (unavailable-1))==0)
                REXLOG_INFO("Native geometry unknown writer providers: unspecified={}, bulk={}, file_read={}, word_fill={}, inline_indices={}, allocation_release={} (active scope counts at sampled rejection)",
                  failure.unknown_by_kind[0],failure.unknown_by_kind[1],failure.unknown_by_kind[2],failure.unknown_by_kind[3],failure.unknown_by_kind[4],failure.unknown_by_kind[5]);
              // Failed guarded acquisition never authorizes an unguarded read.
              // Neither active payload writers nor lost tracking/ownership are
              // repaired by comparing live bytes. Do not wait under these locks;
              // a producer may need the registry lock before it can complete.
              // The outer handler records an unsubmitted draw, not a safe retry.
              native_geometry_rejected=true;
              throw std::runtime_error("native geometry guarded acquisition failed; live fallback rejected");
            }
            const auto attempts=guarded+unavailable;
            if(attempts<=8 || (attempts & (attempts-1))==0) {
              REXLOG_INFO("Native geometry snapshot acquisition: guarded={}, unavailable={} (failed guarded acquisitions reject the draw)",guarded,unavailable);
              const auto trust=edf::native::BufferWrites().Trust();
              REXLOG_INFO("Native geometry comparison schedule: compared={}, trusted={}, unreported_changes={}, revoked={}, interval={}, initial={} (trusted observations reused a revision-proven snapshot without reading guest bytes)",
                trust.verified,trust.trusted,trust.unreported_changes,trust.revoked,
                policy.verify_interval,policy.verify_initial);
            }
            if(revision_audit && (attempts<=8 || (attempts & (attempts-1))==0))
              REXLOG_INFO("Native geometry revision audit coverage: checked_vb={}, checked_ib={}, without_baseline_vb={}, without_baseline_ib={}, missed_vb={}, missed_ib={} (successful snapshots only; checked requires same candidate and unchanged revision; not writer completeness)",
                revision_checked[0],revision_checked[1],revision_without_baseline[0],revision_without_baseline[1],revision_missed[0],revision_missed[1]);
          }
        } else read_live_ranges();
        if(mesh_watch_audit && state.indexed_draws%10000==0) {
          const auto& c=mesh_watch_audit->counters();
          REXLOG_INFO("Native mesh watch audit: checked={}, unsupported={}, stable={}, invalidated={}, missed={}, resets={}, excluded={}, foreign={} (shadow only; checked counts begun observations, rejected snapshots not compared)",
            c.checked,c.unsupported,c.stable,c.invalidated,c.missed,c.resets,c.excluded,c.foreign);
        }
        auto before_snapshot=[&] {
          if(observed_geometry) return; // Never label an earlier copy with a later writer epoch.
          if(native_vb && native_vb->physical) vertex_version=edf::native::BufferWrites().Version(stream.resource);
          if(native_ib && native_ib->physical) index_version=edf::native::BufferWrites().Version(ib);
        };
        observe_timing.Finish();
        edf::native::HookTiming lookup_timing(edf::native::HookPhase::MeshLookup);
        state.meshes.SetTimingsEnabled(REXCVAR_GET(edf_native_hook_timings));
        auto& mesh_backend=edf::native::EnsureSceneBackendLocked(state);
        const edf::native::NativeMeshCache::Key mesh_key{
          stream.resource,ib,decl,state.active_vertex,uint32_t(viewport.reverse_depth)};
        auto* owned_mesh=preacquired_mesh?preacquired_mesh:REXCVAR_GET(edf_native_owned_mesh_hit)?state.meshes.TryAcquireOwned(
          mesh_backend,bindings.shader(),mesh_key,*native_declaration,stream.stride,
          vertices_bytes,indices_bytes,index_width):nullptr;
        auto& mesh=owned_mesh?*owned_mesh:state.meshes.Acquire(mesh_backend,bindings.shader(),mesh_key,
          declaration_bytes,stream.stride,vertices_bytes,indices_bytes,index_width,native_declaration,{},
          native_ib?native_ib->index_storage:nullptr,native_vb?native_vb->vertex_storage:nullptr,
          {&before_snapshot,[](void* context) {
            (*static_cast<decltype(before_snapshot)*>(context))();
          }},vertex_contents,stream.offset,observed_geometry?(*observed_geometry)[1].contents:nullptr);
        lookup_timing.Finish();
        edf::native::HookTiming commit_timing(edf::native::HookPhase::MeshCommit);
        // The registry lock protects lifetimes; the queue handshake additionally
        // rejects completed notified writes during construction. Unnotified raw
        // writes still require the existing live source comparisons.
        if(state.indexed_draws<=5) {
          const auto& source=*mesh.IndexStorage()->SourceSnapshot();
          if(source.size()>=12) {
            const auto word=[&](size_t at) {
              uint32_t value=0;
              for(size_t lane=0;lane<4;++lane) value=(value<<8)|source[at+lane];
              return value;
            };
            REXLOG_INFO("Native indexed resources: VB={:#x} data={:#x} offset={}, IB={:#x} data={:#x}, index_width={}, first_words={:#x}/{:#x}/{:#x} (native draw snapshot)",
              stream.resource,vertex_address,stream.offset,ib,index_address,index_width,word(0),word(4),word(8));
          }
        }
        if(prepared_geometry) {
          // Guarded identities and registry storage matched the prepared mesh;
          // there is no new snapshot or attachment to publish.
        } else if(observed_geometry) {
          // Both source observations came from one guarded acquisition. Keep
          // attachment indivisible too, including a full VB with stream offset.
          if(native_vb->vertex_storage!=mesh.VertexStorage() ||
             native_vb->vertex_contents!=vertex_contents || native_ib->index_storage!=mesh.IndexStorage() ||
             native_ib->index_contents!=(*observed_geometry)[1].contents)
            state.model_buffers.CommitObservedGeometry(
              {stream.resource,native_vb->generation,*vertex_version},
              {ib,native_ib->generation,*index_version},mesh.VertexStorage(),vertex_contents,mesh.IndexStorage(),
              (*observed_geometry)[1].contents);
        } else {
          if(native_vb && native_vb->vertex_storage!=mesh.VertexStorage()) {
            if(vertex_version) state.model_buffers.CommitObservedVertex(stream.resource,native_vb->generation,*vertex_version,mesh.VertexStorage());
            else if(!native_vb->physical) state.model_buffers.RetainVertexStorage(stream.resource,native_vb->generation,mesh.VertexStorage());
          }
          if(native_vb && !stream.offset && !mesh.VertexStorage()->SourceOffset() &&
             mesh.VertexStorage()->SourceBytes()==mesh.VertexStorage()->SourceSnapshot()->size() &&
             native_vb->vertex_contents!=mesh.VertexStorage()->SourceSnapshot()) {
            state.model_buffers.RetainVertexContents(stream.resource,native_vb->generation,
              mesh.VertexStorage()->SourceSnapshot(),vertex_version);
          }
          if(native_ib && native_ib->index_storage!=mesh.IndexStorage()) {
            if(index_version) state.model_buffers.CommitObservedIndex(ib,native_ib->generation,*index_version,mesh.IndexStorage());
            else if(!native_ib->physical) state.model_buffers.RetainIndexStorage(ib,native_ib->generation,mesh.IndexStorage());
          }
        }
        commit_timing.Finish();
        acquire_timing.Finish();
        edf::native::HookTiming draw_range_timing(edf::native::HookPhase::MeshDrawRange);
        if(prepare_queued) prepared_draw=&mesh.PrepareDraw(ctx.r6.u32,ctx.r7.u32,ctx.r5.s32);
        else mesh.ValidateDraw(ctx.r6.u32,ctx.r7.u32,ctx.r5.s32);
        draw_range_timing.Finish();
        mesh_timing.Finish();
        ++state.indexed_uploads;
        if (scene_draw) {
          edf::native::HookTiming binding_timing(edf::native::HookPhase::IndexedBindings);
          const auto key=edf::native::ReadAuditedRenderStateWords(reader,ctx.r3.u32);
          auto found=state.render_states.find(key);
          if (found==state.render_states.end()) found=state.render_states.emplace(key,
            edf::native::CreateNativeRenderState(state.device.Get(),key)).first;
          // 77.4% of this game's indexed draws repeat the one before them in
          // mesh, material and state, differing only in the constants an
          // activation patches between them. The targets and render state do
          // not change across such a run, so binding them again per draw is
          // pure repetition - which is the batch, without needing the draws
          // themselves to merge.
          //
          // Skipped only when nothing else has bound since we did: the
          // generation catches another path replacing the state underneath us,
          // which a comparison against our own cache never would. A state that
          // needs a constant blend factor is never skipped, because the factor
          // can change while the state words do not.
          const bool same_binding=state.indexed_bind_valid &&
            state.bind_generation==state.indexed_bind_generation &&
            state.indexed_bind_key==key &&
            state.indexed_bind_target==state.active_target &&
            state.indexed_bind_scene==state.active_scene &&
            state.indexed_bind_output==state.active_output &&
            !found->second.requires_blend_factor;
          // The same guard extended to the material. When the previous draw
          // left this very shader pair bound with these textures and samplers,
          // the only thing this draw changes is its constants, which an
          // activation has just patched. Re-sending the shader, the texture
          // runs and the sampler runs is the same calls with the same
          // arguments, once per draw, for 77.4% of them.
          //
          // The reversed-depth variant is a different shader object under the
          // same handle, so it has to be part of the comparison: a draw that
          // flips depth direction would otherwise reuse the wrong one.
          const bool same_material=same_binding &&
            REXCVAR_GET(edf_native_reuse_material) &&
            state.indexed_bind_vertex==state.active_vertex &&
            state.indexed_bind_pixel==state.linked_pixel &&
            state.indexed_bind_reversed==viewport.reverse_depth;
          // Recorded draws set all of this per draw through the pipeline and
          // the recorder, so the context bindings below are not merely
          // redundant, they describe a draw that is not going to happen.
          const bool seam_draws=EDF_NATIVE_FLAG(seam_draws);
          if(seam_draws) { /* bound per draw below */ }
          else if(same_binding) ++state.indexed_binds_skipped;
          else {
            edf::native::BindActiveTarget(state);
            edf::native::BindGuestRenderState(found->second,*state.context.Get(),reader,ctx.r3.u32,
                                             &state.bind_generation);
            state.indexed_bind_valid=true;
            state.indexed_bind_key=key;
            state.indexed_bind_target=state.active_target;
            state.indexed_bind_scene=state.active_scene;
            state.indexed_bind_output=state.active_output;
            state.indexed_bind_generation=state.bind_generation;
            ++state.indexed_binds_bound;
          }
          if(!seam_draws) {
            viewport.Bind(*state.context.Get());
            if(same_material) {
              bindings.BindConstants(*state.context.Get());
              state.shaders.at(state.linked_pixel).bindings->BindConstants(*state.context.Get());
              ++state.indexed_materials_reused;
            } else {
              bindings.Bind(*state.context.Get());
              state.shaders.at(state.linked_pixel).bindings->Bind(*state.context.Get());
              state.indexed_bind_vertex=state.active_vertex;
              state.indexed_bind_pixel=state.linked_pixel;
              state.indexed_bind_reversed=viewport.reverse_depth;
            }
          }
          binding_timing.Finish();
          if(REXCVAR_GET(edf_native_capture_indexed_state) &&
             !REXCVAR_GET(edf_native_scene_capture).empty()) {
            // This scene is a candidate for the next indexed output. If output
            // fails, the index may be reused; keep the cap across those retries.
            const auto frame=state.indexed_output_frames+1;
            if(state.indexed_trace_frame!=frame) {
              state.indexed_trace_frame=frame;
              state.indexed_trace_draws=0;
            }
            if(state.indexed_trace_draws<256 && edf::native::ShouldCaptureNativeOutput(
                frame,state.output_captures,REXCVAR_GET(edf_native_output_capture_limit),
                REXCVAR_GET(edf_native_output_capture_interval),REXCVAR_GET(edf_native_output_capture_start_frame))) {
              ++state.indexed_trace_draws;
              REXLOG_INFO("Native indexed capture state: next_output_candidate={}, sample={}/256, scene={:#x}, draw={}, VS={:#x}, PS={:#x}, VB={:#x}, IB={:#x}, declaration={:#x}, first={}, count={}, base={}, blend={:#x}, depth={:#x}, raster={:#x}, alpha={:#x}, mask={}, scissor={}, depth_range={}..{}, reversed={}; state before draw, not proof of output",
                frame,state.indexed_trace_draws,state.active_scene,state.indexed_draws,
                state.active_vertex,state.linked_pixel,stream.resource,ib,decl,
                ctx.r6.u32,ctx.r7.u32,ctx.r5.s32,key[0],key[1],key[2],key[3],key[4],key[5],
                viewport.viewport.MinDepth,viewport.viewport.MaxDepth,viewport.reverse_depth);
              REXLOG_INFO("Native indexed capture viewport: draw={}, xy={},{} extent={}x{}, scene_extent={}x{}",
                state.indexed_draws,viewport.viewport.TopLeftX,viewport.viewport.TopLeftY,
                viewport.viewport.Width,viewport.viewport.Height,
                state.scenes.at(state.active_scene).color.sampled.width,
                state.scenes.at(state.active_scene).color.sampled.height);
            }
          }
          const std::array<uint32_t,4> probe_key{state.active_vertex,state.linked_pixel,key[2],key[1]};
          // The clip probe replays the draw with a stream-output geometry
          // shader and requires this VS already bound on the context. A
          // recorded draw binds nothing there, so the replay would refuse -
          // correctly, but once per draw and in the log. Skip it instead.
          if (!seam_draws && !REXCVAR_GET(edf_native_scene_capture).empty() && state.clip_probes.size()<24 &&
              state.clip_probes.insert(probe_key).second) {
            try {
              const auto positions=mesh.CaptureClipPositions(*state.context.Get(),bindings.shader(),
                ctx.r6.u32,(std::min)(ctx.r7.u32,9u),ctx.r5.s32);
              REXLOG_INFO("Native clip probe: VS={:#x} {}, PS={:#x} {}, raster={:#x}, depth={:#x}, first={}, base={}, vertices={}",
                state.active_vertex,bindings.shader().entry.name,state.linked_pixel,
                state.shaders.at(state.linked_pixel).bindings->shader().entry.name,key[2],key[1],ctx.r6.u32,ctx.r5.s32,positions.size());
              for(const auto* name:{"g_mWorld","g_mView","g_mProjection","g_mViewProjection"}) {
                if(const auto matrix=bindings.ReadFloat4x4(name)) for(size_t row=0;row<4;++row)
                  REXLOG_INFO("Native clip matrix: name={}, row={}, values={},{},{},{}",name,row,
                    (*matrix)[row*4],(*matrix)[row*4+1],(*matrix)[row*4+2],(*matrix)[row*4+3]);
              }
              // Log the original POSITION0 float3 for the same indexed samples.
              // This is diagnostic-only; other declaration formats still render.
              for(uint32_t element=0;element<elements;++element) {
                const auto declaration_word=[&](size_t at) {
                  uint32_t value=0;
                  for(size_t lane=0;lane<4;++lane) value=(value<<8)|declaration_bytes[at+lane];
                  return value;
                };
                const auto at=size_t(element)*12;
                if(declaration_word(at+8)!=0 || declaration_word(at+4)!=0x2a23b9) continue;
                const auto position_offset=declaration_word(at);
                if(position_offset>=stream.stride || stream.stride-position_offset<12) continue;
                const auto inputs=mesh.CaptureSourceFloat3(ctx.r6.u32,uint32_t(positions.size()),ctx.r5.s32,position_offset);
                for(size_t i=0;i<inputs.size();++i) {
                  REXLOG_INFO("Native clip input: index={}, xyz={},{},{} (native draw snapshot)",i,
                    inputs[i][0],inputs[i][1],inputs[i][2]);
                }
                break;
              }
              for(size_t i=0;i<positions.size();++i) {
                const auto& p=positions[i];
                REXLOG_INFO("Native clip vertex: index={}, xyzw={},{},{},{}",i,p[0],p[1],p[2],p[3]);
              }
            } catch(const std::exception& error) { REXLOG_ERROR("Native clip probe: {}",error.what()); }
          }
          Microsoft::WRL::ComPtr<ID3D11Query> visibility;
          // A context query cannot count a draw that was recorded rather than
          // issued on that context: it would return zero for every draw and
          // read as "nothing was visible". The visibility diagnostic is a
          // capture-time tool, so it is simply not taken on the recorded path
          // rather than reporting a number that means nothing.
          if (!seam_draws && !REXCVAR_GET(edf_native_scene_capture).empty() &&
              state.scene_captures<3 && state.visibility.size()<2048) {
            const D3D11_QUERY_DESC query_desc{D3D11_QUERY_OCCLUSION,0};
            if (SUCCEEDED(state.device->CreateQuery(&query_desc,&visibility))) state.context->Begin(visibility.Get());
          }
          // Batching precondition. A run of draws that share mesh, declaration,
          // shader pair, render state and index range, and differ only in the
          // per-instance constants patched by 821D9600, is exactly what one
          // DrawIndexedInstanced replaces. Measure the run lengths before
          // building that: the mean run length is the draw-call reduction, and
          // a mean near 1 would mean there is nothing to collapse.
          // Every hundred thousand rather than every million: a run slow enough
          // to need this explanation never reaches a million draws, which made
          // the one counter that could explain it unreachable.
          if(state.recorded_draws && state.recorded_draws%100000==0)
            REXLOG_INFO("Native recorded binding reuse: draws={}, pipeline_skips={} ({:.1f}%), material_skips={} ({:.1f}%), constant_buffer_skips={} ({:.2f} per draw) (a skip is something the recorder already held, so the draw did not re-send it)",
              state.recorded_draws,state.recorded_pipeline_skips,
              100.0*double(state.recorded_pipeline_skips)/double(state.recorded_draws),
              state.recorded_material_skips,
              100.0*double(state.recorded_material_skips)/double(state.recorded_draws),
              state.recorded_constant_skips,
              double(state.recorded_constant_skips)/double(state.recorded_draws));
          if(REXCVAR_GET(edf_native_batch_audit) && state.indexed_draws%1000000==0)
            REXLOG_INFO("Native indexed binding reuse: bound={}, skipped={} ({:.1f}% of draws bound no target or render state, because the draw before them had already bound the same)",
              state.indexed_binds_bound,state.indexed_binds_skipped,
              (state.indexed_binds_bound+state.indexed_binds_skipped)
                ?100.0*double(state.indexed_binds_skipped)/double(state.indexed_binds_bound+state.indexed_binds_skipped):0.0);
          if(REXCVAR_GET(edf_native_batch_audit) && state.indexed_draws%1000000==0)
            REXLOG_INFO("Native indexed material reuse: draws={}, constants_only={} ({:.1f}% re-sent only their constants, keeping the shader pair, textures and samplers the previous draw bound)",
              state.indexed_draws,state.indexed_materials_reused,
              state.indexed_draws?100.0*double(state.indexed_materials_reused)/double(state.indexed_draws):0.0);
          if(REXCVAR_GET(edf_native_batch_audit)) {
            const std::array<uint32_t,12> batch_key{stream.resource,ib,decl,
              state.active_vertex,state.linked_pixel,ctx.r6.u32,ctx.r7.u32,uint32_t(ctx.r5.s32),
              key[0],key[1],key[2],key[3]};
            ++state.batch_draws;
            if(batch_key==state.last_batch_key && state.instance_shape!=state.last_instance_shape)
              ++state.batch_shape_breaks;
            if(batch_key==state.last_batch_key) ++state.batch_run;
            else {
              if(state.batch_run) {
                state.batch_longest=(std::max)(state.batch_longest,state.batch_run);
                state.batch_run_total+=state.batch_run;
                ++state.batch_runs;
                if(state.batch_run>=2) state.batch_collapsible+=state.batch_run-1;
              }
              state.batch_run=1; state.last_batch_key=batch_key;
            }
            state.last_instance_shape=state.instance_shape;
            if(state.batch_draws%1000000==0) {
              REXLOG_INFO("Native batch audit: draws={}, runs={}, mean_run={:.2f}, longest_run={}, collapsible_draws={} ({:.1f}% of draws could be folded into a preceding instanced draw)",
                state.batch_draws,state.batch_runs,
                state.batch_runs?double(state.batch_run_total)/double(state.batch_runs):0.0,
                state.batch_longest,state.batch_collapsible,
                100.0*double(state.batch_collapsible)/double(state.batch_draws));
              REXLOG_INFO("Native batch shape: collapsible={}, register_shape_breaks={} ({:.2f}% of collapsible draws patch a different register range than the draw before, so cannot share one instanced variant)",
                state.batch_collapsible,state.batch_shape_breaks,
                state.batch_collapsible?100.0*double(state.batch_shape_breaks)/double(state.batch_collapsible):0.0);
            }
          }
          try {
            if(seam_draws) {
              edf::native::HookTiming record_timing(edf::native::HookPhase::IndexedRecord);
              const auto setup=[&]() -> edf::native::NativeBackendRecorder& { return edf::native::RecordDrawSetup(state,reader,ctx.r3.u32,{
                bindings,*state.shaders.at(state.linked_pixel).bindings,viewport,key,
                mesh.input_layout().elements(),mesh.input_layout().fingerprint(),
                (uint64_t(state.active_vertex)<<1)|uint64_t(viewport.reverse_depth?1:0),
                state.linked_pixel,edf::native::NativeBackendTopology::TriangleList,
                REXCVAR_GET(edf_native_world_instancing) && uint32_t(ctx.lr)==0x821D97E8}); };
              auto* group=edf::native::native_queued_scene_group;
              const bool native_material_setup=group && uint32_t(ctx.lr)==0x821D97E8 &&
                EDF_NATIVE_FLAG(scene_material_owned) && !REXCVAR_GET(edf_native_scene_material_audit);
              // Native scene rendering binds its own pipeline, resources and
              // constants. Only audit/fallback draws need the live bindings.
              auto& recorder=native_material_setup?edf::native::SceneRecorderLocked(state):setup();
              record_timing.Finish();
              edf::native::HookTiming draw_timing(edf::native::HookPhase::IndexedDraw);
              bool native_scene_draw=false;
              if(group && uint32_t(ctx.lr)==0x821D97E8) {
                std::optional<edf::native::NativeSceneMaterialCapture> captured;
                uint64_t object=0;
                try {
                  const auto* source=state.scene_sources.Find(group->instance);
                  if(!source) throw std::runtime_error("queued instance has no audited scene lifetime");
                  const bool owned=EDF_NATIVE_FLAG(scene_material_owned);
                  const bool audit=REXCVAR_GET(edf_native_scene_material_audit) && !group->material_audited;
                  if(!owned || audit)
                    captured=edf::native::CaptureNativeSceneMaterial(state.scene_backend,*state.recorded.pipeline,
                      bindings,*state.shaders.at(state.linked_pixel).bindings,
                      state.recorded.blend_factor_needed?std::optional(state.recorded.blend_factor):std::nullopt,group->material);
                  const auto resolve=[&](const auto& constants) {
                      if(!group->published_material || !group->material_pass)
                        throw std::runtime_error("native material publication or pass missing");
                      const auto& program=*group->published_material->program;
                      if(program.inputs.vertex!=state.active_vertex || program.inputs.pixel!=state.linked_pixel)
                        throw std::runtime_error("published shader pair differs from visible group");
                      const auto targets=edf::native::ActiveTargetsLocked(state);
                      if(!targets.count) throw std::runtime_error("native material pass has no color target");
                      edf::native::NativeBackendPipelineDesc desc;
                      desc.vertex_id=(uint64_t(state.active_vertex)<<1)|uint64_t(viewport.reverse_depth);
                      desc.pixel_id=state.linked_pixel;
                      desc.input_layout=mesh.input_layout().elements(); desc.input_layout_id=mesh.input_layout().fingerprint();
                      desc.render_targets=targets.count; desc.rtv_format=targets.rtv_format;
                      desc.dsv_format=targets.dsv_format; desc.sample_count=targets.samples;
                      return program.Resolve(desc,viewport.reverse_depth,constants,*group->material_pass,group->sampler_pass,
                        REXCVAR_GET(edf_native_anisotropic_filtering)).capture;
                  };
                  std::optional<edf::native::NativeSceneMaterialCapture> resolved;
                  if(owned) {
                    resolved=resolve(group->pass_constants);
                    const auto world=state.scene_sources.WorldRegisters(*source,source->world_data);
                    if(!world || !state.active_vertex_parameters)
                      throw std::runtime_error("owned native instance has no published world");
                    const auto& parameters=*state.active_vertex_parameters;
                    const auto parameter=std::find_if(parameters.begin(),parameters.end(),[](const auto& value) {
                      return value.name=="g_mWorld" && value.count==4;
                    });
                    if(parameter==parameters.end() || !edf::native::NativeStaticWorldOnly(reader,state.scene_sources,
                       group->instance,parameter->first,ctx.r3.u32))
                      throw std::runtime_error("owned native instance has additional overrides");
                    edf::native::ApplyNativeScenePublishedWorld(*resolved,*world);
                  }
                  if(audit) {
                    group->material_audited=true;
                    static uint64_t checked=0,mismatched=0,missing=0,animation_generation_differences=0;
                    static std::set<std::string> reasons;
                    if(!group->published_material || !group->material_pass) ++missing;
                    else try {
                      if(!resolved) resolved=resolve(group->pass_constants);
                      bool equivalent=resolved->material->Equivalent(*captured->material);
                      bool generation_difference=false;
                      if(!equivalent) {
                        // Audit a later producer generation separately. Keep the rendered
                        // material on its immutable pass snapshot; never copy live constants.
                        const auto latest=state.scene_adapter.AcquireWorldAnimations();
                        const auto producer=latest->find(edf::native::native_scene_animation_owner);
                        if(latest!=edf::native::native_scene_pass_animations && producer!=latest->end()) {
                          auto constants=group->pass_constants;
                          for(auto& constant:constants) producer->second.Apply(constant);
                          const auto current=resolve(constants);
                          if(current.material->Equivalent(*captured->material)) {
                            equivalent=true;
                            generation_difference=true;
                          }
                        }
                      }
                      if(!equivalent) {
                        std::string difference="pipeline/resources";
                        for(const auto& expected:resolved->material->constants()) {
                          const auto& actuals=captured->material->constants();
                          const auto actual=std::find_if(actuals.begin(),actuals.end(),[&](const auto& value) {
                            return value.stage==expected.stage && value.slot==expected.slot;
                          });
                          if(actual==actuals.end() || actual->bytes.size()!=expected.bytes.size()) {
                            difference="constant buffer shape"; break;
                          }
                          const auto mismatch=std::mismatch(expected.bytes.begin(),expected.bytes.end(),actual->bytes.begin());
                          if(mismatch.first==expected.bytes.end()) continue;
                          const auto offset=size_t(mismatch.first-expected.bytes.begin());
                          const auto& shader=expected.stage==edf::native::NativeBackendStage::Vertex?
                            (viewport.reverse_depth?group->published_material->program->reversed_vertex:group->published_material->program->vertex):
                            group->published_material->program->pixel;
                          D3D11_SHADER_DESC shader_desc{};
                          shader.reflection->GetDesc(&shader_desc);
                          difference="unnamed constant";
                          for(UINT index=0;index<shader_desc.ConstantBuffers;++index) {
                            auto* buffer=shader.reflection->GetConstantBufferByIndex(index);
                            D3D11_SHADER_BUFFER_DESC buffer_desc{}; buffer->GetDesc(&buffer_desc);
                            D3D11_SHADER_INPUT_BIND_DESC binding{};
                            shader.reflection->GetResourceBindingDescByName(buffer_desc.Name,&binding);
                            if(binding.BindPoint!=expected.slot) continue;
                            for(UINT variable=0;variable<buffer_desc.Variables;++variable) {
                              D3D11_SHADER_VARIABLE_DESC value{}; buffer->GetVariableByIndex(variable)->GetDesc(&value);
                              if(offset>=value.StartOffset && offset<value.StartOffset+value.Size) difference=value.Name;
                            }
                          }
                          static uint64_t details=0;
                          if(details++<16) {
                            uint32_t wanted=0,seen=0;
                            std::memcpy(&wanted,expected.bytes.data()+(offset&~size_t(3)),4);
                            std::memcpy(&seen,actual->bytes.data()+(offset&~size_t(3)),4);
                            REXLOG_INFO("Native scene constant difference: name={} stage={} slot={} byte={} native={:#x} visible={:#x}",
                              difference,uint32_t(expected.stage),expected.slot,offset,wanted,seen);
                            const auto latest=state.scene_adapter.AcquireWorldAnimations();
                            const auto producer=latest->find(edf::native::native_scene_animation_owner);
                            const auto selected=edf::native::native_scene_pass_animation;
                            if(producer!=latest->end() && selected)
                              REXLOG_INFO("Native animation timing: owner={:#x} selected_water={:#x} latest_water={:#x} selected_counter={} latest_counter={} same_generation={}",
                                edf::native::native_scene_animation_owner,selected->water_time,producer->second.water_time,
                                selected->signal_counter,producer->second.signal_counter,latest==edf::native::native_scene_pass_animations);
                            if(latest->size()<=4) for(const auto& [owner,value]:*latest)
                              REXLOG_INFO("Native animation producer: owner={:#x} water={:#x} counter={}",owner,value.water_time,value.signal_counter);
                          }
                          break;
                        }
                        throw std::runtime_error("pass-time material bindings differ: "+difference);
                      }
                      if(resolved->camera.view!=captured->camera.view ||
                         resolved->camera.projection!=captured->camera.projection ||
                         resolved->camera.view_projection!=captured->camera.view_projection)
                        throw std::runtime_error("pass-time camera differs from visible capture");
                      if(owned && resolved->world!=captured->world)
                        throw std::runtime_error("published native world differs from visible capture");
                      if(generation_difference) ++animation_generation_differences;
                      else ++checked;
                    } catch(const std::exception& error) {
                      ++mismatched;
                      if(reasons.size()<32 && reasons.insert(error.what()).second)
                        REXLOG_INFO("Native scene material comparison mismatch: {}",error.what());
                      if(owned) throw;
                    }
                    const auto total=checked+mismatched+missing+animation_generation_differences;
                    if(total<=4 || total%10000==0)
                      REXLOG_INFO("Native scene material comparison: checked={} mismatched={} missing={} animation_generation_differences={}",checked,mismatched,missing,animation_generation_differences);
                  }
                  if(owned) {
                    captured=std::move(resolved);
                    if(native_material_setup) ++state.scene_material_binding_bypasses;
                    if(++state.scene_material_constructed<=4 || state.scene_material_constructed%10000==0)
                      REXLOG_INFO("Native scene owned materials: constructed={} rejected={} live_binding_bypasses={} (published constants, native pass inputs and published world)",
                        state.scene_material_constructed,state.scene_material_rejected,state.scene_material_binding_bypasses);
                  }
                  if(!group->geometry) {
                    ++state.scene_group_material_captures;
                    if(captured->material==group->material) ++state.scene_group_material_reused;
                  }
                  group->material=captured->material;
                  object=state.scene_adapter.Observe(*source,state.scene_backend,mesh,ctx.r6.u32,ctx.r7.u32,ctx.r5.s32,*captured);
                } catch(const std::exception& error) {
                  captured.reset(); ++state.scene_native_fallbacks;
                  if(EDF_NATIVE_FLAG(scene_material_owned) &&
                     (++state.scene_material_rejected<=4 || state.scene_material_rejected%10000==0))
                    REXLOG_INFO("Native scene owned material rejected: count={} reason={}",state.scene_material_rejected,error.what());
                  if(state.scene_native_reasons.size()<32 && state.scene_native_reasons.insert(error.what()).second)
                    REXLOG_INFO("Native scene queued fallback: {}",error.what());
                }
                if(captured) {
                  auto view=captured->camera;
                  const auto& v=viewport.viewport; const auto& s=viewport.scissor;
                  view.viewport={v.TopLeftX,v.TopLeftY,v.Width,v.Height,v.MinDepth,v.MaxDepth};
                  view.scissor={s.left,s.top,s.right,s.bottom}; view.scissor_enabled=key[5]!=0;
                  const auto targets=edf::native::ActiveTargetsLocked(state);
                  if(!group->objects.empty() && (group->view.view!=view.view || group->view.projection!=view.projection ||
                     group->view.view_projection!=view.view_projection || group->view.scissor_enabled!=view.scissor_enabled ||
                     std::memcmp(&group->view.viewport,&view.viewport,sizeof(view.viewport)) ||
                     std::memcmp(&group->view.scissor,&view.scissor,sizeof(view.scissor)) ||
                     group->targets.count!=targets.count || group->targets.colors!=targets.colors || group->targets.depth!=targets.depth))
                    edf::native::FlushNativeQueuedSceneLocked(state,*group);
                  group->view=view; group->targets=targets; group->objects.push_back(edf::native::SelectNativeSceneInstanceLocked(state,object));
                  group->geometry=group->objects.back()->object.geometry;
                  group->material=group->objects.back()->object.material;
                  group->reverse_depth=viewport.reverse_depth;
                  group->vertex=state.active_vertex; group->pixel=state.linked_pixel;
                  edf::native::ConfigureNativeQueuedWorldLocked(state,*group);
                  native_scene_draw=true;
                } else {
                  group->geometry.reset();
                  edf::native::FlushNativeQueuedSceneLocked(state,*group);
                  setup(); // Native rendering changed bindings needed by this fallback draw.
                }
              }
              if(!native_scene_draw) {
                if(prepared_draw) prepared_draw->Draw(recorder);
                else mesh.Draw(recorder,ctx.r6.u32,ctx.r7.u32,ctx.r5.s32);
              }
              if(EDF_NATIVE_FLAG(scene_queued) && state.indexed_submitted%100000==0)
                REXLOG_INFO("Native scene queued: objects={} rendered={} draws={} fallback={}",state.scene_adapter.objects(),
                  state.scene_native_objects,state.scene_native_draws,state.scene_native_fallbacks);
            } else mesh.Draw(*state.context.Get(),ctx.r6.u32,ctx.r7.u32,ctx.r5.s32);
          }
          catch (...) { if (visibility) state.context->End(visibility.Get()); throw; }
          if (visibility) {
            state.context->End(visibility.Get());
            state.visibility.push_back({std::move(visibility),{state.active_vertex,state.linked_pixel,key[2],key[1]}});
          }
          ++state.indexed_submitted;
          native_submitted=true;
          edf::native::HookTiming tail_timing(edf::native::HookPhase::IndexedTail);
          const auto probe_x=REXCVAR_GET(edf_native_probe_x),probe_y=REXCVAR_GET(edf_native_probe_y);
          const auto probe_frame=REXCVAR_GET(edf_native_probe_frame);
          const bool probe_window=probe_frame>0 ? state.indexed_output_frames+1>=uint64_t(probe_frame) : state.scene_captures==0;
          if(probe_x>=0 && probe_y>=0 && !state.color_probe_done && probe_window) {
            try {
              const auto& scene=state.scenes.at(state.active_scene);
              uint32_t sample_x=uint32_t(probe_x),sample_y=uint32_t(probe_y);
              const auto width=REXCVAR_GET(edf_native_probe_width),height=REXCVAR_GET(edf_native_probe_height);
              if(width<=0 || height<=0) throw std::runtime_error("invalid diagnostic region size");
              std::array<float,4> rgba{};
              const bool probe_negative=REXCVAR_GET(edf_native_probe_negative);
              if(!scene.color.surface)
                throw std::runtime_error("the colour probe reads the surface through D3D11, which a scene on another backend has not got");
              if((width==1 && height==1) || edf::native::FindNativeInvalidColorPixel(
                  *state.context.Get(),*scene.color.surface.Get(),sample_x,sample_y,
                  uint32_t(width),uint32_t(height),sample_x,sample_y,probe_negative))
                rgba=edf::native::ReadNativeColorPixel(*state.context.Get(),*scene.color.surface.Get(),sample_x,sample_y);
              ++state.color_probe_draws;
              const bool nonfinite=!std::isfinite(rgba[0]) || !std::isfinite(rgba[1]) || !std::isfinite(rgba[2]);
              const bool negative=probe_negative &&
                (rgba[0]<=-1.f || rgba[1]<=-1.f || rgba[2]<=-1.f);
              if(nonfinite || negative) {
                state.color_probe_done=true;
                // Scans since the probe window opened. A hit on the first scan
                // means the pixel was already bad before the blamed draw ran,
                // so the draw is only "the first one scanned", not the producer.
                REXLOG_INFO("Native invalid RGB frame: next_output_candidate={}, kind={}, scans={} (scans==1 means the value predates this draw)",
                  state.indexed_output_frames+1,nonfinite?"nonfinite":"negative",state.color_probe_draws);
                REXLOG_INFO("Native invalid RGB origin: pixel={},{} draw={} VS={:#x} {} source={:#x} PS={:#x} {} source={:#x} rgba={},{},{},{}",
                  sample_x,sample_y,state.indexed_submitted,state.active_vertex,bindings.shader().entry.name,
                  state.shaders.at(state.active_vertex).source_fingerprint,state.linked_pixel,
                  state.shaders.at(state.linked_pixel).bindings->shader().entry.name,
                  state.shaders.at(state.linked_pixel).source_fingerprint,rgba[0],rgba[1],rgba[2],rgba[3]);
                // m_tD_trans is alpha blended and its lighting cannot go
                // negative, so the decoded blend operation is the first thing
                // to rule out: a SUBTRACT or REV_SUBTRACT where the guest asked
                // for ADD drives the target below zero from positive inputs.
                // Everything inside the shader is provably non-negative for the
                // logged constants, so the remaining candidate is the geometry
                // reaching the rasteriser. A vertex at w<=0, or a triangle
                // spanning the eye plane, makes perspective-correct
                // interpolation extrapolate the colour varyings far outside the
                // range the vertex shader can emit.
                try {
                  // Same reason as the clip probe above: the replay needs this
                  // VS bound on the context, and a recorded draw binds nothing
                  // there.
                  if(seam_draws) throw std::runtime_error(
                    "clip positions cannot be replayed for a draw recorded through the backend; "
                    "run with --edf_native_seam_draws=false to diagnose this");
                  const auto clip=mesh.CaptureClipPositions(*state.context.Get(),bindings.shader(),
                    ctx.r6.u32,(std::min)(ctx.r7.u32,24u),ctx.r5.s32);
                  float min_w=std::numeric_limits<float>::infinity(),max_w=-min_w;
                  size_t nonpositive=0,nonfinite_w=0;
                  for(const auto& position:clip) {
                    if(!std::isfinite(position[3])) { ++nonfinite_w; continue; }
                    min_w=(std::min)(min_w,position[3]); max_w=(std::max)(max_w,position[3]);
                    if(position[3]<=0) ++nonpositive;
                  }
                  REXLOG_INFO("Native invalid RGB clip: sampled={}, min_w={}, max_w={}, nonpositive_w={}, nonfinite_w={} (mixed-sign or near-zero w extrapolates interpolated colour)",
                    clip.size(),min_w,max_w,nonpositive,nonfinite_w);
                  for(size_t i=0;i<clip.size() && i<8;++i)
                    REXLOG_INFO("Native invalid RGB clip vertex: index={}, xyzw={},{},{},{}",
                      i,clip[i][0],clip[i][1],clip[i][2],clip[i][3]);
                } catch(const std::exception& error) {
                  REXLOG_INFO("Native invalid RGB clip capture: {}",error.what());
                }
                // Color = Dtex * In.Color + In.Specular, and the lighting terms
                // are provably non-negative for the logged constants, so the
                // sampled texel is the remaining candidate. A UNORM/BC format
                // cannot be negative; a signed or float one can.
                for(const auto* name:{"m_DiffuseTexture0_Sampler","m_ParameterTexture0_Sampler",
                    "m_NormalTexture0_Sampler","m_CubeTexture0_Sampler"}) {
                  auto* bound=state.shaders.at(state.linked_pixel).bindings->ReadTexture(name);
                  if(!bound) continue;
                  auto* view=edf::native::NativeD3D11TextureView(*bound);
                  auto* resource=edf::native::NativeD3D11TextureResource(*bound);
                  if(!view || !resource) continue; // A D3D11-only diagnostic.
                  D3D11_SHADER_RESOURCE_VIEW_DESC view_desc{}; view->GetDesc(&view_desc);
                  D3D11_TEXTURE2D_DESC desc{};
                  resource->GetDesc(&desc);
                  REXLOG_INFO("Native invalid RGB texture: name={}, view_format={}, resource_format={}, {}x{}, mips={}",
                    name,uint32_t(view_desc.Format),uint32_t(desc.Format),desc.Width,desc.Height,desc.MipLevels);
                }
                REXLOG_INFO("Native invalid RGB state: blend={:#x}, depth={:#x}, raster={:#x}, alpha={:#x}, write_mask={}, scissor={}",
                  key[0],key[1],key[2],key[3],key[4],key[5]);
                if(const auto found=state.render_states.find(key);
                   found!=state.render_states.end() && found->second.blend) {
                  D3D11_BLEND_DESC blend{};
                  found->second.blend->GetDesc(&blend);
                  const auto& target=blend.RenderTarget[0];
                  REXLOG_INFO("Native invalid RGB blend: enabled={}, src={}, dest={}, op={}, src_alpha={}, dest_alpha={}, alpha_op={}, mask={}, needs_factor={}, replicate_alpha={} (D3D11_BLEND_OP: 1=ADD 2=SUBTRACT 3=REV_SUBTRACT 4=MIN 5=MAX)",
                    target.BlendEnable!=FALSE,uint32_t(target.SrcBlend),uint32_t(target.DestBlend),
                    uint32_t(target.BlendOp),uint32_t(target.SrcBlendAlpha),uint32_t(target.DestBlendAlpha),
                    uint32_t(target.BlendOpAlpha),uint32_t(target.RenderTargetWriteMask),
                    found->second.requires_blend_factor,found->second.replicate_blend_alpha);
                }
                for(const auto* stage:{&bindings,state.shaders.at(state.linked_pixel).bindings.get()}) {
                  const auto* stage_name=stage->shader().entry.pixel?"PS":"VS";
                  for(const auto* name:{"m_MaterialDiffuse","m_MaterialSpecularColor","m_MaterialSpecularPower",
                      "m_MaterialBumpHeight","m_RefrectionRate","g_LightVector","g_LightDiffuse",
                      "g_LightSpecular","g_LightAmbient","g_HemiSphereVector","g_HemiSphereColor1",
                      "g_HemiSphereColor2","g_FogParam","g_FogColor"}) {
                    const auto values=stage->ReadFloatVector(name);
                    for(size_t lane=0;lane<values.size();++lane)
                      REXLOG_INFO("Native invalid RGB constant: stage={} name={} lane={} value={}",stage_name,name,lane,values[lane]);
                  }
                  for(const auto* name:{"g_mWorld","g_mView","g_mProjection","g_mViewTranspose"}) {
                    if(const auto matrix=stage->ReadFloat4x4(name)) for(size_t row=0;row<4;++row)
                      REXLOG_INFO("Native invalid RGB matrix: stage={} name={} row={} values={},{},{},{}",stage_name,name,row,
                        (*matrix)[row*4],(*matrix)[row*4+1],(*matrix)[row*4+2],(*matrix)[row*4+3]);
                  }
                }
                REXLOG_INFO("Native invalid RGB mesh: declaration={:#x} stride={} first={} base={} count={}",
                  decl,stream.stride,ctx.r6.u32,ctx.r5.s32,ctx.r7.u32);
                const auto declaration_word=[&](size_t at) {
                  uint32_t value=0;
                  for(size_t lane=0;lane<4;++lane) value=(value<<8)|declaration_bytes[at+lane];
                  return value;
                };
                for(uint32_t element=0;element<elements;++element) {
                  const auto at=element*12,offset=declaration_word(at),type=declaration_word(at+4);
                  const auto semantic=declaration_word(at+8);
                  REXLOG_INFO("Native invalid RGB attribute: offset={} type={:#x} semantic={:#x}",offset,type,semantic);
                  const auto usage=(semantic>>16)&255,index=(semantic>>8)&255;
                  if(type!=0x2a23b9 || (usage!=3 && usage!=6) || index!=0) continue;
                  if(offset>stream.stride || stream.stride-offset<12) continue;
                  uint32_t zero=0,invalid=0,samples=(std::min)(ctx.r7.u32,65536u);
                  const auto values=mesh.CaptureSourceFloat3(ctx.r6.u32,samples,ctx.r5.s32,offset);
                  for(uint32_t i=0;i<samples;++i) {
                    const auto& value=values[i];
                    if(value[0]==0 && value[1]==0 && value[2]==0) ++zero;
                    if(!std::isfinite(value[0]) || !std::isfinite(value[1]) || !std::isfinite(value[2])) ++invalid;
                    if(i<3) REXLOG_INFO("Native invalid RGB attribute sample: usage={} index_position={} xyz={},{},{} (native draw snapshot)",
                      usage,ctx.r6.u32+i,value[0],value[1],value[2]);
                  }
                  REXLOG_INFO("Native invalid RGB attribute summary: usage={} indexed_samples={} zero={} nonfinite={}",
                    usage,samples,zero,invalid);
                }
              }
              if(state.color_probe_draws>=uint64_t(std::clamp(REXCVAR_GET(edf_native_probe_draw_limit),1,65536))) {
                state.color_probe_done=true;
                REXLOG_INFO("Native invalid RGB probe exhausted: draws={}, next_output_candidate={}",state.color_probe_draws,state.indexed_output_frames+1);
              }
            } catch(const std::exception& error) {
              state.color_probe_done=true;
              REXLOG_ERROR("Native invalid RGB probe: {}",error.what());
            }
          }
          state.scenes.at(state.active_scene).frame_complete=false;
        }
        if (state.indexed_uploads<=20 || state.indexed_submitted%1000==0) {
          REXLOG_INFO("Native indexed upload: uploaded={}, errors={}, submitted={}, reversed_depth={}, mesh_builds={}, mesh_hits={}, mesh_bytes={}, mesh_entries={}, entry_evictions={}, budget_evictions={}, vertex_mismatches={}, index_mismatches={}",
            state.indexed_uploads,state.indexed_errors,state.indexed_submitted,viewport.reverse_depth,
            state.meshes.builds(),state.meshes.hits(),state.meshes.bytes(),state.meshes.entries(),
            state.meshes.entry_evictions(),state.meshes.budget_evictions(),state.meshes.vertex_mismatches(),state.meshes.index_mismatches());
          if(state.indexed_submitted%100000==0) {
            REXLOG_INFO("Native prepared geometry: hits={} (guarded unchanged snapshots, matching storage and validated range)",prepared_geometry_hits);
            const auto& checks=state.meshes.source_checks();
            REXLOG_INFO("Native mesh source checks: vertex_checks={}, index_checks={}, vertex_candidate_bytes={}, index_candidate_bytes={} (cache-hit checks; not measured memory traffic)",
              checks.vertex_checks,checks.index_checks,checks.vertex_candidate_bytes,checks.index_candidate_bytes);
            const auto& spend=state.meshes.spend();
            if(REXCVAR_GET(edf_native_hook_timings)) REXLOG_INFO("Native mesh acquire spend: calls={}, prologue_ns_avg={}, lookup_ns_avg={}, tail_ns_avg={}, owned_hits={} (a cache hit is prologue+lookup; the tail is construction)",
              spend.calls,spend.calls?spend.prologue_ns/spend.calls:0,
              spend.calls?spend.lookup_ns/spend.calls:0,spend.calls?spend.tail_ns/spend.calls:0,state.meshes.owned_hits());
            REXLOG_INFO("Native published index consumption: reused_builds={}, rejected_generations={} (mesh hits excluded)",
              state.meshes.published_index_reuses(),state.meshes.published_index_rejections());
            size_t vertices=0,vertex_bytes=0,indices=0,index_bytes=0;
            state.model_buffers.VisitVertexStorage([&](uint32_t,const edf::native::NativeVertexBuffer& storage) {
              ++vertices; vertex_bytes+=storage.StorageBytes();
            });
            state.model_buffers.VisitIndexStorage([&](uint32_t,const edf::native::NativeIndexBuffer& storage) {
              ++indices; index_bytes+=storage.StorageBytes();
            });
            REXLOG_INFO("Native model storage: vertices={}, vertex_bytes={}, indices={}, index_bytes={}, vertex_reused_builds={}, vertex_replaced_builds={} (CPU/GPU payload shared with meshes; cache hits excluded)",
              vertices,vertex_bytes,indices,index_bytes,state.meshes.retained_vertex_reuses(),state.meshes.retained_vertex_replacements());
          }
          const auto mismatches=state.meshes.vertex_mismatches()+state.meshes.index_mismatches();
          if(mismatches!=state.reported_mesh_mismatches) {
            state.reported_mesh_mismatches=mismatches;
            const auto& v=state.meshes.last_vertex_mismatch();
            const auto& i=state.meshes.last_index_mismatch();
            REXLOG_INFO("Native geometry mismatch identities: vertex_count={}, last_vertex=(VB={:#x}, IB={:#x}, declaration={:#x}, VS={:#x}, variant={}), index_count={}, last_index=(VB={:#x}, IB={:#x}, declaration={:#x}, VS={:#x}, variant={}) (last detected consumers, not writer PCs)",
              state.meshes.vertex_mismatches(),v[0],v[1],v[2],v[3],v[4],
              state.meshes.index_mismatches(),i[0],i[1],i[2],i[3],i[4]);
          }
        }
      } catch (const std::exception& error) {
        ++state.indexed_errors;
        if (state.indexed_errors<=10) REXLOG_ERROR("Native indexed upload: {} (VS={:#x}, PS={:#x}, caller={:#x})",
          error.what(),state.active_vertex,state.linked_pixel,uint32_t(ctx.lr));
      }
    }
    edf::native::HookTiming coverage_timing(edf::native::HookPhase::IndexedCoverage);
    if(!ctx.r7.u32) ++state.indexed_empty_requests;
    else if(native_submitted) {
      ++state.indexed_nonempty_submitted;
      // Opt-in: enumerating what the content exercises costs a set lookup on
      // every draw, and gameplay submits over a hundred thousand a second.
      if(REXCVAR_GET(edf_native_contract_coverage)) try {
        const edf::native::GuestReader reader(base);
        state.contracts.RecordSubmitted(edf::native::MakeNativeContract(state,
          edf::native::NativeContractPath::Indexed,state.active_vertex,state.linked_pixel,
          reader.Word(reader.Add(ctx.r3.u32,11536)),ctx.r4.u32,drawn_stride,drawn_index_width));
      } catch(const std::exception&) { /* Accounting must never fail a drawn frame. */ }
    }
    else {
      ++state.indexed_unsubmitted_requests;
      try {
        const edf::native::GuestReader reader(base);
        const auto declaration=reader.Word(reader.Add(ctx.r3.u32,11536));
        edf::native::ReportNativeContractRejection(state,edf::native::MakeNativeContract(state,
          edf::native::NativeContractPath::Indexed,state.active_vertex,state.linked_pixel,
          declaration,ctx.r4.u32),"indexed draw not submitted natively");
      } catch(const std::exception&) {
        edf::native::ReportNativeContractRejection(state,edf::native::MakeNativeContract(state,
          edf::native::NativeContractPath::Indexed,state.active_vertex,state.linked_pixel,
          0,ctx.r4.u32),"indexed draw not submitted natively; device unreadable");
      }
      const std::array<uint32_t,5> signature{uint32_t(ctx.lr),ctx.r4.u32,state.active_vertex,state.linked_pixel,state.active_target};
      if(state.indexed_unsubmitted_paths.size()<64 && state.indexed_unsubmitted_paths.insert(signature).second) {
        REXLOG_INFO("Native indexed unsubmitted path: caller={:#x}, primitive={}, count={}, scene={:#x}, target={:#x}, active_VS={:#x}, active_PS={:#x}",
          uint32_t(ctx.lr),ctx.r4.u32,ctx.r7.u32,state.active_scene,state.active_target,state.active_vertex,state.linked_pixel);
        try {
          const edf::native::GuestReader reader(base);
          const auto pair=edf::native::ReadShaderPair(reader,ctx.r3.u32);
          REXLOG_INFO("Native indexed unsubmitted bindings: VS={:#x}, PS={:#x}, surface={:#x}, declaration={:#x}, index_buffer={:#x}",
            pair.vertex,pair.pixel,reader.Word(reader.Add(ctx.r3.u32,12168)),
            reader.Word(reader.Add(ctx.r3.u32,11536)),reader.Word(reader.Add(ctx.r3.u32,12164)));
        } catch(const std::exception& error) {
          REXLOG_INFO("Native indexed unsubmitted inspection: {}",error.what());
        }
      }
    }
    const auto now=std::chrono::steady_clock::now();
    if(state.indexed_draws>=1024 && now-state.indexed_coverage_reported>=std::chrono::seconds(10)) {
      state.indexed_coverage_reported=now;
      REXLOG_INFO("Native indexed coverage: requests={}, empty={}, submitted={}, unsubmitted={}, sampled_paths={} (64-path cap; not visual completeness)",
        state.indexed_draws,state.indexed_empty_requests,state.indexed_nonempty_submitted,
        state.indexed_unsubmitted_requests,state.indexed_unsubmitted_paths.size());
      const auto& coverage=state.contracts.counters();
      REXLOG_INFO("Native contract coverage: rejected_draws={}, distinct_rejected={}, omitted={}, errors={}, clean={}, submitted_contracts={} (clean requires zero rejected and zero omitted)",
        coverage.rejected,coverage.distinct_rejected,coverage.omitted_rejected,
        state.indexed_errors,state.contracts.clean(),coverage.distinct_submitted);
      if(const auto path=REXCVAR_GET(edf_native_contract_export);!path.empty()) {
        // Rewritten whole each time: a partial capture from a killed run is
        // still a valid catalog of everything seen up to that point.
        std::ofstream file(path,std::ios::binary|std::ios::trunc);
        const auto text=state.contracts.Export();
        file.write(text.data(),std::streamsize(text.size()));
        if(!file) REXLOG_ERROR("Native contract export failed: {}",path);
        else REXLOG_INFO("Native contract export: {}, declarations={}, bytes={}",
          path,state.contracts.declarations().size(),text.size());
      }
      for(const auto& rejection:state.contracts.Rejections())
        REXLOG_INFO("Native contract gap: path={}, VS_source={:#x}, PS_source={:#x}, declaration={:#x}, elements={}, topology={}, draws={}, reason={}",
          edf::native::NativeContractPathName(rejection.contract.path),rejection.contract.vertex_source,
          rejection.contract.pixel_source,rejection.contract.declaration,rejection.contract.elements,
          rejection.contract.topology,rejection.draws,rejection.reason);
    }
  }
  native_timing.Finish();
  edf::native::HookTiming guest_timing(edf::native::HookPhase::IndexedGuest);
  // The native CPU chain calls native implementations directly; it needs no
  // thread-local packet-routing scope. Mask inherited ownership only for legacy.
  const bool native_host=EDF_NATIVE_FLAG(host);
  std::optional<edf::native::NativeConstantOwnership> legacy_scope;
  if(!native_host) legacy_scope.emplace(0);
  if(native_host) {
    // Generated from the unchanged retail CPU-state prefix and original ABI
    // epilogue. Native submission replaces the subsequent inline GPU draw loop.
    // Native-host mode has no GPU emulator to consume retail draw packets.
    // Unsupported, empty and failed draws retain CPU updates too, without
    // rereading the index header. Missing
    // native submissions remain explicit coverage failures, not fallback draws.
    __imp__edf_native_indexed_cpu_tail(ctx,base);
    static thread_local uint64_t omitted=0;
    if(++omitted<=3) REXLOG_INFO("Native indexed routing: retained CPU state; omitted Xbox draw packets (submitted={}, geometry_rejected={})",
      native_submitted,native_geometry_rejected);
  } else __imp__sub_821FE358(ctx,base);
}

namespace edf::native {
namespace {
// A Utility 3D / Vs_Particle list or strip that has passed its contract checks.
struct NativeSceneImmediateDraw {
  ShaderBindings& vertex;
  ShaderBindings& pixel;
  const NativeViewportState& viewport;
  const RenderStateWords& state;
  GuestShaderPair shaders{};
  uint32_t declaration=0,element_count=0;
  std::shared_ptr<const NativeDeclaration> owned_declaration;
  uint32_t primitive=13,stride=0;
};
// Records the draw from vertices held in HOST memory, laid out as the guest
// lays them out (big-endian words in declaration order): the DrawPrimitiveUP
// hook passes the bytes it read at r6, the full-frame effect pass passes what
// EncodeNativeEffectVertices built (native_full_frame_effects.h). The vertices
// are never read from the guest here; reader and device serve only the blend
// factor RecordDrawSetup reads and the direct path's render-state bind.
template<class Reader>
void RecordNativeSceneImmediate(Bridge& state,const Reader& reader,uint32_t device,
                                const NativeSceneImmediateDraw& draw,std::span<const uint8_t> vertices) {
  const bool strip=draw.primitive==6;
  if(!draw.stride || vertices.size()%draw.stride) throw std::runtime_error("native immediate vertices are not whole vertices");
  const auto count=uint32_t(vertices.size()/draw.stride);
  if(count<3 || count>16384 || (!strip && count%4)) throw std::runtime_error("unsupported native immediate vertex count");
  const auto owned_indices=state.generated_indices.Get(strip?NativeIndexPattern::Strip:NativeIndexPattern::Quads,count);
  const auto indices=owned_indices->bytes();
  HookTiming acquire_timing(HookPhase::ImmediateAcquire);
  auto& mesh=state.immediate_meshes.Acquire(EnsureSceneBackendLocked(state),draw.vertex.shader(),
    ImmediateStreamKey(vertices.size(),draw.declaration,draw.shaders.vertex,draw.primitive,draw.viewport.reverse_depth),
    {draw.owned_declaration->bytes().data(),draw.element_count*12},draw.stride,
    vertices,indices,2,draw.owned_declaration,owned_indices,{},{},{},{},0,{},
    // Where this mesh's dynamic vertices are rewritten when the draw
    // is recorded; the immediate context does it otherwise.
    EDF_NATIVE_FLAG(seam_draws)?&SceneRecorderLocked(state):nullptr);
  acquire_timing.Finish();
  auto render=state.render_states.find(draw.state);
  if(render==state.render_states.end()) render=state.render_states.emplace(draw.state,
    CreateNativeRenderState(state.device.Get(),draw.state)).first;
  if(EDF_NATIVE_FLAG(seam_draws)) {
    HookTiming record_timing(HookPhase::ImmediateRecord);
    auto& recorder=RecordDrawSetup(state,reader,device,{
      draw.vertex,draw.pixel,draw.viewport,draw.state,
      mesh.input_layout().elements(),mesh.input_layout().fingerprint(),
      (uint64_t(draw.shaders.vertex)<<1)|uint64_t(draw.viewport.reverse_depth?1:0),draw.shaders.pixel,
      NativeBackendTopology::TriangleList});
    mesh.DrawTransient(recorder,vertices,0,uint32_t(indices.size()/2));
  } else {
    BindActiveTarget(state);
    BindGuestRenderState(render->second,*state.context.Get(),reader,device,&state.bind_generation);
    draw.viewport.Bind(*state.context.Get());
    draw.vertex.Bind(*state.context.Get()); draw.pixel.Bind(*state.context.Get());
    mesh.Draw(*state.context.Get(),0,uint32_t(indices.size()/2));
  }
}
}  // namespace
}  // namespace edf::native
namespace edf::native {
// Declaration element words as the guest stores them (big-endian).
template<size_t N>
std::array<uint8_t,N*4> NativeFullFrameDeclarationBytes(const std::array<uint32_t,N>& words) {
  std::array<uint8_t,N*4> bytes{};
  for(size_t i=0;i<N;++i) for(size_t byte=0;byte<4;++byte) bytes[i*4+byte]=uint8_t(words[i]>>(24-byte*8));
  return bytes;
}
// One effect draw of the full frame (native_full_frame_effects.h), recorded
// without guest calls. As the producers do: 821BC4C8 stores the draw's
// texture into the technique's sampler list (BindNativeEffectTexture, the
// guest's own write), then the material 821B94E8 activates, [technique+16]
// (NativeEffectTechniqueMaterial; the technique object itself is not a pass
// record), has its program from the model pass cache - rebuilt when the
// stored texture changed it - applied to the shader bindings as a published
// activation is (NativeSceneMaterialProgram::ApplyBindings), with the pass
// camera over its camera globals. Render state: the shared full-frame base
// state, then for the ribbons the draw's blend (0x48/0x4c) and depth write
// (0x30) and the technique's own operations over them, for the particles the
// technique's operations and the blend over them (state_before_activation).
// The vertices are the host bytes EncodeNativeEffectVertices builds, one
// RecordNativeSceneImmediate per guest DrawPrimitiveUP, under the Vs_Particle
// (44-byte) or VS_3DTex (36-byte) declaration the immediate path accepts.
// Throws when any of it is missing.
//
// Split in two: the activation (everything up to the render state, done once
// for a run of draws that share it, see RecordNativeFullFrameEffectsLocked)
// and the draw's DrawPrimitiveUP calls under it. The activation leaves the
// bound shader pair (the registered shaders' own bindings, holding what
// ApplyBindings set until another activation), the resolved render state and
// the immediate declaration.
struct NativeFullFrameEffectActivation {
  ShaderBindings* vertex=nullptr;
  ShaderBindings* pixel=nullptr;
  std::shared_ptr<const NativeSceneGroupMaterial> material;
  RenderStateWords render{};
  GuestShaderPair shaders{};
  const std::shared_ptr<const NativeDeclaration>* declaration=nullptr;
  uint32_t declaration_id=0,device=0;
};
NativeFullFrameEffectActivation ActivateNativeFullFrameEffectLocked(Bridge& state,const GuestReader& reader,
    const NativeSceneCpuWindow<GuestReader>& window,const NativeEffectDraw& draw,const NativeScenePassCamera& camera,
    const NativeViewportState& viewport,const NativeFullFramePassTargets& formats) {
  NativeFullFrameEffectActivation result;
  if(draw.texture) {
    const auto texture=state.textures.find(draw.texture);
    if(texture==state.textures.end() || !texture->second.content_valid || !texture->second.backend)
      throw std::runtime_error("native effect texture is not decoded");
  }
  BindNativeEffectTexture(window,draw.effect,draw.technique,draw.texture);
  const auto pass=NativeEffectTechniqueMaterial(window,draw.effect,draw.technique);
  if(!pass) throw std::runtime_error("native effect technique has no material");
  // The provider's reason travels with the draw's one failure (the caller's
  // `failed`), not as a second decline of the same draw.
  std::string provider;
  result.material=NativeModelPassProgramLocked(state,window,pass,false,[&](const std::string& reason) { provider=reason; });
  const auto& material=result.material;
  if(!material || !material->program)
    throw std::runtime_error(provider.empty()?"native effect technique has no program":provider);
  const auto& program=*material->program;
  if(draw.texture && std::none_of(program.inputs.textures.begin(),program.inputs.textures.end(),
       [&](const auto& texture) { return texture.handle==draw.texture; }))
    throw std::runtime_error("native effect texture is not sampled by its technique");
  const auto vertex=state.shaders.find(program.inputs.vertex),pixel=state.shaders.find(program.inputs.pixel);
  if(vertex==state.shaders.end() || pixel==state.shaders.end() || !vertex->second.bindings || !pixel->second.bindings)
    throw std::runtime_error("native effect shaders are not registered");
  auto& vs=VertexBindingsForDraw(vertex->second,viewport.reverse_depth);
  auto& ps=*pixel->second.bindings;
  const std::pair<uint32_t,uint32_t> link{program.inputs.vertex,program.inputs.pixel};
  if(!state.validated_links.contains(link)) {
    ValidateNativeShaderLink(vs.shader(),ps.shader());
    state.validated_links.insert(link);
  }
  auto constants=material->constants;
  for(auto& constant:constants) camera.Apply(constant);
  const auto base=NativeFullFrameBaseState(formats);
  const auto resolved=program.ResolveSamplers(base.samplers);
  std::vector<NativeBackendSampler*> samplers;
  for(const auto& texture:program.inputs.textures) {
    if(texture.slot>=resolved.size()) throw std::runtime_error("invalid native effect sampler slot");
    const auto key=NativeFilteringKey(resolved[texture.slot].words,REXCVAR_GET(edf_native_anisotropic_filtering));
    auto cached=state.samplers.find(key);
    if(cached==state.samplers.end())
      cached=state.samplers.emplace(key,&EnsureSceneBackendLocked(state).CreateSampler(DecodeNativeGuestSampler(key))).first;
    samplers.push_back(cached->second);
  }
  state.active_vertex=0;
  state.active_vertex_parameters.reset();
  program.ApplyBindings(vs,ps,constants,samplers);
  state.linked_vertex=program.inputs.vertex; state.linked_pixel=program.inputs.pixel;
  const auto draw_state=[&](NativeMaterialRenderPass pass) {
    if(draw.blend==kNativeEffectBlendAlpha) { ApplyNativeMaterialState(pass,0x48,6); ApplyNativeMaterialState(pass,0x4c,7); }
    else if(draw.blend==kNativeEffectBlendAdditive) { ApplyNativeMaterialState(pass,0x48,1); ApplyNativeMaterialState(pass,0x4c,1); }
    if(draw.sets_depth_write) ApplyNativeMaterialState(pass,0x30,draw.depth_write?1:0);
    return pass;
  };
  const auto render=draw.state_before_activation()?program.ResolveRenderState(draw_state(base.render)):
                                                   draw_state(program.ResolveRenderState(base.render));
  DecodeNativeRenderState(render.words);
  // The immediate path's accepted layouts (see the 821FD8F8 hook's checks):
  // Vs_Particle, VS_3DTex and, for 821A7B58's strips, VS_3D (position and a
  // D3DCOLOR at +12, the "solid" Utility 3D layout).
  const bool particle=draw.kind==NativeEffectDraw::Kind::Particles;
  const bool solid=draw.kind==NativeEffectDraw::Kind::ColourStrip;
  static const auto particle_declaration=NativeDeclaration::Create(NativeFullFrameDeclarationBytes<12>({
    0,0x2a23b9,0, 12,0x2c23a5,0x50000, 20,0x2c23a5,0x50100, 28,0x1a23a6,0xa0000}));
  static const auto ribbon_declaration=NativeDeclaration::Create(NativeFullFrameDeclarationBytes<9>({
    0,0x2a23b9,0, 12,0x2c23a5,0x50000, 20,0x1a23a6,0xa0000}));
  static const auto solid_declaration=NativeDeclaration::Create(NativeFullFrameDeclarationBytes<6>({
    0,0x2a23b9,0, 12,0x182886,0xa0000}));
  result.declaration=particle?&particle_declaration:solid?&solid_declaration:&ribbon_declaration;
  // Synthetic declaration identities: only the immediate mesh cache keys on them.
  result.declaration_id=particle?0xFFFFFF01u:solid?0xFFFFFF03u:0xFFFFFF02u;
  result.device=reader.Word(reader.Add(reader.Word(0x8257bfb4),8));
  result.vertex=&vs; result.pixel=&ps;
  result.render=render.words;
  result.shaders=GuestShaderPair{.pixel=program.inputs.pixel,.vertex=program.inputs.vertex};
  return result;
}
// The draw's DrawPrimitiveUP calls under its activation.
void RecordNativeFullFrameEffectCallsLocked(Bridge& state,const GuestReader& reader,
    const NativeFullFrameEffectActivation& activation,const NativeEffectDraw& draw,const NativeViewportState& viewport) {
  const auto& declaration=*activation.declaration;
  for(const auto& [first,count]:NativeEffectDrawCalls(draw)) {
    const auto bytes=EncodeNativeEffectVertices(draw,first,count);
    RecordNativeSceneImmediate(state,reader,activation.device,{*activation.vertex,*activation.pixel,viewport,activation.render,
      activation.shaders,activation.declaration_id,declaration->count(),declaration,draw.primitive(),draw.stride()},bytes);
  }
}
// A run of adjacent draws that NativeEffectDrawsShareActivation is activated
// once: the activation reads only the fields that predicate compares (never
// the vertices), so the next draw's would bind the same texture word, program,
// constants, samplers and render state onto the same bindings, which only an
// activation changes (the immediate recording reads them). A failed draw
// drops the activation and goes to `failed` once, with its reason; the next
// draw activates again, as it would alone.
uint64_t RecordNativeFullFrameEffectsLocked(Bridge& state,const GuestReader& reader,const NativeSceneCpuWindow<GuestReader>& window,
    std::span<const NativeEffectDraw> draws,const NativeScenePassCamera& camera,const NativeViewportState& viewport,
    const NativeFullFramePassTargets& formats,const std::function<void(const std::exception&)>& failed) {
  uint64_t recorded=0;
  std::optional<NativeFullFrameEffectActivation> activation;
  const NativeEffectDraw* activated=nullptr;
  for(const auto& draw:draws) {
    try {
      // Reuse off (native_reuse.h): every draw activates on its own.
      if(!activation || !NativeReuseAllowed() || !NativeEffectDrawsShareActivation(*activated,draw)) {
        activation.reset();
        activation=ActivateNativeFullFrameEffectLocked(state,reader,window,draw,camera,viewport,formats);
        activated=&draw;
      }
      RecordNativeFullFrameEffectCallsLocked(state,reader,*activation,draw,viewport);
      ++recorded;
    } catch(const std::exception& error) { activation.reset(); failed(error); }
  }
  return recorded;
}
}
REX_EXTERN(__imp__sub_821FD8F8);
REX_EXTERN(__imp__edf_native_immediate_cpu_tail);
REX_HOOK_RAW(sub_821FD8F8) {
  edf::native::HookTiming native_timing(edf::native::HookPhase::ImmediateNative);
  bool native_submitted=false;
  if (EDF_NATIVE_FLAG(shader_bridge)) {
    auto& state = edf::native::State();
    edf::native::HookTiming submission_wait(edf::native::HookPhase::ImmediateSubmissionWait);
    std::lock_guard submission(state.submissions);
    submission_wait.Finish();
    edf::native::HookTiming context_wait(edf::native::HookPhase::ImmediateContextWait);
    std::lock_guard lock(state.mutex);
    context_wait.Finish();
    edf::native::HookTiming classify_timing(edf::native::HookPhase::ImmediateClassify);
    bool movie_draw=false;
    // Setters publish identity independently of material activation. Retain a
    // per-invocation copy while this hook holds the submission/state locks.
    std::optional<edf::native::GuestShaderPair> draw_shaders;
    auto bound_shaders=[&]() -> const edf::native::GuestShaderPair& {
      if(!draw_shaders)
        draw_shaders=state.shader_bindings.Pair(ctx.r3.u32);
      return *draw_shaders;
    };
    // What the pair's identities admit, memoized per pair against the two
    // registry generations (native_immediate_classify.h): the same answer the
    // lookups below each path used to make per draw. Throws as bound_shaders
    // does, inside each path's own handler.
    const decltype(state.immediate_pairs)::Entry* pair_entry=nullptr;
    auto pair_class=[&]() -> const decltype(state.immediate_pairs)::Entry& {
      if(!pair_entry) pair_entry=&edf::native::ClassifyImmediatePairLocked(state,bound_shaders());
      return *pair_entry;
    };
    using PairClass=edf::native::NativeImmediatePairClass;
    if (!state.active_target && state.scenes.contains(state.active_output)) try {
      const auto& pair=bound_shaders();
      const auto movie_kind=pair_class().kind.movie;
      movie_draw=movie_kind!=PairClass::Movie::None;
      const bool movie_sd=movie_kind==PairClass::Movie::Sd;
      if (movie_draw) {
        const edf::native::GuestReader backing(base);
        const edf::native::GuestReadWindow reader(backing,backing.Add(ctx.r3.u32,1024),12416-1024);
        auto word=[&](uint32_t offset) {return reader.Word(reader.Add(ctx.r3.u32,offset));};
        auto& scene=state.scenes.at(state.active_output);
        if (word(12168)!=scene.output_surface || ctx.r4.u32!=5 || ctx.r5.u32!=4 || ctx.r7.u32!=16)
          throw std::runtime_error("unsupported movie output/topology");
        const auto declaration=state.declarations.Get(state.declaration_bindings.at(ctx.r3.u32));
        if (declaration->count()!=2 ||
            declaration->Words<6>()!=std::array<uint32_t,6>{0,0x2c23a5,0,8,0x2c23a5,0x50000})
          throw std::runtime_error("unsupported movie vertex declaration");
        const auto viewport=edf::native::ReadNativeDrawViewport(reader,ctx.r3.u32);
        if (viewport.reverse_depth) throw std::runtime_error("unimplemented reversed movie projection");
        if (!state.movie_vertex) {
          const auto effect=edf::native::MakeNativeMovieEffect();
          auto vertex=std::make_unique<edf::native::ShaderBindings>(state.device.Get(),
            edf::native::CompileNativeShader(state.device.Get(),effect,effect.entries[0],"native_movie.fx"));
          auto pixel=std::make_unique<edf::native::ShaderBindings>(state.device.Get(),
            edf::native::CompileNativeShader(state.device.Get(),effect,effect.entries[1],"native_movie.fx"));
          auto pixel_sd=std::make_unique<edf::native::ShaderBindings>(state.device.Get(),
            edf::native::CompileNativeShader(state.device.Get(),effect,effect.entries[2],"native_movie.fx"));
          edf::native::ValidateNativeShaderLink(vertex->shader(),pixel->shader());
          edf::native::ValidateNativeShaderLink(vertex->shader(),pixel_sd->shader());
          auto vertices=std::make_unique<edf::native::QuadStream>(state.device.Get(),vertex->shader());
          state.movie_bindings[0].emplace(*vertex,*pixel);
          state.movie_bindings[1].emplace(*vertex,*pixel_sd);
          state.movie_vertex=std::move(vertex); state.movie_pixel=std::move(pixel);
          state.movie_pixel_sd=std::move(pixel_sd); state.movie_vertices=std::move(vertices);
        }
        // Choose the program actually bound by the game, not a heuristic based
        // on the output resolution (SD movies may fill a 1280x720 target).
        auto& movie_pixel=movie_sd ? *state.movie_pixel_sd : *state.movie_pixel;
        // CPU constant setters 82149248/82149358 copy float4 rows to
        // device+(112+register)*16 / device+(368+register)*16 respectively.
        auto registers=[&](uint32_t offset,size_t bytes) {return std::span<const uint8_t>{reader.Bytes(reader.Add(ctx.r3.u32,offset),bytes),bytes};};
        const auto& movie_plan=*state.movie_bindings[movie_sd?1:0];
        // At any size but 1280x720 a full-screen (or canvas-placed) movie is
        // fitted into the centred 16:9 rectangle, the whole target on 16:9
        // (native_canvas_constants.h), moved onto host pixel edges by the
        // guest's half-pixel convention.
        const float movie_center=REXCVAR_GET(edf_native_pixel_centers)?
          edf::native::GuestPixelCenterOffset(edf::native::ReadVertexCenterWord(reader,ctx.r3.u32)):0.0f;
        const auto movie_layout=edf::native::MapNativeMovieRegisters(registers(1792,160),
          {reader.Bytes(ctx.r6.u32,64),64},scene.output.sampled.width,scene.output.sampled.height,movie_center);
        if(movie_layout.framing!=edf::native::NativeMovieFraming::Unchanged && state.movie_draws<3)
          REXLOG_INFO("Native movie framing: {} into the 16:9 area of {}x{} (pixel centre {})",
            movie_layout.framing==edf::native::NativeMovieFraming::FullTarget?"full-target quad":"canvas quad",
            scene.output.sampled.width,scene.output.sampled.height,movie_center);
        movie_plan.SetConstants(*state.movie_vertex,movie_pixel,movie_layout.registers,registers(5888,16));
        movie_pixel.ClearTextures(); movie_pixel.ClearSamplers();
        for(uint32_t i=0;i<3;++i) {
          // 8213BA98 stores the raw SetTexture handle at (3068+slot)*4.
          const auto handle=word(12272+i*4);
          const auto texture=state.textures.find(handle);
          const auto creation=state.texture_creations.find(handle);
          if (texture==state.textures.end() || !texture->second.content_valid ||
              creation==state.texture_creations.end() || creation->second.format!=0x28000002)
            throw std::runtime_error("movie draw missing decoded native plane");
          movie_plan.SetTexture(movie_pixel,i,texture->second.backend);
          const auto offset=1024+i*24;
          const auto key=edf::native::SamplerStateKey({word(offset),word(offset+12),word(offset+16),word(offset+20)});
          auto cached=state.samplers.find(key);
          if(cached==state.samplers.end()) {
            const auto desc=edf::native::DecodeNativeGuestSampler(key);
            cached=state.samplers.emplace(key,&edf::native::EnsureSceneBackendLocked(state).CreateSampler(desc)).first;
          }
          movie_plan.SetSampler(movie_pixel,i,cached->second);
        }
        const auto key=edf::native::ReadAuditedRenderStateWords(reader,ctx.r3.u32);
        if ((key[1]&3)!=0) throw std::runtime_error("movie requires an unbound depth/stencil surface");
        auto render=state.render_states.find(key);
        if(render==state.render_states.end()) render=state.render_states.emplace(key,edf::native::CreateNativeRenderState(state.device.Get(),key)).first;
        if(EDF_NATIVE_FLAG(seam_draws)) {
          auto& recorder=edf::native::RecordDrawSetup(state,reader,ctx.r3.u32,{
            *state.movie_vertex,movie_pixel,viewport,key,
            edf::native::QuadStream::Layout(),edf::native::kNativeQuadLayoutId,
            // The reversed-depth variant is a different compiled shader under the
            // same guest handle, so it belongs in the identity: a cache keyed on
            // the handle alone hands back the pipeline built from the other one.
            (uint64_t(pair.vertex)<<1)|uint64_t(viewport.reverse_depth?1:0),
            pair.pixel,edf::native::NativeBackendTopology::TriangleList});
          state.movie_vertices->Draw(edf::native::EnsureSceneBackendLocked(state),recorder,
                                     {reader.Bytes(ctx.r6.u32,64),64});
        } else {
          edf::native::BindActiveTarget(state);
          edf::native::BindGuestRenderState(render->second,*state.context.Get(),reader,ctx.r3.u32,&state.bind_generation); viewport.Bind(*state.context.Get());
          state.movie_vertex->Bind(*state.context.Get()); movie_pixel.Bind(*state.context.Get());
          state.movie_vertices->Draw(*state.context.Get(),{reader.Bytes(ctx.r6.u32,64),64});
        }
        native_submitted=true;
        scene.frame_complete=false;
        ++state.movie_draws;
        state.movie_pacing_active.store(true,std::memory_order_relaxed);
        if(REXCVAR_GET(edf_native_publish_frames) && scene.output.content_valid) {
          if(scene.output.surface)
            state.presentation_frames->Publish(*scene.output.surface.Get(),edf::native::NativeFrameKind::Movie,
              state.display_gamma?&*state.display_gamma:nullptr);
          else {
            edf::native::PublishSceneSharedLocked(state,scene.output,edf::native::NativeFrameKind::Movie);
            edf::native::SubmitSceneFrameLocked(state);
          }
        }
        if(state.movie_draws<=3 || state.movie_draws==30 || state.movie_draws==60 || state.movie_draws==120) {
          REXLOG_INFO("Native movie draw: submitted={}, PS={}, output_initialized={}, frame_complete=false",
            state.movie_draws,movie_pixel.shader().entry.name,scene.output.content_valid);
          const auto prefix=REXCVAR_GET(edf_native_scene_capture);
          if(!prefix.empty() && scene.output.content_valid) {
            const auto path=std::filesystem::path(prefix+".movie."+std::to_string(state.movie_draws)+".bmp");
            if(std::filesystem::exists(path)) throw std::runtime_error("native movie capture path already exists");
            const auto bytes=edf::native::CaptureOutputBmp(state,scene);
            std::ofstream output(path,std::ios::binary);
            output.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
            output.close();
            if(!output) throw std::runtime_error("native movie capture write failed");
          }
        }
      }
    } catch(const std::exception& error) {
      if(++state.movie_draw_errors<=10) REXLOG_ERROR("Native movie draw: {}",error.what());
    }
    bool xui_draw=false;
    const auto xui_scene_owner=state.active_scene?state.active_scene:state.active_output;
    if(!movie_draw && !state.active_target && state.scenes.contains(xui_scene_owner)) try {
      const auto& pair=bound_shaders();
      const auto xui_kind=pair_class().kind.xui;
      xui_draw=xui_kind!=PairClass::Xui::None;
      if(xui_draw) {
        const edf::native::GuestReader backing(base);
        const auto device=ctx.r3.u32;
        const edf::native::GuestReadWindow reader(backing,backing.Add(device,1024),12416-1024);
        const auto snapshot=edf::native::ReadAuditedXuiDeviceWords(reader,device);
        auto& scene=state.scenes.at(xui_scene_owner);
        const bool scene_draw=state.active_scene!=0;
        const auto expected_surface=scene_draw?scene.color_surface:scene.output_surface;
        auto& target=scene_draw?scene.color:scene.output;
        if(!expected_surface || snapshot.surface!=expected_surface || ctx.r4.u32!=4 || ctx.r7.u32!=8 ||
           !ctx.r5.u32 || ctx.r5.u32%3 || ctx.r5.u32>16384)
          throw std::runtime_error("unsupported XUI textured output/topology");
        const auto declaration=state.declarations.Get(state.declaration_bindings.at(device));
        if(declaration->count()!=1 ||
           declaration->Words<3>()!=std::array<uint32_t,3>{0,0x2c23a5,0})
          throw std::runtime_error("unsupported XUI position declaration");
        auto viewport=edf::native::DecodeDrawViewport(snapshot.viewport);
        if(edf::native::NativeRenderDimensions()[0]>0)
          viewport=edf::native::ScaleNativeCanvasScissor(viewport,float(target.sampled.width)/1280,
            float(target.sampled.height)/720,false);
        const auto key=snapshot.render;
        if(!scene_draw && (viewport.reverse_depth || (key[1]&3)))
          throw std::runtime_error("unimplemented XUI depth contract");
        const bool solid=xui_kind==PairClass::Xui::Solid;
        const bool mask=xui_kind==PairClass::Xui::Mask;
        const auto texture=state.textures.find(snapshot.texture);
        if(!solid && (texture==state.textures.end() || !texture->second.content_valid || !texture->second.backend))
          throw std::runtime_error("XUI textured brush has no native texture");
        if(!solid) {
          // Asked of the seam, not of a D3D11 view: a texture created on the
          // scene's own backend has no view to ask, and dereferencing one is a
          // null read rather than a refusal.
          if(texture->second.cube || !target.backend_surface ||
             texture->second.backend.get()==target.backend_surface->texture())
            throw std::runtime_error("unsupported or aliased XUI brush texture");
        }
        if(!state.xui_vertex) {
          const auto effect=edf::native::MakeNativeXuiTextureEffect();
          auto vertex=std::make_unique<edf::native::ShaderBindings>(state.device.Get(),
            edf::native::CompileNativeShader(state.device.Get(),effect,effect.entries[0],"native_xui.fx"));
          auto pixel=std::make_unique<edf::native::ShaderBindings>(state.device.Get(),
            edf::native::CompileNativeShader(state.device.Get(),effect,effect.entries[1],"native_xui.fx"));
          auto solid_pixel=std::make_unique<edf::native::ShaderBindings>(state.device.Get(),
            edf::native::CompileNativeShader(state.device.Get(),effect,effect.entries[2],"native_xui.fx"));
          auto mask_pixel=std::make_unique<edf::native::ShaderBindings>(state.device.Get(),
            edf::native::CompileNativeShader(state.device.Get(),effect,effect.entries[3],"native_xui.fx"));
          edf::native::ValidateNativeShaderLink(vertex->shader(),pixel->shader());
          edf::native::ValidateNativeShaderLink(vertex->shader(),solid_pixel->shader());
          edf::native::ValidateNativeShaderLink(vertex->shader(),mask_pixel->shader());
          auto vertices=std::make_unique<edf::native::PositionTriangleStream>(state.device.Get(),vertex->shader());
          state.xui_vertex_bindings.emplace(*vertex);
          state.xui_pixel_bindings[0].emplace(*pixel,false);
          state.xui_pixel_bindings[1].emplace(*solid_pixel,true);
          state.xui_pixel_bindings[2].emplace(*mask_pixel,false);
          state.xui_vertex=std::move(vertex); state.xui_pixel=std::move(pixel); state.xui_vertices=std::move(vertices);
          state.xui_solid_pixel=std::move(solid_pixel); state.xui_mask_pixel=std::move(mask_pixel);
        }
        if(viewport.reverse_depth && !state.xui_reversed_vertex) {
          const auto effect=edf::native::MakeNativeXuiTextureEffect();
          auto reversed=std::make_unique<edf::native::ShaderBindings>(state.device.Get(),
            edf::native::CompileNativeShader(state.device.Get(),effect,effect.entries[0],"native_xui.fx",true));
          state.xui_reversed_vertex_bindings.emplace(*reversed);
          state.xui_reversed_vertex=std::move(reversed);
        }
        auto& vertex=viewport.reverse_depth?*state.xui_reversed_vertex:*state.xui_vertex;
        auto& pixel=solid ? *state.xui_solid_pixel : mask ? *state.xui_mask_pixel : *state.xui_pixel;
        // Split into the three parts a batch would separate: converting guest
        // data, binding, and the draw itself. 2.87 million of these went
        // through here with no timing at all, so what they cost - and which
        // part of them is worth batching - was unknown.
        edf::native::HookTiming xui_total(edf::native::HookPhase::XuiNative);
        edf::native::HookTiming xui_decode(edf::native::HookPhase::XuiDecode);
        auto registers=[&](uint32_t offset,size_t bytes){
          return std::span<const uint8_t>{reader.Bytes(reader.Add(device,offset),bytes),bytes};};
        const auto vertex_registers=registers(1792,192);
        if(edf::native::NativeRenderDimensions()[0]>0 && state.xui_draws<5) {
          REXLOG_INFO("Native XUI resolution trace: viewport={},{} {}x{}, scissor={},{},{},{}, target={}x{}",
            viewport.viewport.TopLeftX,viewport.viewport.TopLeftY,viewport.viewport.Width,viewport.viewport.Height,
            viewport.scissor.left,viewport.scissor.top,viewport.scissor.right,viewport.scissor.bottom,
            target.sampled.width,target.sampled.height);
          for(uint32_t row=0;row<8;++row) {
            const auto offset=device+1792+row*16;
            REXLOG_INFO("Native XUI resolution matrix: row={}, values={},{},{},{}",row,
              std::bit_cast<float>(reader.Word(offset)),std::bit_cast<float>(reader.Word(offset+4)),
              std::bit_cast<float>(reader.Word(offset+8)),std::bit_cast<float>(reader.Word(offset+12)));
          }
        }
        const auto& vertex_plan=viewport.reverse_depth?*state.xui_reversed_vertex_bindings:*state.xui_vertex_bindings;
        const bool native_canvas=edf::native::NativeRenderDimensions()[0]>0;
        const float canvas_x=native_canvas?float(target.sampled.width)/1280.0f:1.0f;
        const float canvas_y=native_canvas?float(target.sampled.height)/720.0f:1.0f;
        // 2D canvas layout on an output that is not 16:9 (edf_hud_safe_area,
        // native_display_layout.h), per draw so a solid fade still covers the
        // frame; its scissor follows. XUI drawn into the HDR scene keeps the
        // legacy full-target mapping.
        edf::native::NativeClipAffine xui_layout;
        if(native_canvas && !scene_draw) {
          xui_layout=edf::native::NativeHudLayout(target.sampled.width,target.sampled.height);
          if(!xui_layout.identity()) {
            const size_t count=size_t(ctx.r5.u32)*8;
            xui_layout=edf::native::NativeCanvasDrawAffine(xui_layout,edf::native::NativeXuiClipBounds(vertex_registers,
              registers(5872,16),edf::native::NativeXuiCanvasProjection(vertex_registers.subspan(64,64),canvas_x,canvas_y),
              {reader.Bytes(ctx.r6.u32,count),count}),solid);
            viewport=edf::native::MapNativeCanvasViewportScissor(viewport,xui_layout,target.sampled.width,target.sampled.height);
          }
        }
        vertex_plan.SetConstants(vertex,vertex_registers,registers(5872,16),canvas_x,canvas_y,xui_layout);
        const auto& pixel_plan=*state.xui_pixel_bindings[solid?1:mask?2:0];
        pixel_plan.SetConstants(pixel,registers(5904,16),solid?registers(5888,16):std::span<const uint8_t>{});
        if(!solid) {
        pixel_plan.SetTexture(pixel,texture->second.backend);
        pixel_plan.SetSampler(pixel,edf::native::SamplerLocked(state,
          edf::native::SamplerStateKey(edf::native::ReadSamplerWords(reader,device,0))));
        }
        if(REXCVAR_GET(edf_native_batch_audit)) {
          // State a batch must share, and constants it would have to carry per
          // draw, hashed apart - so the answer says not just "could these
          // merge" but what a merge would have to do about their differences.
          const auto mix=[](uint64_t hash,uint64_t value) {
            hash^=value; return hash*1099511628211ull;
          };
          uint64_t shape=1469598103934665603ull;
          for(const auto word:key) shape=mix(shape,word);
          shape=mix(shape,uint64_t(solid?1:mask?2:0));
          shape=mix(shape,reinterpret_cast<uintptr_t>(texture==state.textures.end()?nullptr:texture->second.backend.get()));
          shape=mix(shape,uint64_t(viewport.reverse_depth));
          shape=mix(shape,uint64_t(viewport.viewport.Width)*8191+uint64_t(viewport.viewport.Height));
          uint64_t constants=1469598103934665603ull;
          for(const auto byte:vertex_registers) constants=mix(constants,byte);
          ++state.xui_batch_draws;
          if(shape==state.xui_last_state && state.xui_batch_draws>1) {
            ++state.xui_batch_run;
            ++state.xui_batch_collapsible;
            if(constants!=state.xui_last_constants) ++state.xui_constants_differ;
          } else {
            state.xui_batch_longest=(std::max)(state.xui_batch_longest,state.xui_batch_run);
            if(state.xui_batch_run) ++state.xui_batch_runs;
            state.xui_batch_run=1;
          }
          state.xui_last_state=shape;
          state.xui_last_constants=constants;
          // What differs from the draw before, part by part, and how often
          // only the vertex constants do: the draws transient batching appends
          // (nothing differs) and the ones only constants carried per vertex
          // could (only the vertex constants differ). The recorder also needs
          // equal scissor rectangles and samplers, which the shape above
          // leaves out, so they are counted here.
          uint64_t scissor=1469598103934665603ull;
          for(const auto value:{viewport.scissor.left,viewport.scissor.top,viewport.scissor.right,viewport.scissor.bottom})
            scissor=mix(scissor,uint64_t(uint32_t(value)));
          uint64_t pixel_constants=1469598103934665603ull;
          for(const auto byte:registers(5904,16)) pixel_constants=mix(pixel_constants,byte);
          if(solid) for(const auto byte:registers(5888,16)) pixel_constants=mix(pixel_constants,byte);
          uint64_t sampler=0;
          if(!solid) for(const auto word:edf::native::SamplerStateKey(edf::native::ReadSamplerWords(reader,device,0)))
            sampler=mix(sampler^1469598103934665603ull,word);
          uint64_t render_words=1469598103934665603ull;
          for(const auto word:key) render_words=mix(render_words,word);
          const std::array<uint64_t,9> parts{render_words,uint64_t(solid?1:mask?2:0),
            reinterpret_cast<uintptr_t>(texture==state.textures.end()?nullptr:texture->second.backend.get()),
            uint64_t(viewport.reverse_depth),uint64_t(viewport.viewport.Width)*8191+uint64_t(viewport.viewport.Height),
            scissor,sampler,pixel_constants,constants};
          if(state.xui_batch_draws>1) {
            bool state_differs=false;
            for(size_t part=0;part<parts.size();++part) if(parts[part]!=state.xui_audit_parts[part]) {
              ++state.xui_audit_breaks[part];
              if(part+1<parts.size()) state_differs=true;
            }
            if(!state_differs) {
              if(parts.back()==state.xui_audit_parts.back()) ++state.xui_audit_identical;
              else ++state.xui_audit_constants_only;
            }
          }
          state.xui_audit_parts=parts;
          if(state.xui_batch_draws%500000==0) {
            REXLOG_INFO("Native XUI batch audit: draws={}, runs={}, longest_run={}, collapsible={} ({:.1f}% share the state of the draw before), of those {} also change constants ({:.1f}%)",
              state.xui_batch_draws,state.xui_batch_runs,state.xui_batch_longest,
              state.xui_batch_collapsible,
              100.0*double(state.xui_batch_collapsible)/double(state.xui_batch_draws),
              state.xui_constants_differ,
              state.xui_batch_collapsible?100.0*double(state.xui_constants_differ)/double(state.xui_batch_collapsible):0.0);
            const auto& breaks=state.xui_audit_breaks;
            REXLOG_INFO("Native XUI batch breaks: draws={}, identical_to_previous={} (appendable), only_vertex_constants_differ={}, differs: render_state={}, pixel_shader={}, texture={}, reverse_depth={}, viewport={}, scissor={}, sampler={}, pixel_constants={}, vertex_constants={}",
              state.xui_batch_draws,state.xui_audit_identical,state.xui_audit_constants_only,
              breaks[0],breaks[1],breaks[2],breaks[3],breaks[4],breaks[5],breaks[6],breaks[7],breaks[8]);
          }
        }
        xui_decode.Finish();
        edf::native::HookTiming xui_bind(edf::native::HookPhase::XuiBind);
        auto& render=edf::native::RenderStateLocked(state,key);
        const bool xui_seam=EDF_NATIVE_FLAG(seam_draws);
        if(!xui_seam) {
          edf::native::BindActiveTarget(state);
          edf::native::BindGuestRenderState(render,*state.context.Get(),reader,ctx.r3.u32,&state.bind_generation); viewport.Bind(*state.context.Get());
          vertex.Bind(*state.context.Get()); pixel.Bind(*state.context.Get());
        }
        xui_bind.Finish();
        edf::native::HookTiming xui_draw(edf::native::HookPhase::XuiDraw);
        const size_t bytes=size_t(ctx.r5.u32)*8;
        if(xui_seam) {
          auto& recorder=edf::native::RecordDrawSetup(state,reader,ctx.r3.u32,{
            vertex,pixel,viewport,key,
            edf::native::PositionTriangleStream::Layout(),edf::native::kNativePositionLayoutId,
            // The reversed-depth variant is a different compiled shader under the
            // same guest handle, so it belongs in the identity: a cache keyed on
            // the handle alone hands back the pipeline built from the other one.
            (uint64_t(pair.vertex)<<1)|uint64_t(viewport.reverse_depth?1:0),
            pair.pixel,edf::native::NativeBackendTopology::TriangleList});
          state.xui_vertices->Draw(edf::native::EnsureSceneBackendLocked(state),recorder,
                                   {reader.Bytes(ctx.r6.u32,bytes),bytes});
        } else state.xui_vertices->Draw(*state.context.Get(),{reader.Bytes(ctx.r6.u32,bytes),bytes});
        xui_draw.Finish();
        native_submitted=true;
        // Partial alpha geometry cannot establish initialized full-frame pixels.
        // Preserve the preceding clear/post pass's initialization state.
        scene.frame_complete=false;
        if(++state.xui_draws<=5 || state.xui_draws%10000==0)
          REXLOG_INFO("Native XUI brush draw: submitted={}, PS={}, vertices={}, target_initialized={}, scene={}, reverse_depth={}",
            state.xui_draws,pixel.shader().entry.name,ctx.r5.u32,target.content_valid,scene_draw,viewport.reverse_depth);
      }
    } catch(const std::exception& error) {
      if(++state.xui_errors<=10) REXLOG_ERROR("Native XUI textured draw: {}",error.what());
    }
    bool font_draw=false;
    if(!movie_draw && !xui_draw && !state.active_target && state.scenes.contains(state.active_output)) try {
      const edf::native::GuestReader font_backing(base);
      const auto& pair=bound_shaders();
      // Shared declaration/VS/PS owned by retail font setup821AD3B8. Read
      // current handles rather than retaining identities across font releases.
      const auto font=edf::native::ReadGuestWords<3>(font_backing,0x8257C06C);
      font_draw=font[0] && font[1] && font[2] && pair.vertex==font[1] && pair.pixel==font[2];
      if(font_draw) {
        const auto device=ctx.r3.u32;
        const edf::native::GuestReadWindow reader(font_backing,font_backing.Add(device,1024),12416-1024);
        if(reader.Word(0x82556148)!=0x820179A8)
          throw std::runtime_error("unsupported font source pointer");
        const auto snapshot=edf::native::ReadAuditedXuiDeviceWords(reader,device);
        auto& scene=state.scenes.at(state.active_output);
        if(state.declaration_bindings.at(device)!=font[0] || snapshot.surface!=scene.output_surface ||
           ctx.r4.u32!=13 || ctx.r7.u32!=16 || !ctx.r5.u32 || ctx.r5.u32%4 || ctx.r5.u32>16384)
          throw std::runtime_error("unsupported font output/declaration/topology");
        const auto declaration=state.declarations.Get(font[0]);
        if(declaration->count()!=2 || !edf::native::IsFontVertexDeclaration(declaration->count(),
           declaration->Words<6>()))
          throw std::runtime_error("unsupported font vertex elements");
        auto viewport=edf::native::DecodeDrawViewport(snapshot.viewport);
        if(edf::native::NativeRenderDimensions()[0]>0)
          viewport=edf::native::ScaleNativeCanvasScissor(viewport,float(scene.output.sampled.width)/1280,
            float(scene.output.sampled.height)/720,true);
        const auto font_layout=edf::native::NativeHudLayout(scene.output.sampled.width,scene.output.sampled.height);
        viewport=edf::native::MapNativeCanvasViewportScissor(viewport,font_layout,
          scene.output.sampled.width,scene.output.sampled.height);
        const auto key=snapshot.render;
        if(viewport.reverse_depth || (key[1]&3))
          throw std::runtime_error("unimplemented font depth contract");
        const auto texture=state.textures.find(snapshot.texture);
        if(texture==state.textures.end() || !texture->second.content_valid || !texture->second.backend)
          throw std::runtime_error("font atlas has no native texture");
        // Asked of the seam; see the XUI brush above.
        if(texture->second.cube || !scene.output.backend_surface ||
           texture->second.backend.get()==scene.output.backend_surface->texture())
          throw std::runtime_error("unsupported or aliased font atlas");
        if(!state.font_vertex) {
          const auto effect=edf::native::MakeNativeFontEffect();
          auto vertex=std::make_unique<edf::native::ShaderBindings>(state.device.Get(),
            edf::native::CompileNativeShader(state.device.Get(),effect,effect.entries[0],"native_font.fx"));
          auto pixel=std::make_unique<edf::native::ShaderBindings>(state.device.Get(),
            edf::native::CompileNativeShader(state.device.Get(),effect,effect.entries[1],"native_font.fx"));
          edf::native::ValidateNativeShaderLink(vertex->shader(),pixel->shader());
          auto vertices=std::make_unique<edf::native::QuadStream>(state.device.Get(),vertex->shader());
          const edf::native::NativeFontBindings plan(*vertex,*pixel);
          state.font_bindings=plan;
          state.font_vertex=std::move(vertex); state.font_pixel=std::move(pixel);
          state.font_vertices=std::move(vertices);
        }
        // Font producer821ADCE8 writes c1 directly for each glyph run. The
        // native cbuffer packs float2 fields, unlike the guest's register slots.
        const std::span<const uint8_t> vs{reader.Bytes(reader.Add(device,1808),64),64};
        const std::span<const uint8_t> ps{reader.Bytes(reader.Add(device,5888),32),32};
        const bool font_canvas=edf::native::NativeRenderDimensions()[0]>0;
        if(font_canvas && state.font_draws<5) {
          REXLOG_INFO("Native font canvas trace: viewport={},{} {}x{}, scissor_enabled={}, scissor={},{},{},{}, offset={},{} scale={},{}",
            viewport.viewport.TopLeftX,viewport.viewport.TopLeftY,viewport.viewport.Width,viewport.viewport.Height,
            snapshot.viewport.scissor_enabled,viewport.scissor.left,viewport.scissor.top,viewport.scissor.right,viewport.scissor.bottom,
            std::bit_cast<float>(reader.Word(device+1840)),std::bit_cast<float>(reader.Word(device+1844)),
            std::bit_cast<float>(reader.Word(device+1856)),std::bit_cast<float>(reader.Word(device+1860)));
        }
        // Text follows the 2D canvas layout (native_display_layout.h); its
        // scissor was mapped with it above.
        state.font_bindings->SetConstants(*state.font_vertex,*state.font_pixel,vs,ps,
          font_canvas?float(scene.output.sampled.width)/1280.0f:1.0f,
          font_canvas?float(scene.output.sampled.height)/720.0f:1.0f,font_layout);
        state.font_bindings->SetTexture(*state.font_pixel,texture->second.backend);
        state.font_bindings->SetSampler(*state.font_pixel,edf::native::SamplerLocked(state,
          edf::native::SamplerStateKey(edf::native::ReadSamplerWords(reader,device,0))));
        auto& render=edf::native::RenderStateLocked(state,key);
        const size_t bytes=size_t(ctx.r5.u32)*16;
        if(EDF_NATIVE_FLAG(seam_draws)) {
          auto& recorder=edf::native::RecordDrawSetup(state,reader,ctx.r3.u32,{
            *state.font_vertex,*state.font_pixel,viewport,key,
            edf::native::QuadStream::Layout(),edf::native::kNativeQuadLayoutId,
            // The reversed-depth variant is a different compiled shader under the
            // same guest handle, so it belongs in the identity: a cache keyed on
            // the handle alone hands back the pipeline built from the other one.
            (uint64_t(pair.vertex)<<1)|uint64_t(viewport.reverse_depth?1:0),
            pair.pixel,edf::native::NativeBackendTopology::TriangleList});
          state.font_vertices->Draw(edf::native::EnsureSceneBackendLocked(state),recorder,
                                    {reader.Bytes(ctx.r6.u32,bytes),bytes});
        } else {
          edf::native::BindActiveTarget(state);
          edf::native::BindGuestRenderState(render,*state.context.Get(),reader,ctx.r3.u32,&state.bind_generation); viewport.Bind(*state.context.Get());
          state.font_vertex->Bind(*state.context.Get()); state.font_pixel->Bind(*state.context.Get());
          state.font_vertices->Draw(*state.context.Get(),{reader.Bytes(ctx.r6.u32,bytes),bytes});
        }
        native_submitted=true;
        scene.frame_complete=false;
        if(++state.font_draws<=5 || state.font_draws%10000==0)
          REXLOG_INFO("Native font draw: submitted={}, vertices={}, output_initialized={}",
            state.font_draws,ctx.r5.u32,scene.output.content_valid);
        const auto prefix=REXCVAR_GET(edf_native_scene_capture);
        if(!prefix.empty() && scene.output.content_valid &&
           (state.font_draws==1 || state.font_draws==100 || state.font_draws==1000)) {
          const auto path=std::filesystem::path(prefix+".font."+std::to_string(state.font_draws)+".bmp");
          if(std::filesystem::exists(path)) throw std::runtime_error("native font capture path already exists");
          const auto bytes=edf::native::CaptureOutputBmp(state,scene);
          std::ofstream output(path,std::ios::binary);
          output.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
          output.close();
          if(!output) throw std::runtime_error("native font capture write failed");
        }
      }
    } catch(const std::exception& error) {
      if(++state.font_errors<=10) REXLOG_ERROR("Native font draw: {}",error.what());
    }
    classify_timing.Finish();
    edf::native::HookTiming utility3d_timing(edf::native::HookPhase::ImmediateUtility3D);
    bool utility_3d_draw=false;
    if(!movie_draw && !xui_draw && !font_draw && state.active_scene && !state.active_target) try {
      const auto& pair=bound_shaders();
      const auto& classified=pair_class();
      if(classified.payload.vertex && classified.payload.pixel) {
        auto* const vertex=classified.payload.vertex;
        const auto& vs=vertex->bindings->shader();
        auto& ps=*classified.payload.pixel->bindings;
        const auto kind=classified.kind.utility3d;
        const bool solid=kind==PairClass::Utility3D::Solid;
        const bool textured=kind==PairClass::Utility3D::Textured;
        const bool particle=kind==PairClass::Utility3D::Particle || kind==PairClass::Utility3D::ZParticle;
        utility_3d_draw=solid || textured || particle;
        if(utility_3d_draw) {
          const edf::native::GuestReader backing(base);
          const edf::native::GuestReadWindow reader(backing,backing.Add(ctx.r3.u32,1024),12416-1024);
          const uint32_t stride=solid?16:particle?44:36;
          const uint32_t element_count=solid?2:particle?4:3;
          const bool strip=ctx.r4.u32==6;
          if(pair.vertex!=state.active_vertex || pair.pixel!=state.linked_pixel ||
             (solid?!strip:particle?ctx.r4.u32!=13:(!strip && ctx.r4.u32!=13)) ||
             ctx.r7.u32!=stride || ctx.r5.u32<3 || ctx.r5.u32>16384 || (!strip && ctx.r5.u32%4))
            throw std::runtime_error("unsupported native scene immediate contract");
          auto& scene=state.scenes.at(state.active_scene);
          if(!scene.color_surface || reader.Word(reader.Add(ctx.r3.u32,12168))!=scene.color_surface)
            throw std::runtime_error("native scene immediate surface mismatch");
          const auto declaration=state.declaration_bindings.at(ctx.r3.u32);
          const auto owned_declaration=state.declarations.Get(declaration);
          if(owned_declaration->count()!=element_count)
            throw std::runtime_error("unsupported Utility 3D declaration count");
          const auto* elements=owned_declaration->bytes().data();
          auto word=[&](size_t at){return edf::native::GuestBlockWord(elements+at);};
          const size_t color=(element_count-1)*12;
          if(word(0)!=0 || word(4)!=0x2a23b9 || (word(8)&0xffffff00)!=0 ||
             word(color)!=(solid?12u:particle?28u:20u) || word(color+4)!=(solid?0x182886u:0x1a23a6u) ||
             (word(color+8)&0xffffff00)!=0xa0000 ||
             (!solid && (word(12)!=12 || word(16)!=0x2c23a5 || (word(20)&0xffffff00)!=0x50000)) ||
             (particle && (word(24)!=20 || word(28)!=0x2c23a5 || (word(32)&0xffffff00)!=0x50100)))
            throw std::runtime_error("unsupported Utility 3D vertex layout");
          const auto viewport=edf::native::ReadNativeDrawViewport(reader,ctx.r3.u32);
          auto& bindings=edf::native::VertexBindingsForDraw(*vertex,viewport.reverse_depth);
          if(!bindings.HasAllTextureInputs() || !ps.HasAllTextureInputs())
            throw std::runtime_error("Utility 3D missing texture inputs");
          if(edf::native::SamplesTarget(bindings,scene.color) || edf::native::SamplesTarget(ps,scene.color))
            throw std::runtime_error("native scene immediate samples its target");
          // The one guest read of the vertices; recording takes host bytes.
          const size_t bytes=size_t(ctx.r5.u32)*stride;
          const std::span<const uint8_t> vertices{reader.Bytes(ctx.r6.u32,bytes),bytes};
          const auto key=edf::native::ReadAuditedRenderStateWords(reader,ctx.r3.u32);
          edf::native::RecordNativeSceneImmediate(state,reader,ctx.r3.u32,{
            bindings,ps,viewport,key,pair,declaration,element_count,owned_declaration,ctx.r4.u32,stride},vertices);
          native_submitted=true; scene.frame_complete=false;
          auto& reported=state.scene_immediate_variants_reported[solid?0:textured?(strip?1:2):kind==PairClass::Utility3D::ZParticle?4:3];
          if(++state.utility_3d_draws<=5 || !reported || state.utility_3d_draws%10000==0)
            REXLOG_INFO("Native scene immediate: submitted={}, vertices={}, reverse_depth={}, scene={:#x}, VS={}, PS={}, stride={}",
              state.utility_3d_draws,ctx.r5.u32,viewport.reverse_depth,state.active_scene,
              vs.entry.name,ps.shader().entry.name,stride);
          reported=true;
        }
      }
    } catch(const std::exception& error) {
      if(++state.utility_3d_errors<=10) REXLOG_ERROR("Native Utility 3D strip: {}",error.what());
    }
    utility3d_timing.Finish();
    edf::native::HookTiming immediate_tail_timing(edf::native::HookPhase::ImmediateTail);
    bool utility_draw=false;
    const auto utility_scene_owner=state.active_scene?state.active_scene:state.active_output;
    if(!movie_draw && !xui_draw && !font_draw && !state.active_target && state.scenes.contains(utility_scene_owner)) try {
      const auto& pair=bound_shaders();
      const auto& classified=pair_class();
      if(classified.payload.vertex && classified.payload.pixel) {
        auto* const vertex=classified.payload.vertex;
        auto& vs=*vertex->bindings; auto& ps=*classified.payload.pixel->bindings;
        const bool textured=classified.kind.utility2d==PairClass::Utility2D::Textured;
        utility_draw=classified.kind.utility2d!=PairClass::Utility2D::None;
        if(utility_draw) {
          const edf::native::GuestReader backing(base);
          const edf::native::GuestReadWindow reader(backing,backing.Add(ctx.r3.u32,1024),12416-1024);
          const auto snapshot=edf::native::ReadAuditedXuiDeviceWords(reader,ctx.r3.u32);
          auto& scene=state.scenes.at(utility_scene_owner);
          const bool scene_draw=state.active_scene!=0;
          const auto expected_surface=scene_draw?scene.color_surface:scene.output_surface;
          auto& target=scene_draw?scene.color:scene.output;
          const uint32_t stride=textured?20:12,element_count=textured?3:2;
          const bool lines=ctx.r4.u32==2;
          if(pair.vertex!=state.active_vertex || pair.pixel!=state.linked_pixel ||
             !expected_surface || snapshot.surface!=expected_surface ||
             (!lines && ctx.r4.u32!=13) || ctx.r7.u32!=stride || !ctx.r5.u32 || ctx.r5.u32%(lines?2:4) || ctx.r5.u32>16384)
            throw std::runtime_error("unsupported Utility output/binding/topology");
          const auto declaration_handle=state.declaration_bindings.at(ctx.r3.u32);
          const auto owned_declaration=state.declarations.Get(declaration_handle);
          if(owned_declaration->count()!=element_count)
            throw std::runtime_error("unsupported Utility declaration count");
          const auto* declaration=owned_declaration->bytes().data();
          auto word=[&](size_t offset){return edf::native::GuestBlockWord(declaration+offset);};
          std::shared_ptr<const edf::native::NativeDeclaration> native_declaration;
          try { native_declaration=owned_declaration->Utility2D(textured); }
          catch(const std::exception&) {
            if(state.utility_errors<10) for(uint32_t element=0;element<element_count;++element)
              REXLOG_INFO("Native Utility element: stride={}, element={}, offset={:#x}, type={:#x}, semantic={:#x}",
                stride,element,word(element*12),word(element*12+4),word(element*12+8));
            throw std::runtime_error("unsupported Utility vertex elements");
          }
          auto viewport=edf::native::DecodeDrawViewport(snapshot.viewport);
          if(edf::native::NativeRenderDimensions()[0]>0)
            viewport=edf::native::ScaleNativeCanvasScissor(viewport,float(edf::native::NativeRenderDimensions()[0])/1280,
              float(edf::native::NativeRenderDimensions()[1])/720,true);
          // Ordinary output has no depth attachment. The HDR scene does, and
          // uses the same reversed-clip shader contract as indexed scene draws.
          if(!scene_draw && (viewport.reverse_depth || (snapshot.render[1]&3)))
            throw std::runtime_error("unsupported Utility output depth contract");
          auto& bindings=edf::native::VertexBindingsForDraw(*vertex,viewport.reverse_depth);
          if(!bindings.HasAllTextureInputs()) throw std::runtime_error("Utility vertex shader has missing native texture inputs");
          if(!ps.HasAllTextureInputs()) throw std::runtime_error("Utility has missing native texture inputs");
          if(edf::native::SamplesTarget(ps,target) || edf::native::SamplesTarget(bindings,target))
            throw std::runtime_error("Utility samples its active surface");
          const auto owned_indices=state.generated_indices.Get(lines?edf::native::NativeIndexPattern::Lines:
            edf::native::NativeIndexPattern::Quads,ctx.r5.u32);
          const auto indices=owned_indices->bytes();
          const size_t bytes=size_t(ctx.r5.u32)*stride;
          const std::span<const uint8_t> vertices{reader.Bytes(ctx.r6.u32,bytes),bytes};
          // 2D canvas layout on an output that is not 16:9 (edf_hud_safe_area,
          // native_display_layout.h). The activation uploaded the legacy
          // full-target canvas; this draw re-maps the raw constants through
          // its own layout (a fade keeps covering the frame) and its scissor
          // with them. Draws into the HDR scene belong to the 3D view and
          // keep the legacy mapping.
          if(const auto& dims=edf::native::NativeRenderDimensions();dims[0]>0 && !scene_draw) {
            const auto layout=edf::native::NativeHudLayout(target.sampled.width,target.sampled.height);
            if(!layout.identity() && (vertex->canvas_uploaded&3)==3) {
              const float kx=float(dims[0])/1280.0f,ky=float(dims[1])/720.0f;
              const auto affine=edf::native::NativeCanvasDrawAffine(layout,edf::native::NativeCanvasVertexBounds(vertices,stride,
                edf::native::ScaleNativeCanvasXY(vertex->canvas_scale,kx,ky),
                edf::native::ScaleNativeCanvasXY(vertex->canvas_offset,kx,ky)),!textured);
              bindings.SetGuestFloatRegisters("_g_DX2DScale",edf::native::MapNativeCanvasXY(vertex->canvas_scale,kx,ky,affine,false));
              bindings.SetGuestFloatRegisters("_g_DX2DOffset",edf::native::MapNativeCanvasXY(vertex->canvas_offset,kx,ky,affine,true));
              viewport=edf::native::MapNativeCanvasViewportScissor(viewport,affine,target.sampled.width,target.sampled.height);
            }
          }
          auto& mesh=state.immediate_meshes.Acquire(edf::native::EnsureSceneBackendLocked(state),bindings.shader(),
            edf::native::ImmediateStreamKey(vertices.size(),declaration_handle,pair.vertex,ctx.r4.u32,viewport.reverse_depth),
            native_declaration->bytes(),stride,
            vertices,indices,2,native_declaration,owned_indices,{},{},{},{},0,{},
            // Where this mesh's dynamic vertices are rewritten when the draw
            // is recorded; the immediate context does it otherwise.
            EDF_NATIVE_FLAG(seam_draws)?&edf::native::SceneRecorderLocked(state):nullptr);
          auto& render=edf::native::RenderStateLocked(state,snapshot.render);
          if(EDF_NATIVE_FLAG(seam_draws)) {
            auto& recorder=edf::native::RecordDrawSetup(state,reader,ctx.r3.u32,{
              bindings,ps,viewport,snapshot.render,
              mesh.input_layout().elements(),mesh.input_layout().fingerprint(),
              // The reversed-depth variant is a different compiled shader under
              // the same guest handle, so it belongs in the identity.
              (uint64_t(pair.vertex)<<1)|uint64_t(viewport.reverse_depth?1:0),pair.pixel,
              lines?edf::native::NativeBackendTopology::LineList
                   :edf::native::NativeBackendTopology::TriangleList});
            // state.recorded.pipeline: the pipeline RecordDrawSetup just bound.
            if(NativeTransientBatchingEnabled() && state.recorded.pipeline)
              mesh.DrawTransientExpanded(recorder,*state.recorded.pipeline,vertices,0,uint32_t(indices.size()/2),
                lines?edf::native::NativeBackendTopology::LineList:edf::native::NativeBackendTopology::TriangleList);
            else if(lines) mesh.DrawLinesTransient(recorder,vertices,0,uint32_t(indices.size()/2));
            else mesh.DrawTransient(recorder,vertices,0,uint32_t(indices.size()/2));
          } else {
            edf::native::BindActiveTarget(state);
            edf::native::BindGuestRenderState(render,*state.context.Get(),reader,ctx.r3.u32,&state.bind_generation); viewport.Bind(*state.context.Get());
            bindings.Bind(*state.context.Get()); ps.Bind(*state.context.Get());
            if(lines) mesh.DrawLines(*state.context.Get(),0,uint32_t(indices.size()/2));
            else mesh.Draw(*state.context.Get(),0,uint32_t(indices.size()/2));
          }
          native_submitted=true; scene.frame_complete=false;
          auto& variant_reported=state.utility_variants_reported[(textured?2:0)+(lines?1:0)];
          if(++state.utility_draws<=5 || !variant_reported)
            REXLOG_INFO("Native Utility draw: submitted={}, VS={}, PS={}, vertices={}, lines={}, scene={}, reverse_depth={}",
              state.utility_draws,vs.shader().entry.name,ps.shader().entry.name,ctx.r5.u32,lines,scene_draw,viewport.reverse_depth);
          variant_reported=true;
        }
      }
    } catch(const std::exception& error) {
      if(++state.utility_errors<=10) REXLOG_ERROR("Native Utility quad: {}",error.what());
    }
    const bool output_candidate=!movie_draw && !xui_draw && !font_draw && !utility_draw && !state.active_target && state.scenes.contains(state.active_output) &&
      state.shaders.contains(state.linked_pixel) &&
      state.shaders.at(state.linked_pixel).bindings->shader().entry.name=="PS_Bloom";
    bool output_draw=false;
    if (output_candidate) {
      try {
        const edf::native::GuestReader reader(base);
        const auto& pair=bound_shaders();
        output_draw=pair.vertex==state.active_vertex &&
          pair.pixel==state.linked_pixel &&
          ctx.r4.u32==13 && ctx.r7.u32==16;
        if (!output_draw && ++state.output_unhandled<=5) {
          const auto actual_vs=pair.vertex;
          const auto actual_ps=pair.pixel;
          REXLOG_INFO("Native ordinary output unhandled draw: caller={:#x}, primitive={}, stride={}, VS={:#x}, PS={:#x}; UI coverage incomplete",
            uint32_t(ctx.lr),ctx.r4.u32,ctx.r7.u32,actual_vs,actual_ps);
          for (const auto handle:{actual_vs,actual_ps}) if (state.embedded_shaders.contains(handle)) {
            const auto identity=state.embedded_shaders.at(handle);
            REXLOG_INFO("Native embedded shader identity: handle={:#x}, source={:#x}, pixel={}",handle,identity.source,identity.pixel);
          }
          for(const auto handle:{actual_vs,actual_ps}) if(state.shaders.contains(handle)) {
            const auto& shader=state.shaders.at(handle).bindings->shader();
            REXLOG_INFO("Native source shader identity: handle={:#x}, entry={}, pixel={}, source_bytes={}, fingerprint={:#x}",
              handle,shader.entry.name,shader.entry.pixel,shader.source_bytes,shader.source_fingerprint);
          }
        }
      } catch (const std::exception& error) { REXLOG_ERROR("Native ordinary output selection: {}",error.what()); }
    }
    // Observe every unsupported output pair, not just draws following Bloom.
    // Otherwise an unimplemented menu shader can disappear without evidence.
    if(!movie_draw && !xui_draw && !font_draw && !utility_draw && !output_draw && !state.active_target &&
       state.scenes.contains(state.active_output) && state.unsupported_output_pairs.size()<64) try {
      const auto& pair=bound_shaders();
      const std::array<uint32_t,4> key{pair.vertex,pair.pixel,ctx.r4.u32,ctx.r7.u32};
      edf::native::ReportNativeContractRejection(state,edf::native::MakeNativeContract(state,
        edf::native::NativeContractPath::Output,pair.vertex,pair.pixel,0,ctx.r4.u32,ctx.r7.u32),
        "no native implementation for this output shader pair");
      if(state.unsupported_output_pairs.insert(key).second) {
        REXLOG_INFO("Native unsupported output shader pair: caller={:#x}, VS={:#x}, PS={:#x}, primitive={}, stride={}, vertices={}",
          uint32_t(ctx.lr),pair.vertex,pair.pixel,ctx.r4.u32,ctx.r7.u32,ctx.r5.u32);
        for(const auto handle:{pair.vertex,pair.pixel}) {
          if(const auto found=state.embedded_shaders.find(handle);found!=state.embedded_shaders.end())
            REXLOG_INFO("Native unsupported embedded source: handle={:#x}, source={:#x}, pixel={}",
              handle,found->second.source,found->second.pixel);
          if(const auto found=state.shaders.find(handle);found!=state.shaders.end()) {
            const auto& shader=found->second.bindings->shader();
            REXLOG_INFO("Native unsupported material source: handle={:#x}, entry={}, fingerprint={:#x}",
              handle,shader.entry.name,shader.source_fingerprint);
          }
        }
      }
    } catch(const std::exception& error) {
      if(++state.output_unhandled<=5) REXLOG_ERROR("Native unsupported output inspection: {}",error.what());
    }
    if (state.render_targets.contains(state.active_target) || output_draw) {
      ++state.immediate_draws;
      try {
        const edf::native::GuestReader backing(base);
        const edf::native::GuestReadWindow reader(backing,backing.Add(ctx.r3.u32,1024),12416-1024);
        if (output_draw && reader.Word(reader.Add(ctx.r3.u32,12168))!=state.scenes.at(state.active_output).output_surface)
          throw std::runtime_error("ordinary output does not match guest bound surface");
        const auto declaration = state.declaration_bindings.at(ctx.r3.u32);
        const auto owned_declaration=state.declarations.Get(declaration);
        const auto count = owned_declaration->count();
        if (state.immediate_draws <= 5) REXLOG_INFO("Native immediate draw: primitive={}, vertices={}, stride={}, declaration={:#x}, elements={}",
                    ctx.r4.u32,ctx.r5.u32,ctx.r7.u32,declaration,count);
        if(state.immediate_draws<=5) for (uint32_t i = 0; i < count; ++i) {
          const auto element = owned_declaration->Words<3>(i*12);
          REXLOG_INFO("Native immediate element: stream_offset={:#x}, type={:#x}, method_usage_index={:#x}",
                      element[0],element[1],element[2]);
        }
        if (ctx.r4.u32 != 13 || ctx.r7.u32 != 16 || !ctx.r5.u32 || ctx.r5.u32 % 4 || ctx.r5.u32 > 16384 || count != 2 ||
            owned_declaration->Words<6>()!=std::array<uint32_t,6>{0,0x2c23a5,0,8,0x2c23a5,0x50000})
          throw std::runtime_error("unsupported native immediate vertex declaration/topology");
        const auto found = state.shaders.find(state.active_vertex);
        if (found == state.shaders.end()) throw std::runtime_error("missing native immediate vertex shader");
        const auto& pair=bound_shaders();
        if (pair.vertex!=state.active_vertex || pair.pixel!=state.linked_pixel)
          throw std::runtime_error("native immediate shader pair differs from guest device");
        auto& shader = found->second;
        const size_t bytes = size_t(ctx.r5.u32)*16;
        const auto render_key=edf::native::ReadAuditedRenderStateWords(reader,ctx.r3.u32);
        auto render_state = state.render_states.find(render_key);
        if (render_state == state.render_states.end()) {
          auto native = edf::native::CreateNativeRenderState(state.device.Get(),render_key);
          render_state = state.render_states.emplace(render_key,std::move(native)).first;
          REXLOG_INFO("Native render state: cached={}, blend={:#x}, depth={:#x}, raster={:#x}, alpha={:#x}, write_mask={}, scissor={}",
                      state.render_states.size(),render_key[0],render_key[1],render_key[2],render_key[3],render_key[4],render_key[5]);
        }
        const bool post_seam=EDF_NATIVE_FLAG(seam_draws);
        if(!post_seam)
          edf::native::BindGuestRenderState(render_state->second,*state.context.Get(),reader,ctx.r3.u32,&state.bind_generation);
        const auto viewport=edf::native::ReadNativeDrawViewport(reader,ctx.r3.u32);
        if (state.immediate_draws<=15) {
          REXLOG_INFO("Native post pass: VS={}, PS={}, viewport={},{},{}x{}, depth={}..{}, reverse={}",
            shader.bindings->shader().entry.name,state.shaders.at(state.linked_pixel).bindings->shader().entry.name,
            viewport.viewport.TopLeftX,viewport.viewport.TopLeftY,viewport.viewport.Width,viewport.viewport.Height,
            viewport.viewport.MinDepth,viewport.viewport.MaxDepth,viewport.reverse_depth);
          for (uint32_t i=0;i<4;++i) {
            const auto vertex=reader.Add(ctx.r6.u32,i*16);
            REXLOG_INFO("Native post vertex: index={}, position={},{} UV={},{}",i,
              std::bit_cast<float>(reader.Word(vertex)),std::bit_cast<float>(reader.Word(reader.Add(vertex,4))),
              std::bit_cast<float>(reader.Word(reader.Add(vertex,8))),std::bit_cast<float>(reader.Word(reader.Add(vertex,12))));
          }
        }
        if(!post_seam) viewport.Bind(*state.context.Get());
        auto& bindings=edf::native::VertexBindingsForDraw(shader,viewport.reverse_depth);
        auto& quads=viewport.reverse_depth ? shader.reversed_quads : shader.quads;
        if (!quads) quads=std::make_unique<edf::native::QuadStream>(state.device.Get(),bindings.shader());
        const std::span<const uint8_t> vertices{reader.Bytes(ctx.r6.u32,bytes),bytes};
        auto& pixel=*state.shaders.at(state.linked_pixel).bindings;
        if(!post_seam) { bindings.Bind(*state.context.Get()); pixel.Bind(*state.context.Get()); }
        auto& target=output_draw ? state.scenes.at(state.active_output).output : state.render_targets.at(state.active_target).native;
        if(auto* finish_audit=edf::native::post_finish_recorder; finish_audit && finish_audit->seen.native_armed) {
          // Finish audit: the extent this pass really draws to, and the tone
          // constants the live native bindings carry (no guest setter writes them).
          try {
            std::array<float,3> tone{NAN,NAN,NAN};
            const std::array<const char*,3> names{"g_PostEffect_MiddleGray","g_PostEffect_LuminanceWhite","g_PostEffect_ToneMap"};
            for(size_t c=0;c<names.size();++c)
              if(const auto value=pixel.ReadFloatVector(names[c]);!value.empty()) tone[c]=value[0];
            finish_audit->seen.NativeDraw(int32_t(target.sampled.width),int32_t(target.sampled.height),tone);
          } catch(const std::exception& error) { finish_audit->Fail(error); }
        }
        // Read the actual post-pass views before drawing, not a guessed target
        // from the resource registry after later passes may have changed it.
        const auto post_prefix=REXCVAR_GET(edf_native_output_capture_scene_color)
          ? REXCVAR_GET(edf_native_scene_capture) : std::string{};
        const auto post_frame=state.indexed_output_frames+1;
        if((output_draw || state.active_target) && pixel.shader().source_fingerprint==0x6b7926f9747c6933ull &&
           REXCVAR_GET(edf_native_output_capture_scene_color) && !post_prefix.empty() &&
           edf::native::NativeSceneDrew(state.indexed_submitted,state.scene_indexed_start,state.scene_full_frame) &&
           edf::native::ShouldCaptureNativeOutput(post_frame,state.output_captures,
             REXCVAR_GET(edf_native_output_capture_limit),REXCVAR_GET(edf_native_output_capture_interval),
             REXCVAR_GET(edf_native_output_capture_start_frame)) &&
           (state.post_input_capture_frame!=post_frame || state.post_input_capture_pass<32)) {
          if(state.post_input_capture_frame!=post_frame) {
            state.post_input_capture_frame=post_frame; state.post_input_capture_pass=0;
          }
          const auto pass=++state.post_input_capture_pass;
          try {
            REXLOG_INFO("Native post chain pass: frame={}, pass={}, shader={}, target={}x{}",
              post_frame,pass,pixel.shader().entry.name,target.sampled.width,target.sampled.height);
            for(const auto* name:{"m_DiffuseTexture0_Sampler","m_DiffuseTexture1_Sampler","m_Tone_Sampler","m_OldTone_Sampler"}) {
              auto* bound=pixel.ReadTexture(name);
              if(!bound) continue; // A pass only reflects the inputs it consumes.
              auto* view=edf::native::NativeD3D11TextureView(*bound);
              auto* texture=edf::native::NativeD3D11TextureResource(*bound);
              if(!view || !texture) continue; // A D3D11-only diagnostic.
              D3D11_SHADER_RESOURCE_VIEW_DESC view_desc{}; view->GetDesc(&view_desc);
              if(view_desc.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D || view_desc.Texture2D.MostDetailedMip!=0)
                throw std::runtime_error("unsupported post diagnostic view");
              D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
              const auto value=edf::native::ReadNativeColorPixel(*state.context.Get(),*texture,desc.Width/2,desc.Height/2);
              REXLOG_INFO("Native exact post input: frame={}, pass={}, name={}, {}x{}, view_format={}, center={},{},{},{}",
                post_frame,pass,name,desc.Width,desc.Height,uint32_t(view_desc.Format),value[0],value[1],value[2],value[3]);
              // The saved BMP clamps to [0,1]; the tone curve's behaviour depends
              // on whether anything actually exceeds 1. Report the real range.
              const auto hdr=edf::native::InspectNativeHdrColor(*state.context.Get(),*texture);
              REXLOG_INFO("Native exact post input range: frame={}, pass={}, name={}, pixels={}, nonfinite={}, min={},{},{}, max={},{},{}, mean={},{},{}, above_1={}, above_2={}, above_4={}, above_8={}",
                post_frame,pass,name,hdr.pixels,hdr.nonfinite_pixels,
                hdr.minimum[0],hdr.minimum[1],hdr.minimum[2],hdr.maximum[0],hdr.maximum[1],hdr.maximum[2],
                hdr.mean[0],hdr.mean[1],hdr.mean[2],hdr.above[0],hdr.above[1],hdr.above[2],hdr.above[3]);
              if(hdr.negative_pixels)
                REXLOG_WARN("Native exact post input negatives: frame={}, pass={}, name={}, negative_pixels={} of {}, worst_at={},{} (negative radiance; the tone curve maps a large negative to white)",
                  post_frame,pass,name,hdr.negative_pixels,hdr.pixels,hdr.worst_x,hdr.worst_y);
              const auto path=std::filesystem::path(post_prefix+".pass."+std::to_string(pass)+"."+pixel.shader().entry.name+".input."+name+"."+std::to_string(post_frame)+".bmp");
              if(std::filesystem::exists(path)) throw std::runtime_error("post input capture already exists");
              const auto bmp=edf::native::CaptureNativeHdrBmp(*state.context.Get(),*texture);
              std::ofstream file(path,std::ios::binary);
              file.write(reinterpret_cast<const char*>(bmp.data()),bmp.size()); file.close();
              if(!file) throw std::runtime_error("post input capture write failed");
            }
            for(const auto* name:{"g_PostEffect_MiddleGray","g_PostEffect_LuminanceWhite",
                                  "g_PostEffect_ToneMap"}) {
              const auto value=pixel.ReadFloatVector(name);
              if(!value.empty()) REXLOG_INFO("Native exact post constant: frame={}, pass={}, {}={}",post_frame,pass,name,value[0]);
            }
            // The sampling geometry decides whether a reduction averages the
            // intended footprint and whether a blur preserves its input's mean.
            // Report the authored values, not an assumed normalized kernel.
            if(const auto offsets=pixel.ReadFloatArray("m_DownsampleUVOffset");!offsets.empty()) {
              std::string text;
              for(size_t i=0;i+1<offsets.size();i+=2)
                text+=std::format("{}({},{})",text.empty()?"":" ",offsets[i],offsets[i+1]);
              REXLOG_INFO("Native exact post downsample offsets: frame={}, pass={}, count={}, uv={}",
                post_frame,pass,offsets.size()/2,text);
            }
            if(const auto taps=pixel.ReadFloatArray("m_GaussBlurUVOffset");!taps.empty()) {
              float weight_sum=0;
              std::string text;
              for(size_t i=0;i+2<taps.size();i+=3) {
                weight_sum+=taps[i+2];
                text+=std::format("{}({},{})*{}",text.empty()?"":" ",taps[i],taps[i+1],taps[i+2]);
              }
              REXLOG_INFO("Native exact post blur kernel: frame={}, pass={}, taps={}, weight_sum={}, offsets={}",
                post_frame,pass,taps.size()/3,weight_sum,text);
            }
          } catch(const std::exception& error) {
            // Optional diagnostics must never suppress the real draw.
            REXLOG_ERROR("Native post input capture: {}",error.what());
          }
        }
        const bool initialized=edf::native::CanInitializeReductionTarget(bindings.shader(),pixel.shader(),
          vertices,viewport,render_key,target.sampled.width,target.sampled.height,pixel.HasAllTextureInputs());
        // The offset follows the guest's live PA_SU_VTX_CNTL rather than a
        // fixed assumption, exactly as the SDK's own GPU path selects it. Only
        // the audited full-screen post passes consume it so far: indexed world
        // geometry, font, movie and the remaining immediate draws each need
        // their own validation before the same offset is extended to them.
        const auto centers_word=edf::native::ReadVertexCenterWord(reader,ctx.r3.u32);
        if(state.vertex_center_word!=centers_word) {
          state.vertex_center_word=centers_word;
          const auto centers=edf::native::DecodeGuestVertexCenters(centers_word);
          REXLOG_INFO("Native guest vertex centers: PA_SU_VTX_CNTL={:#x}, integer_centers={}, rounding={}, quantization={}, enabled={}",
            centers_word,centers.integer_centers,centers.rounding,centers.quantization,
            REXCVAR_GET(edf_native_pixel_centers));
        }
        const float center_offset=REXCVAR_GET(edf_native_pixel_centers)
          ? edf::native::GuestPixelCenterOffset(centers_word) : 0.f;
        const bool shift_centers=initialized && center_offset!=0.f;
        // The shadow render's guest post (edf_native_shadow_render) holds the
        // tone history: the target already holds this frame's value from the
        // native post, so the 0.025-per-draw blend is not applied twice.
        const bool shadow_tone_hold=native_shadow_guest && !output_draw && target.content_valid &&
          pixel.shader().entry.name=="PS_Downsample_Tone";
        if(shadow_tone_hold) ++native_shadow_guest->tone_holds;
        else if(post_seam) {
          auto shifted=viewport;
          if(shift_centers) {
            shifted.viewport.TopLeftX+=center_offset;
            shifted.viewport.TopLeftY+=center_offset;
          }
          auto& recorder=edf::native::RecordDrawSetup(state,reader,ctx.r3.u32,{
            bindings,pixel,shifted,render_key,
            edf::native::QuadStream::Layout(),edf::native::kNativeQuadLayoutId,
            (uint64_t(pair.vertex)<<1)|uint64_t(viewport.reverse_depth?1:0),pair.pixel,
            edf::native::NativeBackendTopology::TriangleList});
          quads->Draw(edf::native::EnsureSceneBackendLocked(state),recorder,vertices);
        } else {
          if(shift_centers) {
            auto shifted=viewport.viewport;
            shifted.TopLeftX+=center_offset; shifted.TopLeftY+=center_offset;
            state.context->RSSetViewports(1,&shifted);
          }
          try { quads->Draw(*state.context.Get(),vertices); }
          catch(...) { if(shift_centers) viewport.Bind(*state.context.Get()); throw; }
          if(shift_centers) viewport.Bind(*state.context.Get());
        }
        if(output_draw && state.scene_gpu_timer && state.scene_gpu_timer_resolved &&
           state.scene_gpu_timer_owner==state.active_output) {
          try { state.scene_gpu_timer->End(); }
          catch(const std::exception& error) {
            state.scene_gpu_timer.reset();
            REXLOG_ERROR("Native frame GPU timing end: {}",error.what());
          }
          state.scene_gpu_timer_owner=0;
          state.scene_gpu_timer_resolved=false;
        }
        if(state.scene_gpu_timer_owner && state.scene_gpu_timer_resolved &&
           pixel.shader().entry.name=="PS_Downsample_Tone") {
          for(const auto* name:{"g_PostEffect_MiddleGray","g_PostEffect_ToneMap"}) {
            const auto value=pixel.ReadFloatVector(name);
            if(!value.empty()) REXLOG_INFO("Native tone history constant: {}={}, scene={}",name,value[0],state.scene_begins);
          }
        }
        native_submitted=true;
        ++state.native_quad_draws;
        if(!shadow_tone_hold) target.content_valid=initialized;
        if (output_draw && ++state.output_draws<=5)
          REXLOG_INFO("Native final bloom: draws={}, initialized={}, frame_complete=false",state.output_draws,initialized);
        const auto bloom_now=std::chrono::steady_clock::now();
        if (output_draw && (state.output_draws<=5 ||
            (!REXCVAR_GET(edf_native_scene_capture).empty() &&
             bloom_now-state.bloom_parameters_reported>=std::chrono::seconds(10)))) {
          state.bloom_parameters_reported=bloom_now;
          for (const auto* name:{"g_PostEffect_MiddleGray","g_PostEffect_LuminanceWhite"}) {
            const auto value=pixel.ReadFloatVector(name);
            if (!value.empty()) REXLOG_INFO("Native bloom constant: {}={}, indexed_frame={}, output_draw={}",
              name,value[0],state.indexed_output_frames,state.output_draws);
          }
        }
        if (state.native_quad_draws <= 15 || state.native_quad_draws % 1000 == 0)
          REXLOG_INFO("Native immediate draw: submitted={}, errors={}, contents_valid={}",state.native_quad_draws,state.native_quad_errors,initialized);
      } catch (const std::exception& error) {
        ++state.native_quad_errors;
        if (output_draw) state.scenes.at(state.active_output).output.content_valid=false;
        else state.render_targets.at(state.active_target).native.content_valid=false;
        if (state.native_quad_errors <= 10) REXLOG_ERROR("Native immediate draw: {}",error.what());
      }
    }
    ++state.immediate_requests;
    if(!ctx.r5.u32) ++state.immediate_empty;
    else if(native_submitted) ++state.immediate_submitted;
    else {
      ++state.immediate_unsubmitted;
      try {
        const edf::native::GuestReader reader(base);
        edf::native::ReportNativeContractRejection(state,edf::native::MakeNativeContract(state,
          edf::native::NativeContractPath::Immediate,state.active_vertex,state.linked_pixel,
          reader.Word(reader.Add(ctx.r3.u32,11536)),ctx.r4.u32,ctx.r7.u32),
          "immediate draw not submitted natively");
      } catch(const std::exception&) {
        edf::native::ReportNativeContractRejection(state,edf::native::MakeNativeContract(state,
          edf::native::NativeContractPath::Immediate,state.active_vertex,state.linked_pixel,
          0,ctx.r4.u32,ctx.r7.u32),"immediate draw not submitted natively; device unreadable");
      }
      // This is evidence of routing coverage, not automatically a pixel bug:
      // the guest may issue draws outside a valid target or during teardown.
      if(state.immediate_unsubmitted_paths.size()<64) {
        const std::array<uint32_t,5> key{uint32_t(ctx.lr),ctx.r4.u32,ctx.r7.u32,state.active_vertex,state.linked_pixel};
        if(state.immediate_unsubmitted_paths.insert(key).second) {
          REXLOG_INFO("Native immediate unsubmitted path: caller={:#x}, primitive={}, stride={}, vertices={}, active_VS={:#x}, active_PS={:#x}, target={:#x}, output={:#x}",
            key[0],key[1],key[2],ctx.r5.u32,key[3],key[4],state.active_target,state.active_output);
          try {
            const edf::native::GuestReader reader(base);
            const auto surface=reader.Word(reader.Add(ctx.r3.u32,12168));
            uint32_t scene_owner=0;
            for(const auto& [owner,scene]:state.scenes)
              if(scene.output_surface==surface) {scene_owner=owner; break;}
            REXLOG_INFO("Native unsubmitted target: guest_surface={:#x}, registered_surface={}, known_output_owner={:#x}, scene_count={}",
              surface,state.surface_creations.contains(surface),scene_owner,state.scenes.size());
            REXLOG_INFO("Native unsubmitted scene: active_scene={:#x}",state.active_scene);
            const auto pair=edf::native::ReadShaderPair(reader,ctx.r3.u32);
            const auto draw_view=edf::native::ReadNativeDrawViewport(reader,ctx.r3.u32);
            const auto render_words=edf::native::ReadAuditedRenderStateWords(reader,ctx.r3.u32);
            REXLOG_INFO("Native unsubmitted bindings: VS={:#x}, PS={:#x}, reverse_depth={}, depth_state={:#x}",
              pair.vertex,pair.pixel,draw_view.reverse_depth,render_words[1]);
            for(auto handle:{pair.vertex,pair.pixel}) {
              const auto embedded=state.embedded_shaders.find(handle);
              if(embedded!=state.embedded_shaders.end())
                REXLOG_INFO("Native unsubmitted embedded shader: handle={:#x}, source={:#x}, pixel={}",
                  handle,embedded->second.source,embedded->second.pixel);
              const auto found=state.shaders.find(handle);
              if(found!=state.shaders.end()) {
                const auto& shader=found->second.bindings->shader();
                REXLOG_INFO("Native unsubmitted shader: handle={:#x}, entry={}, fingerprint={:#x}",
                  handle,shader.entry.name,shader.source_fingerprint);
              }
            }
            const auto declaration=reader.Word(reader.Add(ctx.r3.u32,11536));
            const auto count=reader.Word(reader.Add(declaration,24));
            if(count<=16) for(uint32_t element=0;element<count;++element) {
              const auto words=edf::native::ReadGuestWords<3>(reader,reader.Add(declaration,52+element*12));
              REXLOG_INFO("Native unsubmitted element: element={}, offset={:#x}, type={:#x}, semantic={:#x}",
                element,words[0],words[1],words[2]);
            }
          } catch(const std::exception& error) {
            REXLOG_INFO("Native unsubmitted target inspection: {}",error.what());
          }
        }
      }
    }
    const auto now=std::chrono::steady_clock::now();
    if(state.immediate_requests>=1024 && now-state.immediate_coverage_reported>=std::chrono::seconds(10)) {
      state.immediate_coverage_reported=now;
      REXLOG_INFO("Native immediate coverage: requests={}, empty={}, submitted={}, unsubmitted={}, sampled_paths={} (64-path cap; not visual completeness)",
        state.immediate_requests,state.immediate_empty,state.immediate_submitted,state.immediate_unsubmitted,state.immediate_unsubmitted_paths.size());
      REXLOG_INFO("Native immediate mesh cache: builds={}, hits={}, updates={}, bytes={}, entries={}, entry_evictions={}, budget_evictions={}",
        state.immediate_meshes.builds(),state.immediate_meshes.hits(),state.immediate_meshes.updates(),state.immediate_meshes.bytes(),
        state.immediate_meshes.entries(),state.immediate_meshes.entry_evictions(),state.immediate_meshes.budget_evictions());
    }
  }
  const bool native_host=EDF_NATIVE_FLAG(host);
  std::optional<edf::native::NativeConstantOwnership> legacy_scope;
  if(!native_host) legacy_scope.emplace(0);
  native_timing.Finish();
  edf::native::HookTiming guest_timing(edf::native::HookPhase::ImmediateGuest);
  if(native_host) __imp__edf_native_immediate_cpu_tail(ctx,base);
  else __imp__sub_821FD8F8(ctx,base);
}

REX_EXTERN(__imp__sub_8213C328);
REX_EXTERN(__imp__sub_8213ECB0);
REX_EXTERN(__imp__edf_native_main_state_cpu_tail);
REX_HOOK_RAW(sub_8213ECB0) {
  if(!edf::native::NativeConstantOwnership::OwnsMainStatePackets(ctx.r3.u32,uint32_t(ctx.lr))) {
    __imp__sub_8213ECB0(ctx,base); return;
  }
  __imp__edf_native_main_state_cpu_tail(ctx,base);
  static thread_local uint64_t omitted=0;
  if(++omitted<=3) REXLOG_INFO("Native main-state ownership: retained CPU updates/dirty mask; omitted inline Xbox packets");
}
REX_EXTERN(__imp__sub_8213E950);
REX_EXTERN(__imp__edf_native_shader_upload_cpu_tail);
REX_HOOK_RAW(sub_8213E950) {
  if(!edf::native::NativeConstantOwnership::OwnsShaderUpload(ctx.r3.u32,uint32_t(ctx.lr))) {
    __imp__sub_8213E950(ctx,base); return;
  }
  __imp__edf_native_shader_upload_cpu_tail(ctx,base);
  static thread_local uint64_t omitted=0;
  if(++omitted<=3) REXLOG_INFO("Native shader-upload ownership: retained CPU metadata; omitted Xbox allocation/copy/packets");
}
REX_EXTERN(__imp__sub_8213E800);
REX_EXTERN(__imp__edf_native_shader_output_cpu_tail);
REX_HOOK_RAW(sub_8213E800) {
  if(!edf::native::NativeConstantOwnership::OwnsShaderOutputPatch(ctx.r29.u32,uint32_t(ctx.lr))) {
    __imp__sub_8213E800(ctx,base); return;
  }
  // E950 keeps its device in r29 at this exact call. Preserve E800's CPU output
  // through r6, but do not rewrite Xbox instructions via E678/E748.
  __imp__edf_native_shader_output_cpu_tail(ctx,base);
  static thread_local uint64_t omitted=0;
  if(++omitted<=3) REXLOG_INFO("Native shader-output ownership: retained CPU output; omitted Xbox instruction patches");
}
REX_EXTERN(__imp__sub_8213E070);
REX_HOOK_RAW(sub_8213E070) {
  if(!edf::native::NativeConstantOwnership::OwnsShaderMicrocodePatch(ctx.r6.u32,uint32_t(ctx.lr))) {
    __imp__sub_8213E070(ctx,base); return;
  }
  // Recovered native HLSL/input layouts replace the Xbox instruction patcher.
  // Keep EB68/E950's surrounding shader cache/stride metadata updates intact.
  static thread_local uint64_t omitted=0;
  if(++omitted<=3) REXLOG_INFO("Native shader-patch ownership: omitted Xbox microcode rewrite");
}
REX_EXTERN(__imp__sub_8213D750);
REX_EXTERN(__imp__edf_native_derived_cpu_tail);
REX_HOOK_RAW(sub_8213D750) {
  if(!edf::native::NativeConstantOwnership::OwnsDerivedStatePackets(ctx.r3.u32,uint32_t(ctx.lr))) {
    __imp__sub_8213D750(ctx,base); return;
  }
  // Keep the exact CPU state calculation, flag transitions and dirty-mask return.
  // Only the command-buffer rollover and packet write are removed by extraction.
  __imp__edf_native_derived_cpu_tail(ctx,base);
  static thread_local uint64_t omitted=0;
  if(++omitted<=3) REXLOG_INFO("Native derived-state ownership: preserved CPU updates; omitted Xbox packet block");
}
REX_EXTERN(__imp__sub_8213EAB0);
REX_HOOK_RAW(sub_8213EAB0) {
  if(!edf::native::NativeConstantOwnership::OwnsShaderLoadPackets(ctx.r3.u32,uint32_t(ctx.lr))) {
    __imp__sub_8213EAB0(ctx,base); return;
  }
  // Native-host tails retain ECB0's CPU updates regardless of draw success, but
  // don't copy the guest program relocation table into Xenos load packets.
  static thread_local uint64_t omitted=0;
  if(++omitted<=3) REXLOG_INFO("Native shader-load ownership: omitted Xbox program-load packets");
}

REX_EXTERN(__imp__sub_8213D938);
REX_HOOK_RAW(sub_8213D938) {
  if(!edf::native::NativeConstantOwnership::OwnsSpecialRenderPacket(
      ctx.r3.u32,uint32_t(ctx.lr),ctx.r4.u64,ctx.r6.u32)) {
    __imp__sub_8213D938(ctx,base); return;
  }
  // Both simple and tiled branches write only packets/cursor40. Preserve the
  // exact dirty-mask return used by the CPU prefix's subsequent bank dispatch.
  ctx.r3.u64=ctx.r4.u64 & ~uint64_t(0x100);
  static thread_local uint64_t omitted=0;
  if(++omitted<=3) REXLOG_INFO("Native special-render ownership: omitted Xbox packets; preserved dirty-mask return");
}

REX_HOOK_RAW(sub_8213C328) {
  if(!edf::native::NativeConstantOwnership::OwnsImmediateAllocation(
      ctx.r3.u32,uint32_t(ctx.lr),ctx.r4.u32,ctx.r5.u32)) {
    __imp__sub_8213C328(ctx,base);
    return;
  }
  // FD428 has already applied derived CPU state, cleared dirty banks and
  // restored its temporary stride byte. Returning no guest storage takes its
  // existing no-buffer exit, preserving cursor40 while bypassing GPU packets.
  // FD8F8 then skips its redundant vertex memcpy and cursor13076 commit.
  // The native draw already succeeded. Do NOT run the allocator's failure path
  // or set device+10809's allocation-failure bit; no allocation was attempted.
  // GPU-only restore bit4096 and scratch fields13076/13080/13088 remain untouched.
  ctx.r3.u64=0;
  static thread_local uint64_t removed=0;
  if(++removed<=3)
    REXLOG_INFO("Native immediate ownership: omitted Xbox vertex allocation/copy and draw packets");
}

REX_EXTERN(__imp__sub_8213DF00);
REX_HOOK_RAW(sub_8213DF00) {
  if(!edf::native::NativeConstantOwnership::Owns(ctx.r3.u32,ctx.r5.u32,ctx.r6.u32)) {
    __imp__sub_8213DF00(ctx,base);
    return;
  }
  // The original groups dirty float4 registers into GPU packet payloads and
  // advances cursor40. It never updates the source constants. Native-host
  // mode has no GPU emulator consuming this packet allocation/copy.
  // The caller still owns its CPU dirty-mask clear and all other derived state.
  static thread_local uint64_t removed=0;
  if(++removed<=3)
    REXLOG_INFO("Native constant ownership: omitted Xbox float-constant packet encoding, bank={:#x}",ctx.r5.u32);
}

REX_EXTERN(__imp__sub_8213DB60);
REX_HOOK_RAW(sub_8213DB60) {
  if(!edf::native::NativeConstantOwnership::OwnsRenderWords(ctx.r3.u32,ctx.r5.u32,ctx.r6.u32,ctx.r4.u64)) {
    __imp__sub_8213DB60(ctx,base);
    return;
  }
  // Pure packet copy in the audited native CPU-tail render-state path.
  // Preserve all CPU source words; the original caller clears its dirty mask.
  // Unknown banks/extents and all nonnative draws continue through the encoder.
  static thread_local uint64_t removed=0;
  if(++removed<=3)
    REXLOG_INFO("Native render ownership: omitted Xbox state packet encoding, dirty={:#x}",ctx.r4.u64);
}

REX_EXTERN(__imp__sub_8213DDA0);
REX_EXTERN(__imp__sub_8213DC20);
REX_HOOK_RAW(sub_8213DC20) {
  if(!edf::native::NativeConstantOwnership::OwnsVectorStatePackets(ctx.r3.u32,ctx.r4.u64)) {
    __imp__sub_8213DC20(ctx,base); return;
  }
  // Preserve CPU plane/control records and the caller's dirty clear. This
  // removes only the redundant GPU packet copy, not native clipping behavior.
  static thread_local uint64_t omitted=0;
  if(++omitted<=3) REXLOG_INFO("Native vector-state ownership: omitted Xbox packet encoding");
}

REX_HOOK_RAW(sub_8213DDA0) {
  if(!edf::native::NativeConstantOwnership::OwnsFetchWords(ctx.r3.u32,ctx.r4.u64)) {
    __imp__sub_8213DDA0(ctx,base);
    return;
  }
  // DDA0 reads device+1024 descriptors and writes only command packets/cursor40.
  // Its caller retains descriptor updates and the device+16 dirty-mask clear.
  // Native-host mode consumes neither the packet payloads nor GPU cache packets,
  // including when native submission failed and was recorded as unsubmitted.
  static thread_local uint64_t removed=0;
  if(++removed<=3)
    REXLOG_INFO("Native fetch ownership: omitted Xbox descriptor packet encoding, dirty={:#x}",ctx.r4.u64);
}

namespace edf::native {
namespace {
// The scene publication, queue, ownership and preload flags a native pass
// drawing published scene state depends on; the first one off, or null.
const char* NativeScenePassMissingFlag() {
  const std::pair<const char*,bool> required[]{
    {"edf_native_frame_dispatch",EDF_NATIVE_FLAG(frame_dispatch)},
    {"edf_native_scene_tree",EDF_NATIVE_FLAG(scene_tree)},
    {"edf_native_scene_tree_published",EDF_NATIVE_FLAG(scene_tree_published)},
    {"edf_native_scene_queued",EDF_NATIVE_FLAG(scene_queued)},
    {"edf_native_scene_visibility",EDF_NATIVE_FLAG(scene_visibility)},
    {"edf_native_scene_sources_owned",EDF_NATIVE_FLAG(scene_sources_owned)},
    {"edf_native_scene_membership_owned",EDF_NATIVE_FLAG(scene_membership_owned)},
    {"edf_native_scene_selection_owned",EDF_NATIVE_FLAG(scene_selection_owned)},
    {"edf_native_scene_camera_owned",EDF_NATIVE_FLAG(scene_camera_owned)},
    {"edf_native_scene_geometry_owned",EDF_NATIVE_FLAG(scene_geometry_owned)},
    {"edf_native_scene_material_owned",EDF_NATIVE_FLAG(scene_material_owned)},
    {"edf_native_scene_preload",EDF_NATIVE_FLAG(scene_preload)},
    {"edf_native_scene_group_order",EDF_NATIVE_FLAG(scene_group_order)}};
  for(const auto& [name,enabled]:required) if(!enabled) return name;
  return nullptr;
}
}
bool NativeStaticWorldPassEnabled() {
  if(!EDF_NATIVE_FLAG(static_world_pass)) return false;
  if(const auto* name=NativeScenePassMissingFlag()) {
    static std::atomic<bool> reported=false;
    if(!reported.exchange(true))
      REXLOG_INFO("Native static world pass disabled: requires {} (original group traversal retained)",name);
    return false;
  }
  return true;
}
bool NativeModelPassEnabled() {
  if(!EDF_NATIVE_FLAG(model_pass)) return false;
  const char* name=!EDF_NATIVE_FLAG(model_publication)?"edf_native_model_publication":
    !EDF_NATIVE_FLAG(shader_bridge)?"edf_native_shader_bridge":NativeScenePassMissingFlag();
  if(name) {
    static std::atomic<bool> reported=false;
    if(!reported.exchange(true))
      REXLOG_INFO("Native model pass disabled: requires {} (original model draws retained)",name);
    return false;
  }
  return true;
}
}
namespace edf::native {
void ResolveNativeRendererPreset() {
  const auto& name=REXCVAR_GET(edf_native_renderer);
  const auto preset=ParseNativeRendererPreset(name);
  if(!preset) throw std::runtime_error("edf_native_renderer must be off, world, full or native, not '"+std::string(name)+"'");
  const auto& scene_backend=REXCVAR_GET(edf_native_scene_backend);
  const auto resolved=ResolveNativeRendererPresetForBackend(*preset,scene_backend);
  if(resolved.backend_fallback)
    REXLOG_WARN("Native renderer: preset {} needs the d3d12 scene backend (the full frame has never run on '{}'); using preset off, the guest renderer",
      std::string(name),std::string(scene_backend));
  native_renderer_preset_mask.store(NativeRendererPresetMask(resolved.preset),std::memory_order_relaxed);
  // Effective values, in NativeRendererFlag order.
  const bool effective[kNativeRendererFlagCount]{
    EDF_NATIVE_FLAG(host),EDF_NATIVE_FLAG(shader_bridge),EDF_NATIVE_FLAG(seam_draws),
    EDF_NATIVE_FLAG(material_activation),EDF_NATIVE_FLAG(scene_queued),EDF_NATIVE_FLAG(scene_preload),
    EDF_NATIVE_FLAG(scene_sources_owned),EDF_NATIVE_FLAG(scene_membership_owned),
    EDF_NATIVE_FLAG(scene_selection_owned),EDF_NATIVE_FLAG(scene_camera_owned),
    EDF_NATIVE_FLAG(scene_geometry_owned),EDF_NATIVE_FLAG(scene_material_owned),
    EDF_NATIVE_FLAG(scene_tree),EDF_NATIVE_FLAG(scene_tree_published),EDF_NATIVE_FLAG(scene_visibility),
    EDF_NATIVE_FLAG(frame_dispatch),EDF_NATIVE_FLAG(scene_group_order),EDF_NATIVE_FLAG(static_world_pass),
    EDF_NATIVE_FLAG(model_publication),EDF_NATIVE_FLAG(model_pass),EDF_NATIVE_FLAG(post_finish),
    EDF_NATIVE_FLAG(full_frame)};
  std::string on,off;
  for(uint32_t i=0;i<kNativeRendererFlagCount;++i) {
    auto& list=effective[i]?on:off;
    if(!list.empty()) list+=',';
    list+=kNativeRendererFlagNames[i];
  }
  REXLOG_INFO("Native renderer: preset={}{} on=[{}] off=[{}]",name.empty()?std::string("off"):std::string(name),
    resolved.backend_fallback?" (off on this scene backend)":"",on,off);
}
namespace {
void LogNativeStaticWorldGroup(uint32_t group,uint64_t recorded) {
  // Same line and sampling as the guest group hook; a native group enters no boundary.
  static std::atomic<uint64_t> shapes=0;
  const auto count=++shapes;
  if(count<=4 || !(count&(count-1)))
    REXLOG_INFO("Native static group execution: group={:#x} completed=true recorded_batches={} compatibility_calls=0 boundary_mask={:#x} occurrences={}",
      group,recorded,0u,count);
}
// Handoff bind effects of one owed group, keyed like the group material cache:
// the published group material (its program and constants generation; held,
// so neither address is reused while cached), for this instance and device.
// Built once per key; unreadable effects return null and the group binds
// through ActivateNativeMaterial, which reports the failure as before.
std::shared_ptr<const NativeStaticWorldBindEffects> NativeStaticWorldBindEffectsFor(const GuestReader& reader,
    uint32_t instance,uint32_t device,const std::shared_ptr<const NativeSceneGroupMaterial>& material) {
  if(!material) return nullptr;
  struct Entry {
    std::shared_ptr<const NativeSceneGroupMaterial> material;
    std::shared_ptr<const NativeSceneMaterialProgram> program;
    std::shared_ptr<const NativeStaticWorldBindEffects> effects;
  };
  static std::mutex mutex;
  static std::map<std::pair<uint32_t,uint32_t>,Entry> cache;
  const std::pair key{instance,device};
  {
    std::lock_guard lock(mutex);
    const auto found=cache.find(key);
    if(found!=cache.end() && found->second.material==material && found->second.program==material->program)
      return found->second.effects;
  }
  std::shared_ptr<const NativeStaticWorldBindEffects> effects;
  try { effects=std::make_shared<const NativeStaticWorldBindEffects>(ReadNativeStaticWorldBindEffects(reader,instance,device)); }
  catch(const std::exception&) { return nullptr; }
  std::lock_guard lock(mutex);
  if(cache.size()>=16384) cache.clear();
  cache.insert_or_assign(key,Entry{material,material->program,effects});
  return effects;
}
// Retirement allocation and legacy tag of a composed bind, from the frames the
// setters get under ActivateNativeMaterial's 144-byte frame below `frame`: the
// texture setter's 160 bytes (tag at its r1-80), the shader setters' 128 (r1-48).
uint32_t ReserveNativeHandoffRetirement(const PPCContext& frame,uint8_t* base,uint32_t device,NativeStaticWorldRetirement kind) {
  const bool texture=kind==NativeStaticWorldRetirement::Texture;
  const uint32_t below=144+(texture?160:128);
  auto work=frame;
  if(work.r1.u32<below) throw std::runtime_error("invalid native handoff retirement stack");
  work.r1.u64=work.r1.u32-below; work.r3.u64=device;
  work.lr=texture?0x8213BBE0:kind==NativeStaticWorldRetirement::Pixel?0x82149664:0x82149924;
  EnterNativeSceneBoundary(NativeSceneBoundary::RetirementAllocation);
  sub_82141440(work,base);
  return work.r3.u32;
}
uint32_t NativeHandoffRetirementTag(const GuestReader& reader,const PPCContext& frame,NativeStaticWorldRetirement kind) {
  const uint32_t below=144+(kind==NativeStaticWorldRetirement::Texture?80:48);
  if(frame.r1.u32<below) throw std::runtime_error("invalid native handoff retirement tag");
  return reader.Word(frame.r1.u32-below);
}
}
// Replaces 821C3BB8(owner+240) for one world owner. Native groups draw from the
// scene publication with explicit pass inputs; the device state they owe is
// handed off before any guest group and at the end, so later stages inherit
// what the sequential guest groups would have left. Guest calls run unlocked:
// the bridge locks are taken once per contiguous run of native groups and
// released before every handoff (its activations run guest code) and every
// fallback group. A group's pass-invariant work - eligibility, the chained
// pass state, the resolved and interned material, its pipeline and world
// binding - comes from NativeStaticWorldGroupCache while its key matches; the
// key's comment lists why every input of that work is compared or signalled.
// Per frame only the instances' worlds, the camera and the queues change.
void RenderNativeStaticWorldPass(PPCContext& ctx,uint8_t* base,uint32_t owner) {
  const GuestReader reader(base);
  static NativeStaticWorldPassCounters counters;
  using GroupCache=NativeStaticWorldGroupCache<NativeStaticPassView,std::optional<NativeStaticGroupMaterial>>;
  // Read and written only under state.mutex.
  static GroupCache cache;
  static NativeSceneInstanceReuse reuse;
  const auto publication=native_scene_publication;
  auto* queues=native_scene_queues;
  const auto order=native_scene_pass_camera?NativeStaticWorldOrder(publication.get(),owner,queues):nullptr;
  if(!order) {
    const auto count=++counters.original;
    if(count<=4 || count%1000==0)
      REXLOG_INFO("Native static world pass: original traversal owner={:#x} count={} (no publication, group order, camera or native queues)",owner,count);
    auto work=ctx; work.r3.u64=reader.Add(owner,240); work.lr=0x820B434C; sub_821C3BB8(work,base);
    return;
  }
  HookTiming timing(HookPhase::RenderQueued);
  auto frame=ctx;
  if(frame.r1.u32<112) throw std::runtime_error("invalid native static world pass stack");
  frame.r1.u64=frame.r1.u32-112;
  reader.StoreWord(frame.r1.u32,ctx.r1.u32);
  const auto device=reader.Word(reader.Add(reader.Word(0x8257BFB4),8));
  auto& state=State();
  // One acquisition per run of native groups (submission gate, then registry).
  std::unique_lock run_submission(state.submissions,std::defer_lock);
  std::unique_lock run_lock(state.mutex,std::defer_lock);
  const auto lock_run=[&] { if(!run_lock.owns_lock()) { run_submission.lock(); run_lock.lock(); } };
  const auto unlock_run=[&] { if(run_lock.owns_lock()) { run_lock.unlock(); run_submission.unlock(); } };
  // Pass state and view come from the device once per pass and again after
  // each guest group; native groups chain it and never read bound state.
  NativeScenePassCursorState cursor;
  const auto load_pass=[&] {
    NativeSceneMaterialPassState pass;
    pass.render=ReadNativeMaterialRenderPass(reader,device);
    for(uint32_t slot=0;slot<16;++slot) pass.samplers[slot]=ReadNativeMaterialSamplerPass(reader,device,slot);
    std::lock_guard lock(state.mutex);
    cursor=NativeScenePassCursorState{device,pass.Inputs(),ReadNativeDrawViewportWords(reader,device),ActiveTargetsLocked(state)};
  };
  load_pass();
  struct Owed { uint32_t material; std::shared_ptr<const NativeSceneMaterialProgram> program; std::shared_ptr<const NativeSceneGroupMaterial> published; };
  struct World { uint32_t vertex; VertexParameterRange parameter; std::array<uint8_t,64> bytes; };
  std::vector<Owed> owed;
  std::optional<World> world;
  NativeMaterialRenderPass owed_start;
  const auto handoff=[&] {
    // Ends the run: the activations below are guest code.
    unlock_run();
    if(owed.empty()) return;
    HookTiming handoff_timing(HookPhase::QueuedHandoff);
    std::vector<NativeStaticWorldHandoffGroup> groups;
    std::vector<std::shared_ptr<const NativeStaticWorldBindEffects>> effects;
    groups.reserve(owed.size()); effects.reserve(owed.size());
    for(const auto& group:owed) {
      uint32_t slots=0;
      for(const auto& operation:group.program->sampler_operations) slots|=1u<<operation.slot;
      effects.push_back(NativeStaticWorldBindEffectsFor(reader,group.material,device,group.published));
      groups.push_back({group.program->inputs.state_overrides,slots,effects.back().get()});
    }
    auto work=frame;
    const auto replays=counters.replays,binds=counters.binds;
    const auto writes=HandOffNativeStaticWorld(reader,device,owed_start,cursor.material.samplers,groups,[&](size_t index) {
      // 821B94E8 through the activation hook: CPU program, host shader
      // bindings and setter publications, exactly as the guest group would.
      HookTiming replay_timing(HookPhase::QueuedHandoffReplays);
      work.r3.u64=owed[index].material; work.lr=0x821D979C; sub_821B94E8(work,base);
      ++counters.replays;
    },[&](size_t index) {
      // A group without composable effects: its binds and uploads through the
      // activation, so intermediate textures and shaders retire and registers
      // only it wrote hold its values.
      HookTiming bind_timing(HookPhase::QueuedHandoffBinds);
      ActivateNativeMaterial(work,base,owed[index].material,device,false);
      ++counters.binds;
    },[&](NativeStaticWorldRetirement kind) { return ReserveNativeHandoffRetirement(frame,base,device,kind); },
      [&](NativeStaticWorldRetirement kind) { return NativeHandoffRetirementTag(reader,frame,kind); });
    counters.composed+=owed.size()-(counters.replays-replays)-(counters.binds-binds);
    if(writes.render!=cursor.material.render) throw std::runtime_error("native static world handoff diverged from the pass cursor");
    for(const auto offset:writes.operations) {
      const auto setter=NativeMaterialStateSetter(offset);
      if(offset==0x44) {
        std::lock_guard lock(state.mutex);
        state.render_state_snapshots.PublishBlend(device,ReadGuestWords<4>(reader,reader.Add(device,10336)),setter);
      } else if(offset!=0x64) PublishNativeRenderState(base,device,setter);
    }
    {
      std::lock_guard submission(state.submissions);
      std::lock_guard lock(state.mutex);
      if(world) {
        // The last group's final instance owns g_mWorld after its activation defaults.
        auto& shader=state.shaders.at(world->vertex);
        RestoreNativeSceneWorld(reader,device,world->parameter.first,world->bytes,[&](const auto& restored) {
          shader.bindings->PatchGuestFloatRegisters(world->parameter.normal,0,restored);
          if(!shader.reversed_mirrors) shader.reversed_bindings->PatchGuestFloatRegisters(world->parameter.reversed,0,restored);
        });
      }
      state.recorded={}; ++state.bind_generation;
    }
    if(REXCVAR_GET(edf_native_material_state_audit) || REXCVAR_GET(edf_native_material_sampler_audit)) {
      NativeSceneMaterialPassState actual;
      actual.render=ReadNativeMaterialRenderPass(reader,device);
      for(uint32_t slot=0;slot<16;++slot) actual.samplers[slot]=ReadNativeMaterialSamplerPass(reader,device,slot);
      actual=actual.Inputs();
      if(actual!=cursor.material) {
        REXLOG_ERROR("Native static world handoff mismatch: owner={:#x} render_equal={} samplers_equal={}",
          owner,actual.render==cursor.material.render,actual.samplers==cursor.material.samplers);
        throw std::runtime_error("native static world handoff differs from the pass cursor");
      }
    }
    owed.clear(); world.reset(); ++counters.handoffs;
  };
  const auto report=[](const std::string& reason) {
    static std::set<std::string> reported;
    if(reported.size()<32 && reported.insert(reason).second) REXLOG_INFO("Native static world group declined: {}",reason);
  };
  const auto native=[&](uint32_t group) -> std::optional<NativeStaticWorldFallback> {
    using F=NativeStaticWorldFallback;
    // 821D96D8 drains a non-empty guest queue; an empty one draws nothing.
    if(reader.Word(reader.Add(group,4))!=reader.Word(reader.Add(group,8))) return F::GuestQueue;
    if(!queues->Contains(group)) { ++counters.empty_groups; LogNativeStaticWorldGroup(group,0); return {}; }
    lock_run();
    HookTiming eligibility_timing(HookPhase::QueuedEligibility);
    std::shared_ptr<const NativeSceneGroupMaterial> material;
    std::optional<NativeSceneGeometrySource> setup;
    std::shared_ptr<const NativeIndexedMesh::RetainedDraw> retained;
    for(const auto& published:publication->group_materials) if(published->group==group) { material=published; break; }
    if(const auto* source=NativeSceneSourcesForPass(state).FindGroup(group)) {
      const auto latest=state.scene_adapter.GroupGeometry(group,source->revision);
      for(const auto& published:publication->group_geometry)
        if(published==latest && published->group==group && published->setup &&
           published->geometry->backend()==state.scene_backend.get()) { setup=published->setup; retained=published->geometry; break; }
    }
    if(!material || !material->program || !setup) return F::Program;
    const auto& program=*material->program;
    if(!program.CanDeferCpuActivation()) return F::Scissor;
    GroupCache::Key key{.group=group,.device=device,.vertex=program.inputs.vertex,.pixel=program.inputs.pixel,
      .program=material->program,.setup=*setup,.geometry=retained,.backend=state.scene_backend,
      .pass=cursor.material,.view=NativeStaticPassView{*cursor.viewport,cursor.targets},
      .filtering=REXCVAR_GET(edf_native_anisotropic_filtering),.shaders=state.shader_registry_generation};
    // Miss diagnostic: why a group with an entry missed, per component, plus
    // whether the published object or the stack moved (observed, not keyed).
    using M=NativeStaticWorldMiss;
    uint32_t missed=0;
    std::optional<uint32_t> changed_read;
    std::string changed_constant;
    if(const auto* existing=cache.Find(group)) {
      missed=NativeStaticWorldKeyDifferences(existing->key,key);
      if(existing->observed.stack!=frame.r1.u32) missed|=NativeStaticWorldMissBit(M::Stack);
      if(existing->observed.published!=material) missed|=NativeStaticWorldMissBit(M::Published);
    } else missed=NativeStaticWorldMissBit(M::Absent);
    // A matching entry's eligibility holds while every guest byte its
    // assessment recorded still holds what it read and its per-frame witness
    // (stack, fence, retirement slots) holds now; otherwise assess again,
    // recording what this assessment reads.
    auto* candidate=cache.Candidate(key);
    if(candidate && !candidate->reads.Unchanged(reader)) {
      missed|=NativeStaticWorldMissBit(M::Reads); changed_read=candidate->reads.FirstChange(reader); candidate=nullptr;
    } else if(candidate && !candidate->eligibility.Holds(reader,frame.r1.u32)) {
      missed|=NativeStaticWorldMissBit(M::Witness); candidate=nullptr;
    }
    NativeRecordedReads reads;
    NativeStaticEligibilityWitness witness;
    if(!candidate && AssessNativeStaticGroup(NativeRecordingReader(reader,reads),device,frame.r1.u32,*setup,&witness)!=
       NativeStaticGroupEligibility::Supported) return F::Eligibility;
    eligibility_timing.Finish();
    HookTiming resolve_timing(HookPhase::QueuedResolve);
    NativeSceneMaterialPassState next;
    auto constants=material->constants;
    try {
      // After and its decode read only the incoming pass and the program.
      if(candidate) next=candidate->next;
      else { next=cursor.material.After(program); DecodeNativeRenderState(next.render.words); }
      for(auto& constant:constants) {
        if(native_scene_pass_camera->Apply(constant) || !constant.global ||
           (constant.name!="m_WaterTime" && constant.name!="g_SignalBrightness")) continue;
        if(!native_scene_pass_animation) throw std::runtime_error("missing native animation pass");
        native_scene_pass_animation->Apply(constant);
      }
    } catch(const std::exception& error) { report(error.what()); return F::PassState; }
    // The entry's material is this pass's when the constants match but for
    // those feeding only the camera, which is derived from them into its
    // capture. A miss resolves into fresh with the first instance.
    std::optional<NativeStaticGroupMaterial> fresh;
    auto* shared=&fresh;
    M stale=M::Constants;
    size_t differed=0;
    if(candidate && candidate->material && GroupCache::Current(*candidate,constants,
       candidate->material->resolved.capture.material.get(),candidate->material->resolved.capture.camera,&stale,&differed)) {
      shared=&candidate->material; ++cache.hits;
    } else {
      ++cache.misses;
      if(candidate) {
        missed|=NativeStaticWorldMissBit(stale);
        if(differed<constants.size()) changed_constant=constants[differed].name;
      }
      const auto events=cache.Missed(missed);
      if(events<=4 || events%10000==0) {
        std::string reasons;
        for(size_t i=0;i<kNativeStaticWorldMissNames.size();++i)
          if(missed>>i&1) { if(!reasons.empty()) reasons+=','; reasons+=kNativeStaticWorldMissNames[i]; }
        std::string read="-";
        if(changed_read) read=*changed_read-device<0x4000?std::format("device+{:#x}",*changed_read-device):std::format("{:#x}",*changed_read);
        REXLOG_INFO("Native static world cache miss: events={} group={:#x} reasons=[{}] read={} constant={} totals: {}",
          events,group,reasons,read,changed_constant.empty()?std::string("-"):changed_constant,cache.MissSummary());
      }
    }
    resolve_timing.Finish();
    // Resolve every instance before recording: a decline returns the whole
    // group, with its selections restored, to the guest callback.
    const auto instances=queues->Take(group);
    uint64_t batches=0;
    std::vector<NativeStaticInstanceWorld> resolved;
    NativeStaticGroupResolution resolution;
    const NativeStaticPassInputs pass{device,constants,cursor.material.render,cursor.material.samplers,
      NativeStaticPassView{*cursor.viewport,cursor.targets}};
    {
      HookTiming instances_timing(HookPhase::QueuedInstances);
      // One group resolve: its instances share geometry, program and these
      // pass inputs, so they share one material and view and record as
      // instanced draws. Per instance only its source, world and object.
      try {
        const auto decline=ResolveNativeStaticGroupInstances(instances,resolved,[&] {
          resolution=ResolveNativePublishedStaticGroupLocked(state,reader,*publication,
            {group,setup->count,material,setup},instances,pass,nullptr,*shared,true);
          return resolution.decline;
        },[&](uint32_t instance) {
          return ResolveNativePublishedStaticInstanceWorldLocked(state,reader,*publication,resolution,instance,device,&reuse);
        });
        if(const auto* reason=NativeStaticWorldDeclineReason(decline)) report(reason);
      } catch(const std::exception& error) { report(error.what()); resolved.clear(); }
    }
    if(resolved.size()!=instances.size() || !resolution.material) {
      for(auto at=instances.rbegin();at!=instances.rend();++at) queues->Push(group,*at);
      return F::Instance;
    }
    // A miss resolved under exactly this key (its geometry is the one the
    // instances resolved against). The eligibility reads are this frame's, or
    // the candidate's, just revalidated.
    if(shared==&fresh && fresh && fresh->geometry==retained.get()) {
      const auto captured=fresh->resolved.capture.material;
      const auto camera=fresh->resolved.capture.camera;
      if(candidate) { reads=std::move(candidate->reads); witness=std::move(candidate->eligibility); }
      auto& stored=cache.Store(std::move(key),constants,std::move(reads),std::move(witness),next,std::move(fresh),captured.get(),camera,
        {frame.r1.u32,material});
      // fresh moved into the entry: the resolution's material is the entry's.
      resolution.material=&*stored.material;
    }
    NativeQueuedSceneGroup batch;
    batch.targets=cursor.targets;
    batch.objects.reserve(resolved.size());
    const auto draws=state.scene_native_draws;
    // The shared capture gives every instance one view, so this is one flush.
    try {
      HookTiming record_timing(HookPhase::QueuedRecord);
      batch.view=resolution.view;
      for(const auto& result:resolved) batch.objects.push_back(result.object);
      FlushNativeQueuedSceneLocked(state,batch);
    }
    catch(const std::exception& error) {
      // Nothing of this group is owed to the guest yet: hand its selections back.
      report(error.what());
      ++state.bind_generation; state.recorded={};
      for(auto at=instances.rbegin();at!=instances.rend();++at) queues->Push(group,*at);
      return F::Instance;
    }
    // Each flush already advanced bind_generation and cleared recorded.
    batches=batch.execution.recordings();
    counters.draws+=state.scene_native_draws-draws;
    // Keep the guest path's lookup hint current with what this frame drew.
    state.scene_adapter.RememberGroupMaterial(group,resolution.material->resolved.capture.material);
    world=World{material->program->inputs.vertex,*resolution.material->world_parameter,resolved.back().world};
    if(owed.empty()) owed_start=cursor.material.render;
    owed.push_back({setup->material,material->program,material});
    cursor.material=std::move(next);
    ++counters.native_groups; counters.instances+=instances.size(); counters.batches+=batches;
    LogNativeStaticWorldGroup(group,batches);
    return {};
  };
  const auto fallback=[&](uint32_t group,NativeStaticWorldFallback reason) {
    unlock_run();
    const auto count=++counters.fallbacks[size_t(reason)];
    if(count<=4 || !(count&(count-1)))
      REXLOG_INFO("Native static world fallback: group={:#x} reason={} count={}",group,kNativeStaticWorldFallbackNames[size_t(reason)],count);
    NativeMaterialPassCursor guest;
    struct RestoreMaterialPass {
      NativeMaterialPassCursor* previous=native_material_pass_cursor;
      ~RestoreMaterialPass() { native_material_pass_cursor=previous; }
    } restore;
    native_material_pass_cursor=REXCVAR_GET(edf_native_scene_pass_owned)?&guest:nullptr;
    auto work=frame; work.r3.u64=group; work.lr=0x821C3C04; sub_821D96D8(work,base);
    load_pass();
  };
  WalkNativeStaticWorld(*order,native,fallback,handoff);
  uint64_t hits=0,misses=0,stores=0;
  size_t entries=0;
  lock_run();
  cache.EndPass(); reuse.EndPass();
  hits=cache.hits; misses=cache.misses; stores=cache.stores; entries=cache.size();
  const auto reused=reuse.reuses,allocated=reuse.allocations;
  unlock_run();
  const auto passes=++counters.passes;
  if(passes<=4 || passes%1000==0) {
    uint64_t fallbacks=0;
    for(const auto count:counters.fallbacks) fallbacks+=count;
    const auto& f=counters.fallbacks;
    REXLOG_INFO("Native static world pass: passes={} native_groups={} empty_groups={} instances={} draws={} batches={} fallback_groups={} "
      "guest_queue={} program={} scissor={} eligibility={} pass_state={} instance={} handoffs={} replays={} binds={} composed={} original={} "
      "cache_hits={} cache_misses={} cache_stores={} cache_entries={} instance_reuses={} instance_allocations={}",
      passes,counters.native_groups,counters.empty_groups,counters.instances,counters.draws,counters.batches,fallbacks,
      f[0],f[1],f[2],f[3],f[4],f[5],counters.handoffs,counters.replays,counters.binds,counters.composed,counters.original,
      hits,misses,stores,entries,reused,allocated);
  }
}
}

namespace edf::native {
// Material program of one model pass record (Bridge::model_pass_loads, the
// static group material shape), shared by the model pass and the full frame's
// Models and Sky passes: the program is rebuilt only when its recorded program
// bytes or host identities changed; constant values are re-read at every use,
// since the guest activation uploads them live. That includes the pass-owned
// camera and animation globals (the static pass substitutes its published
// camera; a model draw uploads whatever the globals hold now), and excludes
// g_mWorld, which each record supplies, and with skip_palette g_mWorldArray,
// which a palette object packs from its pose. Failures retry once per tick.
std::shared_ptr<const NativeSceneGroupMaterial> NativeModelPassProgramLocked(Bridge& state,const NativeSceneCpuWindow<GuestReader>& window,
    uint32_t pass,bool skip_palette,const std::function<void(const std::string&)>& report) {
  const auto refresh=[&](Bridge::ModelPassLoad& load) {
    const auto& schema=*load.schema;
    const auto& published=load.published->constants;
    if(published.size()!=load.layout.size()) throw std::runtime_error("native model material constants do not match their layout");
    std::optional<std::vector<NativeSceneMaterialInputs::Constant>> constants;
    for(size_t i=0;i<load.layout.size();++i) {
      const auto& slot=load.layout[i];
      if(published[i].global && (published[i].name=="g_mWorld" || (skip_palette && published[i].name=="g_mWorldArray"))) continue;
      const auto* data=ReadNativeSceneMaterialConstant(window,schema,slot);
      const auto& old=published[i].registers;
      if(old.size()==slot.bytes && std::equal(old.begin(),old.end(),data)) continue;
      if(!constants) constants=published;
      (*constants)[i].registers.assign(data,data+slot.bytes);
    }
    if(!constants) return;
    auto material=std::make_shared<NativeSceneGroupMaterial>(*load.published);
    material->constants=std::move(*constants);
    load.published=std::move(material);
    ++state.model_pass_constants;
  };
  auto& load=state.model_pass_loads[pass];
  if(load.published && NativeSceneMaterialHostCurrent(state,*load.published->program,pass,load.schema) &&
     load.reads.Unchanged(window)) {
    // A failed refresh rebuilds below: only a rebuild failure, which returns
    // no program, is the caller's to report (and to count as a decline).
    try { refresh(load); ++state.model_pass_reused; return load.published; }
    catch(const std::exception& error) {
      static std::set<std::string> noted;  // Under the bridge mutex.
      if(noted.size()<32 && noted.insert(error.what()).second)
        REXLOG_INFO("Native model pass program refresh failed, rebuilding: {}",error.what());
    }
  }
  if(!load.published && load.failed_tick==native_render_budget.tick) return nullptr;
  try {
    NativeRecordedReads reads;
    const NativeRecordingReader recorder(window,reads);
    auto build=BuildNativeSceneMaterialLocked(state,recorder,window,pass,load.published?load.published->program.get():nullptr);
    auto material=std::make_shared<NativeSceneGroupMaterial>();
    material->group=pass;
    material->program=build.reused?load.published->program:std::move(build.program);
    material->constants=std::move(build.constants);
    load.schema=std::move(build.schema); load.layout=std::move(build.layout); load.published=std::move(material);
    load.reads=std::move(reads); load.failed_tick=UINT64_MAX;
    ++state.model_pass_loaded;
    return load.published;
  } catch(const std::exception& error) {
    load.published.reset(); load.failed_tick=native_render_budget.tick;
    report(error.what());
    return nullptr;
  }
}
// The geometry source of one model batch under one pass record: as
// native_scene_geometry.h, pass=*(material+108), shader=**pass.
NativeSceneGeometrySource NativeModelGeometrySource(const GuestReader& reader,const NativeModelBatchLayout& batch,uint32_t pass) {
  return NativeSceneGeometrySource{batch.vertex.owner,batch.index.owner,batch.declaration,batch.stride,batch.draw_count,
    pass,reader.Word(reader.Word(reader.Word(reader.Add(pass,108))))};
}
// Retained geometry of one batch under one vertex shader
// (Bridge::model_geometry_loads), reloaded through the guarded observed-set
// copy, as the static preload does, when changed. Null when its buffers are
// not the batch's published generations or its shader is not registered.
std::shared_ptr<const NativeIndexedMesh::RetainedDraw> NativeModelGeometryLocked(Bridge& state,const GuestReader& reader,
    const NativeSceneGeometrySource& source,const NativeModelBatchLayout& batch) {
  const auto* vb=state.model_buffers.Find(source.vertex,NativeModelBuffers::Kind::Vertex);
  const auto* ib=state.model_buffers.Find(source.index,NativeModelBuffers::Kind::Index);
  if(!vb || !ib || !vb->physical || !ib->physical || vb->generation!=batch.vertex.generation ||
     ib->generation!=batch.index.generation || vb->stride!=source.stride || !vb->bytes || !ib->bytes) return nullptr;
  const auto registered=state.shaders.find(source.shader);
  if(registered==state.shaders.end() || !registered->second.bindings) return nullptr;
  const auto declaration=state.declarations.Get(source.declaration);
  const auto& shader=registered->second.bindings->shader();
  const auto index_contents=ib->index_contents?ib->index_contents:(ib->index_storage?ib->index_storage->SourceSnapshot():nullptr);
  NativeBufferWrites::SnapshotPolicy policy{};
  policy.audit_revisions=REXCVAR_GET(edf_native_retirement_audit);
  if(!policy.audit_revisions && state.mesh_watch_audit.expired()) {
    policy.verify_interval=uint64_t(std::clamp(REXCVAR_GET(edf_native_geometry_verify_interval),0,1<<20));
    policy.verify_initial=uint64_t(std::clamp(REXCVAR_GET(edf_native_geometry_verify_initial),0,1<<16));
  }
  using View=NativeBufferWrites::SnapshotIdentityView;
  const auto versions=BufferWrites().TryValidateObservedSet(std::array<View,2>{{
    {source.vertex,*vb->physical,vb->bytes,&vb->vertex_contents},
    {source.index,*ib->physical,ib->bytes,&index_contents}}},policy);
  auto& load=state.model_geometry_loads[{batch.address,source.shader}];
  const bool same=load.geometry && load.source==source && load.vertex_generation==vb->generation &&
    load.index_generation==ib->generation && load.declaration==declaration &&
    load.shader.Get()==shader.bytecode.Get() && load.geometry->backend()==state.scene_backend.get();
  const auto same_versions=[&](const std::array<NativeBufferWrites::ObservedVersion,2>& observed) {
    for(size_t i=0;i<2;++i)
      if(observed[i].lifetime!=load.versions[i].lifetime || observed[i].revision!=load.versions[i].revision) return false;
    return true;
  };
  if(versions && same && same_versions(*versions)) return load.geometry;
  using Snapshots=std::array<NativeBufferWrites::ObservedSnapshot,2>;
  std::optional<Snapshots> observed;
  if(versions) observed=Snapshots{{
    {(*versions)[0],vb->vertex_contents,false,true,false},
    {(*versions)[1],index_contents,false,true,false}}};
  else {
    using Source=NativeBufferWrites::SnapshotSource;
    observed=BufferWrites().CopyObservedSet(std::array<Source,2>{{
      {source.vertex,*vb->physical,{reader.Bytes(vb->address,vb->bytes),vb->bytes},vb->vertex_contents},
      {source.index,*ib->physical,{reader.Bytes(ib->address,ib->bytes),ib->bytes},index_contents}}},nullptr,policy);
  }
  if(!observed) return nullptr;
  if(same && (*observed)[0].contents==vb->vertex_contents && (*observed)[1].contents==index_contents &&
     same_versions({(*observed)[0].version,(*observed)[1].version})) return load.geometry;
  const auto vertex_generation=vb->generation,index_generation=ib->generation;
  auto geometry=RetainNativeSceneGeometryLocked(state,source,*vb,*ib,declaration,shader,*observed);
  load=Bridge::ModelGeometryLoad{source,vertex_generation,index_generation,
    {(*observed)[0].version,(*observed)[1].version},declaration,shader.bytecode,geometry};
  ++state.model_geometry_loaded;
  return geometry;
}
// Replaces one 821C9C20 object (rigid path: per record 821A17D8 then 821B2C28,
// which binds stream/declaration/indices per batch and runs 821B94E8 +
// 821FE358 per material pass; with edf_native_model_pass_skinned also the
// palette path: 821A1738 once, then 821A17D8 only for records with rec+48
// clear). Everything is resolved from the layout and
// pose publications, the per-pass-record material programs and retained
// batch geometry before anything is recorded; a decline leaves guest and
// device state untouched and returns false so the caller runs the original.
// After recording, the object's guest-visible CPU effects are handed off the
// way the static world pass hands off its groups.
bool RenderNativeModelPass(PPCContext& ctx,uint8_t* base,const std::vector<NativePoseMatrix>* interpolated,
    bool interpolating,bool render_dependent) {
  using D=NativeModelPassDecline;
  static NativeModelPassCounters counters;
  const GuestReader reader(base);
  const auto instance=ctx.r3.u32,vector=ctx.r4.u32;
  ++counters.objects;
  const auto summary=[&] {
    if(counters.objects>4 && counters.objects%1000) return;
    std::string reasons;
    for(size_t i=0;i<counters.fallbacks.size();++i)
      if(counters.fallbacks[i]) reasons+=std::format(" {}={}",kNativeModelPassDeclineNames[i],counters.fallbacks[i]);
    REXLOG_INFO("Native model pass: objects={} native={} draws={} passes={} handoffs={} replays={} fallbacks={}{}",
      counters.objects,counters.native,counters.draws,counters.passes,counters.handoffs,counters.replays,
      counters.Fallbacks(),reasons);
  };
  const auto report=[](const std::string& reason) {
    static std::set<std::string> reported;
    if(reported.size()<32 && reported.insert(reason).second) REXLOG_INFO("Native model pass declined: {}",reason);
  };
  const auto decline=[&](D reason) {
    const auto count=++counters.fallbacks[size_t(reason)];
    if(count<=4 || !(count&(count-1)))
      REXLOG_INFO("Native model pass fallback: instance={:#x} reason={} count={}",instance,kNativeModelPassDeclineNames[size_t(reason)],count);
    summary();
    return false;
  };
  // Publication gate: rigid (or palette-skinned when enabled), layout current,
  // pose published for this tick and not render-dependent.
  auto& models=::ModelPublications();
  const auto published=models.Find(instance);
  if(published && render_dependent) models.MarkRenderDependent(instance,published.generation);
  const auto identity=ReadGuestWords<2>(reader,instance);
  const auto poses=models.AcquirePoses();
  const auto gate=GateNativeModelPass(published,published && models.Current(instance,identity[0],identity[1],vector),
    reader.Bytes(reader.Add(instance,12),1)[0],poses.get(),native_render_budget.tick,
    render_dependent || (published && models.RenderDependent(instance,published.generation)),
    REXCVAR_GET(edf_native_model_pass_skinned));
  if(!gate) return decline(*gate.decline);
  if(interpolating && (!interpolated || interpolated->size()!=gate.pose->matrices.size())) return decline(D::Pose);
  const auto& matrices=interpolating?*interpolated:gate.pose->matrices;
  const auto& layout=*published.layout;
  const auto plan=NativeModelDrawPlan(layout);
  // 821A17D8(*(*0x8257C02C+32)): a null parameter uploads nothing, which the
  // native world cannot represent.
  const auto parameter=reader.Word(reader.Add(reader.Word(0x8257C02C),32));
  const auto world_storage=parameter?reader.Word(parameter):0;
  if(!world_storage) return decline(D::World);
  // Palette path: 821A1738(*(*0x8257C02C+36)) packs the pose into the scratch
  // at descriptor+0, clamped to descriptor+16, before the first record. A null
  // descriptor (the guest skips the pack) or storage is not represented.
  // Records that do not upload their bone draw with the g_mWorld storage as
  // it stands, so its entry bytes seed the world plan.
  uint32_t palette_storage=0,palette_limit=0;
  std::array<uint8_t,64> world_entry{};
  if(layout.skinned) {
    const auto descriptor=reader.Word(reader.Add(reader.Word(0x8257C02C),36));
    palette_storage=descriptor?reader.Word(descriptor):0;
    if(!palette_storage) return decline(D::Palette);
    palette_limit=reader.Word(reader.Add(descriptor,16));
    try {
      const auto count=NativeBonePaletteCount(uint32_t(matrices.size()),palette_limit);
      if(count) reader.WritableBytes(palette_storage,size_t(count)*kNativeBonePaletteBytes,4);
      std::memcpy(world_entry.data(),reader.Bytes(world_storage,64),64);
    } catch(const std::exception& error) { report(error.what()); return decline(D::Palette); }
  }
  std::vector<std::array<uint8_t,64>> worlds;
  try { worlds=NativeModelWorldPlan(layout,matrices,world_entry); }
  catch(const std::exception& error) { report(error.what()); return decline(D::World); }
  const auto device=reader.Word(reader.Add(reader.Word(0x8257BFB4),8));
  // The 821C9C20 and 821B2C28 frames the setters and activations run under.
  if(ctx.r1.u32<4096+128+176) return decline(D::Eligibility);
  const uint32_t stack=ctx.r1.u32-128-176;
  const auto batch_of=[&](const NativeModelDraw& draw) -> const NativeModelBatchLayout& {
    return layout.meshes[draw.mesh].batches[draw.batch];
  };
  const auto source_of=[&](const NativeModelDraw& draw) { return NativeModelGeometrySource(reader,batch_of(draw),draw.pass); };
  // Static-group eligibility per pass: the same descriptor shape and CPU
  // activation contract. Guest reads only.
  try {
    for(const auto& draw:plan)
      if(const auto result=AssessNativeStaticGroup(reader,device,stack,source_of(draw));
         result!=NativeStaticGroupEligibility::Supported) {
        static std::array<std::atomic<uint64_t>,8> reasons{};
        const auto count=++reasons.at(size_t(result)&7);
        if(count<=4 || !(count&(count-1)))
          REXLOG_INFO("Native model pass eligibility: reason={} occurrences={}",uint32_t(result),count);
        return decline(D::Eligibility);
      }
  } catch(const std::exception& error) { report(error.what()); return decline(D::Eligibility); }
  // Pass inputs at object entry: nothing runs between here and the original's
  // first activation, and each pass chains the state its predecessor left.
  NativeSceneMaterialPassState start;
  try {
    start.render=ReadNativeMaterialRenderPass(reader,device);
    for(uint32_t slot=0;slot<16;++slot) start.samplers[slot]=ReadNativeMaterialSamplerPass(reader,device,slot);
    start=start.Inputs();
  } catch(const std::exception& error) { report(error.what()); return decline(D::PassState); }
  auto& state=State();
  std::vector<std::shared_ptr<const NativeSceneGroupMaterial>> materials;
  std::vector<NativeStaticWorldHandoffGroup> owed;
  NativeSceneMaterialPassState cursor=start;
  {
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    if(!state.initialized) return decline(D::Scene);
    if(BufferWrites().Pending()) return decline(D::Pending);
    if(!state.active_scene || state.active_target || !state.scenes.contains(state.active_scene)) return decline(D::Scene);
    const auto targets=ActiveTargetsLocked(state);
    if(!targets.count) return decline(D::Scene);
    EnsureSceneBackendLocked(state);
    const auto viewport=DecodeDrawViewport(ReadNativeDrawViewportWords(reader,device));
    const NativeSceneCpuWindow window(reader);
    if(state.model_pass_loads.size()>8192) state.model_pass_loads.clear();
    if(state.model_geometry_loads.size()>8192) state.model_geometry_loads.clear();
    // Program and geometry of each pass record (NativeModelPassProgramLocked,
    // NativeModelGeometryLocked, shared with the full frame).
    const auto program_of=[&](uint32_t pass) { return NativeModelPassProgramLocked(state,window,pass,layout.skinned,report); };
    const auto geometry_of=[&](const NativeSceneGeometrySource& source,const NativeModelBatchLayout& batch) {
      return NativeModelGeometryLocked(state,reader,source,batch);
    };
    struct Resolved { std::shared_ptr<const NativeSceneInstance> object; NativeSceneView view; };
    std::vector<Resolved> resolved;
    resolved.reserve(plan.size());
    static uint64_t ids=0;
    for(const auto& draw:plan) {
      const auto source=source_of(draw);
      const auto material=program_of(draw.pass);
      if(!material || !material->program) return decline(D::Program);
      const auto& program=*material->program;
      if(!program.CanDeferCpuActivation()) return decline(D::Scissor);
      std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
      try { geometry=geometry_of(source,batch_of(draw)); }
      catch(const std::exception& error) { report(error.what()); }
      if(!geometry || geometry->backend()!=state.scene_backend.get()) return decline(D::Geometry);
      // g_mWorldArray of a palette object: the pass's global must name the
      // scratch 821A1738 packed, and the shader's array (the slot's reflected
      // bytes) must hold every packed bone; the tail keeps the live scratch.
      const std::vector<NativeSceneMaterialInputs::Constant>* constants=&material->constants;
      std::vector<NativeSceneMaterialInputs::Constant> palette_constants;
      if(layout.skinned) {
        const auto& load=state.model_pass_loads.at(draw.pass);
        for(size_t i=0;i<material->constants.size();++i) {
          const auto& constant=material->constants[i];
          if(constant.name!="g_mWorldArray") continue;
          if(!constant.global || constant.pixel || !load.schema || i>=load.layout.size()) return decline(D::Palette);
          std::optional<std::vector<uint8_t>> registers;
          try {
            const auto& schema=*load.schema;
            const auto& slot=load.layout[i];
            if(slot.group!=1 || slot.parameter>=schema[1].size() || schema[1][slot.parameter].ReadValue(window,true).data!=palette_storage)
              return decline(D::Palette);
            const auto* live=ReadNativeSceneMaterialConstant(window,schema,slot);
            registers=NativeModelPaletteRegisters(matrices,palette_limit,{live,slot.bytes});
          } catch(const std::exception& error) { report(error.what()); return decline(D::Palette); }
          if(!registers) return decline(D::Palette);
          if(constants!=&palette_constants) { palette_constants=material->constants; constants=&palette_constants; }
          palette_constants[i].registers=std::move(*registers);
        }
      }
      NativeSceneResolvedMaterial result;
      NativeSceneMaterialPassState next;
      try {
        next=cursor.After(program);
        DecodeNativeRenderState(next.render.words);
        NativeBackendPipelineDesc desc;
        desc.vertex_id=(uint64_t(program.inputs.vertex)<<1)|uint64_t(viewport.reverse_depth);
        desc.pixel_id=program.inputs.pixel;
        desc.input_layout=geometry->input_layout().elements(); desc.input_layout_id=geometry->input_layout().fingerprint();
        desc.render_targets=targets.count; desc.rtv_format=targets.rtv_format;
        desc.dsv_format=targets.dsv_format; desc.sample_count=targets.samples;
        result=program.Resolve(desc,viewport.reverse_depth,*constants,cursor.render,cursor.samplers,
          REXCVAR_GET(edf_native_anisotropic_filtering),layout.skinned);
      } catch(const std::exception& error) { report(error.what()); return decline(D::PassState); }
      // g_mWorld exactly as 821A17D8 stores it: the record's bone, column-major,
      // or on the palette path, for a record with rec+48 set, what the storage
      // holds. A palette shader need not declare g_mWorld at all.
      try {
        if(!layout.skinned || NativeSceneCaptureBindsWorld(result.capture))
          ApplyNativeScenePublishedWorld(result.capture,worlds[draw.mesh]);
      } catch(const std::exception& error) { report(error.what()); return decline(D::World); }
      auto object=std::make_shared<NativeSceneInstance>();
      object->id=(uint64_t(1)<<62)+ ++ids; object->changed_tick=UINT64_MAX;
      object->object.geometry=std::move(geometry); object->object.material=result.capture.material;
      object->object.world=result.capture.world; object->previous=result.capture.world;
      resolved.push_back({std::move(object),NativeStaticInstanceView(result.capture.camera,viewport,result.render.words[5]!=0)});
      uint32_t slots=0;
      for(const auto& operation:program.sampler_operations) slots|=1u<<operation.slot;
      owed.push_back({program.inputs.state_overrides,slots});
      materials.push_back(material);
      cursor=std::move(next);
    }
    // The handoff's combined state writes must be representable and land on
    // the chained cursor before anything is recorded.
    try {
      if(!owed.empty() && NativeStaticWorldStateHandoff(start.render,owed).render!=cursor.render) return decline(D::PassState);
    } catch(const std::exception& error) { report(error.what()); return decline(D::PassState); }
    // Record in guest order. A partial recording cannot be rolled back; a
    // throw past this point reaches TryNativeModelPass, which runs the original.
    NativeQueuedSceneGroup batch;
    batch.targets=targets;
    const auto draws=state.scene_native_draws;
    try {
      for(auto& item:resolved) {
        if(!batch.objects.empty() && (batch.view.view!=item.view.view || batch.view.projection!=item.view.projection ||
           batch.view.view_projection!=item.view.view_projection || batch.view.scissor_enabled!=item.view.scissor_enabled))
          FlushNativeQueuedSceneLocked(state,batch);
        batch.view=item.view; batch.objects.push_back(std::move(item.object));
      }
      FlushNativeQueuedSceneLocked(state,batch);
    } catch(const std::exception&) {
      // The recorder may hold part of this object's bindings: invalidate its cache.
      ++state.bind_generation; state.recorded={};
      throw;
    }
    ++state.bind_generation;
    counters.draws+=state.scene_native_draws-draws; counters.passes+=plan.size();
  }
  // Device-state handoff: guest calls run unlocked, in guest order.
  auto frame=ctx;
  frame.r1.u64=ctx.r1.u32-128; reader.StoreWord(frame.r1.u32,ctx.r1.u32);
  frame.r1.u64=stack; reader.StoreWord(stack,ctx.r1.u32-128);
  // 1. 821A1738 on the palette path: the scratch holds the packed pose, as the
  // 821A1738 hook stores it. Written, not proven unread: later skinned draws
  // pack before they upload, but the scratch is shared and other readers of
  // it were not audited, and the material replays below upload from it.
  if(layout.skinned) {
    const auto bones=NativeModelPaletteWords(matrices,palette_limit);
    for(uint32_t bone=0;bone<bones.size();++bone)
      reader.StoreCpuWords(reader.Add(palette_storage,bone*kNativeBonePaletteBytes),bones[bone]);
  }
  // 2. 821A17D8 of the last uploading record: g_mWorld's storage holds its bone
  // (on the palette path, unchanged when no record uploads).
  if(const auto uploaded=NativeModelLastWorldUpload(layout))
    reader.StoreCpuWords(world_storage,NativeModelWorldWords(matrices[layout.meshes[*uploaded].bone]));
  // 3. 821B2C28's stream/declaration/index setters for the last batch.
  const NativeModelBatchLayout* last=nullptr;
  for(const auto& mesh:layout.meshes) if(!mesh.batches.empty()) last=&mesh.batches.back();
  if(last) {
    NativeSceneGeometryInstallState progress;
    auto work=frame;
    ::InstallNativeStaticGeometry(work,base,device,NativeSceneGeometrySource{last->vertex.owner,last->index.owner,
      last->declaration,last->stride,last->draw_count,last->passes.empty()?0:last->passes.back(),0},progress);
  }
  // 4. Material activations: the last pass and each sampler slot's last
  // binder through 821B94E8, then every pass's combined render words, dirty
  // masks and sampler words, exactly as for static world groups.
  if(!owed.empty()) {
    auto work=frame;
    // As for static world groups: a pass not replayed still binds and uploads,
    // so its textures and shaders retire in guest order; composed from its
    // cached effects, or through the activation when they are unreadable.
    std::vector<std::shared_ptr<const NativeStaticWorldBindEffects>> effects;
    effects.reserve(owed.size());
    for(size_t index=0;index<owed.size();++index) {
      effects.push_back(NativeStaticWorldBindEffectsFor(reader,plan[index].pass,device,materials[index]));
      owed[index].effects=effects.back().get();
    }
    const auto writes=HandOffNativeStaticWorld(reader,device,start.render,cursor.samplers,owed,[&](size_t index) {
      work.r3.u64=plan[index].pass; work.lr=0x821B2ECC; sub_821B94E8(work,base);
      ++counters.replays;
    },[&](size_t index) {
      ActivateNativeMaterial(work,base,plan[index].pass,device,false);
    },[&](NativeStaticWorldRetirement kind) { return ReserveNativeHandoffRetirement(frame,base,device,kind); },
      [&](NativeStaticWorldRetirement kind) { return NativeHandoffRetirementTag(reader,frame,kind); });
    if(writes.render!=cursor.render) throw std::runtime_error("native model handoff diverged from the pass cursor");
    for(const auto offset:writes.operations) {
      const auto setter=NativeMaterialStateSetter(offset);
      if(offset==0x44) {
        std::lock_guard lock(state.mutex);
        state.render_state_snapshots.PublishBlend(device,ReadGuestWords<4>(reader,reader.Add(device,10336)),setter);
      } else if(offset!=0x64) PublishNativeRenderState(base,device,setter);
    }
    if(REXCVAR_GET(edf_native_material_state_audit) || REXCVAR_GET(edf_native_material_sampler_audit)) {
      NativeSceneMaterialPassState actual;
      actual.render=ReadNativeMaterialRenderPass(reader,device);
      for(uint32_t slot=0;slot<16;++slot) actual.samplers[slot]=ReadNativeMaterialSamplerPass(reader,device,slot);
      actual=actual.Inputs();
      if(actual!=cursor) {
        REXLOG_ERROR("Native model handoff mismatch: instance={:#x} render_equal={} samplers_equal={}",
          instance,actual.render==cursor.render,actual.samplers==cursor.samplers);
        throw std::runtime_error("native model handoff differs from the pass cursor");
      }
    }
  }
  {
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    state.recorded={}; ++state.bind_generation;
  }
  ++counters.handoffs; ++counters.native;
  summary();
  return true;
}
}
