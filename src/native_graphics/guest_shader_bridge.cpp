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

REXCVAR_DECLARE(int32_t, window_width);
REXCVAR_DECLARE(int32_t, window_height);

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
namespace {

template<class Reader>
NativeViewportState ReadDrawViewport(const Reader& reader,uint32_t device) {
  return DecodeDrawViewport(ReadViewportWords(reader,device));
}
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
}

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
namespace edf::native {
thread_local constinit PostFinishRecorder* post_finish_recorder=nullptr;
}
// Render helper entries (sub_821A5080), one per frame; census periods count these.
std::atomic<uint64_t> native_render_frames{0};
namespace edf::native {
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
}
