#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "guest_shader_bridge.h"
#include "../scripted_input_logic.h"
#include "native_backend_frame.h"
#include "native_backend_frame_queue.h"
#include "native_frame_flight.h"
#include "d3d11_backend.h"
#include "d3d12_backend.h"
#include "native_render_backend.h"
#include "native_constant_cache.h"
#include "native_scene_sources.h"
#include "native_scene_adapter.h"
#include "native_scene_pass_inputs.h"
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
#include "native_movie_bindings.h"
#include "native_generated_indices.h"
#include "native_physical_write_notify.h"
#include "native_contract_ledger.h"
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
#include "native_model_pass.h"
#include "native_profile_result.h"
#include "native_capture_policy.h"
#include "native_ab_alternate.h"
#include "native_post_finish_plan.h"
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
#include "font_effect.h"
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

REXCVAR_DEFINE_BOOL(edf_native_shader_bridge, false, "EDF2027",
                   "Validate native shader resources against live guest loads (development)");
REXCVAR_DEFINE_BOOL(edf_native_render_state_audit,false,"EDF2027",
                   "Compare setter-owned render-state snapshots with live draw state (diagnostic; does not bypass reads)");
REXCVAR_DEFINE_BOOL(edf_native_material_sampler_audit,false,"EDF2027",
                   "Compare explicit native material sampler programs with original activation (diagnostic)");
REXCVAR_DEFINE_BOOL(edf_native_material_state_audit,false,"EDF2027",
                   "Compare explicit native material render-state programs with original activation (diagnostic)");
REXCVAR_DEFINE_BOOL(edf_native_scene_material_audit,false,"EDF2027",
                   "Compare published programs with explicit pass-time constants and visible group setup (diagnostic)");
REXCVAR_DEFINE_BOOL(edf_native_scene_material_owned,false,"EDF2027",
                   "Construct queued rigid scene materials from published programs and native pass inputs");
REXCVAR_DEFINE_BOOL(edf_native_scene_activation_owned,false,"EDF2027",
                   "Activate queued scene shader bindings from published material inputs");
REXCVAR_DEFINE_BOOL(edf_native_scene_geometry_owned,false,"EDF2027",
                   "Use published geometry for the first native group draw, retaining the indexed CPU tail");
REXCVAR_DEFINE_BOOL(edf_native_scene_instance_owned,false,"EDF2027",
                   "Use lifecycle-owned world-only instance register metadata");
REXCVAR_DEFINE_BOOL(edf_native_scene_pass_owned,false,"EDF2027",
                   "Carry explicit render and sampler pass state between native groups");
REXCVAR_DEFINE_BOOL(edf_native_scene_geometry_deferred,false,"EDF2027",
                   "Submit eligible native groups before installing compatibility geometry bindings");
REXCVAR_DEFINE_BOOL(edf_native_scene_material_deferred,false,"EDF2027",
                   "Submit eligible native groups before CPU material activation and restore state at handoff");
REXCVAR_DEFINE_BOOL(edf_native_scene_reject_compatibility,false,"EDF2027",
                   "Diagnostic: reject counted static-group compatibility boundaries before calling them");
REXCVAR_DEFINE_BOOL(edf_native_scene_view_owned,false,"EDF2027",
                   "Carry viewport, scissor and native target selection across native material groups");
REXCVAR_DEFINE_BOOL(edf_native_scene_camera_owned,false,"EDF2027",
                   "Consume immutable producer camera matrices at native render entry");
REXCVAR_DEFINE_BOOL(edf_native_scene_sources_owned,false,"EDF2027",
                   "Select native static sources, LOD, visibility and world from one scene publication");
REXCVAR_DEFINE_BOOL(edf_native_scene_selection_owned,false,"EDF2027",
                   "Resolve published static instances without mutating the current scene database");
REXCVAR_DEFINE_BOOL(edf_native_scene_membership_owned,false,"EDF2027",
                   "Select spatial lists and hierarchy from the same publication as native scene sources and assets");
REXCVAR_DEFINE_BOOL(edf_native_worker_callback_audit,false,"EDF2027",
                   "Log up to 256 render-worker callback registrations for writer provenance (diagnostic)");
REXCVAR_DEFINE_BOOL(edf_native_retirement_audit,false,"EDF2027",
                   "Sample completed native setter retirement branches and descriptor identity (diagnostic)");
REXCVAR_DEFINE_INT32(edf_native_geometry_verify_interval,256,"EDF2027",
                    "Sample the guest geometry byte comparison once per N snapshot observations of an owner once its revision proves the retained candidate; 0 compares every draw. A detected miss permanently restores full comparison");
REXCVAR_DEFINE_INT32(edf_native_geometry_verify_initial,8,"EDF2027",
                    "Snapshot observations of each subscription lifetime that always compare guest geometry bytes before the sampled schedule applies (0..65536)");
REXCVAR_DEFINE_INT32(edf_native_wait_stall_ms,5000,"EDF2027",
                    "Native wait no-progress deadline in host milliseconds (1..600000); failure does not fake GPU completion");
REXCVAR_DEFINE_BOOL(edf_native_owned_render_state,true,"EDF2027",
                   "Use native setter-owned render words, scissor enable and blend factors; false enables diagnostic legacy reads");
REXCVAR_DEFINE_INT32(edf_native_anisotropic_filtering, -1, "EDF2027",
                    "Native material filtering: -1 game default, 0 off, 1..5 for 1x..16x; preserves point/base-only sampling");
REXCVAR_DEFINE_INT32(edf_native_render_width, 0, "EDF2027",
                    "Native render width at startup; 0 preserves the original scene size (restart required)");
REXCVAR_DEFINE_INT32(edf_native_msaa, 0, "EDF2027",
                    "Native scene samples: 0 game default, 1 off, 2 or 4 MSAA (restart required)");
REXCVAR_DEFINE_INT32(edf_native_render_height, 0, "EDF2027",
                    "Native render height at startup; paired with render width (restart required)");
REXCVAR_DEFINE_STRING(edf_native_scene_capture, "", "EDF2027",
                     "Optional prefix for partial scene, font/movie and output-frame BMP diagnostics (not presentation)");
REXCVAR_DEFINE_INT32(edf_native_output_capture_interval,0,"EDF2027",
                    "Additional output capture interval in indexed frames; 0 disables periodic captures (development)");
REXCVAR_DEFINE_INT32(edf_native_output_capture_limit,32,"EDF2027",
                    "Maximum diagnostic output BMPs per run, clamped to 0..128; requires scene capture prefix (development)");
REXCVAR_DEFINE_INT32(edf_native_output_capture_start_frame,0,"EDF2027",
                    "Positive indexed frame starts interval captures and disables startup milestones; 0 preserves defaults (development)");
REXCVAR_DEFINE_BOOL(edf_native_output_capture_scene_color,false,"EDF2027",
                   "Also capture scene color at each selected output frame to diagnose post-processing differences (development)");
REXCVAR_DEFINE_BOOL(edf_native_pixel_centers,true,"EDF2027",
                   "Apply the guest PA_SU_VTX_CNTL half-pixel offset to the audited retail post passes; false restores the unshifted viewport for regression diagnosis");
REXCVAR_DEFINE_INT32(edf_native_loop_trace,0,"EDF2027",
                    "Trace the first N engine heartbeat/update/helper calls with thread and timing for frame-rate decoupling; 0 disables (development)").range(0,10000);
REXCVAR_DEFINE_INT32(edf_native_motion_trace,0,"EDF2027",
                    "Trace the first N scene camera submissions and matrix fingerprints; 0 disables (development)").range(0,10000);
REXCVAR_DEFINE_INT32(edf_native_instance_motion_trace,0,"EDF2027",
                    "Observe transforms for N scene frames, up to 256 instance and 64 palette sources; 0 disables (development)").range(0,10000);
REXCVAR_DEFINE_BOOL(edf_native_scene_adapter_audit,false,"EDF2027",
                   "Log bounded world-object dispatch samples for native scene lifetime integration (development)");
REXCVAR_DEFINE_BOOL(edf_native_scene_queued,false,"EDF2027",
                   "Render supported queued static-world groups from retained native scene objects (development)");
REXCVAR_DEFINE_BOOL(edf_native_scene_preload,false,"EDF2027",
                   "Publish static-group geometry and owned material inputs without draw callbacks (development)");
REXCVAR_DEFINE_BOOL(edf_native_scene_transform_audit,false,"EDF2027",
                   "Compare event-published native static transforms with every queued guest matrix (development)");
REXCVAR_DEFINE_BOOL(edf_native_scene_visibility,false,"EDF2027",
                   "Use native visibility and static LOD selection within the opt-in native scene path (development)");
REXCVAR_DEFINE_BOOL(edf_native_scene_visibility_audit,false,"EDF2027",
                   "Compare native visibility against original culling routines and live bounds (development)");
REXCVAR_DEFINE_BOOL(edf_native_bucket_dispatch_audit,false,"EDF2027",
                   "Compare the native sort-mode 1/2 bucket key and insert with sub_821C0C00 (development)");
REXCVAR_DEFINE_BOOL(edf_native_bucket_dispatch,false,"EDF2027",
                   "Insert sort-mode 1/2 objects into the guest depth buckets natively instead of sub_821C0C00 (development)");
REXCVAR_DEFINE_BOOL(edf_native_scene_static_walk,false,"EDF2027",
                   "Publish a per-world static walk plan at the simulation step and drive the native visibility walk from it: planned membership, vtable slot and source candidates; mode, hidden flag and vtable stay live (development)");
REXCVAR_DEFINE_BOOL(edf_native_scene_static_walk_audit,false,"EDF2027",
                   "Publish the static walk plan and compare its classification with the live walk reads, counting mismatches; the walk itself stays live (development)");
REXCVAR_DEFINE_BOOL(edf_native_unlock_framerate,false,"EDF2027",
                   "Experimental independent render loop with 60 Hz step dispatch; motion interpolation and timing validation are in progress");
REXCVAR_DEFINE_BOOL(edf_native_camera_interpolation,true,"EDF2027",
                   "Interpolate published camera poses in experimental unlocked mode; false permits diagnostic comparison");
REXCVAR_DEFINE_BOOL(edf_native_model_interpolation,true,"EDF2027",
                   "Interpolate model pose uploads in experimental unlocked mode; false permits diagnostic comparison");
REXCVAR_DEFINE_BOOL(edf_native_model_publication,false,"EDF2027",
                   "Capture model draw layouts at first sight and publish per-tick pose snapshots; draws are unchanged (development)");
REXCVAR_DEFINE_BOOL(edf_native_model_publication_audit,false,"EDF2027",
                   "Compare published model layouts and poses with live memory at model draw entry (development)");
REXCVAR_DEFINE_BOOL(edf_native_model_pass,false,"EDF2027",
                   "Draw rigid published models natively at 821C9C20; any unsupported object runs the original draw. Requires edf_native_model_publication and the static world pass scene flags (development)");
REXCVAR_DEFINE_BOOL(edf_native_capture_indexed_state,false,"EDF2027",
                   "Trace up to 256 indexed draw states per selected capture frame; requires scene capture prefix (development)");
REXCVAR_DEFINE_INT32(edf_native_probe_x, -1, "EDF2027", "Optional native scene invalid-RGB probe pixel X");
REXCVAR_DEFINE_INT32(edf_native_probe_y, -1, "EDF2027", "Optional native scene invalid-RGB probe pixel Y");
REXCVAR_DEFINE_INT32(edf_native_probe_frame, 0, "EDF2027",
                    "First indexed output candidate to probe; 0 retains first-scene diagnostics");
REXCVAR_DEFINE_INT32(edf_native_probe_width, 1, "EDF2027", "Invalid-RGB diagnostic region width");
REXCVAR_DEFINE_INT32(edf_native_probe_height, 1, "EDF2027", "Invalid-RGB diagnostic region height");
REXCVAR_DEFINE_INT32(edf_native_probe_draw_limit, 4096, "EDF2027", "Maximum invalid-RGB diagnostic draws, capped at 65536");
REXCVAR_DEFINE_BOOL(edf_native_probe_negative, false, "EDF2027",
                   "Also stop the invalid-RGB probe on a scene channel at or below -1; the tone curve maps a large negative to white");
REXCVAR_DEFINE_STRING(edf_native_backend, "d3d12", "EDF2027",
                     "Backend used by everything that draws through the renderer's backend interface: d3d12 (default), d3d12-warp, d3d11, d3d11-warp, or empty for none. Built on first use, so selecting one costs nothing until something draws through it. An unknown name is refused rather than silently falling back");
REXCVAR_DEFINE_STRING(edf_native_scene_backend, "d3d12", "EDF2027",
                     "Scene rendering backend: d3d12 (default) or d3d11 for comparison. D3D12 publishes a fenced GPU snapshot through the host compositor, preserving display gamma and overlays. D3D11 is an explicit fallback; backend initialization failures do not silently change this setting");
REXCVAR_DEFINE_INT32(edf_native_upload_megabytes, 256, "EDF2027",
                    "Upload-ring megabytes for a D3D12 backend. Every recorded draw stages its constants here and the ring is retired by fence, so it has to hold every frame still in flight. A frame that does not fit is refused with the high water it reached, which is what to set this from");
REXCVAR_DEFINE_INT32(edf_native_geometry_workers, 4, "EDF2027",
    "D3D12 geometry recording workers (0 direct, 1 serial packets, 2..32 parallel); restart required");
REXCVAR_DEFINE_INT32(edf_native_frame_operations, 8192, "EDF2027",
                    "Recorded operations after which the scene's frame is submitted and a new one opened, rather than waiting for the guest's swap. The guest swaps once a frame but begins render targets far more often than that - measured at 1,000 begins across 8 swaps - so a frame tied only to the swap accumulates without bound during loading, which is one command list, one ring's worth of uploads, and eventually a GPU with more work in one submission than it will accept");
REXCVAR_DEFINE_BOOL(edf_native_d3d12_debug_layer, false, "EDF2027",
                   "Turn the D3D12 debug layer, and GPU-based validation with it, on for every backend this process builds - including the hardware one. Very slow. Worth it when something removes the device: the plain layer names an invalid call, and GPU-based validation names what a shader did with a valid one, which is the half that presents as a hang with nothing in the log");
REXCVAR_DEFINE_BOOL(edf_native_seam_draws, true, "EDF2027",
                   "Record the scene's draws through the backend interface instead of calling the D3D11 context directly. True by default, and required by --edf_native_scene_backend=d3d12: a draw issued straight to the D3D11 context cannot bind a resource that lives on another device. False keeps the old direct path, which only works with the d3d11 scene backend and exists as the A/B control - with both on d3d11 the two draw the same thing on the same device, so a difference is a wiring mistake rather than a backend one");
REXCVAR_DEFINE_BOOL(edf_native_backend_present, true, "EDF2027",
                   "Use a separate presenting backend for the D3D11 fallback. D3D12 always uses its native host and presenter");
REXCVAR_DEFINE_BOOL(edf_native_reuse_material, true, "EDF2027",
                   "Skip re-binding the shader pair, textures and samplers when the previous indexed draw already bound the same ones and nothing has bound since. Set false if repeated objects ever show another material's textures; that is what a wrong guard here looks like");
REXCVAR_DEFINE_INT32(edf_native_shader_workers, -1, "EDF2027",
                    "Threads used to compile a shader registration's entries: -1 picks one per core up to eight, 0 compiles inline on the calling thread. Compilation is the load cost worth threading - the entries are a real batch and each takes milliseconds, unlike the per-draw work, which has neither property");
REXCVAR_DEFINE_BOOL(edf_native_backend_preview, false, "EDF2027",
                   "Open a second window drawn and presented entirely by the selected backend. Needs --edf_native_backend and --edf_native_publish_frames. The renderer's own window is untouched");
REXCVAR_DEFINE_BOOL(edf_native_batch_audit, false, "EDF2027",
                   "Measure runs of consecutive indexed draws that differ only in per-instance constants; the mean run length is the draw-call reduction instancing would give");
REXCVAR_DEFINE_INT32(edf_native_contract_limit, 4096, "EDF2027",
                    "Distinct draw contracts the coverage ledger retains (1..1048576); reaching it is counted, never silently dropped");
REXCVAR_DEFINE_STRING(edf_native_contract_export, "", "EDF2027",
                     "Write the captured draw-contract catalog to this path; the offline geometry check replays it");
REXCVAR_DEFINE_BOOL(edf_native_contract_coverage, false, "EDF2027",
                   "Also record every submitted draw contract, so a run can enumerate what the content exercises; costs a set lookup per draw");
REXCVAR_DEFINE_INT32(edf_native_shared_constant_audit, 0, "EDF2027",
                    "Audit Common.fx globals a stage's native shader consumes but the material never lists for that stage, for this many activations; 0 disables (development)");
REXCVAR_DEFINE_INT32(edf_native_frame_latency,2,"EDF2027",
  "D3D12 frame credits: 1 drains each frame, 2 overlaps next-frame preparation; restart required.");
REXCVAR_DEFINE_STRING(edf_native_frame_trace, "", "EDF2027",
  "Optional CSV of swap-boundary wall times; restart to change the output path.");
REXCVAR_DEFINE_BOOL(edf_native_owned_mesh_hit,true,"EDF2027",
                   "Reuse consecutive mesh hits with identical owned geometry snapshots");
REXCVAR_DEFINE_BOOL(edf_native_world_instancing,true,"EDF2027",
                   "Combine compatible queued world-matrix draws into GPU instances");
REXCVAR_DEFINE_BOOL(edf_native_world_constant_reuse,true,"EDF2027",
                   "Retain shared vertex constants when only an instance world matrix changes");
REXCVAR_DEFINE_BOOL(edf_native_prepared_geometry,true,"EDF2027",
                   "Reuse prepared queued geometry after guarded snapshot validation");
REXCVAR_DEFINE_INT32(edf_native_hook_sample_period,0,"EDF2027",
  "Sample one in N bridge timing scopes (0 disables); independent of full hook/load instrumentation").range(0,4096);
REXCVAR_DEFINE_BOOL(edf_native_hook_timings, false, "EDF2027",
                   "Log inclusive CPU wall times for native/original graphics hook phases (development)");
REXCVAR_DEFINE_BOOL(edf_native_loading_trace, false, "EDF2027",
                   "Sample end-frame publication eligibility and cumulative UI draws; does not capture pixels (development)");
REXCVAR_DEFINE_BOOL(edf_native_load_timings, false, "EDF2027",
                   "Log each texture/shader load phase CPU duration, including nested work (development)");
REXCVAR_DEFINE_BOOL(edf_native_mesh_watch_audit,false,"EDF2027",
                   "Audit physical mesh write versions against exact bytes; never skips validation (development)");
REXCVAR_DEFINE_BOOL(edf_native_untiled_scene, true, "EDF2027",
                   "Deprecated compatibility setting; native untiled scene lifecycle is always used");
REXCVAR_DEFINE_BOOL(edf_native_guest_heap_reads, true, "EDF2027",
                   "Validate committed readable SDK regions; Windows validation remains fallback (false forces OS checks)");
REXCVAR_DEFINE_BOOL(edf_native_fence_probe, false, "EDF2027",
                   "Observe native event-query completion at guest fence boundaries; never writes guest counters");
REXCVAR_DEFINE_BOOL(edf_native_validate_wait, false, "EDF2027",
                   "Development: await native completion after original guest waits; requires native bridge and fence probe");
REXCVAR_DEFINE_BOOL(edf_native_publish_frames, false, "EDF2027",
                   "Publish native movie/partial scene GPU snapshots for host presentation; requires native bridge");
REXCVAR_DEFINE_BOOL(edf_native_preview_window, false, "EDF2027",
                   "Show native frames in a separate development window; requires frame publication");
REXCVAR_DECLARE(bool, edf_native_host);
REX_EXTERN(__imp__KeSetEvent);

namespace edf::native {
NativeSceneTreePublications& TreePublications() {
  static NativeSceneTreePublications publications;
  return publications;
}
// Capture at renderer initialization. Saving a new F1 choice must not change
// live UI scaling while the current render targets still have the old size.
const std::array<int32_t,2>& NativeRenderDimensions() {
  static const std::array<int32_t,2> dimensions{
    REXCVAR_GET(edf_native_render_width),REXCVAR_GET(edf_native_render_height)};
  return dimensions;
}
namespace {
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
                       TextureSnapshot, TextureOriginal, TextureLock, TextureCreate,
                       ShaderRegistration, ShaderLock, ShaderEntry,
                       TextureAllocate, TextureUpload2D, TextureUploadVolume, TexturePrepare,
                       ResourceOneShot, ResourceCoordinator, ResourceHelper, ResourceTransition, Count };
thread_local uint32_t texture_loader_depth=0;
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
    static constexpr const char* names[]{"activation.original","activation.native",
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
      "load.texture.snapshot","load.texture.original","load.texture.lock","load.texture.create",
      "load.shader.registration","load.shader.lock","load.shader.entry",
      "load.texture.allocate","load.texture.upload2d","load.texture.upload_volume","load.texture.prepare",
      "load.resource.oneshot","load.resource.coordinator","load.resource.helper","load.resource.transition"};
    static_assert(std::size(names)==static_cast<size_t>(HookPhase::Count));
    const auto index=static_cast<size_t>(phase_);
    auto& bucket=buckets[index];
    ++bucket.count; bucket.total+=ms; bucket.maximum=(std::max)(bucket.maximum,ms);
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
// Coarse wait totals are sampled once per swap. They include all participating
// threads, so they locate waits but must not be summed as a CPU-time partition.
enum class FrameWaitKind { Engine,GuestFence,SharedSlot };
// Extra simulation steps folded into one engine update. This is diagnostic
// bookkeeping only; the retail result and clock writeback remain unchanged.
std::atomic<uint64_t>& FrameExtraSimulationSteps() {
  static std::atomic<uint64_t> total{0};
  return total;
}
std::array<std::atomic<uint64_t>,3>& FrameWaitTotals() {
  static std::array<std::atomic<uint64_t>,3> totals{};
  return totals;
}
class NativeFrameWaitTrace {
 public:
  explicit NativeFrameWaitTrace(FrameWaitKind kind,bool active=true)
      : kind_(kind),enabled_(active && !REXCVAR_GET(edf_native_frame_trace).empty()) {
    if(enabled_) start_=Clock::now();
  }
  ~NativeFrameWaitTrace() { Finish(); }
  void Finish() {
    if(!enabled_) return;
    enabled_=false;
    const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start_).count();
    FrameWaitTotals()[size_t(kind_)].fetch_add(uint64_t(ns),std::memory_order_relaxed);
  }
 private:
  using Clock=std::chrono::steady_clock;
  FrameWaitKind kind_;
  bool enabled_;
  Clock::time_point start_{};
};
// Only consume ranges the guest mapped and made readable. The source loader
// gives a pointer, not an allocation length; never manufacture a 16 MiB span.
class GuestReader {
 public:
  explicit GuestReader(uint8_t* base) : memory_(REX_KERNEL_MEMORY()) {
    if (!memory_ || memory_->virtual_membase()!=base) throw std::runtime_error("native bridge guest memory mismatch");
  }
  const uint8_t* Bytes(uint32_t address, size_t size) const {
    if (!address || size > 0x100000000ull - address) throw std::runtime_error("invalid guest shader address");
    auto* start = memory_->TranslateVirtual(address);
    if (size && memory_->TranslateVirtual(address+static_cast<uint32_t>(size-1))!=start+size-1)
      throw std::runtime_error("native bridge range crosses noncontiguous guest heaps");
    if(size && REXCVAR_GET(edf_native_guest_heap_reads)) {
      const uint32_t last=address+static_cast<uint32_t>(size-1);
      auto* heap=memory_->LookupHeap(address);
      // QueryRangeAccess alone ignores commitment (Decommit retains protect).
      // QueryRegionInfo checks both state and access under the heap mutex.
      // Require containment; retain TranslateVirtual's alias offset.
      // Loader/untracked/cross-heap ranges keep the original OS validation.
      if(heap && heap==memory_->LookupHeap(last) && address>=heap->heap_base() &&
         uint64_t(last)-heap->heap_base()<heap->heap_size()) {
        const bool readable=GuestHeapCommittedReadable(*heap,address,size);
        if(readable) {
          static thread_local bool reported=false;
          if(!reported) {
            reported=true;
            REXLOG_INFO("Native guest read: SDK committed-region validation active, heap={:#x}",heap->heap_base());
          }
          return start;
        }
      }
    }
    auto* cursor = start;
    const auto* end = cursor + size;
    while (cursor < end) {
      MEMORY_BASIC_INFORMATION region{};
      if (!VirtualQuery(cursor, &region, sizeof(region)) || region.State != MEM_COMMIT ||
          (region.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        throw std::runtime_error("unreadable guest shader memory");
      auto* region_end = static_cast<uint8_t*>(region.BaseAddress) + region.RegionSize;
      if (region_end <= cursor) throw std::runtime_error("invalid guest memory extent");
      cursor = region_end < end ? region_end : const_cast<uint8_t*>(end);
    }
    return start;
  }
  uint32_t Word(uint32_t address) const {
    const auto* p = Bytes(address, 4);
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  void StoreWord(uint32_t address,uint32_t value) const {
    const auto* data=WritableBytes(address,4,4);
    // Permission lookup is optimized, not publication semantics.
    InterlockedExchange(reinterpret_cast<volatile LONG*>(const_cast<uint8_t*>(data)),
                        static_cast<LONG>(std::byteswap(value)));
  }
  void StoreByte(uint32_t address,uint8_t value) const {
    const auto aligned=address&~3u;
    auto* data=const_cast<uint8_t*>(WritableBytes(aligned,4,4));
    data[address-aligned]=value;
  }
  template<size_t N>
  void StoreCpuWords(uint32_t address,const std::array<uint32_t,N>& words) const {
    static_assert(N>0);
    // CPU-owned render state, as in the original PPC ordinary stores. Never
    // use this for completion publication, worker signals or shared fences.
    // Validate the whole write before mutation; do not use one interlocked
    // exchange per word for viewport/transform data.
    const auto* data=WritableBytes(address,N*4,4);
    StoreGuestCpuWords(std::span<uint8_t>{const_cast<uint8_t*>(data),N*4},words);
  }
  const uint8_t* WritableBytes(uint32_t address,size_t size,size_t alignment) const {
    if(!size || (alignment!=4 && alignment!=8))
      throw std::runtime_error("invalid native guest write extent/alignment");
    const auto* data=Bytes(address,size);
    if((address&(alignment-1)) || (reinterpret_cast<uintptr_t>(data)&(alignment-1)))
      throw std::runtime_error("native guest write destination is not aligned");
    bool sdk_writable=false;
    if(REXCVAR_GET(edf_native_guest_heap_reads)) {
      auto* heap=memory_->LookupHeap(address);
      sdk_writable=heap && heap==memory_->LookupHeap(address+uint32_t(size-1)) &&
        GuestVirtualHeapCommittedWritable(*heap,address,size);
    }
    const auto* cursor=sdk_writable?data+size:data;
    const auto* end=data+size;
    while(cursor<end) {
      MEMORY_BASIC_INFORMATION region{};
      if(!VirtualQuery(cursor,&region,sizeof(region)) || region.State!=MEM_COMMIT ||
         (region.Protect&PAGE_GUARD) ||
         !(region.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))
        throw std::runtime_error("native guest write destination is not writable");
      const auto* region_end=static_cast<const uint8_t*>(region.BaseAddress)+region.RegionSize;
      if(region_end<=cursor) throw std::runtime_error("invalid native CPU state extent");
      cursor=(std::min)(region_end,end);
    }
    return data;
  }
  uint64_t DoubleWord(uint32_t address) const {
    uint64_t value;
    std::memcpy(&value,Bytes(address,8),8);
    return std::byteswap(value);
  }
  void StoreDoubleWord(uint32_t address,uint64_t value) const {
    const auto* data=WritableBytes(address,8,8);
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(const_cast<uint8_t*>(data)),
                          static_cast<LONG64>(std::byteswap(value)));
  }
  uint32_t Add(uint32_t address, uint32_t offset) const {
    if (offset > UINT32_MAX - address) throw std::runtime_error("guest shader reference overflow");
    return address + offset;
  }
  std::string String(uint32_t address, size_t limit) const {
    std::string value;
    // Query at page boundaries, not once per character.
    while (value.size() < limit) {
      const auto at = Add(address, static_cast<uint32_t>(value.size()));
      const size_t count = (std::min)(limit - value.size(), size_t(4096 - (at & 4095)));
      const auto* p = Bytes(at, count);
      const auto* zero = static_cast<const uint8_t*>(std::memchr(p, 0, count));
      value.append(reinterpret_cast<const char*>(p), zero ? size_t(zero - p) : count);
      if (zero) return value;
    }
    throw std::runtime_error("unterminated guest shader string");
  }
 private:
  rex::memory::Memory* memory_;
};

NativeViewportState DecodeDrawViewport(const GuestViewportWords& snapshot) {
  const auto& words=snapshot.words;
  return MakeNativeDrawViewport(words[0],words[1],words[2],words[3],
    std::bit_cast<float>(words[4]),std::bit_cast<float>(words[5]),snapshot.scissor_enabled,
    {std::bit_cast<int32_t>(words[6]),std::bit_cast<int32_t>(words[7]),
     std::bit_cast<int32_t>(words[8]),std::bit_cast<int32_t>(words[9])});
}
template<class Reader>
NativeViewportState ReadDrawViewport(const Reader& reader,uint32_t device) {
  return DecodeDrawViewport(ReadViewportWords(reader,device));
}
std::array<float,4> ResolveBlendFactorForDraw(uint32_t device,
    const NativeRenderStateSnapshots::BlendWords* live);
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
template <typename Reader>
std::array<float,4> GuestBlendFactorForDraw(const Reader& reader,uint32_t device) {
  if(!REXCVAR_GET(edf_native_owned_render_state) && !REXCVAR_GET(edf_native_render_state_audit))
    return ReadBlendFactor(reader,device);
  NativeRenderStateSnapshots::BlendWords live{};
  const bool audit=REXCVAR_GET(edf_native_render_state_audit);
  if(audit) live=ReadGuestWords<4>(reader,reader.Add(device,10336));
  return ResolveBlendFactorForDraw(device,audit?&live:nullptr);
}
struct VertexParameterRange {
  std::string name;
  uint32_t first,count;
  ShaderBindings::FloatRegisterBinding normal,reversed;
};
struct RegisteredShader {
  uint32_t owner;
  std::unique_ptr<ShaderBindings> bindings;
  std::unique_ptr<QuadStream> quads;
  std::unique_ptr<ShaderBindings> reversed_bindings;
  std::unique_ptr<QuadStream> reversed_quads;
  uint64_t source_fingerprint=0;
  // Whether the reversed-depth variant reflects the same constant buffers as
  // the normal one, so its bytes can be mirrored from it at draw time instead
  // of every material and instance being uploaded into both.
  bool reversed_mirrors=false;
  struct ParameterBinding {
    ShaderBindings::FloatRegisterBinding binding;
    bool canvas_xy=false;
  };
  struct ParameterUpload {
    size_t index=0;
    std::array<const ShaderBindings::FloatRegisterBinding*,2> targets{};
    std::array<size_t,2> sizes{};
    size_t maximum=0;
    bool canvas_xy=false;
  };
  struct UploadPlan {
    std::vector<ParameterUpload> parameters;
    uint64_t optimized_out=0;
    bool ready=false;
  };
  struct ParameterPlan {
    std::array<std::vector<ParameterBinding>,4> groups;
    std::array<bool,4> ready{};
    std::array<std::vector<ShaderBindings::ResourceBinding>,2> textures;
    std::array<bool,2> textures_ready{};
    std::shared_ptr<const std::vector<VertexParameterRange>> vertex_ranges;
    // Normal-only and normal+reversed uploads use different active sets.
    std::array<std::array<UploadPlan,4>,2> uploads;
  };
  using ParameterOwner=std::weak_ptr<const NativeMaterialParameters::Groups>;
  std::array<std::map<ParameterOwner,ParameterPlan,std::owner_less<ParameterOwner>>,2> parameter_plans;
  std::array<ParameterOwner,2> last_plan_owner;
  std::array<ParameterPlan*,2> last_plan{};
  using PublishedProgramOwner=std::weak_ptr<const NativeSceneMaterialProgram>;
  std::map<PublishedProgramOwner,std::shared_ptr<const std::vector<VertexParameterRange>>,
    std::owner_less<PublishedProgramOwner>> published_vertex_ranges;
  std::shared_ptr<const std::vector<VertexParameterRange>> ResolvePublishedVertexRanges(
      const std::shared_ptr<const NativeSceneMaterialProgram>& program) {
    const auto found=published_vertex_ranges.find(PublishedProgramOwner(program));
    if(found!=published_vertex_ranges.end()) return found->second;
    if(!reversed_bindings) throw std::runtime_error("published vertex parameters have no reversed shader");
    auto ranges=std::make_shared<std::vector<VertexParameterRange>>();
    for(const auto& range:program->inputs.vertex_registers) {
      if(range.first>256 || range.count>256-range.first)
        throw std::runtime_error("invalid published activation register range");
      ranges->push_back({range.name,range.first,range.count,
        bindings->ResolveFloatRegisters(range.name),reversed_bindings->ResolveFloatRegisters(range.name)});
    }
    std::erase_if(published_vertex_ranges,[](const auto& entry) { return entry.first.expired(); });
    published_vertex_ranges.emplace(PublishedProgramOwner(program),ranges);
    return ranges;
  }
  ParameterPlan& MaterialPlan(const std::shared_ptr<const NativeMaterialParameters::Groups>& material,bool reverse) {
    const size_t slot=reverse?1:0;
    auto& previous=last_plan_owner[slot];
    if(last_plan[slot] && !previous.owner_before(material) && !material.owner_before(previous))
      return *last_plan[slot];
    auto& cache=parameter_plans[slot];
    auto found=cache.find(ParameterOwner(material));
    if(found==cache.end()) {
      std::erase_if(cache,[](const auto& entry){return entry.first.expired();});
      found=cache.try_emplace(ParameterOwner(material)).first;
    }
    previous=material; last_plan[slot]=&found->second;
    return found->second;
  }
  const std::vector<ShaderBindings::ResourceBinding>& ResolveTextures(
      const std::shared_ptr<const NativeMaterialParameters::Groups>& material,size_t group) {
    auto& plan=MaterialPlan(material,false);
    if(!plan.textures_ready[group]) {
      std::vector<ShaderBindings::ResourceBinding> fresh;
      fresh.reserve(material->textures[group].size());
      for(const auto& texture:material->textures[group]) fresh.push_back(bindings->ResolveResource(texture.name));
      plan.textures[group]=std::move(fresh); plan.textures_ready[group]=true;
    }
    return plan.textures[group];
  }
  const std::vector<ParameterBinding>& ResolveParameters(
      const std::shared_ptr<const NativeMaterialParameters::Groups>& material,size_t group,bool reverse) {
    auto& plan=MaterialPlan(material,reverse);
    if(!plan.ready[group]) {
      std::vector<ParameterBinding> fresh;
      fresh.reserve((*material)[group].size());
      const auto& destination=reverse?*reversed_bindings:*bindings;
      for(const auto& parameter:(*material)[group])
        fresh.push_back({destination.ResolveFloatRegisters(parameter.name),
          IsNativeCanvasXY(destination.shader().source_fingerprint,destination.shader().entry.name,parameter.name,group)});
      plan.groups[group]=std::move(fresh); plan.ready[group]=true;
    }
    return plan.groups[group];
  }
  const UploadPlan& ResolveUploads(
      const std::shared_ptr<const NativeMaterialParameters::Groups>& material,size_t group,bool alternate) {
    auto& plan=MaterialPlan(material,false).uploads[alternate?1:0][group];
    if(!plan.ready) {
      // Resolved vectors are immutable after publication; their binding tokens
      // stay owned by this shader's material plans for the upload plan's life.
      const auto& normal=ResolveParameters(material,group,false);
      const auto* reversed=alternate?&ResolveParameters(material,group,true):nullptr;
      UploadPlan fresh;
      fresh.parameters.reserve((*material)[group].size());
      for(size_t index=0;index<(*material)[group].size();++index) {
        ParameterUpload upload;
        upload.index=index; upload.canvas_xy=normal[index].canvas_xy;
        upload.targets={&normal[index].binding,reversed?&(*reversed)[index].binding:nullptr};
        for(size_t target=0;target<upload.targets.size();++target) {
          if(!upload.targets[target]) continue;
          const auto required=upload.targets[target]->bytes();
          if(!required) { ++fresh.optimized_out; upload.targets[target]=nullptr; continue; }
          upload.sizes[target]=(group&1)?required:size_t((*material)[group][index].registers)*16;
          upload.maximum=std::max(upload.maximum,upload.sizes[target]);
        }
        if(upload.targets[0] || upload.targets[1]) fresh.parameters.push_back(upload);
      }
      fresh.ready=true; plan=std::move(fresh);
    }
    return plan;
  }
  std::shared_ptr<const std::vector<VertexParameterRange>> ResolveVertexRanges(
      const std::shared_ptr<const NativeMaterialParameters::Groups>& material) {
    auto& plan=MaterialPlan(material,false);
    if(!plan.vertex_ranges) {
      if(!reversed_bindings) throw std::runtime_error("vertex parameter has no reversed binding plan");
      auto fresh=std::make_shared<std::vector<VertexParameterRange>>();
      for(size_t group=0;group<2;++group) {
        const auto& normal=ResolveParameters(material,group,false);
        const auto& reversed=ResolveParameters(material,group,true);
        size_t index=0;
        for(const auto& parameter:(*material)[group]) {
          if(parameter.registers) {
            if(parameter.first>256 || parameter.registers>256-parameter.first)
              throw std::runtime_error("invalid vertex parameter register range");
            fresh->push_back({parameter.name,parameter.first,parameter.registers,normal[index].binding,reversed[index].binding});
          }
          ++index;
        }
      }
      // Publish only a complete plan. The active shared owner survives material
      // retirement; binding tokens still reject a replaced shader generation.
      plan.vertex_ranges=std::move(fresh);
    }
    return plan.vertex_ranges;
  }
};
// The vertex bindings a draw with this depth convention uses, with the
// reversed variant brought up to date from the normal one where it mirrors it.
// Every draw path that picks a variant goes through here, so a variant can
// never be drawn with the constants of the activation before last.
ShaderBindings& VertexBindingsForDraw(RegisteredShader& shader,bool reverse_depth) {
  if(!reverse_depth) return *shader.bindings;
  if(shader.reversed_mirrors) shader.reversed_bindings->MirrorConstantsFrom(*shader.bindings);
  return *shader.reversed_bindings;
}
struct TextureCreation {
  uint32_t width, height, depth, levels, usage, format, pool, type, caller;
};
struct RegisteredTarget {
  uint32_t texture_handle, surface_handle;
  NativeRenderTarget native;
};
NativeBufferWrites& BufferWrites() {
  static NativeBufferWrites writes;
  return writes;
}
struct GuestStream {
  uint32_t resource, offset, stride;
};
struct NativeScene {
  NativeRenderTarget color;
  NativeDepthTarget depth;
  NativeRenderTarget output;
  uint32_t output_surface=0;
  uint32_t color_surface=0;
  uint32_t samples=1;
  bool frame_complete=false;
  std::unordered_map<uint32_t,NativeRenderTarget> direct_outputs;
};
struct SurfaceCreation { uint32_t width,height,format,msaa; };
struct EmbeddedShader { uint32_t source; bool pixel; };
struct MoviePlaneLock { uint32_t texture,pitch,pixels; };
struct MovieDecodeLocks { std::vector<MoviePlaneLock> planes; bool failed=false; };
thread_local MovieDecodeLocks* active_movie_decode=nullptr;
struct DrawVisibility {
  Microsoft::WRL::ComPtr<ID3D11Query> query;
  std::array<uint32_t,4> key; // VS, PS, raster state, depth state.
};
struct NativePacingState {
  // Separate from the rendering mutex: sleeping must not block host UI paints.
  std::mutex mutex;
  NativePacingClock clock;
  uint64_t calls=0;
};
NativePacingState& PacingState() {
  static NativePacingState state;
  return state;
}
struct Bridge {
  // Game command ordering is separate from immediate-context access. A swap
  // may hold this gate while releasing mutex between polls so the host can paint.
  std::recursive_mutex submissions;
  std::mutex mutex;
  std::filesystem::path root;
  bool initialized=false;
  std::string scene_backend_name;
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  // Created when --edf_native_backend names one. The renderer still draws
  // through its direct D3D11 path; this exists so the backend can be created
  // and reported inside the real process, which is where device creation
  // actually fails, and so paths can be moved onto it one at a time.
  std::unique_ptr<edf::native::NativeRenderBackend> backend;
  // The scene's resources, separate from the selection above because the two
  // answer different questions while the port is under way: --edf_native_backend
  // is what the player picked, --edf_native_scene_backend is what the half-ported
  // scene can actually share targets with. See the cvar.
  std::shared_ptr<edf::native::NativeRenderBackend> scene_backend;
  NativeSceneAdapter scene_adapter;
  uint64_t scene_publication_tick=0,scene_published_selections=0,scene_current_selections=0;
  NativeSceneRenderer scene_renderer;
  std::vector<NativeSceneSnapshot> scene_recorded_snapshots;
  uint64_t scene_native_objects=0,scene_native_draws=0,scene_native_fallbacks=0;
  uint64_t scene_native_direct_instances=0,scene_native_direct_retries=0;
  uint64_t scene_native_direct_worlds=0;
  std::set<std::string> scene_native_reasons;
  std::unique_ptr<NativeFrameHandoff> presentation_frames;
  std::weak_ptr<GuestMeshWatchAudit> mesh_watch_audit;
  std::optional<NativeDisplayGamma> display_gamma;
  uint32_t display_gamma_device=0;
  std::map<uint32_t,uint32_t> native_published_completions;
  NativeSubmissionCursors submission_cursors;
  struct SwapClock { NativePacingClock clock; uint64_t sampled=0;
    std::unique_ptr<NativeFrameFlight> flight; };
  std::map<uint32_t,SwapClock> swap_clocks;
  std::unordered_map<uint32_t, RegisteredShader> shaders;
  std::shared_ptr<const std::vector<VertexParameterRange>> active_vertex_parameters;
  uint64_t instance_parameter_updates=0, instance_parameter_errors=0;
  NativeSceneSources scene_sources;
  NativeSceneMembership scene_membership;
  NativeStaticWalkPlans static_walk_plans;
  NativeStaticWalkAudit static_walk_audit;
  uint64_t static_walk_lists=0,static_walk_misses=0,static_walk_stale=0,static_walk_members=0;
  uint64_t static_walk_direct_reuses=0,static_walk_source_reuses=0,static_walk_abandoned=0,static_walk_bucket_native=0;
  uint64_t scene_membership_events=0,scene_membership_lists=0,scene_membership_nodes=0;
  uint64_t scene_membership_checks=0,scene_membership_mismatches=0;
  uint64_t scene_source_draws=0,scene_source_misses=0;
  uint64_t scene_source_publications=0;
  uint64_t scene_world_publications=0,scene_world_reused=0,scene_world_reads=0,scene_world_checks=0,scene_world_mismatches=0;
  uint64_t scene_queue_instances=0,scene_queue_groups=0,scene_queue_fallbacks=0;
  uint64_t scene_group_material_captures=0,scene_group_material_reused=0;
  uint64_t scene_asset_examined=0,scene_asset_created=0,scene_asset_rejected=0;
  struct SceneGeometryLoad {
    NativeSceneGeometrySource source;
    uint64_t revision=0,vertex_generation=0,index_generation=0;
    std::array<NativeBufferWrites::ObservedVersion,2> versions{};
    std::shared_ptr<const NativeDeclaration> declaration;
    Microsoft::WRL::ComPtr<ID3DBlob> shader;
    NativeRecordedReads reads; // Descriptor bytes behind `source`.
  };
  std::map<uint32_t,SceneGeometryLoad> scene_geometry_loads;
  struct SceneMaterialLoad {
    uint64_t revision=0;
    uint32_t material=0;
    std::shared_ptr<const NativeMaterialParameters::Groups> schema;
    std::shared_ptr<const NativeSceneGroupMaterial> published;
    // Program bytes only: pass, shader, texture, sampler and state inputs.
    // Constant values change per frame and are refreshed through `constants`.
    NativeRecordedReads reads;
    NativeSceneMaterialConstantLayout constants;
  };
  std::map<uint32_t,SceneMaterialLoad> scene_material_loads;
  // Model pass (821C9C20): material programs keyed by pass record address and
  // retained geometry keyed by (batch descriptor, vertex shader). Validated at
  // each use; never trusted across a changed input.
  struct ModelPassLoad {
    std::shared_ptr<const NativeMaterialParameters::Groups> schema;
    std::shared_ptr<const NativeSceneGroupMaterial> published;
    NativeRecordedReads reads; // Program bytes only; constant values are re-read at each use.
    NativeSceneMaterialConstantLayout layout;
    uint64_t failed_tick=UINT64_MAX;
  };
  std::unordered_map<uint32_t,ModelPassLoad> model_pass_loads;
  struct ModelGeometryLoad {
    NativeSceneGeometrySource source;
    uint64_t vertex_generation=0,index_generation=0;
    std::array<NativeBufferWrites::ObservedVersion,2> versions{};
    std::shared_ptr<const NativeDeclaration> declaration;
    Microsoft::WRL::ComPtr<ID3DBlob> shader;
    std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
  };
  std::map<std::pair<uint32_t,uint32_t>,ModelGeometryLoad> model_geometry_loads;
  uint64_t model_pass_loaded=0,model_pass_reused=0,model_pass_constants=0,model_geometry_loaded=0;
  uint64_t scene_preload_group_revision=UINT64_MAX;
  uint64_t scene_geometry_loaded=0,scene_geometry_reused=0,scene_geometry_deferred=0;
  uint64_t scene_geometry_unchanged=0,scene_geometry_verified=0,scene_material_unchanged=0;
  uint64_t scene_material_constants=0;
  std::set<std::string> scene_geometry_reasons;
  uint64_t scene_material_loaded=0,scene_material_reused=0,scene_material_deferred=0;
  uint64_t scene_material_constructed=0,scene_material_rejected=0;
  uint64_t scene_material_binding_bypasses=0;
  uint64_t scene_geometry_draw_bypasses=0,scene_geometry_draw_verified=0;
  std::set<std::string> scene_material_reasons;
  uint64_t scene_visibility_candidates=0,scene_visibility_retained=0,scene_visibility_selected=0;
  uint64_t scene_visibility_checks=0,scene_visibility_mismatches=0;
  std::unordered_map<uint32_t, EmbeddedShader> embedded_shaders;
  std::unordered_map<uint32_t, NativeTexture> textures;
  std::unordered_map<uint32_t, TextureCreation> texture_creations;
  std::unordered_map<uint32_t, RegisteredTarget> render_targets;
  std::unordered_map<uint32_t, NativeDepthTarget> depth_targets;
  std::unordered_map<uint32_t, SurfaceCreation> surface_creations;
  std::unordered_map<uint32_t, NativeScene> scenes;
  uint32_t active_scene = 0;
  std::set<uint32_t> untiled_devices;
  std::unique_ptr<NativeGpuTimer> scene_gpu_timer;
  uint32_t scene_gpu_timer_owner=0;
  bool scene_gpu_timer_resolved=false;
  uint32_t active_output = 0, output_captures = 0;
  uint64_t loading_trace_frames=0;
  std::array<uint64_t,4> loading_trace_routes{};
  std::chrono::steady_clock::time_point loading_trace_reported{};
  std::chrono::steady_clock::time_point bloom_parameters_reported{};
  uint64_t output_draws=0;
  uint64_t output_unhandled=0;
  // Sentinel distinguishes "not observed" from a real zero register word.
  uint64_t vertex_center_word=UINT64_MAX;
  uint64_t shared_constant_activations=0,shared_constant_unsupplied=0,shared_constant_split_storage=0;
  uint64_t shared_constant_both_supplied=0;
  uint32_t last_activation_instance=0,last_activation_vertex=0,last_activation_pixel=0;
  uint64_t repeat_activations=0;
  // Whether the scene backend has a frame open. Opened lazily by the first
  // thing that records into it and closed at the guest's swap barrier, which
  // is the only point in the frame where the renderer already knows the frame
  // is over. A backend that records has to be told where a frame ends: D3D11
  // did not, which is why nothing needed this until now.
  bool scene_frame_open=false;
  uint64_t scene_frames=0;
  // Recorded operations in the open frame. A backend frame has to be bounded
  // by something: the guest's swap alone is not, because the renderer records
  // far more between two swaps than one command list should carry.
  uint64_t scene_frame_operations=0;
  uint64_t scene_frame_splits=0;
  // The scene's finished frame, in a surface the window's backend can open.
  //
  // This is how a scene drawn on one backend reaches a window presented by
  // another. The D3D11 path hands the compositor an ID3D11ShaderResourceView
  // instead, which is exactly what a D3D12 scene cannot produce.
  std::shared_ptr<edf::native::NativeBackendSharedSurface> scene_shared;
  std::array<std::shared_ptr<NativeBackendSharedSurface>,NativeBackendFrameQueue::kSlots> scene_shared_slots;
  std::array<uint64_t,NativeBackendFrameQueue::kSlots> scene_shared_slot_generations{};
  std::optional<size_t> scene_shared_slot;
  uint64_t scene_shared_next_generation=0;
  NativeBackendFrameQueue scene_frame_queue;
  uint32_t scene_shared_width=0,scene_shared_height=0;
  // Set when a copy into it has been recorded and not yet signalled. The
  // signal is a queue signal on the backends that have a queue, so it has to
  // happen after the frame is submitted, not while it is still open.
  bool scene_shared_pending=false;
  uint64_t scene_shared_sequence=0,scene_shared_generation=0;
  std::optional<NativeDisplayGamma> scene_shared_gamma;
  NativeFrameKind scene_shared_kind=NativeFrameKind::PartialScene;
  bool scene_shared_refused=false;
  // What the last recorded draw left the recorder holding.
  //
  // The same reasoning the direct path already uses: 77.4% of this game's
  // draws repeat the one before them in everything but the constants an
  // activation patched between them, so re-sending the pipeline, the targets,
  // the viewport and the material is the same calls with the same arguments.
  // The direct path skips those and this has to as well, or recording is
  // slower than the thing it replaces for no reason anyone would accept.
  struct RecordedBindings {
    bool valid=false;
    // Bumped by anything that binds the context directly. On the adopted D3D11
    // backend the recorder and the direct paths share one context, so a direct
    // bind invalidates what the recorder believes is still set.
    uint64_t bind_generation=0;
    uint64_t frame=0;
    edf::native::NativeBackendPipeline* pipeline=nullptr;
    // What that pipeline was built from. Compared here so a repeat draw does
    // not go through the backend's cache at all: that lookup builds a string
    // key per call, which is a heap allocation on a path that runs a million
    // times a minute.
    uint64_t vertex_id=0,pixel_id=0,layout_id=0;
    edf::native::RenderStateWords state{};
    edf::native::NativeBackendTopology topology=edf::native::NativeBackendTopology::TriangleList;
    uint32_t render_targets=0,sample_count=0,dsv_format=0;
    std::array<uint32_t,8> rtv_format{};
    std::array<edf::native::NativeBackendRenderTarget*,8> colors{};
    uint32_t color_count=0;
    edf::native::NativeBackendRenderTarget* depth=nullptr;
    D3D11_VIEWPORT viewport{};
    D3D11_RECT scissor{};
    bool scissor_enabled=false;
    const edf::native::ShaderBindings* pixel=nullptr;
    uint64_t pixel_resources=0;
    bool blend_factor_needed=false;
    std::array<float,4> blend_factor{};
    // The bytes last staged for each constant buffer, so an unchanged buffer
    // is not copied into the upload ring again.
    edf::native::NativeConstantCache vertex_constants,pixel_constants;
  } recorded;
  uint64_t recorded_draws=0,recorded_pipeline_skips=0,recorded_material_skips=0;
  uint64_t recorded_constant_skips=0;
  // Declared after the backend so it is destroyed before it: the preview
  // thread uses the backend on every tick and must be stopped first.
  std::unique_ptr<edf::native::NativeD3D12Preview> backend_preview;
  // Run-length accounting for the XUI path, the same question the indexed
  // audit answered: how many consecutive draws differ only in things a batch
  // would carry per-item, and how many change state that a batch cannot.
  // Bumped by every path that binds a target or render state, so the indexed
  // path can tell whether anything has bound since it last did. Without this a
  // skip would compare against its own cache and miss that another path had
  // replaced the state underneath it.
  uint64_t bind_generation=0,indexed_bind_generation=0;
  edf::native::RenderStateWords indexed_bind_key{};
  uint32_t indexed_bind_target=0,indexed_bind_scene=0,indexed_bind_output=0;
  uint32_t indexed_bind_vertex=0,indexed_bind_pixel=0;
  bool indexed_bind_reversed=false;
  uint64_t indexed_materials_reused=0;
  bool indexed_bind_valid=false;
  uint64_t indexed_binds_skipped=0,indexed_binds_bound=0;
  uint64_t xui_batch_draws=0,xui_batch_runs=0,xui_batch_run=0,xui_batch_longest=0;
  uint64_t xui_batch_collapsible=0,xui_last_state=0,xui_last_constants=0;
  uint64_t xui_constants_differ=0;
  std::array<uint32_t,12> last_batch_key{};
  uint64_t batch_draws=0,batch_runs=0,batch_run=0,batch_run_total=0,batch_longest=0,batch_collapsible=0;
  uint64_t instance_shape=0,last_instance_shape=0,batch_shape_breaks=0;

  std::set<std::array<uint32_t,2>> shared_constant_pairs;
  std::set<std::array<uint32_t,3>> shared_constant_storage_reported;
  std::set<std::array<uint32_t,3>> shared_constant_reported;
  uint64_t movie_uploads=0, movie_upload_errors=0;
  uint64_t movie_draws=0, movie_draw_errors=0;
  // Movie drawing can run on a helper between main-thread UI swaps. Keep both
  // clocks paced until a completed 3D scene takes over or movie_pacing sees
  // NativeMoviePacing::kIdleSwaps swaps in a row without a new movie draw,
  // rather than clearing on the first UI-only swap that did not draw the movie.
  std::atomic<bool> movie_pacing_active{false};
  NativeMoviePacing movie_pacing;
  std::unique_ptr<ShaderBindings> movie_vertex,movie_pixel,movie_pixel_sd;
  std::array<std::optional<NativeMovieBindings>,2> movie_bindings;
  std::unique_ptr<QuadStream> movie_vertices;
  std::unique_ptr<ShaderBindings> xui_vertex,xui_pixel;
  std::unique_ptr<ShaderBindings> xui_reversed_vertex;
  std::optional<NativeXuiVertexBindings> xui_vertex_bindings,xui_reversed_vertex_bindings;
  std::array<std::optional<NativeXuiPixelBindings>,3> xui_pixel_bindings;
  std::unique_ptr<ShaderBindings> xui_solid_pixel,xui_mask_pixel;
  std::unique_ptr<PositionTriangleStream> xui_vertices;
  uint64_t xui_draws=0,xui_errors=0;
  std::unique_ptr<ShaderBindings> font_vertex,font_pixel;
  std::optional<NativeFontBindings> font_bindings;
  std::unique_ptr<QuadStream> font_vertices;
  uint64_t font_draws=0,font_errors=0;
  uint64_t utility_draws=0,utility_errors=0;
  uint64_t utility_3d_draws=0,utility_3d_errors=0;
  std::array<bool,6> scene_immediate_variants_reported{};
  std::array<bool,4> utility_variants_reported{};
  std::set<std::array<uint32_t,4>> unsupported_output_pairs;
  uint64_t immediate_requests=0,immediate_empty=0,immediate_submitted=0,immediate_unsubmitted=0;
  std::set<std::array<uint32_t,5>> immediate_unsubmitted_paths;
  std::chrono::steady_clock::time_point immediate_coverage_reported{};
  uint64_t scene_begins = 0, scene_ends = 0;
  uint64_t scene_resolves = 0,scene_resolve_errors = 0;
  uint64_t scene_indexed_start = 0;
  uint32_t scene_captures = 0;
  uint64_t indexed_output_frames = 0;
  uint64_t indexed_trace_frame = 0;
  uint64_t post_input_capture_frame = 0;
  uint32_t post_input_capture_pass = 0;
  uint32_t indexed_trace_draws = 0;
  std::vector<DrawVisibility> visibility;
  std::set<std::array<uint32_t,4>> clip_probes;
  uint32_t color_probe_draws=0;
  bool color_probe_done=false;
  uint64_t depth_clears = 0, depth_clear_skips = 0, depth_errors = 0;
  // Backend samplers, keyed by the guest words they were decoded from. The
  // backend owns the sampler objects and caches them by combination too; this
  // map only saves decoding the same words again.
  std::map<SamplerStateWords, edf::native::NativeBackendSampler*> samplers;
  std::map<RenderStateWords,NativeRenderState> render_states;
  std::map<std::pair<uint32_t,uint32_t>,GuestStream> streams;
  std::map<uint32_t,uint32_t> index_bindings;
  std::map<uint32_t,uint32_t> declaration_bindings;
  NativeShaderState shader_bindings;
  NativeRenderStateSnapshots render_state_snapshots;
  NativeDeclarations declarations;
  NativeMaterialParameters material_parameters;
  NativeGeneratedIndexCache generated_indices;
  NativeModelBuffers model_buffers{&BufferWrites()};
  uint64_t buffer_update_notifications=0;
  uint64_t buffer_write_batches=0;
  uint64_t buffer_write_all_batches=0;
  uint64_t reported_mesh_mismatches=0;
  NativeMeshCache meshes;
  // Immediate guest buffers may be reused or mutated every draw. This cache
  // compares all bytes and stays separate from long-lived scene geometry.
  NativeMeshCache immediate_meshes{4*1024*1024,256,true};
  std::map<uint32_t,std::unique_ptr<NativeCompletionQueue>> completion_queues;
  std::map<uint32_t,std::unique_ptr<NativePresentProfiler>> present_profilers;
  std::set<uint32_t> completion_faults;
  uint64_t completion_submits=0;
  std::unordered_map<uint32_t,std::unique_ptr<NativeSignalQueue>> signal_queues;
  // Registry/context -> delivery, or SDK global -> delivery. Never delivery ->
  // registry/context/global. CPU delivery must not depend on renderer progress.
  std::mutex signal_delivery_mutex;
  std::unordered_map<uint32_t,NativeSignalDelivery> signal_deliveries;
  uint64_t completion_waits=0,completion_waits_native_pending=0;
  uint64_t completion_validated_waits=0;
  uint64_t indexed_draws = 0, indexed_uploads = 0, indexed_errors = 0, indexed_submitted = 0;
  std::array<uint64_t,4> indexed_ownership{}; // Bit0: registered VB; bit1: registered IB.
  uint64_t indexed_ownership_attempts=0,indexed_physical_pairs=0;
  uint64_t indexed_empty_requests=0,indexed_nonempty_submitted=0,indexed_unsubmitted_requests=0;
  std::set<std::array<uint32_t,5>> indexed_unsubmitted_paths;
  // Identity is the contract, not the caller: see native_contract_ledger.h.
  edf::native::NativeContractLedger contracts;
  std::chrono::steady_clock::time_point indexed_coverage_reported{};
  uint64_t indexed_outside_scene = 0;
  uint64_t sampler_bindings = 0;
  std::vector<std::pair<uint32_t,uint32_t>> target_stack;
  uint32_t active_target = 0;
  uint64_t target_begins = 0, target_resolves = 0, target_unwritten = 0;
  uint64_t immediate_draws = 0;
  uint64_t native_quad_draws = 0, native_quad_errors = 0;
  uint64_t color_clears = 0, color_clear_skips = 0;
  uint32_t active_vertex = 0, linked_vertex = 0, linked_pixel = 0;
  // Shader pairs whose varyings have been checked against each other. The
  // check reflects both signatures, and the pair changes hundreds of times a
  // frame; a pair only needs it once per registration of its shaders.
  std::set<std::pair<uint32_t,uint32_t>> validated_links;
  uint64_t texture_loads = 0, texture_errors = 0;
  uint64_t texture_bindings = 0, texture_missing = 0, texture_binding_errors = 0;
  uint64_t activations = 0, misses = 0, parameter_uploads = 0, optimized_out = 0, parameter_errors = 0;
};
Bridge& State() { static Bridge state; return state; }
// State().mutex for one visibility walk; see native_scene_walk_lock.h.
using BridgeWalkLock=NativeWalkLockScope<std::mutex>;
using BridgeGuestCall=NativeWalkGuestCall<std::mutex>;
// The current tree walk's camera view, shared with the list gathers it runs
// under its own scope: one read per walk, again only after a guest call.
struct BridgeWalkView {
  const BridgeWalkLock* scope; uint32_t context;
  NativeGuestCallCached<NativeSceneVisibilityView> view;
};
inline thread_local BridgeWalkView* bridge_walk_view=nullptr;
// Defined below, next to the selection it mirrors; declared here because
// the texture hook, further up, is the first thing to create a scene resource.
edf::native::NativeRenderBackend& EnsureSceneBackendLocked(Bridge& state);
// The scene backend's recorder, with a frame open.
edf::native::NativeBackendRecorder& SceneRecorderLocked(Bridge& state);
// Records a copy of a finished frame into the scene's shared surface. Does
// nothing when the scene is on D3D11, whose frames reach the window through
// the compositor, or when the backend cannot share.
void PublishSceneSharedLocked(Bridge& state,const NativeRenderTarget& output,
                             NativeFrameKind kind=NativeFrameKind::PartialScene);
// A scene's ordinary output as a BMP, by whichever route its backend allows.
// The D3D11 one reads the surface directly; anything else has to close the
// open frame first, because the readback waits for the GPU and waiting on work
// that has not been submitted never returns.
template <typename Scene>
std::vector<uint8_t> CaptureOutputBmp(Bridge& state,Scene& scene);
// Closes the frame if one is open. Safe to call when none is.
void SubmitSceneFrameLocked(Bridge& state);
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
void CaptureScene(Bridge& state,uint32_t owner) {
  const auto prefix=REXCVAR_GET(edf_native_scene_capture);
  if (prefix.empty() || state.scene_captures>=3 || state.indexed_submitted==state.scene_indexed_start) return;
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
    state.textures.insert_or_assign(handle,scene.color.sampled);
    if (++state.scene_resolves<=5 || state.scene_resolves%1000==0)
      REXLOG_INFO("Native HDR scene resolve: count={}, texture={:#x}, initialized={}, frame_complete={}",
        state.scene_resolves,handle,scene.color.sampled.content_valid,scene.frame_complete);
  } catch (const std::exception& error) {
    if (++state.scene_resolve_errors<=10) REXLOG_ERROR("Native HDR scene resolve: {}",error.what());
  }
}

void ForgetOwner(uint32_t owner) {
  auto& state = State();
  // Registry/cache retirement does not submit commands. The registry mutex
  // excludes native users; bound GPU resources retain their D3D references.
  std::lock_guard lock(state.mutex);
  const auto erased = std::erase_if(state.shaders, [owner](const auto& item) { return item.second.owner == owner; });
  state.active_vertex = state.linked_vertex = state.linked_pixel = 0;
  state.validated_links.clear();
  state.active_vertex_parameters.reset();
  if (erased) REXLOG_INFO("Native shader bridge: released {} shaders for owner={:#x}", erased, owner);
  if (erased) { state.meshes.Clear(); state.immediate_meshes.Clear(); }
}

template<class Original>
void ImportTexture(PPCContext& ctx, uint8_t* base, Original original) {
  if (!REXCVAR_GET(edf_native_shader_bridge)) { original(ctx, base); return; }
  const GuestReader reader(base);
  uint32_t output = 0;
  std::vector<uint8_t> image;
  try {
    HookTiming snapshot_timing(HookPhase::TextureSnapshot);
    // Shared D3DX image loader sub_82201458, reached by both engine loaders
    // and direct callers. The output parameter is at entry SP+148; the
    // function's 1520-byte frame accesses it at SP+1668.
    output = reader.Word(reader.Add(ctx.r1.u32, 148));
    const auto address = ctx.r4.u32;
    const auto size = ctx.r5.u32;
    if (size > 256 * 1024 * 1024) throw std::runtime_error("texture source exceeds size limit");
    const auto* bytes = reader.Bytes(address, size);
    image.assign(bytes, bytes + size);
  } catch (const std::exception& error) {
    REXLOG_ERROR("Native texture bridge: snapshot failed: {}", error.what());
  }
  {
    struct LoaderScope {
      LoaderScope() { ++texture_loader_depth; }
      ~LoaderScope() { --texture_loader_depth; }
    } scope;
    HookTiming original_timing(HookPhase::TextureOriginal);
    original(ctx, base);
  }
  if (image.empty() || ctx.r3.s32 < 0) return;
  auto& state = State();
  HookTiming lock_timing(HookPhase::TextureLock);
  // CreateNativeDdsTexture uses device resource creation with initial data,
  // not immediate-context uploads. Registry publication is serialized below;
  // it must not wait for the rendering submission barrier's refresh sleeps.
  std::lock_guard lock(state.mutex);
  lock_timing.Finish();
  try {
    const auto handle = reader.Word(output);
    if (!handle) throw std::runtime_error("guest texture creation returned null");
    // Erase first: a failed replacement must not leave an old native image
    // associated with an address the guest has reused for a new resource.
    state.textures.erase(handle);
    if (!state.initialized) throw std::runtime_error("native texture bridge not initialized");
    HookTiming create_timing(HookPhase::TextureCreate);
    auto native = CreateNativeDdsTexture(EnsureSceneBackendLocked(state), image);
    create_timing.Finish();
    ++state.texture_loads;
    REXLOG_INFO("Native texture bridge: handle={:#x}, {}x{}, mips={}, cube={}, loads={}",
                handle, native.width, native.height, native.mip_count, native.cube, state.texture_loads);
    state.textures.emplace(handle, std::move(native));
  } catch (const std::exception& error) {
    ++state.texture_errors;
    REXLOG_ERROR("Native texture bridge: {} (errors={})", error.what(), state.texture_errors);
  }
}

void RecordEmbeddedShader(const GuestReader& reader,uint32_t output,uint32_t source,bool pixel,uint32_t caller) {
  const auto handle=reader.Word(output);
  if (!handle) throw std::runtime_error("middleware shader creation returned null");
  reader.Bytes(source,4);
  auto& state=State();
  std::lock_guard lock(state.mutex);
  state.embedded_shaders.insert_or_assign(handle,EmbeddedShader{source,pixel});
  REXLOG_INFO("Native middleware shader source: handle={:#x}, source={:#x}, pixel={}, caller={:#x}",handle,source,pixel,caller);
}
Effect SnapshotEffect(const GuestReader& reader, uint32_t data) {
  Effect result;
  result.source = reader.String(reader.Add(data, reader.Word(reader.Add(data, 4))), 16 * 1024 * 1024);
  const auto count = reader.Word(reader.Add(data, 16));
  if (count > 4096) throw std::runtime_error("invalid guest shader count");
  const auto table = reader.Add(data, reader.Word(reader.Add(data, 20)));
  reader.Bytes(table, size_t(count) * 12);
  for (uint32_t i = 0; i < count; ++i) {
    const auto record = reader.Add(table, i * 12);
    const auto stage = reader.Word(record);
    if (stage > 1) throw std::runtime_error("invalid guest shader stage");
    result.entries.push_back({stage == 1,
      reader.String(reader.Add(record, reader.Word(reader.Add(record, 4))), 256),
      reader.String(reader.Add(record, reader.Word(reader.Add(record, 8))), 64)});
  }
  return result;
}

// One pool for the run. Created on first registration so a run that loads no
// shaders never starts a thread.
NativeDecodeWorkers& ShaderWorkers() {
  static NativeDecodeWorkers workers([] {
    const auto requested=REXCVAR_GET(edf_native_shader_workers);
    if(requested==0) return uint32_t(0);
    if(requested>0) return uint32_t((std::min)(requested,64));
    // One per core, less the one doing the loading, capped: compilation is
    // memory-bound enough that more threads stop helping well before the core
    // count on a large machine.
    const auto cores=std::thread::hardware_concurrency();
    return uint32_t((std::min)(cores>1?cores-1:1u,8u));
  }());
  return workers;
}

void RegisterShaders(const GuestReader& reader, uint32_t owner, const Effect& effect) {
  HookTiming registration_timing(HookPhase::ShaderRegistration);
  auto& state = State();
  HookTiming lock_timing(HookPhase::ShaderLock);
  // Compilation, device-only creation and registry replacement issue no
  // immediate-context commands. Keep registry serialization, not refresh waits.
  std::lock_guard lock(state.mutex);
  lock_timing.Finish();
  if (!state.initialized) throw std::runtime_error("native shader bridge not initialized");
  const auto count = reader.Word(reader.Add(owner, 8));
  if (count != effect.entries.size()) throw std::runtime_error("guest compiled shader count mismatch");
  const auto records = reader.Word(owner);
  reader.Bytes(records, size_t(count) * 72);
  // Guest memory is read here, on the calling thread, before anything is
  // handed to a worker: the reads must happen while the game is inside this
  // call, and a worker touching guest memory would be reading it at a time the
  // game never agreed to.
  std::vector<uint32_t> handles(count);
  for (uint32_t i = 0; i < count; ++i) {
    const auto& entry = effect.entries[i];
    const auto record = reader.Add(records, i * 72);
    if (reader.Word(reader.Add(record, 68)) != uint32_t(entry.pixel))
      throw std::runtime_error("guest compiled shader stage mismatch");
    handles[i] = reader.Word(reader.Add(record, entry.pixel ? 4 : 0));
    if (!handles[i]) throw std::runtime_error("guest compiled shader handle is null");
  }

  // Compilation is the part worth threading: each entry costs milliseconds and
  // a registration brings several, which is the batch the per-draw path does
  // not have. D3DCompile is thread-safe and ID3D11Device resource creation is
  // free-threaded; nothing below touches the immediate context or the bridge
  // state, both of which are not.
  std::vector<RegisteredShader> built(count);
  std::vector<std::exception_ptr> failures(count);
  auto& workers = ShaderWorkers();
  std::vector<uint64_t> tickets;
  tickets.reserve(count);
  const auto source_path = state.root / "Shader" / "guest.fx";
  auto* device = state.device.Get();
  for (uint32_t i = 0; i < count; ++i)
    tickets.push_back(workers.Submit([&, i] {
      HookTiming entry_timing(HookPhase::ShaderEntry);
      try {
        const auto& entry = effect.entries[i];
        auto shader = CompileNativeShader(device, effect, entry, source_path);
        if(!entry.pixel) AddNativeWorldInstancing(shader,effect,source_path);
        built[i] = RegisteredShader{owner, std::make_unique<ShaderBindings>(device, std::move(shader))};
        built[i].source_fingerprint = EffectSourceFingerprint(effect.source);
        if (!entry.pixel) {
          auto reversed=CompileNativeShader(device,effect,entry,source_path,true);
          AddNativeWorldInstancing(reversed,effect,source_path,true);
          built[i].reversed_bindings = std::make_unique<ShaderBindings>(device,std::move(reversed));
          built[i].reversed_mirrors=built[i].reversed_bindings->SharesConstantLayout(*built[i].bindings);
        }
      } catch (...) {
        failures[i] = std::current_exception();
      }
    }));
  for (const auto ticket : tickets) workers.Wait(ticket);
  // Rethrown in entry order, so which entry is blamed does not depend on which
  // worker happened to finish first.
  for (uint32_t i = 0; i < count; ++i) if (failures[i]) std::rethrow_exception(failures[i]);
  for (uint32_t i = 0; i < count; ++i)
    if (built[i].reversed_bindings && !built[i].reversed_mirrors)
      REXLOG_INFO("Native shader bridge: reversed variant of {} reflects a different constant layout; both variants take their own uploads",
                  effect.entries[i].name);

  std::unordered_map<uint32_t, RegisteredShader> fresh;
  for (uint32_t i = 0; i < count; ++i)
    if (!fresh.emplace(handles[i], std::move(built[i])).second)
      throw std::runtime_error("duplicate guest shader handle");
  // Handle equality is not shader-generation equality. The linked pair/active
  // ranges must not survive a replacement at the same guest address. Invalidate
  // before mutating the registry, including a handle reused by another owner.
  const bool replacing=std::any_of(state.shaders.begin(),state.shaders.end(),
    [&](const auto& item){return item.second.owner==owner || fresh.contains(item.first);});
  if(replacing) {
    state.active_vertex=state.linked_vertex=state.linked_pixel=0;
    state.active_vertex_parameters.reset();
    // Mesh caches independently compare owned bytecode identity on acquisition.
  }
  // Replace the entire owner on reload so address reuse cannot select old code.
  std::erase_if(state.shaders, [owner](const auto& item) { return item.second.owner == owner; });
  for (auto& [handle, shader] : fresh) state.shaders.insert_or_assign(handle, std::move(shader));
  // A handle may now name different code; every pair is checked again.
  state.validated_links.clear();
  REXLOG_INFO("Native shader bridge: owner={:#x}, {} guest shaders registered, {} resident",
              owner, count, state.shaders.size());
}
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
void RegisterRenderTarget(const GuestReader& reader, uint32_t owner) {
  const auto format = reader.Word(reader.Add(owner,16));
  const auto surface_format = reader.Word(reader.Add(owner,20));
  // Verified post-processing pairs in sub_820B1028. Other pairs need their
  // component swizzles and resolve conversions recovered before enabling them.
  const bool luminance=format==0x2da2ab5e && surface_format==0x2da2aba4;
  const bool bloom=format==0x18280186 && surface_format==0x1a2201bf;
  const bool rgba16=(format==0x1a22ab60 || format==0x1a22ab5d) && surface_format==0x1a2201bf;
  if (!luminance && !bloom && !rgba16) return;
  auto& state = State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  const auto width = reader.Word(reader.Add(owner,24));
  const auto height = reader.Word(reader.Add(owner,28));
  const auto texture = reader.Word(reader.Add(owner,4));
  const auto surface = reader.Word(reader.Add(owner,12));
  if (!texture || !surface || !state.initialized) throw std::runtime_error("invalid native render target registration");
  auto native = luminance ? CreateNativeLuminanceTarget(EnsureSceneBackendLocked(state),width,height) :
    bloom ? CreateNativeBloomTarget(EnsureSceneBackendLocked(state),width,height) :
    CreateNativeRenderTarget(EnsureSceneBackendLocked(state),width,height,DXGI_FORMAT_R16G16B16A16_FLOAT);
  if (luminance && width==1 && height==1) {
    // 8213B730 inserts the allocation address in the header's upper 20 bits
    // at +32. Snapshot at creation, before any GPU writes; never reread stale
    // CPU backing memory as if it were a later rendered history value.
    const auto allocation=reader.Word(reader.Add(texture,32))&0xfffff000u;
    try {
      const bool imported=ImportZeroLuminanceHistory(SceneRecorderLocked(state),native,
        {reader.Bytes(allocation,4096),4096});
      REXLOG_INFO("Native tone initial allocation: texture={:#x}, page={:#x}, uniform_zero_import={}",texture,allocation,imported);
    } catch (const std::exception& error) {
      REXLOG_INFO("Native tone initial allocation not imported: {}",error.what());
    }
  }
  state.textures.insert_or_assign(texture,native.sampled);
  state.render_targets.insert_or_assign(owner,RegisteredTarget{texture,surface,std::move(native)});
  REXLOG_INFO("Native render target: owner={:#x}, texture={:#x}, surface={:#x}, {}x{}",owner,texture,surface,width,height);
}
// The targets a draw goes into, as backend handles, plus the formats a
// pipeline has to declare to match them.
//
// Mirrors BindActiveTarget below, which answers the same question for the
// context. The two must agree: a pipeline built for one set of formats and
// bound while another is set does not draw, it fails to create or draws
// nothing, and neither says why.
struct ActiveTargets {
  std::array<edf::native::NativeBackendRenderTarget*,8> colors{};
  uint32_t count=0;
  edf::native::NativeBackendRenderTarget* depth=nullptr;
  std::array<uint32_t,8> rtv_format{};
  uint32_t dsv_format=0,samples=1;
  uint32_t width=0,height=0;
  bool operator==(const ActiveTargets&) const=default;
};
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
struct NativeScenePassCursorState {
  uint32_t device=0;
  NativeSceneMaterialPassState material;
  std::optional<GuestViewportWords> viewport;
  ActiveTargets targets;
};
using NativeMaterialPassCursor=std::optional<NativeScenePassCursorState>;
thread_local NativeMaterialPassCursor* native_material_pass_cursor=nullptr;
thread_local NativeSceneQueues* native_scene_queues=nullptr;
thread_local std::shared_ptr<const NativeScenePublication> native_scene_publication;
thread_local std::optional<NativeScenePassCamera> native_scene_pass_camera;
thread_local std::shared_ptr<const NativeScenePassCameras> native_scene_pass_cameras;
thread_local std::optional<NativeScenePassAnimation> native_scene_pass_animation;
thread_local std::shared_ptr<const NativeSceneAdapter::WorldAnimations> native_scene_pass_animations;
thread_local uint32_t native_scene_animation_owner=0;
const NativeSceneSources& NativeSceneSourcesForPass(const Bridge& state) {
  if(REXCVAR_GET(edf_native_scene_sources_owned) && native_scene_publication && native_scene_publication->sources) {
    static uint64_t reads=0;
    if(++reads<=4 || reads%100000==0)
      REXLOG_INFO("Native published source reads: count={} tick={}",reads,native_scene_publication->snapshot->tick);
    return *native_scene_publication->sources;
  }
  return state.scene_sources;
}
// The immutable source generation this pass reads, or null when the pass reads
// the producer's current sources (under state.mutex) instead. Taken once per
// walk: a published generation needs no lock, and cannot change within a pass.
std::shared_ptr<const NativeSceneSources> PublishedNativeSceneSourcesForPass(const Bridge& state) {
  if(!REXCVAR_GET(edf_native_scene_sources_owned) || !native_scene_publication || !native_scene_publication->sources) return {};
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
template<class Reader>
bool NativeStaticWorldOnly(const Reader& reader,const NativeSceneSources& sources,
                          uint32_t instance,uint32_t first,uint32_t device) {
  const auto* source=sources.Find(instance);
  if(!source || !source->world_data) return false;
  static thread_local std::vector<InstanceParameter> parameters;
  ReadNativeSceneInstanceParameters(reader,source,instance,parameters);
  return parameters.size()==1 && parameters[0].count==4 && parameters[0].first==first &&
    parameters[0].data==source->world_data &&
    !(uint64_t(parameters[0].data)<uint64_t(device)+5888 && uint64_t(parameters[0].data)+64>uint64_t(device)+1792);
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
// Everything a recorded draw needs before its geometry.
//
// The D3D11 paths spell this out as four separate bindings - target, render
// state, viewport, shader pair - because D3D11 keeps those apart. One pipeline
// carries most of it here, and the rest is recorder state, so every draw path
// that used those four calls uses this one instead. Written once because five
// paths need it and five copies would drift.
struct RecordedDraw {
  ShaderBindings& vertex;
  ShaderBindings& pixel;
  const NativeViewportState& viewport;
  const RenderStateWords& state;
  std::span<const edf::native::NativeBackendInputElement> layout;
  uint64_t layout_id=0;
  // The guest handles, plus whatever else makes two compiled shaders under one
  // handle different - the reversed-depth variant being the one that does.
  uint64_t vertex_id=0,pixel_id=0;
  edf::native::NativeBackendTopology topology=edf::native::NativeBackendTopology::TriangleList;
  bool world_instancing=false;
};
template <typename Reader>
edf::native::NativeBackendRecorder& RecordDrawSetup(Bridge& state,const Reader& reader,
                                                    uint32_t device,const RecordedDraw& draw) {
  auto& backend=EnsureSceneBackendLocked(state);
  auto& recorder=SceneRecorderLocked(state);
  recorder.SetWorldInstancing(draw.world_instancing,REXCVAR_GET(edf_native_world_constant_reuse));
  const auto targets=ActiveTargetsLocked(state);
  if(!targets.count) throw std::runtime_error("a recorded draw has no colour target to record into");
  ++state.recorded_draws;
  auto& last=state.recorded;
  // Nothing the recorder already holds is re-sent. The comparison is against
  // what this code last sent, not against device state, because neither target
  // API has device state to ask - which is also why a direct bind on the shared
  // context has to invalidate it explicitly.
  const bool same_frame=last.valid && last.frame==state.scene_frames &&
                        last.bind_generation==state.bind_generation;
  bool same_targets=same_frame && last.color_count==targets.count && last.depth==targets.depth;
  for(uint32_t index=0;same_targets && index<targets.count;++index)
    same_targets=last.colors[index]==targets.colors[index];
  if(!same_targets) {
    recorder.SetRenderTargets({targets.colors.data(),targets.count},targets.depth);
    last.color_count=targets.count; last.colors=targets.colors; last.depth=targets.depth;
  }
  const auto& view=draw.viewport.viewport;
  const auto& scissor=draw.viewport.scissor;
  const bool scissor_enabled=draw.state[5]!=0;
  if(!same_frame || std::memcmp(&last.viewport,&view,sizeof(view))!=0) {
    recorder.SetViewport({view.TopLeftX,view.TopLeftY,view.Width,view.Height,
                          view.MinDepth,view.MaxDepth});
    last.viewport=view;
  }
  if(!same_frame || last.scissor_enabled!=scissor_enabled ||
     std::memcmp(&last.scissor,&scissor,sizeof(scissor))!=0) {
    recorder.SetScissor({scissor.left,scissor.top,scissor.right,scissor.bottom},scissor_enabled);
    last.scissor=scissor; last.scissor_enabled=scissor_enabled;
  }
  // Pixel-stage resources only, which is what every shader in this game and
  // this renderer uses. A vertex shader that sampled something would have it
  // silently unbound, so it is refused instead: no backend root signature
  // declares vertex-stage textures, and this is where that would be noticed.
  if(draw.vertex.BindsResources())
    throw std::runtime_error("a vertex shader with textures or samplers cannot be recorded: "
                             "no backend root signature declares them");
  const bool same_pipeline=same_frame && last.pipeline &&
    last.vertex_id==draw.vertex_id && last.pixel_id==draw.pixel_id &&
    last.layout_id==draw.layout_id && last.state==draw.state && last.topology==draw.topology &&
    last.render_targets==targets.count && last.sample_count==targets.samples &&
    last.dsv_format==targets.dsv_format && last.rtv_format==targets.rtv_format;
  if(same_pipeline) ++state.recorded_pipeline_skips;
  else {
    edf::native::NativeBackendPipelineDesc desc{};
    auto* vertex_code=draw.vertex.shader().bytecode.Get();
    auto* pixel_code=draw.pixel.shader().bytecode.Get();
    desc.vertex={static_cast<const uint8_t*>(vertex_code->GetBufferPointer()),vertex_code->GetBufferSize()};
    desc.pixel={static_cast<const uint8_t*>(pixel_code->GetBufferPointer()),pixel_code->GetBufferSize()};
    desc.vertex_id=draw.vertex_id;
    desc.pixel_id=draw.pixel_id;
    desc.input_layout=draw.layout;
    desc.input_layout_id=draw.layout_id;
    desc.state=draw.state;
    desc.topology=draw.topology;
    desc.render_targets=targets.count;
    desc.rtv_format=targets.rtv_format;
    desc.dsv_format=targets.dsv_format;
    desc.sample_count=targets.samples;
    auto& pipeline=backend.CreatePipeline(desc);
    if(auto* code=draw.vertex.shader().instanced_bytecode.Get(); code && !pipeline.world_instanced &&
       draw.layout.size()<=28 && std::none_of(draw.layout.begin(),draw.layout.end(),
         [](const auto& element) { return element.slot==15 || element.per_instance; })) {
      edf::native::NativeOwnedInputLayout instanced_layout;
      for(const auto& element:draw.layout) instanced_layout.Add(element.semantic,element.semantic_index,
        element.format,element.slot,element.offset,element.per_instance,element.step_rate);
      for(uint32_t row=0;row<4;++row)
        instanced_layout.Add("EDFINSTANCE",row,DXGI_FORMAT_R32G32B32A32_FLOAT,15,row*16,true,1);
      desc.vertex={static_cast<const uint8_t*>(code->GetBufferPointer()),code->GetBufferSize()};
      desc.vertex_id|=uint64_t(1)<<63;
      desc.input_layout=instanced_layout.elements();
      desc.input_layout_id=instanced_layout.fingerprint();
      pipeline.world_instanced=&backend.CreatePipeline(desc);
      pipeline.instance_world_slot=draw.vertex.shader().instance_world_slot;
      pipeline.instance_world_offset=draw.vertex.shader().instance_world_offset;
    }
    recorder.SetPipeline(pipeline);
    last.pipeline=&pipeline;
    last.vertex_id=draw.vertex_id; last.pixel_id=draw.pixel_id; last.layout_id=draw.layout_id;
    last.state=draw.state; last.topology=draw.topology;
    last.render_targets=targets.count; last.sample_count=targets.samples;
    last.dsv_format=targets.dsv_format; last.rtv_format=targets.rtv_format;
    // A blend factor belongs to the pipeline that was bound with it; a new
    // pipeline has not been given one.
    last.blend_factor_needed=false;
  }
  if(last.pipeline->requires_blend_factor()) {
    const auto factor=GuestBlendFactorForDraw(reader,device);
    if(!last.blend_factor_needed || factor!=last.blend_factor) {
      recorder.SetBlendFactor(factor);
      last.blend_factor=factor; last.blend_factor_needed=true;
    }
  }
  // Constants are re-sent when they have changed. An activation patches the
  // vertex constants between draws, which is the whole reason a run of
  // otherwise identical draws exists - but the pixel constants usually do not
  // move, and every re-send stages a copy in the backend's upload ring. The
  // bindings count their own changes, so this is an integer comparison per
  // register rather than the memcmp and the private copy it used to be.
  const auto send=[&](edf::native::NativeBackendStage stage,
                      edf::native::NativeConstantCache& sent,
                      const edf::native::ShaderBindings& bindings) {
    if(same_frame && sent.MatchesComplete(&bindings,bindings.constant_generation())) {
      state.recorded_constant_skips+=bindings.ConstantImages().size();
      return;
    }
    for(const auto& image:bindings.ConstantImages()) {
      // Reflection order is not a binding slot. Different shaders can put
      // identical bytes in different registers; each register must be bound.
      if(same_frame && sent.Matches(image.slot,&bindings,*image.version)) {
        ++state.recorded_constant_skips;
        continue;
      }
      recorder.SetConstants(stage,image.slot,image.bytes);
      sent.Store(image.slot,&bindings,*image.version);
    }
    sent.StoreComplete(&bindings,bindings.constant_generation());
  };
  send(edf::native::NativeBackendStage::Vertex,last.vertex_constants,draw.vertex);
  send(edf::native::NativeBackendStage::Pixel,last.pixel_constants,draw.pixel);
  if(&draw.pixel!=last.pixel || draw.pixel.resource_generation()!=last.pixel_resources || !same_frame) {
    for(const auto& image:draw.pixel.TextureImages())
      recorder.SetTexture(edf::native::NativeBackendStage::Pixel,image.slot,image.texture);
    for(const auto& image:draw.pixel.SamplerImages())
      recorder.SetSampler(edf::native::NativeBackendStage::Pixel,image.slot,image.sampler);
    last.pixel=&draw.pixel;
    last.pixel_resources=draw.pixel.resource_generation();
  } else ++state.recorded_material_skips;
  last.valid=true;
  last.frame=state.scene_frames;
  last.bind_generation=state.bind_generation;
  return recorder;
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
void BeginRenderTarget(uint32_t owner) {
  auto& state = State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  state.target_stack.emplace_back(owner,state.active_target);
  state.active_target = owner;
  BindActiveTarget(state);
  ++state.target_begins;
}
void EndRenderTarget(uint32_t owner) {
  auto& state = State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  if (state.target_stack.empty() || state.target_stack.back().first != owner)
    throw std::runtime_error("unbalanced native render target scope");
  auto found = state.render_targets.find(owner);
  if (found != state.render_targets.end()) {
    // Unbind SRVs before writing the resolved resource. The next material
    // activation rebinds its own resources by name.
    ID3D11ShaderResourceView* empty[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
    if(state.context) {
      state.context->PSSetShaderResources(0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,empty);
      state.context->VSSetShaderResources(0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,empty);
    }
    auto& target = found->second;
    // Through the recorder, always: a converting target's resolve is a draw,
    // and on the adopted D3D11 backend the recorder issues straight to the same
    // immediate context, so the ordering against the direct paths is exact.
    ResolveNativeRenderTarget(SceneRecorderLocked(state),target.native);
    // A converting resolve is a draw and binds its own targets, so whatever
    // this code last bound - on the context or on the recorder - is no longer
    // what is set.
    ++state.bind_generation;
    state.recorded={};
    state.textures.insert_or_assign(target.texture_handle,target.native.sampled);
    if (target.native.sampled.content_valid) ++state.target_resolves;
    else ++state.target_unwritten;
  }
  state.active_target = state.target_stack.back().second;
  state.target_stack.pop_back();
  BindActiveTarget(state);
  if (state.target_begins <= 5 || state.target_begins % 1000 == 0)
    REXLOG_INFO("Native render target: begins={}, valid_resolves={}, unwritten={}",
                state.target_begins,state.target_resolves,state.target_unwritten);
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
  const bool found = state.shaders.contains(vertex) && state.shaders.contains(pixel);
  state.active_vertex = 0;
  state.active_vertex_parameters.reset();
  if (!found) ++state.misses;
  if (found) {
    try {
      auto& vertex_shader=state.shaders.at(vertex);
      auto& pixel_shader=state.shaders.at(pixel);
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
void SetNativeMeshWatchAudit(std::weak_ptr<GuestMeshWatchAudit> audit) {
  auto& state=State();
  std::lock_guard lock(state.mutex);
  state.mesh_watch_audit=std::move(audit);
}
namespace {
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
}
void PublishNativeRenderState(uint8_t* base,uint32_t device,uint32_t producer) {
  if((!REXCVAR_GET(edf_native_render_state_audit) && !REXCVAR_GET(edf_native_owned_render_state)) ||
      !REXCVAR_GET(edf_native_shader_bridge)) return;
  auto& state=State();
  std::lock_guard lock(state.mutex);
  try {
    const auto fields=NativeRenderStateProducerFields(producer);
    const GuestReader reader(base);
    constexpr std::array<uint32_t,6> offsets{10424,10420,10440,10428,10332,11584};
    NativeRenderStateSnapshots::Words words{};
    for(size_t field=0;field<words.size();++field) if(fields&(1u<<field)) {
      auto word=reader.Word(reader.Add(device,offsets[field]));
      if(field==4) word&=15;
      if(field==5) word=uint32_t(word!=0);
      words[field]=word;
    }
    state.render_state_snapshots.Publish(device,words,producer,fields);
  } catch(const std::exception& error) {
    state.render_state_snapshots.Retire(device);
    REXLOG_ERROR("Native render-state publication failed: device={:#x}, producer={:#x}, error={}",device,producer,error.what());
  }
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
NativeRenderStateSnapshots::Words ReadAuditedRenderStateWords(const Reader& reader,uint32_t device) {
  return ResolveNativeRenderState(State().render_state_snapshots,device,
    REXCVAR_GET(edf_native_owned_render_state),REXCVAR_GET(edf_native_render_state_audit),
    [&]{return ReadRenderStateWords(reader,device);},
    [&](const auto& live){AuditNativeRenderState(device,live);});
}
template <typename Reader>
GuestXuiDeviceWords ReadAuditedXuiDeviceWords(const Reader& reader,uint32_t device) {
  if(REXCVAR_GET(edf_native_owned_render_state))
    return ReadXuiDeviceWords(reader,device,ReadAuditedRenderStateWords(reader,device));
  auto snapshot=ReadXuiDeviceWords(reader,device);
  AuditNativeRenderState(device,snapshot.render);
  return snapshot;
}
template <typename Reader>
GuestViewportWords ReadNativeDrawViewportWords(const Reader& reader,uint32_t device) {
  if(!REXCVAR_GET(edf_native_owned_render_state)) return ReadViewportWords(reader,device);
  const auto render=ReadAuditedRenderStateWords(reader,device);
  return ReadViewportWords(reader,device,render[5]!=0);
}
template <typename Reader>
NativeViewportState ReadNativeDrawViewport(const Reader& reader,uint32_t device) {
  return DecodeDrawViewport(ReadNativeDrawViewportWords(reader,device));
}
namespace {
// The D3D12 options the cvars carry, applied before anything can build a D3D12
// device. Every site that registers the backend goes through here, because the
// debug layer is a process-wide switch that only the first device gets to throw
// and the presenter's device is usually the first: setting it later - which is
// what the scene backend used to do - removes the device that already exists.
void RegisterD3D12BackendLocked() {
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
  for(const auto& message:state.scene_backend->DrainValidationMessages())
    REXLOG_WARN("Native scene backend validation: {}",message);
  return *state.scene_backend;
}
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
  return backend.Recorder(0);
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
template <typename Scene>
std::vector<uint8_t> CaptureOutputBmp(Bridge& state,Scene& scene) {
  if(scene.output.surface)
    return edf::native::CaptureNativeHdrBmp(*state.context.Get(),*scene.output.surface.Get());
  if(!scene.output.backend_surface)
    throw std::runtime_error("this scene has no output surface to capture");
  SubmitSceneFrameLocked(state);
  return edf::native::CaptureNativeBmp(EnsureSceneBackendLocked(state),
                                       *scene.output.backend_surface,scene.output.format);
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
    REXLOG_INFO("Native world constants: reused={}, snapshot_bytes={} (full immutable constant images copied by the producer)",
      counts.geometry_world_constant_reuses,counts.geometry_constant_snapshot_bytes);
    REXLOG_INFO("Native scene backend spend: frames={}, splits={}, operations_last_frame={}, "
      "upload_stalls={}, descriptor_stalls={}, pipelines={} (hits={}, misses={}), "
      "sampler_tables={} (hits={}, misses={}, evictions={}), retiring={}, "
      "frame_waits={} averaging {}us",
      state.scene_frames,state.scene_frame_splits,operations,
      counts.upload_stalls,counts.descriptor_stalls,counts.pipelines,counts.pipeline_hits,
      counts.pipeline_misses,counts.sampler_tables,counts.sampler_hits,counts.sampler_misses,
      counts.sampler_evictions,counts.retiring,counts.frame_waits,
      counts.frame_waits?counts.frame_wait_ns/counts.frame_waits/1000:0);
  }
}
}  // namespace

void InitializeGuestShaderBridge(const std::filesystem::path& game_root) {
  REXLOG_INFO("Native render-state consumption: owned={}, audit={}",
    REXCVAR_GET(edf_native_owned_render_state),REXCVAR_GET(edf_native_render_state_audit));
  if(REXCVAR_GET(edf_native_preview_window) && !REXCVAR_GET(edf_native_publish_frames))
    throw std::runtime_error("native preview requires native frame publication");
  if(REXCVAR_GET(edf_native_publish_frames) && !REXCVAR_GET(edf_native_shader_bridge))
    throw std::runtime_error("native frame publication requires native shader bridge");
  // Said here rather than discovered per draw. A scene on its own device with
  // the direct path still in use fails at the first mesh - "this mesh is not
  // on a D3D11 backend and cannot be drawn through a context" - once per draw,
  // for the rest of the run, which is a worse way to learn it.
  if(REXCVAR_GET(edf_native_scene_backend)!="d3d11" && !REXCVAR_GET(edf_native_seam_draws))
    throw std::runtime_error("--edf_native_scene_backend="+REXCVAR_GET(edf_native_scene_backend)+
      " needs --edf_native_seam_draws=true: a draw issued straight to the D3D11 context cannot "
      "bind a resource that lives on another device");
  if(REXCVAR_GET(edf_native_validate_wait) &&
     (!REXCVAR_GET(edf_native_shader_bridge) || !REXCVAR_GET(edf_native_fence_probe)))
    throw std::runtime_error("native wait validation requires native shader bridge and fence probe");
  if (!REXCVAR_GET(edf_native_shader_bridge)) return;
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
  return State().scene_frame_queue.Visit(after_sequence,copy,timing);
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
void PublishNativeCompletion(const GuestReader& reader,Bridge& state,uint32_t device,bool allow_flush=false) {
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

namespace {
void ObserveAudioOutputPages(uint8_t* base,uint32_t address,bool indirect,const char* phase) {
  if(!REXCVAR_GET(edf_native_mesh_watch_audit)) return;
  auto& state=edf::native::State();
  std::lock_guard lock(state.mutex);
  auto audit=state.mesh_watch_audit.lock();
  if(!audit) return;
  try {
    const edf::native::GuestReader reader(base);
    const auto owner=indirect ? reader.Word(address) : address;
    const auto ranges=edf::native::VisitGuestAudioOutputRanges(reader,owner,
      [&](uint32_t physical,uint32_t length) { audit->ExcludePhysical(physical,length); });
    static uint64_t observed=0;
    if(++observed<=3 || (observed&(observed-1))==0)
      REXLOG_INFO("Native mesh watch audio exclusion: observations={}, ranges={}, phase={} (conservative shadow-only pages)",observed,ranges,phase);
  } catch(const std::exception& error) {
    audit->Disable();
    REXLOG_ERROR("Native mesh watch audit disabled after audio exclusion failure: {}",error.what());
  }
}
}
// Capture registration inputs rather than reading callback fields after the
// call: another worker can replace them immediately. This is workload evidence,
// not an ownership proof, and never changes the guest registration or arguments.
REX_EXTERN(__imp__sub_8243A000);
REX_HOOK_RAW(sub_8243A000) {
  if(!REXCVAR_GET(edf_native_worker_callback_audit)) {
    __imp__sub_8243A000(ctx,base);
    return;
  }
  const uint32_t worker=ctx.r3.u32, mode=ctx.r4.u32;
  const uint32_t callback=ctx.r5.u32, context=ctx.r6.u32;
  const uint32_t return_address=static_cast<uint32_t>(ctx.lr);
  __imp__sub_8243A000(ctx,base);
  static std::atomic<uint64_t> registrations{0};
  const auto sequence=registrations.fetch_add(1,std::memory_order_relaxed)+1;
  if(sequence<=256) {
    REXLOG_INFO("Native worker callback registration: sequence={}, worker={:#x}, mode={}, callback={:#x}, context={:#x}, caller_return={:#x} (inputs; observed workload only)",
                sequence,worker,mode,callback,context,return_address);
  } else if(sequence==257) {
    REXLOG_WARN("Native worker callback audit truncated: registration limit 256 reached; later registrations are not logged");
  }
}

// Retain construction coverage for deferred contexts, and observe every pending
// and live output before 1F88 copies records / signals the decoder. No guest
// registers or original audio behavior are changed by these shadow hooks.
REX_EXTERN(__imp__sub_823C1108);
REX_HOOK_RAW(sub_823C1108) {
  const uint32_t output=ctx.r6.u32;
  __imp__sub_823C1108(ctx,base);
  if(!ctx.r3.u32) ObserveAudioOutputPages(base,output,true,"constructed");
}
REX_EXTERN(__imp__sub_823C1F88);
REX_HOOK_RAW(sub_823C1F88) {
  ObserveAudioOutputPages(base,ctx.r3.u32,false,"before_decode");
  __imp__sub_823C1F88(ctx,base);
}

// Observational resource-worker boundaries. The persistent coordinator/helper
// are not BeginLoading/EndLoading transactions; preserve their original calls.
namespace {
class ScriptLoadTiming {
 public:
  ScriptLoadTiming(const char* api,uint32_t mode,uint32_t index,bool active=true)
      : api_(api),mode_(mode),index_(index),enabled_(active && REXCVAR_GET(edf_native_load_timings)) {
    if(enabled_) start_=std::chrono::steady_clock::now();
  }
  ~ScriptLoadTiming() {
    if(!enabled_) return;
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start_).count();
    if(ms>=1.0) REXLOG_INFO("Native script timing: api={} mode={} index={} total_ms={} (inclusive, calls below 1ms omitted)",
                            api_,mode_,index_,ms);
  }
 private:
  const char* api_;
  uint32_t mode_,index_;
  bool enabled_;
  std::chrono::steady_clock::time_point start_{};
};
thread_local uint32_t map_load_depth=0;
}
REX_EXTERN(__imp__sub_820CBD28);
REX_HOOK_RAW(sub_820CBD28) {
  struct Scope {
    Scope() { ++map_load_depth; }
    ~Scope() { --map_load_depth; }
  } scope;
  __imp__sub_820CBD28(ctx,base);
}
// Inclusive nested timings, restricted to this thread's LoadMap call chain.
// Address labels avoid assigning unverified semantic names to guest helpers.
#define EDF_MAP_TIMED_HOOK(address) \
  REX_EXTERN(__imp__sub_##address); \
  REX_HOOK_RAW(sub_##address) { \
    ScriptLoadTiming timing("map." #address,0,0,map_load_depth!=0); \
    __imp__sub_##address(ctx,base); \
  }
EDF_MAP_TIMED_HOOK(821B8090)
EDF_MAP_TIMED_HOOK(820BE628)
EDF_MAP_TIMED_HOOK(820B4D58)
EDF_MAP_TIMED_HOOK(820B3B00)
EDF_MAP_TIMED_HOOK(821A6158)
EDF_MAP_TIMED_HOOK(821B7278)
EDF_MAP_TIMED_HOOK(821B2850)
EDF_MAP_TIMED_HOOK(821ACBD0)
EDF_MAP_TIMED_HOOK(820ADD28)
EDF_MAP_TIMED_HOOK(821B24C0)
EDF_MAP_TIMED_HOOK(820B77A0)
EDF_MAP_TIMED_HOOK(820B6458)
EDF_MAP_TIMED_HOOK(820B6FA8)
EDF_MAP_TIMED_HOOK(821A6278)
EDF_MAP_TIMED_HOOK(820B4858)
EDF_MAP_TIMED_HOOK(821C7B20)
EDF_MAP_TIMED_HOOK(821C7D80)
EDF_MAP_TIMED_HOOK(821C7E30)
EDF_MAP_TIMED_HOOK(821ACAD8)
EDF_MAP_TIMED_HOOK(821AB708)
EDF_MAP_TIMED_HOOK(821D6448)
EDF_MAP_TIMED_HOOK(821D6AE0)
EDF_MAP_TIMED_HOOK(821D6C20)
EDF_MAP_TIMED_HOOK(821AA130)
EDF_MAP_TIMED_HOOK(821AAB70)
EDF_MAP_TIMED_HOOK(821B5568)
EDF_MAP_TIMED_HOOK(821AB520)
EDF_MAP_TIMED_HOOK(821CB6A0)
EDF_MAP_TIMED_HOOK(821CB550)
EDF_MAP_TIMED_HOOK(821D88C0)
EDF_MAP_TIMED_HOOK(821DB008)
EDF_MAP_TIMED_HOOK(821D85B8)
EDF_MAP_TIMED_HOOK(821D7C18)
EDF_MAP_TIMED_HOOK(821D7850)
EDF_MAP_TIMED_HOOK(821D79D8)
EDF_MAP_TIMED_HOOK(821D8770)
EDF_MAP_TIMED_HOOK(821B3C98)
#undef EDF_MAP_TIMED_HOOK
REX_EXTERN(__imp__sub_820C7220);
REX_HOOK_RAW(sub_820C7220) {
  ScriptLoadTiming timing("menu",ctx.r6.u32,ctx.r7.u32);
  __imp__sub_820C7220(ctx,base);
}
REX_EXTERN(__imp__sub_820D1518);
REX_HOOK_RAW(sub_820D1518) {
  ScriptLoadTiming timing("mission",ctx.r6.u32,ctx.r7.u32);
  __imp__sub_820D1518(ctx,base);
}
REX_EXTERN(__imp__sub_821A4170);
REX_HOOK_RAW(sub_821A4170) {
  const bool trace=REXCVAR_GET(edf_native_load_timings);
  const uint32_t manager=ctx.r3.u32,mode=ctx.r4.u32;
  if(trace) REXLOG_INFO("Native load request: begin manager={:#x} mode={} caller={:#x}",manager,mode,ctx.lr);
  __imp__sub_821A4170(ctx,base);
  if(trace) REXLOG_INFO("Native load request: armed manager={:#x}",manager);
}
REX_EXTERN(__imp__sub_821A41E8);
REX_HOOK_RAW(sub_821A41E8) {
  const bool trace=REXCVAR_GET(edf_native_load_timings);
  const uint32_t manager=ctx.r3.u32;
  if(trace) REXLOG_INFO("Native load request: finish manager={:#x} caller={:#x}",manager,ctx.lr);
  __imp__sub_821A41E8(ctx,base);
  if(trace) REXLOG_INFO("Native load request: disarmed manager={:#x}",manager);
}
REX_EXTERN(__imp__sub_821A4BA0);
namespace {
// The heartbeat and step dispatcher run on the engine thread. Rendering work
// may run on a helper, so this budget must not be shared with that thread.
struct NativeLoopBudget {
  bool unlocked=false;
  uint32_t steps=0;
  uint64_t tick=0;
  float fraction=0;
  uint32_t divisor=1;
};
thread_local NativeLoopBudget native_loop_budget;
struct NativeModelMotionState {
  struct Source {
    uint32_t vector=0,owner=0,node=0;
    uint64_t publication=0;
    edf::native::NativeModelPoseHistory history;
    size_t history_bytes=0;
    bool trace=false;
    uint64_t samples=0,source_changes=0,rendered_changes=0,blended=0,trace_publication=0;
    uint64_t source_hash=0,rendered_hash=0;
  };
  std::mutex mutex;
  NativeLoopBudget published;
  uint64_t publication=0;
  uint32_t trace_sources=0;
  std::unordered_map<uint32_t,Source> sources;
  size_t history_bytes=0;
  void Clear() { sources.clear(); history_bytes=0; }
  void Erase(uint32_t address) {
    const auto found=sources.find(address);
    if(found==sources.end()) return;
    history_bytes-=found->second.history_bytes;
    sources.erase(found);
  }
};
NativeModelMotionState& ModelMotionState() { static NativeModelMotionState value; return value; }
// Leaked: guest frees (820B2510) may still arrive during static destruction.
edf::native::NativeModelPublications& ModelPublications() { static auto* value=new edf::native::NativeModelPublications; return *value; }
// Pose vectors rebuilt by 821C9478 during this thread's 821A4DE8 dirty walk
// (slot +8 after the helper join); null outside that walk.
thread_local std::vector<uint32_t>* native_model_dirty_poses=nullptr;
thread_local NativeLoopBudget native_render_budget;
thread_local uint64_t native_render_publication=0;
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
  NativeLoopTrace trace("step_dispatch",ctx.r3.u32,ctx.lr,ctx.r4.u32);
  edf::native::HookTiming timing(edf::native::HookPhase::ResourceCoordinator);
  edf::native::HookTiming engine_timing(edf::native::HookPhase::SimulationDispatch);
  __imp__sub_821A4BA0(ctx,base);
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
// Finish/post stage 820B0B80. Steps 1-2 of making it native: the guest body
// still runs every time and is the fallback for every failure below. The plan
// derives what its passes will set; the audit compares that plan with the
// setters, targets and quads the guest body actually issues.
REXCVAR_DEFINE_BOOL(edf_native_post_finish,false,"EDF2027",
  "Build a native plan of the finish/post stage 820B0B80 before its guest body runs. The guest body still runs; it remains the fallback whenever a record or format check fails");
REXCVAR_DEFINE_BOOL(edf_native_post_finish_audit,false,"EDF2027",
  "Compare the native finish plan with the setters, targets and quads 820B0B80 actually issues, and log mismatches (development)");
namespace edf::native {
namespace {
// Guest-thread only: the post chain, its setters and its quad draws all run
// synchronously inside the hooked call.
struct PostFinishRecorder {
  uint32_t self=0;
  std::set<uint32_t> targets,techniques;
  PostFinishObservation seen;
  uint64_t errors=0;
  std::string first_error;
  bool Effect(uint32_t effect) const { return effect>=self && effect-self<PostFinishLayout::kEffectSpan; }
  void Fail(const std::exception& error) { if(!errors++) first_error=error.what(); }
};
thread_local PostFinishRecorder* post_finish_recorder=nullptr;
struct PostFinishStats {
  uint64_t planned=0,rejected=0,audited=0,clean=0,mismatches=0,unobserved=0,recorder_errors=0;
  std::optional<std::array<float,3>> tone; // last values the native bindings reflected
};
PostFinishStats& FinishStats() { static PostFinishStats stats; return stats; }
// 820A62E8/820A6908 strings: +4 inline buffer or pointer, +20 size, +24 capacity.
std::string ReadGuestStdString(const GuestReader& reader,uint32_t address) {
  const auto size=reader.Word(reader.Add(address,20)),capacity=reader.Word(reader.Add(address,24));
  if(size>256 || size>capacity) throw std::runtime_error("unsupported guest parameter name string");
  const auto data=capacity>=16 ? reader.Word(reader.Add(address,4)) : reader.Add(address,4);
  if(!size) return {};
  return std::string(reinterpret_cast<const char*>(reader.Bytes(data,size)),size);
}
PostFinishInput ReadPostFinishInput(const GuestReader& reader,uint32_t self,const std::optional<std::array<float,3>>& tone) {
  using L=PostFinishLayout;
  if(!self) throw std::runtime_error("no post owner");
  PostFinishInput in; in.self=self;
  const auto owner=reader.Word(L::kOwnerGlobal);
  in.screen_width=std::bit_cast<int32_t>(reader.Word(reader.Add(owner,L::kOwnerWidth)));
  in.screen_height=std::bit_cast<int32_t>(reader.Word(reader.Add(owner,L::kOwnerHeight)));
  in.scene_texture=reader.Word(reader.Add(owner,L::kOwnerSceneTexture));
  const auto record=[&](uint32_t address) {
    PostFinishRecord r; r.address=address;
    r.texture=reader.Word(reader.Add(address,L::kRecordTexture));
    r.width=std::bit_cast<int32_t>(reader.Word(reader.Add(address,L::kRecordWidth)));
    r.height=std::bit_cast<int32_t>(reader.Word(reader.Add(address,L::kRecordHeight)));
    r.texel_x=std::bit_cast<float>(reader.Word(reader.Add(address,L::kRecordTexelX)));
    r.texel_y=std::bit_cast<float>(reader.Word(reader.Add(address,L::kRecordTexelY)));
    return r;
  };
  for(uint32_t k=0;k<L::kFirstPyramidCount;++k) in.first[k]=record(reader.Add(self,L::kFirstPyramid+k*L::kRecordStride));
  const auto records=reader.Word(reader.Add(self,L::kSecondPyramid));
  const auto count=reader.Word(reader.Add(self,L::kSecondCount));
  if(!records || !count || count>L::kMaxSecondCount)
    throw std::runtime_error(std::format("second pyramid {:#x} count {} unsupported",records,count));
  for(uint32_t i=0;i<count;++i) in.second.push_back(record(reader.Add(records,i*L::kRecordStride)));
  in.blur=record(reader.Add(self,L::kBlurTarget));
  in.blur_vertical=record(reader.Add(self,L::kBlurTargetVertical));
  const auto technique=[&](uint32_t effect) { return reader.Word(reader.Add(self,effect+L::kTechniqueOffset)); };
  in.mono_technique=technique(L::kMonoEffect); in.downsample_tone_technique=technique(L::kDownsampleToneEffect);
  in.downsample_technique=technique(L::kDownsampleEffect); in.tone_technique=technique(L::kToneEffect);
  in.blur_technique=technique(L::kBlurEffect); in.bloom_technique=technique(L::kBloomEffect);
  in.tone=tone; in.tone_source=tone?PostToneSource::LivePreviousFrame:PostToneSource::None;
  return in;
}
}
}
REX_EXTERN(__imp__sub_820B0B80);
REX_HOOK_RAW(sub_820B0B80) {
  edf::native::HookTiming timing(edf::native::HookPhase::RenderFinish);
  const bool audit=REXCVAR_GET(edf_native_post_finish_audit);
  if((!REXCVAR_GET(edf_native_post_finish) && !audit) || !REXCVAR_GET(edf_native_host) ||
     !REXCVAR_GET(edf_native_shader_bridge) || !REXCVAR_GET(edf_native_seam_draws) ||
     edf::native::post_finish_recorder) {
    __imp__sub_820B0B80(ctx,base); return;
  }
  auto& stats=edf::native::FinishStats();
  const auto self=ctx.r3.u32;
  std::optional<edf::native::PostFinishPlan> plan;
  try {
    const edf::native::GuestReader reader(base);
    const auto input=edf::native::ReadPostFinishInput(reader,self,stats.tone);
    plan=edf::native::BuildPostFinishPlan(input);
    if(++stats.planned<=2) {
      std::string first,second;
      for(const auto& r:input.first) first+=std::format(" {}x{}/texel={},{}",r.width,r.height,r.texel_x,r.texel_y);
      for(const auto& r:input.second) second+=std::format(" {}x{}",r.width,r.height);
      REXLOG_INFO("Native post finish plan: owner={:#x}, passes={}, screen={}x{}, first=[{} ], second=[{} ], blur={}x{}, tone_source={}",
        self,plan->passes.size(),input.screen_width,input.screen_height,first,second,input.blur.width,input.blur.height,
        input.tone?"live-previous-frame":"none");
    }
  } catch(const std::exception& error) {
    plan.reset();
    if(edf::native::ShouldLogPostFinish(++stats.rejected))
      REXLOG_INFO("Native post finish fallback: {} (rejected={}, planned={})",error.what(),stats.rejected,stats.planned);
  }
  if(!plan || !audit) { __imp__sub_820B0B80(ctx,base); return; }
  edf::native::PostFinishRecorder recorder;
  recorder.self=self;
  for(const auto& pass:plan->passes) {
    if(pass.target) recorder.targets.insert(pass.target);
    recorder.techniques.insert(pass.technique);
  }
  {
    struct Scope { ~Scope() { edf::native::post_finish_recorder=nullptr; } } scope;
    edf::native::post_finish_recorder=&recorder;
    __imp__sub_820B0B80(ctx,base);
  }
  try {
    const auto result=edf::native::ComparePostFinish(*plan,recorder.seen);
    ++stats.audited;
    if(result.mismatches.empty() && !recorder.errors) ++stats.clean;
    stats.unobserved+=result.native_unobserved;
    if(recorder.errors && edf::native::ShouldLogPostFinish(++stats.recorder_errors))
      REXLOG_WARN("Native post finish audit recorder: frame={}, errors={}, first={}",stats.audited,recorder.errors,recorder.first_error);
    for(const auto& mismatch:result.mismatches)
      if(edf::native::ShouldLogPostFinish(++stats.mismatches))
        REXLOG_WARN("Native post finish mismatch: frame={}, pass={}, {} (mismatches={})",
          stats.audited,mismatch.pass,mismatch.what,stats.mismatches);
    if(edf::native::ShouldLogPostFinish(stats.audited))
      REXLOG_INFO("Native post finish audit: frames={}, clean={}, mismatches={}, native_unobserved_passes={}, tone_compared={}, tone_source={}, recorder_errors={}",
        stats.audited,stats.clean,stats.mismatches,stats.unobserved,result.tone_compared,
        plan->tone?"live-previous-frame":"none",stats.recorder_errors);
    if(const auto tone=edf::native::PostObservedTone(*plan,recorder.seen)) stats.tone=tone;
  } catch(const std::exception& error) {
    REXLOG_ERROR("Native post finish audit: {}",error.what());
  }
}
// Observers for the finish audit. Each records only while an audited 820B0B80
// is on this thread's stack, and only for the post owner's own effects,
// targets and techniques; the original always runs, and a failed read is
// counted, never thrown into guest code.
#define EDF_POST_FINISH_OBSERVER(address,...) \
  REX_EXTERN(__imp__sub_##address); \
  REX_HOOK_RAW(sub_##address) { \
    if(auto* recorder=edf::native::post_finish_recorder) { \
      try { __VA_ARGS__ } catch(const std::exception& error) { recorder->Fail(error); } \
    } \
    __imp__sub_##address(ctx,base); \
  }
// 821BCD58(effect,name,float4*,count)
EDF_POST_FINISH_OBSERVER(821BCD58,
  if(recorder->Effect(ctx.r3.u32)) {
    const edf::native::GuestReader reader(base);
    edf::native::PostSetterCall call;
    call.effect=ctx.r3.u32; call.name=edf::native::ReadGuestStdString(reader,ctx.r4.u32);
    call.kind=edf::native::PostSetterKind::Vectors;
    if(ctx.r6.u32>64) throw std::runtime_error("post vector setter count");
    reader.Bytes(ctx.r5.u32,size_t(ctx.r6.u32)*16);
    for(uint32_t i=0;i<ctx.r6.u32*4;++i) call.values.push_back(std::bit_cast<float>(reader.Word(ctx.r5.u32+i*4)));
    recorder->seen.Setter(std::move(call));
  })
// 821BCE98(effect,name,texture)
EDF_POST_FINISH_OBSERVER(821BCE98,
  if(recorder->Effect(ctx.r3.u32)) {
    const edf::native::GuestReader reader(base);
    edf::native::PostSetterCall call;
    call.effect=ctx.r3.u32; call.name=edf::native::ReadGuestStdString(reader,ctx.r4.u32);
    call.kind=edf::native::PostSetterKind::Texture; call.texture=ctx.r5.u32;
    recorder->seen.Setter(std::move(call));
  })
// 821BCF28(effect,name,f1,r5,r6,r7)
EDF_POST_FINISH_OBSERVER(821BCF28,
  if(recorder->Effect(ctx.r3.u32)) {
    const edf::native::GuestReader reader(base);
    edf::native::PostSetterCall call;
    call.effect=ctx.r3.u32; call.name=edf::native::ReadGuestStdString(reader,ctx.r4.u32);
    call.kind=edf::native::PostSetterKind::Sampler;
    call.values={float(ctx.f1.f64)}; call.words={ctx.r5.u32,ctx.r6.u32,ctx.r7.u32};
    recorder->seen.Setter(std::move(call));
  })
// 821B94E8(technique): closes a pass. Unrelated activations inside the scope
// (no pending setters or target) are not passes of this chain.
EDF_POST_FINISH_OBSERVER(821B94E8,
  auto& pending=recorder->seen.pending;
  if(recorder->techniques.contains(ctx.r3.u32) && (pending.target || !pending.setters.empty()))
    recorder->seen.Activate(ctx.r3.u32);)
// 821A79B8(device,primitive,vertices,count): the pass quad, four (x,y,u,v).
EDF_POST_FINISH_OBSERVER(821A79B8,
  if(recorder->seen.quad_armed) {
    const edf::native::GuestReader reader(base);
    reader.Bytes(ctx.r5.u32,64);
    edf::native::PostQuad quad{};
    for(uint32_t i=0;i<16;++i) quad[i]=std::bit_cast<float>(reader.Word(ctx.r5.u32+i*4));
    recorder->seen.Quad(quad);
  })
#undef EDF_POST_FINISH_OBSERVER
REXCVAR_DEFINE_BOOL(edf_native_scene_tree,false,"EDF2027",
  "Use native spatial tree traversal and culling; leaf callbacks remain explicit.");
REXCVAR_DEFINE_BOOL(edf_native_scene_tree_published,false,"EDF2027",
  "Read immutable spatial hierarchy published by the world producer.");
REXCVAR_DEFINE_BOOL(edf_native_scene_group_order,false,"EDF2027",
  "Publish each world owner's static group walk order (owner+240) at simulation step.");
REXCVAR_DEFINE_BOOL(edf_native_scene_group_order_audit,false,"EDF2027",
  "Compare the published static group order with a live walk at world-pass entry (development).");
namespace edf::native {
void RetireGroupOrder(uint32_t owner) {
  // Destructor path of every world: stay off the bridge lock unless orders exist.
  if(!REXCVAR_GET(edf_native_scene_group_order) && !REXCVAR_GET(edf_native_scene_group_order_audit)) return;
  auto& state=State();
  std::lock_guard lock(state.mutex);
  state.scene_adapter.RetireGroupOrder(owner);
}
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
// so the list it belongs to (by the plan's node index) loses its plan.
void TouchStaticWalkPlans(std::initializer_list<uint32_t> anchors) {
  if(!NativeStaticWalkPlansEnabled()) return;
  auto& state=State();
  std::lock_guard lock(state.mutex);
  for(const auto anchor:anchors) state.static_walk_plans.Touch(anchor);
}
// 820B4250 post-hook: the world's plan, published after its tree.
void PublishStaticWalkPlans(uint8_t* base,uint32_t owner) {
  if(!NativeStaticWalkPlansEnabled()) return;
  auto& state=State();
  const auto epoch=TreePublications().Epoch();
  std::lock_guard lock(state.mutex);
  try {
    const GuestReader reader(base);
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
  if(!REXCVAR_GET(edf_native_scene_tree) || !REXCVAR_GET(edf_native_shader_bridge) ||
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
  if(REXCVAR_GET(edf_native_scene_tree_published)) {
    if(REXCVAR_GET(edf_native_scene_membership_owned) && edf::native::native_scene_publication) {
      const auto& trees=edf::native::native_scene_publication->trees;
      const auto found=trees.find(manager);
      if(found!=trees.end()) hierarchy=found->second;
    } else hierarchy=publications.Acquire(manager);
  }
  edf::native::NativeSceneTreeReader tree_reader(reader,publications,std::move(hierarchy),
    REXCVAR_GET(edf_native_scene_visibility_audit));
  // One bridge lock scope for the walk: each leaf list's gather reuses it,
  // every guest call below releases it and so does the end of every list, so
  // the hold never spans the traversal. The view is read once and again only
  // after a guest call, which alone can change camera data; the gathers share
  // it, and releasing the lock does not invalidate it.
  edf::native::BridgeWalkLock walk_lock(edf::native::State().mutex);
  edf::native::BridgeWalkView walk_view{&walk_lock,context,{}};
  const auto outer_view=std::exchange(edf::native::bridge_walk_view,&walk_view);
  struct RestoreWalkView {
    edf::native::BridgeWalkView* outer;
    ~RestoreWalkView() { edf::native::bridge_walk_view=outer; }
  } restore_view{outer_view};
  edf::native::TraverseNativeSceneTree(tree_reader,manager,[&](uint32_t node) {
    const auto& view=walk_view.view.Get(edf::native::BridgeWalkLock::guest_calls,
      [&] { return edf::native::ReadNativeSceneVisibilityView(reader,context); });
    const auto center=edf::native::ReadNativeVisibilityFloats<4>(tree_reader,reader.Add(node,32));
    const auto transformed=edf::native::NativeVisibilityTransform(center,view.matrix);
    const auto radius=std::bit_cast<float>(tree_reader.Word(reader.Add(node,64)));
    const auto sphere=edf::native::NativeVisibilitySphere(view,transformed,radius);
    const auto result=sphere==2?edf::native::NativeVisibilityAabb(view,center,
      edf::native::ReadNativeVisibilityFloats<4>(tree_reader,reader.Add(node,48))):sphere;
    if(REXCVAR_GET(edf_native_scene_visibility_audit)) {
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
  if(REXCVAR_GET(edf_native_scene_material_audit) || REXCVAR_GET(edf_native_scene_material_owned)) {
    const edf::native::GuestReader reader(base);
    published=edf::native::NativeScenePassAnimation{
      reader.Word(reader.Add(owner,356)),reader.Word(reader.Add(owner,364))};
  }
  __imp__sub_820B4250(ctx,base);
  if(REXCVAR_GET(edf_native_scene_tree_published)) {
    try {
      if(edf::native::TreePublications().Publish(edf::native::GuestReader(base),owner)) {
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
  if(REXCVAR_GET(edf_native_scene_group_order) || REXCVAR_GET(edf_native_scene_group_order_audit)) {
    static thread_local std::vector<uint32_t> order;
    try {
      const edf::native::GuestReader reader(base);
      edf::native::CaptureNativeSceneGroupOrder(reader,reader.Add(owner,240),order);
      auto& state=edf::native::State();
      std::lock_guard lock(state.mutex);
      if(state.scene_adapter.PublishGroupOrder(owner,order)) {
        static std::atomic<uint64_t> changes=0;
        const auto count=++changes;
        if(count<=4 || count%1000==0) REXLOG_INFO("Native group order publication: owner={:#x} groups={} changes={}",owner,order.size(),count);
      }
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
    std::lock_guard lock(state.mutex);
    state.scene_adapter.PublishWorldAnimation(owner,*published);
  }
}
REX_EXTERN(__imp__sub_820B4310);
REX_EXTERN(sub_821C61D8);
REX_EXTERN(sub_821C3BB8);
namespace edf::native {
bool NativeStaticWorldPassEnabled();
void RenderNativeStaticWorldPass(PPCContext& ctx,uint8_t* base,uint32_t owner);
bool NativeModelPassEnabled();
bool RenderNativeModelPass(PPCContext& ctx,uint8_t* base,const std::vector<NativePoseMatrix>* interpolated,
  bool interpolating,bool render_dependent);
}
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
  if(REXCVAR_GET(edf_native_scene_material_audit) || REXCVAR_GET(edf_native_scene_material_owned)) {
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
  if(REXCVAR_GET(edf_native_scene_tree) && REXCVAR_GET(edf_native_shader_bridge)) {
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
  if(!REXCVAR_GET(edf_native_scene_tree) || !REXCVAR_GET(edf_native_shader_bridge)) {
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
REXCVAR_DEFINE_BOOL(edf_native_map_effect_census,false,"EDF2027",
  "Tally map-effect objects by (vtable, mode, slot-4 method) before each map-effect walk; log the top classes every 600 frames.");
REXCVAR_DEFINE_BOOL(edf_native_map_effect_list,false,"EDF2027",
  "Walk the map-effect list (sub_820B35A0) natively; each object still goes through the hooked sub_821C0C00.");
namespace {
// Render helper entries (sub_821A5080), one per frame; census periods count these.
std::atomic<uint64_t> native_render_frames{0};
// Unvalidated big-endian words at the same host address REX_LOAD_U32 and
// REX_STORE_U32 use, so the native walk faults exactly where the guest would.
struct NativeRawGuestWords {
  uint8_t* base;
  static uint32_t Add(uint32_t address,uint32_t offset) { return address+offset; }
  uint8_t* Host(uint32_t address) const { return base+address+(address>=0xE0000000u?0x1000u:0u); }
  uint32_t Word(uint32_t address) const { return edf::native::GuestBlockWord(Host(address)); }
  void StoreWord(uint32_t address,uint32_t value) const {
    auto* p=Host(address); for(uint32_t i=0;i<4;++i) p[i]=uint8_t(value>>(24-i*8));
  }
};
void RecordNativeMapEffectCensus(uint8_t* base,uint32_t list) {
  constexpr uint64_t period=600;
  static std::mutex mutex;
  static edf::native::NativeMapEffectCensus census;
  static uint64_t first_frame=0,failures=0;
  const auto frame=native_render_frames.load(std::memory_order_relaxed);
  std::lock_guard lock(mutex);
  if(!census.walks()) first_frame=frame;
  try {
    const edf::native::GuestReader reader(base);
    const edf::native::NativeSceneCpuWindow window(reader);
    census.Record(window,list);
  } catch(const std::exception& error) {
    if(failures++<8) REXLOG_ERROR("Native map-effect census: walk failed list={:#x}: {}",list,error.what());
  }
  const auto frames=frame-first_frame;
  if(frames<period) return;
  REXLOG_INFO("Native map-effect census: frames={} walks={} objects={} per_frame={:.1f} classes={} truncated={} failures={}",
    frames,census.walks(),census.objects(),double(census.objects())/double(frames),census.classes(),census.truncated(),failures);
  uint32_t rank=0;
  for(const auto& row:census.Top(16)) {
    const auto* name=edf::native::NativeMapEffectClassName(row.key.vtable);
    REXLOG_INFO("Native map-effect census: #{} vtable={:#x} class={} mode={} render={:#x} count={} share={:.2f}% hidden={}",
      ++rank,row.key.vtable,name?name:"?",row.key.mode,row.key.render,row.count,row.percent,row.hidden);
  }
  census.Reset();
}
}
REX_EXTERN(__imp__sub_820B35A0);
REX_EXTERN(sub_821C0C00);
// clMapEffectManager slot 2 (sub_820B3610 tail-calls with r4=manager+48,
// r5=context). The native walk replaces only the loop: same frame size and
// back chain, same object/context registers and return address per call, next
// link read after each callback, and nonvolatile/stack/lr state restored as
// __restgprlr_29 does. Volatile registers are left as the last callee left them.
REX_HOOK_RAW(sub_820B35A0) {
  edf::native::HookTiming timing(edf::native::HookPhase::RenderList);
  if(REXCVAR_GET(edf_native_map_effect_census)) RecordNativeMapEffectCensus(base,ctx.r4.u32);
  if(!REXCVAR_GET(edf_native_map_effect_list)) { __imp__sub_820B35A0(ctx,base); return; }
  const NativeRawGuestWords words{base};
  auto work=ctx;
  const auto stack=work.r1.u32-112;
  words.StoreWord(stack,work.r1.u32); work.r1.u64=stack;
  const auto context=ctx.r5.u64;
  edf::native::WalkNativeMapEffects(words,ctx.r4.u32,[&](uint32_t object) {
    work.r4.u64=context; work.r3.u64=object; work.lr=0x820B35E4;
    // Single per-object call site. The mode-1/2 bucket port
    // (native_bucket_dispatch.h) takes over here; everything else stays guest.
    sub_821C0C00(work,base);
  });
  work.r11.u64=0; work.cr6.compare<uint32_t>(work.r11.u32,0,work.xer);
  work.r1=ctx.r1; work.lr=ctx.lr; work.r29=ctx.r29; work.r30=ctx.r30; work.r31=ctx.r31;
  ctx=work;
}
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
  if(!REXCVAR_GET(edf_native_scene_queued)) {
    if(edf::native::native_material_pass_cursor) edf::native::native_material_pass_cursor->reset();
    edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::OriginalGroup);
    __imp__sub_821D96D8(ctx,base); group.execution.Complete(); return;
  }
  size_t native_queue_size=0;
  uint32_t group_device=0;
  if(REXCVAR_GET(edf_native_host) && REXCVAR_GET(edf_native_shader_bridge) && REXCVAR_GET(edf_native_seam_draws)) {
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
        if(REXCVAR_GET(edf_native_scene_geometry_owned) && edf::native::native_scene_publication) {
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
           REXCVAR_GET(edf_native_scene_activation_owned) && REXCVAR_GET(edf_native_scene_material_owned) &&
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
      if(REXCVAR_GET(edf_native_scene_material_audit) || REXCVAR_GET(edf_native_scene_material_owned)) {
        const auto publication=edf::native::native_scene_publication;
        if(publication) for(const auto& published:publication->group_materials)
          if(published->group==group_address) { group.published_material=published; break; }
        if(group.published_material) {
          try {
          if(!edf::native::native_scene_pass_camera) throw std::runtime_error("native material pass has no camera");
          group.pass_constants=group.published_material->constants;
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
// Retains one descriptor's observed VB/IB snapshots as native indexed geometry
// and commits them as the buffers' observed storage; throws if either buffer
// changed generation or version meanwhile. Shared by the static preload and
// the model pass, which draw the same 148-byte descriptor shape.
std::shared_ptr<const NativeIndexedMesh::RetainedDraw> RetainNativeSceneGeometryLocked(Bridge& state,
    const NativeSceneGeometrySource& input,const auto& vb,const auto& ib,const auto& declaration,const auto& shader,
    const std::array<NativeBufferWrites::ObservedSnapshot,2>& observed) {
  auto& backend=EnsureSceneBackendLocked(state);
  auto& mesh=state.meshes.Acquire(backend,shader,
    {input.vertex,input.index,input.declaration,input.shader,0},declaration->bytes(),input.stride,
    *observed[0].contents,*observed[1].contents,ib.stride,declaration,{},
    ib.index_storage,vb.vertex_storage,{},observed[0].contents,0,observed[1].contents);
  auto geometry=state.scene_adapter.RetainGeometry(state.scene_backend,mesh,0,input.count);
  if(!state.model_buffers.CommitObservedGeometry(
      {input.vertex,vb.generation,observed[0].version},
      {input.index,ib.generation,observed[1].version},
      mesh.VertexStorage(),observed[0].contents,mesh.IndexStorage(),observed[1].contents))
    throw std::runtime_error("native scene geometry changed before publication");
  return geometry;
}
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
  const auto current=[&](uint32_t address,const auto& group,const auto& load) {
    if(load.revision!=group.revision || !load.reads.Unchanged(reader)) return false;
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
  const auto loaded_before=state.scene_geometry_loaded;
  for(const auto& entry:state.scene_sources.Groups()) {
    const auto address=entry.first; const auto& group=entry.second; // Captured below.
    const auto cached=state.scene_geometry_loads.find(address);
    if(cached!=state.scene_geometry_loads.end() && current(address,group,cached->second)) {
      ++state.scene_geometry_unchanged; continue;
    }
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
// One material (a 112-byte pass record: +96/+104 state operations, +108 the
// shader pair) as an owned program plus its constant layout and values.
// Program inputs go through `recorder` (the change signal); constant values
// through `reader`, unrecorded, for the per-use refresh. The previous program
// is kept when every program input and host identity is unchanged.
struct NativeSceneMaterialBuild {
  std::shared_ptr<const NativeMaterialParameters::Groups> schema;
  NativeSceneMaterialConstantLayout layout;
  std::shared_ptr<const NativeSceneMaterialProgram> program;
  std::vector<NativeSceneMaterialInputs::Constant> constants;
  bool reused=false;
};
template<class Recorder,class Reader>
NativeSceneMaterialBuild BuildNativeSceneMaterialLocked(Bridge& state,const Recorder& recorder,const Reader& reader,
    uint32_t material,const NativeSceneMaterialProgram* previous) {
  const auto pass=recorder.Word(recorder.Add(material,108));
  const auto vertex=recorder.Word(recorder.Word(pass));
  const auto pixel=recorder.Word(recorder.Add(recorder.Word(recorder.Add(pass,4)),4));
  const auto& vs=state.shaders.at(vertex);
  const auto& ps=state.shaders.at(pixel);
  if(!vs.reversed_bindings) throw std::runtime_error("native material has no vertex variants");
  NativeSceneMaterialBuild result;
  result.schema=state.material_parameters.Get(material);
  const auto& schema=*result.schema;
  result.layout=ResolveNativeSceneMaterialConstants(schema,[&](bool pixel_stage,const std::string& name) {
    return pixel_stage?ps.bindings->GuestFloatRegisterBytes(name):std::max(
      vs.bindings->GuestFloatRegisterBytes(name),vs.reversed_bindings->GuestFloatRegisterBytes(name));
  });
  auto definition=ReadNativeSceneMaterialDefinition(recorder,material,schema,
    [&](const std::string& name) { return ps.bindings->ResolveResource(name).used(); });
  auto sampler_operations=ReadNativeMaterialSamplerOperations(recorder,schema);
  result.constants=ReadNativeSceneMaterialConstants(reader,schema,result.layout);
  std::vector<std::shared_ptr<NativeBackendTexture>> textures;
  for(const auto& input:definition.textures) {
    if(!input.handle) { textures.emplace_back(); continue; }
    const auto texture=state.textures.find(input.handle);
    if(texture==state.textures.end() || !texture->second.content_valid || !texture->second.backend)
      throw std::runtime_error("native material texture is not ready");
    textures.push_back(texture->second.backend);
  }
  if(previous && previous->inputs==definition && previous->textures==textures &&
     previous->sampler_operations==sampler_operations && previous->backend==state.scene_backend &&
     previous->vertex.bytecode==vs.bindings->shader().bytecode &&
     previous->reversed_vertex.bytecode==vs.reversed_bindings->shader().bytecode &&
     previous->pixel.bytecode==ps.bindings->shader().bytecode) {
    result.reused=true;
    return result;
  }
  auto program=std::make_shared<NativeSceneMaterialProgram>();
  program->backend=state.scene_backend; program->inputs=std::move(definition); program->textures=std::move(textures);
  program->sampler_operations=std::move(sampler_operations);
  program->vertex=vs.bindings->shader(); program->reversed_vertex=vs.reversed_bindings->shader();
  program->pixel=ps.bindings->shader();
  result.program=std::move(program);
  return result;
}
void PreloadStaticSceneMaterialsLocked(Bridge& state,const GuestReader& backing) {
  const NativeSceneCpuWindow reader(backing);
  // Program change signals: group revision, the descriptor's material, the
  // published program, schema/shader/texture identities and every recorded
  // program byte. Constant values are not program inputs: they change per
  // frame (untracked stores) and are refreshed on their own below.
  const auto current=[&](uint32_t address,const auto& group,const auto& load) {
    const auto geometry=state.scene_geometry_loads.find(address);
    if(load.revision!=group.revision || !load.published || geometry==state.scene_geometry_loads.end() ||
       geometry->second.source.material!=load.material ||
       state.scene_adapter.GroupMaterial(address,group.revision)!=load.published) return false;
    if(!NativeSceneMaterialHostCurrent(state,*load.published->program,load.material,load.schema)) return false;
    return load.reads.Unchanged(reader);
  };
  for(const auto& [address,group]:state.scene_sources.Groups()) {
    const auto cached=state.scene_material_loads.find(address);
    if(cached!=state.scene_material_loads.end() && current(address,group,cached->second)) {
      // The program is unchanged; only constant values are re-read, and the
      // program object is kept when one of them moved.
      auto& load=cached->second;
      bool refreshed=false;
      try {
        auto constants=RefreshNativeSceneMaterialConstants(reader,*load.schema,load.constants,load.published->constants);
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
  const auto parts=ReadNativeStaticSceneParts(reader,owner);
  state.scene_sources.Observe(owner,parts);
  state.scene_sources.PublishWorld(owner,ReadNativeStaticWorld(reader,owner));
  state.scene_sources.PublishVisibility(owner,ReadNativeSceneVisibility(reader,owner,true));
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
  const bool scene=REXCVAR_GET(edf_native_scene_adapter_audit) || REXCVAR_GET(edf_native_scene_queued);
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
    state.scene_sources.Born(object);
    edf::native::PublishStaticScenePartsLocked(state,edf::native::GuestReader(base),object);
  }
}
REX_EXTERN(__imp__sub_820B2870);
REX_HOOK_RAW(sub_820B2870) {
  if(REXCVAR_GET(edf_native_scene_adapter_audit) || REXCVAR_GET(edf_native_scene_queued)) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    state.scene_adapter.Retire(ctx.r3.u32);
    state.scene_sources.Retire(ctx.r3.u32);
  }
  __imp__sub_820B2870(ctx,base);
}
REX_EXTERN(__imp__sub_820B2AC0);
REX_HOOK_RAW(sub_820B2AC0) {
  const auto owner=ctx.r3.u32;
  __imp__sub_820B2AC0(ctx,base);
  if(REXCVAR_GET(edf_native_scene_adapter_audit) || REXCVAR_GET(edf_native_scene_queued)) {
    auto& state=edf::native::State();
    // Nested in construction the owner is not yet born and cannot become so
    // on another thread; skip the submission wait for a publication of nothing.
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
  if(REXCVAR_GET(edf_native_scene_queued)) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    if(const auto generation=state.scene_sources.Generation(owner)) {
      const auto world=edf::native::ReadNativeStaticWorld(edf::native::GuestReader(base),owner);
      state.scene_sources.PublishWorld(owner,world);
      state.scene_sources.PublishVisibility(owner,edf::native::ReadNativeSceneVisibility(edf::native::GuestReader(base),owner,true));
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
  if(!REXCVAR_GET(edf_native_bucket_dispatch) || REXCVAR_GET(edf_native_bucket_dispatch_audit)) return false;
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
  const bool audit=REXCVAR_GET(edf_native_bucket_dispatch_audit),native=REXCVAR_GET(edf_native_bucket_dispatch);
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
  if(REXCVAR_GET(edf_native_scene_queued)) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    if(state.scene_sources.HasOwner(owner)) state.scene_sources.PublishVisibility(owner,
      edf::native::ReadNativeSceneVisibility(edf::native::GuestReader(base),owner,true));
  }
}
REX_EXTERN(__imp__sub_821BEF10);
REX_HOOK_RAW(sub_821BEF10) {
  const auto destination=ctx.r4.u32;
  __imp__sub_821BEF10(ctx,base);
  if(REXCVAR_GET(edf_native_scene_queued) && destination>=288) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    const auto owner=destination-288;
    if(state.scene_sources.HasOwner(owner)) state.scene_sources.PublishVisibility(owner,
      edf::native::ReadNativeSceneVisibility(edf::native::GuestReader(base),owner,true));
  }
}
REX_EXTERN(__imp__sub_820B4038);
REX_EXTERN(__imp__sub_821C4EB8);
REX_HOOK_RAW(sub_821C4EB8) {
  edf::native::TreePublications().Invalidate();
  const auto node=ctx.r3.u32;
  __imp__sub_821C4EB8(ctx,base);
  edf::native::TouchStaticWalkPlans({node+120});
  if(REXCVAR_GET(edf_native_scene_queued) && REXCVAR_GET(edf_native_scene_visibility)) {
    auto& state=edf::native::State(); std::lock_guard lock(state.mutex);
    state.scene_membership.Born(node+120); ++state.scene_membership_events;
  }
}
REX_EXTERN(__imp__sub_821C5D28);
REX_HOOK_RAW(sub_821C5D28) {
  edf::native::TreePublications().Invalidate();
  const auto node=ctx.r3.u32;
  __imp__sub_821C5D28(ctx,base);
  edf::native::TouchStaticWalkPlans({node+120});
  if(REXCVAR_GET(edf_native_scene_queued) && REXCVAR_GET(edf_native_scene_visibility)) {
    auto& state=edf::native::State(); std::lock_guard lock(state.mutex);
    state.scene_membership.Born(node+120,edf::native::GuestReader(base).Word(node+132));
    ++state.scene_membership_events;
  }
}
REX_EXTERN(__imp__sub_821A1628);
REX_HOOK_RAW(sub_821A1628) {
  if(REXCVAR_GET(edf_native_scene_tree_published)) {
    auto& state=edf::native::State(); std::lock_guard lock(state.mutex);
    if(state.scene_membership.HasAnchor(ctx.r3.u32) || state.scene_membership.HasAnchor(ctx.r4.u32))
      edf::native::TreePublications().Invalidate();
  }
  const auto anchor=ctx.r3.u32,node=ctx.r4.u32;
  __imp__sub_821A1628(ctx,base);
  // After the link: the destination by its anchor, the source list by the node.
  edf::native::TouchStaticWalkPlans({anchor,node});
  if(REXCVAR_GET(edf_native_scene_queued) && REXCVAR_GET(edf_native_scene_visibility)) {
    auto& state=edf::native::State(); std::lock_guard lock(state.mutex);
    if(state.scene_membership.HasAnchor(anchor)) {
      state.scene_membership.InsertAfter(anchor,node,edf::native::GuestReader(base).Word(node+8));
      ++state.scene_membership_events;
    } else if(state.scene_membership.Remove(node)) ++state.scene_membership_events;
  }
}
REX_EXTERN(__imp__sub_821A1678);
REX_HOOK_RAW(sub_821A1678) {
  if(REXCVAR_GET(edf_native_scene_tree_published)) {
    auto& state=edf::native::State(); std::lock_guard lock(state.mutex);
    if(state.scene_membership.HasAnchor(ctx.r3.u32)) edf::native::TreePublications().Invalidate();
  }
  const auto node=ctx.r3.u32;
  __imp__sub_821A1678(ctx,base);
  edf::native::TouchStaticWalkPlans({node});
  if(REXCVAR_GET(edf_native_scene_queued) && REXCVAR_GET(edf_native_scene_visibility)) {
    auto& state=edf::native::State(); std::lock_guard lock(state.mutex);
    if(state.scene_membership.Remove(node)) ++state.scene_membership_events;
  }
}
REX_EXTERN(__imp__sub_821B0198);
REX_EXTERN(__imp__sub_821C3070);
REX_EXTERN(__imp__sub_821C33E8);
REX_HOOK_RAW(sub_820B4038) {
  edf::native::HookTiming timing(edf::native::HookPhase::RenderGather);
  auto* queues=edf::native::native_scene_queues;
  if(!queues || !queues->enabled || !REXCVAR_GET(edf_native_scene_visibility)) {
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
  NativeSceneCpuWindow cpu(reader);
  const auto context=ctx.r5.u32;
  uint32_t end=0;
  const auto generation=reader.Word(reader.Add(context,12));
  uint32_t cursor=0;
  // The walk's view when this list runs under the walk's own scope.
  auto* walk_view=bridge_walk_view && bridge_walk_view->scope==bridge && bridge_walk_view->context==context?bridge_walk_view:nullptr;
  const auto read_view=[&] {
    const auto read=[&] { return ReadNativeSceneVisibilityView(cpu,context); };
    return walk_view?walk_view->view.Get(BridgeWalkLock::guest_calls,read):read();
  };
  auto view=read_view();
  auto* center_destination=const_cast<uint8_t*>(cpu.WritableBytes(reader.Add(context,32),16,4));
  auto work=ctx;
  if(work.r1.u32<160) throw std::runtime_error("invalid native visibility stack");
  work.r1.u64=work.r1.u32-160;
  reader.StoreWord(work.r1.u32,ctx.r1.u32);
  const bool audit=REXCVAR_GET(edf_native_scene_visibility_audit);
  std::shared_ptr<const NativeSceneMembership::Snapshot> membership;
  // Resolve the pass's source generation once per list, not per candidate:
  // each candidate then costs one owner lookup and no further lock.
  std::shared_ptr<const NativeSceneSources> pass_sources;
  {
    bridge->Hold();
    bool published=false;
    if(REXCVAR_GET(edf_native_scene_membership_owned) && native_scene_publication && native_scene_publication->membership) {
      const auto& lists=*native_scene_publication->membership;
      if(state.scene_membership.Current(lists)) {
        const auto found=lists.lists.find(ctx.r4.u32);
        if(found!=lists.lists.end()) { membership=found->second; published=true; }
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
      end=reader.Word(reader.Add(ctx.r4.u32,12)); cursor=reader.Word(ctx.r4.u32);
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
      NativeSceneSources::Candidate looked_up;
      const NativeSceneSources::Candidate* found=&looked_up;
      if(planned && plan_drives && plan_sources) { found=&planned->source; ++source_reuses; }
      else if(pass_sources) looked_up=pass_sources->FindCandidate(owner);
      else { bridge->Hold(); looked_up=state.scene_sources.FindCandidate(owner); }
      const auto& source=*found;
      if(planned && !plan_drives) {
        const bool live_direct=!hidden && !mode && cpu.Word(reader.Add(table,16))==kNativeStaticDirectRender;
        AuditNativeStaticWalkClassification(*planned,table,mode,hidden,live_direct,plan_sources?&source:nullptr,plan_audit);
      }
      // vtable+16 from the plan while the live vtable is the one it was read
      // through (vtables are image data); otherwise the live slot.
      const auto direct=[&] {
        if(planned && plan_drives && planned->vtable==table) { ++direct_reuses; return planned->direct; }
        return cpu.Word(reader.Add(table,16))==kNativeStaticDirectRender;
      };
      const auto& published=source.visibility;
      const auto read_live=[&](bool lods) {
        const GuestReadWindow window(cpu,owner,lods?540:356);
        return ReadNativeSceneVisibility(window,owner,lods);
      };
      auto object=published?*published:read_live(false);
      ++candidates; retained+=bool(published);
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
      auto center=NativeVisibilityTransform({object.box[0],object.box[1],object.box[2],object.box[3]},view.matrix);
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
      StoreGuestCpuWords(std::span<uint8_t>{center_destination,16},encoded_center);
      const float depth=-float(center[2]*view.depth_scale);
      bool visible=!(depth>object.distance);
      if(visible) {
        auto sphere=NativeVisibilitySphere(view,center,object.radius);
        auto box=sphere==2?NativeVisibilityBox(view,object.box):sphere;
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
      bool native_selected=false;
      // Preserve the guest hidden flag and nonzero sorting modes. Only the
      // audited static direct-dispatch method may bypass the virtual callback.
      if(visible && published && object.lod_count && queues->enabled &&
         hidden==0 && mode==0 && direct()) {
        const auto parts=source.Lod(NativeVisibilityLod(object,depth));
        native_selected=parts.has_value();
        if(parts) for(const auto& part:*parts) {
          if(!part.group || (!queues->Contains(part.group) &&
             cpu.Word(reader.Add(part.group,4))!=cpu.Word(reader.Add(part.group,8)))) { native_selected=false; break; }
        }
        if(native_selected) {
          for(const auto& part:*parts) queues->Push(part.group,part.instance);
          ++selected;
        }
      }
      if(visible && !native_selected) {
        // Classify first. Sort modes 1/2 go through the native bucket insert
        // when it is enabled: it touches guest memory only, so it is not a
        // guest call, keeps the hold, and membership, the view and the plan
        // stay valid. Only a route into the original routine is a guest call.
        const bool try_native=hidden==0 && (int32_t(mode)==1 || int32_t(mode)==2);
        work.r3.u64=owner; work.r4.u64=context; work.lr=0x820B410C;
        callback=NativeWalkBucketDispatch<std::mutex>(try_native,[&] { return TryNativeBucketInsert(work,base); },[&] {
          // Unported callbacks may change membership or node values. Continue
          // from the original post-callback link rather than an older snapshot.
          membership.reset();
          cpu.Invalidate(); center_destination=nullptr;
          // Hierarchy writer hooks invalidate tree images if this callback
          // changes membership, bounds or topology. Unrelated callback activity
          // must not discard every completed producer publication.
          DispatchNativeBucketObject(work,base,try_native);
        });
        bucket_native+=try_native && !callback;
        if(callback) {
          // A remaining callback can update camera data; no live read window or
          // registry span survives it.
          view=read_view();
          center_destination=const_cast<uint8_t*>(cpu.WritableBytes(reader.Add(context,32),16,4));
        }
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
REXCVAR_DEFINE_BOOL(edf_native_frame_dispatch,false,"EDF2027",
  "Own outer render phase dispatch in native code; remaining phase callbacks are retained.");
REXCVAR_DEFINE_INT32(edf_native_ab_alternate,0,"EDF2027",
  "A/B diagnostics: alternate guest and native passes in runs of N indexed output frames from the capture start frame; odd runs are native, 0 off (development)").range(0,1000);
REX_HOOK_RAW(sub_821A5080) {
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
  native_render_frames.fetch_add(1,std::memory_order_relaxed);
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
  if(REXCVAR_GET(edf_native_scene_queued) && REXCVAR_GET(edf_native_host) &&
     REXCVAR_GET(edf_native_shader_bridge) && REXCVAR_GET(edf_native_seam_draws)) {
    edf::native::native_scene_queues=&queues;
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    edf::native::native_scene_publication=state.scene_adapter.AcquirePublication();
    if(REXCVAR_GET(edf_native_scene_camera_owned))
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
  NativeLoopTrace trace("helper_dispatch",ctx.r3.u32,ctx.lr);
  edf::native::HookTiming timing(edf::native::HookPhase::ResourceHelper);
  edf::native::HookTiming engine_timing(edf::native::HookPhase::RenderHelper);
  if(REXCVAR_GET(edf_native_frame_dispatch) && REXCVAR_GET(edf_native_shader_bridge)) {
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
  } else __imp__sub_821A5080(ctx,base);
  if(!queues.empty()) throw std::runtime_error("native scene selections survived their render helper");
}
REX_EXTERN(__imp__sub_820B2510);
REX_HOOK_RAW(sub_820B2510) {
  if(REXCVAR_GET(edf_native_unlock_framerate)) {
    auto& motion=ModelMotionState();
    std::lock_guard lock(motion.mutex);
    motion.Erase(ctx.r3.u32);
  }
  // Ungated so a live toggle cannot leave a layout past its free: one relaxed
  // load while nothing is registered, and never the bridge lock.
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
    try {
      auto layout=decode([&](const auto& lookup) {
        return edf::native::DecodeNativeModelLayoutWith(reader,instance,vector,lookup);
      });
      const auto storage=edf::native::ReadNativeModelPoseRange(reader,vector).begin;
      const auto meshes=layout.meshes.size(),batches=layout.Batches();
      const auto captured=models.Register(std::move(layout),storage);
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
  if(REXCVAR_GET(edf_native_model_publication)) ObserveNativeModelPublication(base,ctx.r3.u32,ctx.r4.u32);
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
  if(REXCVAR_GET(edf_native_scene_material_audit) || REXCVAR_GET(edf_native_scene_material_owned)) {
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
  // Publication follows the render-helper join. Constructors and other callers
  // must initialize their real matrices and invalidate any reused address.
  if(ctx.lr!=0x821A4EB0 || !native_loop_budget.unlocked || native_loop_budget.divisor!=1 ||
     !REXCVAR_GET(edf_native_camera_interpolation)) {
    histories.erase(scene);
    __imp__sub_821CDDF8(ctx,base); return;
  }
  const edf::native::GuestReader reader(base);
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
  if(rendered==pose) { __imp__sub_821CDDF8(ctx,base); return; }
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
  interpolated[16]=std::bit_cast<uint32_t>(rendered.fov);
  edf::native::StoreGuestCpuWords(std::span<uint8_t>{destination,68},interpolated);
  __imp__sub_821CDDF8(ctx,base);
}
REX_EXTERN(__imp__sub_821A4FC0);
REX_HOOK_RAW(sub_821A4FC0) {
  const bool trace=REXCVAR_GET(edf_native_load_timings);
  const uint32_t manager=ctx.r3.u32;
  if(trace) REXLOG_INFO("Native resource worker: begin manager={:#x}",manager);
  if(trace) {
    // Snapshot identities only; do not invoke callbacks or replace the original
    // traversal. r4 owns the list, while r3 supplies the slot-6 argument.
    // The bounded snapshot is diagnostic, not an ownership certificate.
    try {
      const edf::native::GuestReader reader(base);
      const uint32_t list_owner=ctx.r4.u32;
      const uint32_t owner=reader.Word(reader.Add(list_owner,132));
      const uint32_t owner_vtable=reader.Word(owner);
      REXLOG_INFO("Native resource callbacks: manager={:#x} owner={:#x} enter={:#x} leave={:#x}",
        manager,owner,reader.Word(reader.Add(owner_vtable,32)),reader.Word(reader.Add(owner_vtable,36)));
      const uint32_t sentinel=reader.Word(reader.Add(list_owner,2232));
      uint32_t node=reader.Word(sentinel);
      uint32_t count=0;
      for(;node!=sentinel && count<256;++count) {
        const uint32_t object=reader.Word(reader.Add(node,12));
        const uint32_t vtable=reader.Word(object);
        REXLOG_INFO("Native resource callback: manager={:#x} index={} object={:#x} vtable={:#x} slot6={:#x}",
          manager,count,object,vtable,reader.Word(reader.Add(vtable,24)));
        node=reader.Word(node);
      }
      if(node!=sentinel) REXLOG_WARN("Native resource callback snapshot truncated at {} records",count);
    } catch(const std::exception& error) {
      REXLOG_WARN("Native resource callback snapshot unavailable: {}",error.what());
    }
  }
  edf::native::HookTiming timing(edf::native::HookPhase::ResourceOneShot);
  __imp__sub_821A4FC0(ctx,base);
  timing.Finish();
  if(trace) REXLOG_INFO("Native resource worker: end manager={:#x}",manager);
}
REX_EXTERN(__imp__sub_821A4DE8);
REX_HOOK_RAW(sub_821A4DE8) {
  const bool trace=REXCVAR_GET(edf_native_load_timings);
  const uint32_t manager=ctx.r3.u32;
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
  if(edge) REXLOG_INFO("Native resource transition: begin manager={:#x} actual={} desired={}",
                      manager,actual,desired);
  edf::native::HookTiming timing(edf::native::HookPhase::ResourceTransition,edge);
  edf::native::HookTiming engine_timing(edf::native::HookPhase::FrameTransition);
  const bool model_publication=REXCVAR_GET(edf_native_model_publication);
  std::vector<uint32_t> dirty_poses;
  {
    struct Scope {
      std::vector<uint32_t>* previous=native_model_dirty_poses;
      ~Scope() { native_model_dirty_poses=previous; }
    } scope;
    native_model_dirty_poses=model_publication?&dirty_poses:nullptr;
    __imp__sub_821A4DE8(ctx,base);
  }
  if(REXCVAR_GET(edf_native_scene_camera_owned)) {
    auto cameras=edf::native::ReadNativeScenePassCameras(edf::native::GuestReader(base),manager);
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    state.scene_adapter.PublishCameras(std::move(cameras));
  }
  {
    auto& motion=ModelMotionState();
    std::lock_guard lock(motion.mutex);
    motion.published=native_loop_budget;
    ++motion.publication;
    if(!native_loop_budget.unlocked) motion.Clear();
  }
  if(REXCVAR_GET(edf_native_scene_queued) && (!native_loop_budget.unlocked || native_loop_budget.steps)) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    state.scene_publication_tick+=std::max(1u,native_loop_budget.steps);
    if(REXCVAR_GET(edf_native_scene_visibility)) state.scene_membership.Publish();
    if(REXCVAR_GET(edf_native_scene_tree_published)) {
      auto& trees=edf::native::TreePublications();
      const edf::native::GuestReader reader(base);
      for(const auto owner:trees.Owners()) {
        try { trees.Publish(reader,owner); }
        catch(const std::exception&) { trees.Retire(owner); }
      }
    }
    if(REXCVAR_GET(edf_native_scene_preload)) {
      const edf::native::GuestReader reader(base);
      edf::native::PreloadStaticSceneGeometryLocked(state,reader);
      edf::native::PreloadStaticSceneMaterialsLocked(state,reader);
    }
    if(state.scene_adapter.objects() || state.scene_adapter.geometry_groups() || state.scene_adapter.AcquirePublication())
      state.scene_adapter.Publish(state.scene_publication_tick,
        REXCVAR_GET(edf_native_scene_sources_owned)?state.scene_sources.AcquireSnapshot():nullptr,
        REXCVAR_GET(edf_native_scene_membership_owned)?state.scene_membership.AcquirePublication():nullptr,
        REXCVAR_GET(edf_native_scene_membership_owned)?edf::native::TreePublications().AcquireAll():edf::native::NativeSceneTreePublications::Images{});
  }
  // Pose generation for this tick, beside the scene publication: only vectors
  // the dirty walk rebuilt (and first-sight seeds) are read from guest memory.
  if(model_publication) {
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
  } else if(ModelPublications().size()) ModelPublications().Clear();
  timing.Finish();
  if(edge) REXLOG_INFO("Native resource transition: end manager={:#x}",manager);
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
  if(REXCVAR_GET(edf_native_host)) {
    CaptureNativeDisplayGamma(base,ctx.r3.u32,ctx.r4.u32,edf::native::NativeDisplayGamma::Mode::Table256);
    __imp__edf_native_gamma_table_cpu_tail(ctx,base);
    return;
  }
  __imp__sub_82142050(ctx,base);
}
REX_HOOK_RAW(sub_82142130) {
  if(REXCVAR_GET(edf_native_host)) {
    CaptureNativeDisplayGamma(base,ctx.r3.u32,ctx.r4.u32,edf::native::NativeDisplayGamma::Mode::Piecewise128);
    __imp__edf_native_gamma_pwl_cpu_tail(ctx,base);
    return;
  }
  __imp__sub_82142130(ctx,base);
}
REX_EXTERN(__imp__sub_821B6880);
REX_HOOK_RAW(sub_821B6880) {
  if (!REXCVAR_GET(edf_native_shader_bridge)) { __imp__sub_821B6880(ctx, base); return; }
  const uint32_t owner = ctx.r3.u32;
  edf::native::ForgetOwner(owner);
  edf::native::Effect effect;
  bool captured = false;
  try {
    effect = edf::native::SnapshotEffect(edf::native::GuestReader(base), ctx.r4.u32);
    captured = true;
  } catch (const std::exception& error) { REXLOG_ERROR("Native shader bridge: {}", error.what()); }
  __imp__sub_821B6880(ctx, base);
  if (captured && ctx.r3.u8) {
    try { edf::native::RegisterShaders(edf::native::GuestReader(base), owner, effect); }
    catch (const std::exception& error) { REXLOG_ERROR("Native shader bridge: {}", error.what()); }
  }
}
REX_EXTERN(__imp__sub_821B8E48);
REXCVAR_DEFINE_BOOL(edf_native_material_activation,false,"EDF2027",
  "Run material activation from a native operation list with native sampler resolution");
REX_EXTERN(sub_821498C8);
REX_EXTERN(sub_82149608);
REX_EXTERN(sub_82149248);
REX_EXTERN(sub_82149358);
REX_EXTERN(sub_8213BA98);
namespace {
void BindNativeShaderResource(PPCContext&,uint8_t*,bool);
void PublishNativeShaderBinding(uint32_t,uint32_t,bool);
// states=false runs the activation without its state operations (the static
// world handoff writes those combined): shader binds with defaults, constant
// uploads and texture binds with their retirements and sampler words.
void ActivateNativeMaterial(PPCContext& ctx,uint8_t* base,uint32_t instance,uint32_t device,bool states=true) {
  const edf::native::GuestReader reader(base);
  const auto program=edf::native::ReadNativeMaterialCpuProgram(reader,instance,device);
  auto work=ctx;
  if(work.r1.u32<144) throw std::runtime_error("native activation stack");
  work.r1.u64-=144;
  reader.StoreWord(work.r1.u32,ctx.r1.u32);
  edf::native::ExecuteNativeMaterialCpuProgram(program,[&](uint32_t shader,bool pixel) {
    work.r3.u64=device; work.r4.u64=shader; work.lr=pixel?0x821B8E84:0x821B8E70;
    BindNativeShaderResource(work,base,pixel);
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
      auto* target=const_cast<uint8_t*>(reader.WritableBytes(destination,bytes,4));
      const auto dirty_address=reader.Add(device,operation.pixel?8:0);
      const uint64_t dirty=(uint64_t(reader.Word(dirty_address))<<32)|reader.Word(reader.Add(dirty_address,4));
      if(!edf::native::UploadNativeMaterialConstant(reader,device,operation,mask))
        throw std::runtime_error("native constant eligibility changed during activation");
      if(REXCVAR_GET(edf_native_scene_material_audit)) {
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
    work.r6.u64=mask; work.lr=0x821B9090; sub_8213BA98(work,base);
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
      edf::native::PublishNativeRenderState(base,device,operation.setter);
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
  if(REXCVAR_GET(edf_native_material_activation) && !REXCVAR_GET(edf_native_material_state_audit) &&
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
  std::map<uint32_t,edf::native::NativeMaterialSamplerPass> expected_samplers;
  std::optional<edf::native::NativeMaterialRenderPass> expected_state;
  const bool sampler_audit=REXCVAR_GET(edf_native_material_sampler_audit);
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
      auto found=expected_samplers.find(operation.slot);
      if(found==expected_samplers.end()) found=expected_samplers.emplace(operation.slot,
        edf::native::ReadNativeMaterialSamplerPass(reader,device,operation.slot)).first;
      found->second=edf::native::ApplyNativeMaterialSampler(found->second,operation);
    }
  }
  {
    edf::native::HookTiming timing(edf::native::HookPhase::ActivationGuest);
    if(REXCVAR_GET(edf_native_shader_bridge) && REXCVAR_GET(edf_native_material_activation) &&
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
    for(const auto& [slot,expected]:expected_samplers) {
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
  if (REXCVAR_GET(edf_native_shader_bridge)) {
    edf::native::HookTiming timing(edf::native::HookPhase::ActivationNative);
    if(const auto* group=edf::native::native_queued_scene_group;
       group && group->material_handoff.activation_pending() && group->activation_instance==instance) return;
    try {
      if(!REXCVAR_GET(edf_native_scene_activation_owned) || !REXCVAR_GET(edf_native_scene_material_owned) ||
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
struct NativeStaticPassView { GuestViewportWords viewport; ActiveTargets targets; };
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
struct NativeStaticGroupMaterial {
  const NativeSceneGroupMaterial* group=nullptr;
  const NativeIndexedMesh::RetainedDraw* geometry=nullptr;
  NativeStaticPassView pass{};
  NativeSceneResolvedMaterial resolved;
  std::optional<VertexParameterRange> world_parameter;
  bool world_column_major=false;
};
// Resolve one published static instance from publication and pass inputs only.
// Never reads the live bound shaders, streams, index bindings or view; those
// checks belong to the caller, through draw. Exceptions propagate. shared, when
// given, carries the group's material between instances resolved against one
// NativeStaticPassInputs; a different group, geometry or view resolves afresh.
NativeStaticInstanceResolution ResolveNativePublishedStaticInstanceLocked(Bridge& state,const GuestReader& reader,
    const NativeScenePublication& publication,const NativeStaticGroupInputs& group,uint32_t instance,
    const NativeStaticPassInputs& pass,const NativeStaticDrawBindings* draw=nullptr,
    std::optional<NativeStaticGroupMaterial>* shared=nullptr) {
  using D=NativeStaticWorldDecline;
  NativeStaticInstanceResolution result;
  const auto decline=[&](D reason) { result.decline=reason; return result; };
  if(BufferWrites().Pending()) return decline(D::PendingWrites);
  const auto& sources=NativeSceneSourcesForPass(state);
  const auto* membership=sources.FindGroup(group.address);
  const auto cached=state.scene_geometry_loads.find(group.address);
  if(!membership || !membership->parts.contains(instance) || cached==state.scene_geometry_loads.end() ||
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
  std::optional<NativeStaticGroupMaterial> local;
  auto& slot=shared?*shared:local;
  if(!slot || slot->group!=group.material.get() || slot->geometry!=geometry.get() ||
     slot->pass.viewport!=result.pass.viewport || slot->pass.targets!=targets) {
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
    slot=NativeStaticGroupMaterial{group.material.get(),geometry.get(),result.pass,std::move(resolved),
      std::move(candidate.world_parameter),candidate.world_column_major};
  }
  const auto& material=*slot;
  if(!material.world_parameter || !NativeStaticWorldOnly(reader,sources,instance,material.world_parameter->first,pass.device)) return decline(D::WorldParameter);
  const auto* source=sources.Find(instance);
  if(!source) return decline(D::Source);
  const auto world=sources.WorldRegisters(*source,source->world_data);
  if(!world) return decline(D::WorldRegisters);
  if(REXCVAR_GET(edf_native_scene_sources_owned) && REXCVAR_GET(edf_native_scene_transform_audit)) {
    ++state.scene_world_checks;
    if(std::memcmp(world->data(),reader.Bytes(source->world_data,64),64)) {
      ++state.scene_world_mismatches;
      REXLOG_ERROR("Native published first world mismatch: owner={:#x} instance={:#x}",source->owner,instance);
      return decline(D::WorldMismatch);
    }
  }
  // The instance's own world, on its copy of the group's capture.
  auto capture=material.resolved.capture;
  ApplyNativeScenePublishedWorld(capture,*world);
  result.view=NativeStaticInstanceView(capture.camera,viewport,material.resolved.render.words[5]!=0);
  if(REXCVAR_GET(edf_native_scene_selection_owned)) {
    result.object=publication.Resolve(*source,geometry,capture);
    if(!result.object) return decline(D::Lifetime);
  } else {
    const auto id=state.scene_adapter.Observe(*source,geometry,capture);
    result.object=SelectNativeSceneInstanceLocked(state,id);
  }
  result.geometry=std::move(geometry); result.material=std::move(capture.material);
  result.vertex=program.inputs.vertex; result.pixel=program.inputs.pixel;
  result.reverse_depth=viewport.reverse_depth;
  result.world_parameter=material.world_parameter; result.world_column_major=material.world_column_major;
  result.world=*world;
  return result;
}
bool TryAppendPublishedNativeSceneInstance(uint8_t* base,uint32_t device,uint32_t address,uint32_t instance,uint32_t count,NativeQueuedSceneGroup& group) {
  if(!REXCVAR_GET(edf_native_scene_geometry_owned) || !REXCVAR_GET(edf_native_scene_material_owned) ||
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
    if(REXCVAR_GET(edf_native_scene_selection_owned) && world_only && native_scene_publication) {
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
  if(!REXCVAR_GET(edf_native_shader_bridge)) return;
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
  if(REXCVAR_GET(edf_native_host)) {
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
  const bool probe=REXCVAR_GET(edf_native_shader_bridge) && REXCVAR_GET(edf_native_fence_probe);
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
REX_EXTERN(edf_native_swap_wait) {
  using namespace edf::native;
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
  auto [it,inserted]=state.swap_clocks.try_emplace(device);
  auto& timing=it->second;
  if(inserted) timing.clock.Reset(NativePacingClock::Clock::now(),0);
  HookTiming gpu_timing(HookPhase::SwapGpuWait);
  // Bound CPU lead with presentation credits. Real guest resource fences and
  // worker callbacks retain their independent GPU completion queues.
  const auto latency=uint32_t(std::clamp(REXCVAR_GET(edf_native_frame_latency),1,3));
  std::unique_ptr<NativeCompletionQueue> barrier;
  if(state.scene_backend->name()=="d3d12") {
    if(!timing.flight) timing.flight=std::make_unique<NativeFrameFlight>(latency);
    timing.flight->Submit(state.scene_backend->MarkCompletion());
  } else {
    barrier=CreateCompletionQueueLocked(state,1); barrier->Submit(2);
  }
  const auto gpu_deadline=NativePacingClock::Clock::now()+std::chrono::seconds(10);
  while(timing.flight?!timing.flight->Ready():barrier->Poll(true)!=2) {
    if(NativePacingClock::Clock::now()>=gpu_deadline)
      throw std::runtime_error("native swap GPU completion timed out");
    lock.unlock();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    lock.lock();
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
  if(REXCVAR_GET(edf_native_host) && callback!=0x8214EBA0) {
    REXLOG_ERROR("Unsupported native signal callback: caller={:#x}, callback={:#x}, device={:#x}, argument={:#x}; no Xbox packet fallback",
      uint32_t(ctx.lr),callback,device,argument);
    throw std::runtime_error("native signal callback has no CPU completion implementation");
  }
  if(REXCVAR_GET(edf_native_host) && REXCVAR_GET(edf_native_hook_timings)) {
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
  if(REXCVAR_GET(edf_native_host) && callback==0x8214EBA0)
    __imp__edf_native_worker_signal_cpu_tail(ctx,base);
  else __imp__sub_8213C9F0(ctx,base);
  if(REXCVAR_GET(edf_native_host) && callback==0x8214EBA0) {
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
  const bool audit=REXCVAR_GET(edf_native_host) && REXCVAR_GET(edf_native_hook_timings);
  static std::atomic<uint64_t> calls{0};
  const auto count=audit?calls.fetch_add(1,std::memory_order_relaxed)+1:0;
  const bool sample=count && (count<=8 || !(count&(count-1)));
  const edf::native::GuestReader reader(base);
  const auto worker=sample?reader.Word(worker_arg):0;
  if(sample)
    REXLOG_INFO("Native CPU worker entered: count={}, worker={:#x}, CPU={}, busy={}, nesting={}, argument={:#x}, continuation={:#x}",
      count,worker,reader.Word(worker_arg+4),reader.Word(worker+56),reader.Word(worker+48),reader.Word(worker+88),reader.Word(worker+80));
  if(REXCVAR_GET(edf_native_host)) __imp__edf_native_worker_audit(ctx,base);
  else __imp__sub_8214E8E0(ctx,base);
  if(REXCVAR_GET(edf_native_host)) {
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
  if(REXCVAR_GET(edf_native_host) && REXCVAR_GET(edf_native_hook_timings)) {
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
  if(REXCVAR_GET(edf_native_host)) {
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
  if(REXCVAR_GET(edf_native_host) && uint32_t(ctx.lr)==0x8213CF9C) {
    __imp__edf_native_cache_range_cpu_tail(ctx,base); return;
  }
  if(REXCVAR_GET(edf_native_host) && uint32_t(ctx.lr)==0x8214ED04) {
    __imp__edf_native_cache_reservation_cpu_tail(ctx,base); return;
  }
  if(REXCVAR_GET(edf_native_host)) {
    REXLOG_ERROR("Unsupported native cache-range caller={:#x}; no Xbox packet fallback",uint32_t(ctx.lr));
    throw std::runtime_error("native cache range caller has no CPU reservation contract");
  }
  __imp__sub_8213C5F0(ctx,base);
}
REX_EXTERN(__imp__edf_native_device_reset);
REX_EXTERN(__imp__sub_8213D1C8);
REX_EXTERN(__imp__edf_native_device_drain);
REX_HOOK_RAW(sub_8213D1C8) {
  if(REXCVAR_GET(edf_native_host)) {
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
  if(REXCVAR_GET(edf_native_host)) {
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
  if(REXCVAR_GET(edf_native_shader_bridge) && REXCVAR_GET(edf_native_fence_probe)) {
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

REX_EXTERN(__imp__sub_82139638);
REX_HOOK_RAW(sub_82139638) {
  // 82139A40 allocates and zero-fills the device before either initializer.
  // Publish the known zero result here, not a later draw-time memory snapshot.
  const bool device_allocation=uint32_t(ctx.lr)==0x82139a70 &&
    ctx.r3.u32==20480 && ctx.r4.u32==128;
  __imp__sub_82139638(ctx,base);
  if(device_allocation && ctx.r3.u32 &&
      (REXCVAR_GET(edf_native_render_state_audit) || REXCVAR_GET(edf_native_owned_render_state)) &&
      REXCVAR_GET(edf_native_shader_bridge)) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    state.render_state_snapshots.Retire(ctx.r3.u32);
    state.render_state_snapshots.Publish(ctx.r3.u32,{},0x82139638);
    state.render_state_snapshots.PublishBlend(ctx.r3.u32,{},0x82139638);
    REXLOG_INFO("Native render-state zeroed device: device={:#x}",ctx.r3.u32);
  }
}
REX_EXTERN(__imp__sub_82139760);
REX_HOOK_RAW(sub_82139760) {
  if((REXCVAR_GET(edf_native_render_state_audit) || REXCVAR_GET(edf_native_owned_render_state)) &&
      REXCVAR_GET(edf_native_shader_bridge)) {
    const edf::native::GuestReader reader(base);
    // Final Release invokes destruction/free; non-final Release keeps ownership.
    if(reader.Word(reader.Add(ctx.r3.u32,52))==1) {
      auto& state=edf::native::State();
      std::lock_guard lock(state.mutex);
      state.render_state_snapshots.Retire(ctx.r3.u32);
    }
  }
  __imp__sub_82139760(ctx,base);
}

REX_EXTERN(__imp__sub_82135418);
REX_HOOK_RAW(sub_82135418) {
  const auto device=ctx.r3.u32;
  __imp__sub_82135418(ctx,base);
  if((REXCVAR_GET(edf_native_owned_render_state) || REXCVAR_GET(edf_native_render_state_audit)) &&
      REXCVAR_GET(edf_native_shader_bridge)) {
    const edf::native::GuestReader reader(base);
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    state.render_state_snapshots.PublishBlend(device,
      edf::native::ReadGuestWords<4>(reader,reader.Add(device,10336)),0x82135418);
  }
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
  if(!REXCVAR_GET(edf_native_host)) { __imp__sub_821390B8(ctx,base); return; }
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
  if(!REXCVAR_GET(edf_native_host) || ctx.r4.u32!=6) { __imp__sub_82138158(ctx,base); return; }
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
  if(REXCVAR_GET(edf_native_host) && (ctx.r3.u32==0 ||
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
  if(!REXCVAR_GET(edf_native_host)) { __imp__sub_821394D8(ctx,base); return; }
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
  if(!REXCVAR_GET(edf_native_host)) { __imp__sub_82139508(ctx,base); return; }
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
  const bool native=REXCVAR_GET(edf_native_host);
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
  if(!REXCVAR_GET(edf_native_host)) { __imp__sub_821BEBF0(ctx,base); return; }
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
REX_HOOK_RAW(sub_821BEAB0) {
  if(!REXCVAR_GET(edf_native_host)) { __imp__sub_821BEAB0(ctx,base); return; }
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
  while(!unlocked && edf::native::NativePacingPending(steps)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    sampled_at=edf::native::NativePacingClock::Clock::now();
    current=state.clock.Sample(sampled_at);
    steps=edf::native::NativePacingSteps(current,previous,divisor);
  }
  waiting.Finish();
  reader.StoreDoubleWord(0x8257C300,current);
  // A render-only iteration must not discard fractional divisor progress.
  if(!unlocked || steps) reader.StoreDoubleWord(0x8257C308,current);
  ctx.r3.u64=reader.Add(object,8);
  sub_821FAC28(ctx,base);
  const auto simulation_steps=edf::native::NativePacingResult(steps);
  native_loop_budget={unlocked,simulation_steps,current,state.clock.Fraction(sampled_at),divisor};
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
  if(!REXCVAR_GET(edf_native_host)) { __imp__sub_821BEB38(ctx,base); return; }
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
  if(!REXCVAR_GET(edf_native_host)) { __imp__sub_8213CF60(ctx,base); return; }
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
  if(!REXCVAR_GET(edf_native_host)) { __imp__sub_8213CDC0(ctx,base); return; }
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
  if(!REXCVAR_GET(edf_native_host)) { __imp__sub_8213BC48(ctx,base); return; }
  RunNativeAllocatorWait(ctx,base,true);
}
REX_EXTERN(__imp__sub_8213BCE0);
REX_HOOK_RAW(sub_8213BCE0) {
  if(!REXCVAR_GET(edf_native_host)) { __imp__sub_8213BCE0(ctx,base); return; }
  RunNativeAllocatorWait(ctx,base,false);
}
REX_EXTERN(__imp__sub_8213C928);
REX_EXTERN(__imp__sub_8214E5B8);
REX_HOOK_RAW(sub_8214E5B8) {
  if(!REXCVAR_GET(edf_native_host)) { __imp__sub_8214E5B8(ctx,base); return; }
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
  if(REXCVAR_GET(edf_native_host)) {
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
  if(!REXCVAR_GET(edf_native_shader_bridge) || !REXCVAR_GET(edf_native_fence_probe)) return;
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

// sub_820AB2F8 destroys the 72-byte shader-record array and clears its owner
// pointer/count/capacity. Native resources must not outlive that ownership.
REX_EXTERN(__imp__sub_820AB2F8);
REX_HOOK_RAW(sub_820AB2F8) {
  if (REXCVAR_GET(edf_native_shader_bridge)) edf::native::ForgetOwner(ctx.r3.u32);
  __imp__sub_820AB2F8(ctx, base);
}

REX_EXTERN(__imp__sub_82201458);
REX_HOOK_RAW(sub_82201458) { edf::native::ImportTexture(ctx, base, __imp__sub_82201458); }
// Attribution only: retain every original call and limit nested measurements
// to this thread's active image-loader scope. These phases overlap its total.
REX_EXTERN(__imp__sub_821FFB38);
REX_HOOK_RAW(sub_821FFB38) {
  edf::native::HookTiming timing(edf::native::HookPhase::TextureUpload2D,edf::native::texture_loader_depth!=0);
  __imp__sub_821FFB38(ctx,base);
}
REX_EXTERN(__imp__sub_822001E0);
REX_HOOK_RAW(sub_822001E0) {
  edf::native::HookTiming timing(edf::native::HookPhase::TextureUploadVolume,edf::native::texture_loader_depth!=0);
  __imp__sub_822001E0(ctx,base);
}
REX_EXTERN(__imp__sub_822009B0);
REX_HOOK_RAW(sub_822009B0) {
  edf::native::HookTiming timing(edf::native::HookPhase::TexturePrepare,edf::native::texture_loader_depth!=0);
  __imp__sub_822009B0(ctx,base);
}

// Final resource destruction, reached after sub_821347C0 decrements the
// reference count to zero. Remove the mapping before the address can be reused.
REX_EXTERN(__imp__sub_82134220);
REX_HOOK_RAW(sub_82134220) {
  if (REXCVAR_GET(edf_native_shader_bridge)) {
    auto& state = edf::native::State();
    // Registry retirement issues no context commands. Submitted D3D work owns
    // its resource references; registry users are serialized by state.mutex.
    // Do not wait behind the swap's refresh-pacing submission barrier.
    std::lock_guard lock(state.mutex);
    state.texture_creations.erase(ctx.r3.u32);
    for(auto& [owner,scene]:state.scenes) scene.direct_outputs.erase(ctx.r3.u32);
    state.meshes.Invalidate(ctx.r3.u32);
    state.model_buffers.Retire(ctx.r3.u32);
    state.declarations.Retire(ctx.r3.u32);
    state.depth_targets.erase(ctx.r3.u32);
    state.surface_creations.erase(ctx.r3.u32);
    state.embedded_shaders.erase(ctx.r3.u32);
    for (auto& [owner,scene]:state.scenes) if (scene.output_surface==ctx.r3.u32) {
      scene.output={}; scene.output_surface=0;
      if (state.active_output==owner) state.active_output=0;
    }
    std::erase_if(state.render_targets,[handle=ctx.r3.u32](const auto& item) {
      return item.second.texture_handle == handle || item.second.surface_handle == handle;
    });
    if (state.textures.erase(ctx.r3.u32))
      REXLOG_INFO("Native texture bridge: released handle={:#x}, resident={}", ctx.r3.u32, state.textures.size());
  }
  __imp__sub_82134220(ctx, base);
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
  if (!REXCVAR_GET(edf_native_shader_bridge)) return;
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
  RetireNativeModelBuffer(ctx.r3.u32);
  if(REXCVAR_GET(edf_native_shader_bridge)) { CleanupNativeModelBuffer(ctx,base,false); return; }
  __imp__sub_821D7468(ctx,base);
}
REX_HOOK_RAW(sub_821D75F8) {
  RetireNativeModelBuffer(ctx.r3.u32);
  if(REXCVAR_GET(edf_native_shader_bridge)) { CleanupNativeModelBuffer(ctx,base,true); return; }
  __imp__sub_821D75F8(ctx,base);
}

// Generic pool release receives the allocation record in r4. Observe before
// its own guest critical section and before the pool makes these bytes reusable.
REX_EXTERN(__imp__sub_821D3DC8);
REX_HOOK_RAW(sub_821D3DC8) {
  if(REXCVAR_GET(edf_native_shader_bridge)) {
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
  if(REXCVAR_GET(edf_native_shader_bridge) && uint32_t(ctx.lr)==0x821D3E30) {
    const auto block=edf::native::ReadNativePoolBlock(edf::native::GuestReader(base),ctx.r3.u32,ctx.r4.u64);
    auto* memory=REX_KERNEL_MEMORY();
    if(!memory || memory->virtual_membase()!=base)
      throw std::runtime_error("native pool block release memory mismatch");
    const auto extent=edf::native::MapCompletedNativePhysicalWrite(*memory,block.address,block.bytes);
    if(block.bytes && (!extent || extent->all))
      throw std::runtime_error("invalid native pool block physical extent");
    if(extent) {
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
  if(REXCVAR_GET(edf_native_shader_bridge)) {
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
  if(REXCVAR_GET(edf_native_shader_bridge) &&
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
  if(REXCVAR_GET(edf_native_shader_bridge) && (ctx.r4.u32&0x80000000u) && ctx.r3.u32) {
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
  if(!REXCVAR_GET(edf_native_shader_bridge)) return;
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
      auto index_storage=std::make_shared<const edf::native::NativeIndexBuffer>(EnsureSceneBackendLocked(state),
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
  if(REXCVAR_GET(edf_native_shader_bridge)) { ConstructNativeModelBuffer(ctx,base,false); return; }
  const auto owner=ctx.r3.u32,stride=ctx.r5.u32,count=ctx.r6.u32;
  __imp__sub_821D7530(ctx,base);
  if(ctx.r3.u32) PublishNativeModelBuffer(base,owner,edf::native::NativeModelBuffers::Kind::Vertex,stride,count);
}
REX_EXTERN(__imp__sub_821D76A8);
REX_HOOK_RAW(sub_821D76A8) {
  if(REXCVAR_GET(edf_native_shader_bridge)) { ConstructNativeModelBuffer(ctx,base,true); return; }
  const auto owner=ctx.r3.u32,count=ctx.r5.u32;
  __imp__sub_821D76A8(ctx,base);
  if(ctx.r3.u32) PublishNativeModelBuffer(base,owner,edf::native::NativeModelBuffers::Kind::Index,2,count);
}

// Bulk writes can run under a native submission lock. Queue completed physical
// writes without entering renderer state; the next indexed draw retires affected
// storage. Scalar/inline stores and other native providers are not covered here.
namespace {
edf::native::NativeBufferWrites* NativeBufferWriteQueue(uint8_t* base,uint32_t destination,uint32_t bytes,
    std::optional<edf::native::NativeBufferWrites::Range>* range=nullptr) {
  if(range) range->reset();
  auto* memory=REX_KERNEL_MEMORY();
  if(!REXCVAR_GET(edf_native_shader_bridge) || !memory || memory->virtual_membase()!=base) return nullptr;
  const auto extent=edf::native::MapCompletedNativePhysicalWrite(*memory,destination,bytes);
  if(!extent) return nullptr;
  if(range && !extent->all) *range=edf::native::NativeBufferWrites::Range{extent->address,extent->bytes};
  return &edf::native::BufferWrites();
}
edf::native::NativeBufferWrites::WriterScope BeginNativeBufferWrite(uint8_t* base,uint32_t destination,uint32_t bytes,bool exact=false,
    edf::native::NativeBufferWrites::WriterKind kind=edf::native::NativeBufferWrites::WriterKind::Bulk) {
  std::optional<edf::native::NativeBufferWrites::Range> range;
  auto* queue=NativeBufferWriteQueue(base,destination,bytes,exact?&range:nullptr);
  return edf::native::NativeBufferWrites::WriterScope(queue,range,kind);
}
void NotifyCompletedNativeBufferWrite(uint8_t* base,uint32_t destination,uint32_t bytes,bool notify_versions=false,bool generated=false,
    edf::native::NativeBufferWrites::WriterSite site={nullptr,0}) {
  if(!REXCVAR_GET(edf_native_shader_bridge) || !bytes || destination<0xa0000000u) return;
  auto* memory=REX_KERNEL_MEMORY();
  if(!memory || memory->virtual_membase()!=base) return;
  const auto extent=edf::native::MapCompletedNativePhysicalWrite(*memory,destination,bytes);
  if(!extent) return;
  if(site.provider && !REXCVAR_GET(edf_native_retirement_audit)) site={};
  edf::native::BufferWrites().Record(extent->address,extent->bytes,generated,site);
  if(!extent->all && notify_versions && REXCVAR_GET(edf_native_mesh_watch_audit))
    edf::native::NotifyPhysicalProviderWrite(*memory,extent->address,extent->bytes);
}
}
REX_EXTERN(__imp__sub_821E8320);
// Called by the hash-gated native lock tail only after retained fence/range
// services return. The following suffix writes header +0, +0x14 or +0x18.
edf::native::NativeBufferWrites::WriterScope edf_native_begin_lock_header_write(uint8_t* base,uint32_t owner) {
  return BeginNativeBufferWrite(base,owner,28,true);
}
void edf_native_complete_lock_header_write(uint8_t* base,uint32_t owner) {
  NotifyCompletedNativeBufferWrite(base,owner,28);
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
  if(REXCVAR_GET(edf_native_host)) {
    const edf::native::GuestReader reader(base);
    if(edf::native::RunNativePixIdle(ctx,reader,[&](uint32_t callback,auto& work) {
      rex::runtime::ResolveIndirectFunction(callback)(work,base);
    })) return;
  }
  __imp__sub_82138858(ctx,base);
}
REX_EXTERN(__imp__edf_native_counter_reset_cpu_tail);
REX_HOOK_RAW(sub_82139228) {
  if(REXCVAR_GET(edf_native_host)) __imp__edf_native_counter_reset_cpu_tail(ctx,base);
  else __imp__sub_82139228(ctx,base);
}
// Optional generated-store endpoint (scalar, SIMD, atomic and inline zero).
// Uncovered SDK providers remain outside it; source comparisons stay enabled.
extern "C" void edf_native_observe_guest_scalar_store(uint8_t* base,uint32_t address,uint32_t bytes,const char* file,uint32_t line) {
  NotifyCompletedNativeBufferWrite(base,address,bytes,false,true,{file,line});
}
REX_HOOK_RAW(sub_821E8320) {
  const auto destination=ctx.r3.u32,bytes=ctx.r5.u32;
  const auto caller=uint32_t(ctx.lr);
  // Mark before entering the provider, including writes to not-yet-published
  // owners. Nesting and exceptional returns release the active-write count.
  const auto writer=BeginNativeBufferWrite(base,destination,bytes,true);
  __imp__sub_821E8320(ctx,base);
  NotifyCompletedNativeBufferWrite(base,destination,bytes,false,false,{nullptr,0,0x821e8320,caller});
}
// Off-thread dirty-range accumulation atomically updates one packed min/max
// pair, including the retry-path conditional store. No nested guest services.
REX_EXTERN(__imp__sub_8213BDF8);
REX_HOOK_RAW(sub_8213BDF8) {
  const auto destination=ctx.r3.u32;
  const auto writer=BeginNativeBufferWrite(base,destination,8,true);
  __imp__sub_8213BDF8(ctx,base);
  NotifyCompletedNativeBufferWrite(base,destination,8);
}

// Independent forward-copy implementation (aligned word loop plus byte tails).
// It does not call 821E8320, and mutates r5 while copying the leading bytes.
// Capture the original extent and notify only after the guest copy completes.
REX_EXTERN(__imp__sub_821E8740);
REX_HOOK_RAW(sub_821E8740) {
  const auto destination=ctx.r3.u32,bytes=ctx.r5.u32;
  const auto caller=uint32_t(ctx.lr);
  const auto writer=BeginNativeBufferWrite(base,destination,bytes,true);
  __imp__sub_821E8740(ctx,base);
  NotifyCompletedNativeBufferWrite(base,destination,bytes,false,false,{nullptr,0,0x821e8740,caller});
}

// A320's backward memmove path writes inline and consumes r5. The forward
// branch tail-calls the already tracked 8320; equal addresses perform no write.
// Match the original signed address comparison when selecting the notification.
REX_EXTERN(__imp__sub_821EA320);
REX_HOOK_RAW(sub_821EA320) {
  const auto destination=ctx.r3.u32,bytes=ctx.r5.u32;
  const auto caller=uint32_t(ctx.lr);
  const bool backward=ctx.r3.s32>ctx.r4.s32;
  const auto writer=BeginNativeBufferWrite(base,destination,backward?bytes:0,true);
  __imp__sub_821EA320(ctx,base);
  if(backward) NotifyCompletedNativeBufferWrite(base,destination,bytes,false,false,{nullptr,0,0x821ea320,caller});
}

// Bulk fill writes its byte prefix, 16-byte blocks and tails inline. Save the
// original extent because alignment consumes r5 before the routine returns.
REX_EXTERN(__imp__sub_821E9BA0);
REX_HOOK_RAW(sub_821E9BA0) {
  const auto destination=ctx.r3.u32,bytes=ctx.r5.u32;
  const auto caller=uint32_t(ctx.lr);
  const auto writer=BeginNativeBufferWrite(base,destination,bytes,true);
  __imp__sub_821E9BA0(ctx,base);
  NotifyCompletedNativeBufferWrite(base,destination,bytes,false,false,{nullptr,0,0x821e9ba0,caller});
}

// The current SDK NtReadFile implementation completes XFile::Read synchronously
// before returning (even when reporting STATUS_PENDING for asynchronous handles).
// XFile bypasses virtual write protection for physical destinations and only
// emits its own callbacks on success. Notify the requested extent on all return
// statuses: failed/short reads must not leave a potentially changed native owner
// reusable. This is conservative and is not synchronization with a concurrent draw.
REX_EXTERN(__imp__NtReadFile);
REX_HOOK_RAW(edf_native_NtReadFile) {
  const auto destination=ctx.r8.u32,bytes=ctx.r9.u32,status=ctx.r7.u32;
  // SDK queues an APC only with a non-low-bit routine and nonnull context.
  // Without that path, audited guest outputs are data and the status block;
  // event/completion-port and memory-invalidation work changes host metadata.
  // APC-capable calls retain global exclusion for their guest queue writes.
  const bool exact=!((ctx.r5.u32&~1u) && ctx.r6.u32);
  const auto writer=BeginNativeBufferWrite(base,destination,bytes,exact,edf::native::NativeBufferWrites::WriterKind::FileRead);
  const auto status_writer=BeginNativeBufferWrite(base,status,status?8u:0u,exact,edf::native::NativeBufferWrites::WriterKind::FileRead);
  __imp__NtReadFile(ctx,base);
  NotifyCompletedNativeBufferWrite(base,destination,bytes,true);
  if(status) NotifyCompletedNativeBufferWrite(base,status,8,true);
}

// SDK bulk fill bypasses generated store instrumentation. Preserve the original
// provider, including floor(length/4) words and its register/return behavior.
REX_EXTERN(__imp__RtlFillMemoryUlong);
REX_HOOK_RAW(edf_native_RtlFillMemoryUlong) {
  const auto destination=ctx.r3.u32,bytes=ctx.r4.u32&~3u;
  // Audited SDK body only stores floor(length/4) words to this destination;
  // no secondary guest outputs or callbacks require global exclusion.
  const auto writer=BeginNativeBufferWrite(base,destination,bytes,true,edf::native::NativeBufferWrites::WriterKind::WordFill);
  __imp__RtlFillMemoryUlong(ctx,base);
  NotifyCompletedNativeBufferWrite(base,destination,bytes,true);
}

// Explicit VB/IB Unlock boundaries. Their callers finish CPU writes before
// entering these wrappers; preserve original coherency/wait behavior first.
// Other writers may bypass Lock/Unlock, so source comparisons remain required.
REX_EXTERN(__imp__sub_82134408);
REX_EXTERN(__imp__edf_native_buffer_lock_cpu_tail);
REX_HOOK_RAW(sub_82134408) {
  // AD70 is the audited texture subresource lock wrapper. Its access code is
  // 14; the extracted helper preserves parent fences, both dirty ranges and
  // the returned CPU alias, omitting only the Xbox cache packet block.
  const bool texture_lock=ctx.r4.u32==14 && uint32_t(ctx.lr)==0x8213ae24;
  if(REXCVAR_GET(edf_native_shader_bridge) && (ctx.r4.u32==10 || ctx.r4.u32==12 || texture_lock)) {
    __imp__edf_native_buffer_lock_cpu_tail(ctx,base);
    if(texture_lock) {
      static thread_local uint64_t completed=0;
      if(++completed<=3) REXLOG_INFO("Native texture lock: preserved CPU lock/fence state; omitted Xbox cache packets");
    }
  } else __imp__sub_82134408(ctx,base);
}
namespace {
void NotifyNativeBufferUpdate(uint8_t* base,uint32_t owner,bool index_buffer) {
  if(!REXCVAR_GET(edf_native_shader_bridge)) return;
  auto& state=edf::native::State();
  // Invalidate CPU ownership/cache snapshots under the registry lock. No GPU
  // commands are issued, so completed CPU writes need not wait for refresh.
  std::lock_guard lock(state.mutex);
  const bool tracked=state.model_buffers.NotifyUpdateAliases(owner,
    [&](uint32_t affected) { state.meshes.Invalidate(affected); });
  if(!tracked) {
    state.meshes.Invalidate(owner);
    // A generic (non-model) resource header can alias a published model's
    // payload. Owner-only invalidation misses that relationship. Queue its
    // physical extent through the same checked mapper as other CPU writers;
    // the next indexed consumer drains it before consulting retained storage.
    const edf::native::GuestReader reader(base);
    const auto extent=edf::native::DecodeNativeBufferUpdateExtent(
      reader.Word(reader.Add(owner,24)),reader.Word(reader.Add(owner,28)),index_buffer);
    NotifyCompletedNativeBufferWrite(base,extent.address,extent.bytes);
  }
  if(++state.buffer_update_notifications<=8)
    REXLOG_INFO("Native buffer update: owner={:#x}, model_tracked={}, notification={}",
      owner,tracked,state.buffer_update_notifications);
}
}
REX_EXTERN(__imp__sub_82141AB8);
REX_HOOK_RAW(sub_82141AB8) {
  if(REXCVAR_GET(edf_native_shader_bridge)) {
    edf::native::NativeCacheFlushCpu(ctx,edf::native::GuestReader(base));
    return;
  }
  __imp__sub_82141AB8(ctx,base);
}
REX_EXTERN(__imp__sub_821349B8);
REX_EXTERN(__imp__edf_native_unlock_821349B8);
REX_HOOK_RAW(sub_821349B8) {
  const auto owner=ctx.r3.u32;
  {
    // VB unlock passes r5=0 to 82134640, so its non-stack outputs are
    // header word 0 and conditional word +20, never word +24. Payload
    // copies have their own producer scopes; release this before state.mutex.
    const auto bytes=REXCVAR_GET(edf_native_shader_bridge)?24u:0u;
    const auto header_writer=BeginNativeBufferWrite(base,owner,bytes,true);
    if(bytes) __imp__edf_native_unlock_821349B8(ctx,base);
    else __imp__sub_821349B8(ctx,base);
    if(bytes) NotifyCompletedNativeBufferWrite(base,owner,bytes);
  }
  NotifyNativeBufferUpdate(base,owner,false);
}
REX_EXTERN(__imp__sub_82134AD8);
REX_EXTERN(__imp__edf_native_unlock_82134AD8);
REX_HOOK_RAW(sub_82134AD8) {
  const auto owner=ctx.r3.u32;
  const bool inline_indices=REXCVAR_GET(edf_native_shader_bridge) && uint32_t(ctx.lr)==0x8242D3DC;
  if(inline_indices) edf::native::NativeBufferWriteFrame::Current().RequireOwner(owner);
  {
    // Index unlock tail-calls 82134640 with r5=0: header word 0 and
    // conditional word +0x14 are its guest header outputs, not +0x18.
    // This header contract holds for every caller, independently of the
    // special twelve-byte inline payload and its physical mapping.
    const auto bytes=REXCVAR_GET(edf_native_shader_bridge)?24u:0u;
    const auto header_writer=BeginNativeBufferWrite(base,owner,bytes,true,
      edf::native::NativeBufferWrites::WriterKind::InlineIndices);
    if(bytes) __imp__edf_native_unlock_82134AD8(ctx,base);
    else __imp__sub_82134AD8(ctx,base);
    if(bytes) NotifyCompletedNativeBufferWrite(base,owner,bytes);
  }
  if(inline_indices) edf::native::NativeBufferWriteFrame::Current().Finish(owner,[&](uint32_t destination) {
    NotifyCompletedNativeBufferWrite(base,destination,12);
  });
  // No inline payload scope may remain active while this takes state.mutex.
  // The queued exact write already protects a draw that gets that lock first.
  NotifyNativeBufferUpdate(base,owner,true);
}
// Ghidra/assembly: this producer writes six uint16 indices at 8242D3B0..D3D0
// between the lock return D3A8 and unlock return D3DC. Do not generalize this
// extent to arbitrary index locks, saved aliases, or other producer callbacks.
REX_EXTERN(__imp__sub_8242D2B0);
REX_HOOK_RAW(sub_8242D2B0) {
  if(!REXCVAR_GET(edf_native_shader_bridge)) { __imp__sub_8242D2B0(ctx,base); return; }
  edf::native::NativeBufferWriteFrame frame;
  __imp__sub_8242D2B0(ctx,base);
  frame.RequireFinished();
}
REX_EXTERN(__imp__sub_82134A78);
REX_EXTERN(__imp__edf_native_lock_82134A78);
REX_EXTERN(__imp__sub_82134958);
REX_EXTERN(__imp__edf_native_lock_82134958);
REX_HOOK_RAW(sub_82134958) {
  if(REXCVAR_GET(edf_native_shader_bridge)) __imp__edf_native_lock_82134958(ctx,base);
  else __imp__sub_82134958(ctx,base);
}
REX_HOOK_RAW(sub_82134A78) {
  const auto owner=ctx.r3.u32;
  const bool inline_indices=REXCVAR_GET(edf_native_shader_bridge) && uint32_t(ctx.lr)==0x8242D3A8;
  if(REXCVAR_GET(edf_native_shader_bridge)) __imp__edf_native_lock_82134A78(ctx,base);
  else __imp__sub_82134A78(ctx,base);
  if(inline_indices) {
    // Fence callbacks and lock-entry writes have already returned. The six
    // payload stores follow; unlock header writes have an independent guard.
    std::optional<edf::native::NativeBufferWrites::Range> range;
    auto* queue=NativeBufferWriteQueue(base,ctx.r3.u32,12,&range);
    edf::native::NativeBufferWriteFrame::Current().Begin(owner,queue,ctx.r3.u32,range);
  }
}

// Surface creation uses width/height/format/MSAA in r3-r6. Floating depth
// uses host D32S8 in this development bridge; this does not reproduce the
// guest's 24-bit floating quantization or establish frame equivalence.
REX_EXTERN(__imp__sub_8213B850);
REX_HOOK_RAW(sub_8213B850) {
  const auto width=ctx.r3.u32,height=ctx.r4.u32,format=ctx.r5.u32,msaa=ctx.r6.u32;
  __imp__sub_8213B850(ctx,base);
  if (REXCVAR_GET(edf_native_shader_bridge) && ctx.r3.u32) {
    auto& state=edf::native::State();
    // Device-only creation/registry replacement; no context submission here.
    std::lock_guard lock(state.mutex);
    state.surface_creations.insert_or_assign(ctx.r3.u32,edf::native::SurfaceCreation{width,height,format,msaa});
    state.depth_targets.erase(ctx.r3.u32);
    if (format==0x1a220197) {
      try {
        if (msaa) {
          REXLOG_INFO("Native depth allocation: {}x{}, MSAA={} not supported",width,height,msaa);
        } else {
          auto target=edf::native::CreateNativeDepthTarget(EnsureSceneBackendLocked(state),width,height,DXGI_FORMAT_D32_FLOAT_S8X24_UINT);
          state.depth_targets.insert_or_assign(ctx.r3.u32,std::move(target));
          REXLOG_INFO("Native depth allocation: handle={:#x}, {}x{}, host=D32S8 (development)",ctx.r3.u32,width,height);
        }
      } catch (const std::exception& error) { REXLOG_ERROR("Native depth allocation: {}",error.what()); }
    }
  }
}

// Clear's native flags are low four color-target bits, bit 4 depth, bit 5
// stencil (verified in 821334E8), not desktop D3D9's 1/2/4 flag values.
REX_EXTERN(__imp__sub_821340D0);
REX_HOOK_RAW(sub_821340D0) {
  if (REXCVAR_GET(edf_native_shader_bridge) && (ctx.r6.u32&15)) {
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
  if (REXCVAR_GET(edf_native_shader_bridge) && (ctx.r6.u32&0x30)) {
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
  const bool audit=REXCVAR_GET(edf_native_host);
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
// The engine ignores video-mode dimensions and initializes its renderer to
// 1280x720/960. Replace that choice before device/resource initialization, so
// all consumers (scene, depth, post pyramid and UI) see one consistent extent.
// Audited call: 8219E3B8 -> 82139A40 at LR 8219E4D4; r7 is presentation
// parameters, r8 is &renderer.device at renderer+8. Later consumers reload
// renderer+84/+88 rather than retaining the fixed dimensions in registers.
REX_EXTERN(__imp__sub_82139A40);
REX_HOOK_RAW(sub_82139A40) {
  if (uint32_t(ctx.lr)==0x8219E4D4u) {
    const auto width=edf::native::NativeRenderDimensions()[0];
    const auto height=edf::native::NativeRenderDimensions()[1];
    if(width || height) {
      if(width<640 || width>4095 || height<480 || height>4095)
        throw std::runtime_error("native render dimensions require width 640..4095 and height 480..4095");
      const edf::native::GuestReader reader(base);
      if(ctx.r8.u32<8 || reader.Word(ctx.r7.u32)!=1280 || reader.Word(reader.Add(ctx.r8.u32,76))!=1280)
        throw std::runtime_error("native resolution initialization contract changed");
      const std::array<uint32_t,2> dimensions{uint32_t(width),uint32_t(height)};
      reader.StoreCpuWords(ctx.r7.u32,dimensions);
      reader.StoreCpuWords(reader.Add(ctx.r8.u32,76),dimensions);
      REXLOG_INFO("Native render resolution selected: {}x{} (engine resource initialization)",width,height);
    }
  }
  __imp__sub_82139A40(ctx,base);
}
REX_EXTERN(__imp__sub_8219C7A8);
REX_HOOK_RAW(sub_8219C7A8) {
  edf::native::HookTiming setup_timing(edf::native::HookPhase::SceneSetup);
  const auto owner=ctx.r3.u32;
  {
    edf::native::HookTiming original_timing(edf::native::HookPhase::SceneSetupGuest);
    __imp__sub_8219C7A8(ctx,base);
  }
  edf::native::HookTiming native_timing(edf::native::HookPhase::SceneSetupNative);
  if (REXCVAR_GET(edf_native_shader_bridge) && ctx.r3.u32) {
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
      static const int32_t sample_override=REXCVAR_GET(edf_native_msaa);
      const uint32_t samples=edf::native::NativeSceneSamples(creation.msaa,sample_override);
      auto found=state.scenes.find(owner);
      if (found==state.scenes.end() || found->second.color.sampled.width!=width || found->second.color.sampled.height!=height || found->second.samples!=samples) {
        edf::native::NativeScene scene{
          edf::native::CreateNativeRenderTarget(EnsureSceneBackendLocked(state),width,height,DXGI_FORMAT_R16G16B16A16_FLOAT,samples),
          edf::native::CreateNativeDepthTarget(EnsureSceneBackendLocked(state),width,height,DXGI_FORMAT_D32_FLOAT_S8X24_UINT,samples)};
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
  if (REXCVAR_GET(edf_native_shader_bridge)) {
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
  if (REXCVAR_GET(edf_native_shader_bridge)) {
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
        scene.output=edf::native::CreateNativeRenderTarget(EnsureSceneBackendLocked(state),creation.width,creation.height,DXGI_FORMAT_R8G8B8A8_UNORM);
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
  if (REXCVAR_GET(edf_native_shader_bridge)) {
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
      const bool indexed_output=scene.output.content_valid && state.indexed_submitted>state.scene_indexed_start;
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
          const auto bmp=CaptureOutputBmp(state,scene);
          std::ofstream output(path,std::ios::binary);
          output.write(reinterpret_cast<const char*>(bmp.data()),bmp.size()); output.close();
          if (!output) throw std::runtime_error("native output capture write failed");
          REXLOG_INFO("Native bloom output capture: {}, draws={}, frame_complete=false (UI and scene coverage incomplete)",path.string(),state.output_draws);
          if(REXCVAR_GET(edf_native_output_capture_scene_color)) {
            if(!scene.color.content_valid || !scene.color.backend_surface)
              throw std::runtime_error("paired scene-color capture has no valid scene surface");
            const auto scene_path=std::filesystem::path(prefix+".scene-color."+std::to_string(state.indexed_output_frames)+".bmp");
            if(std::filesystem::exists(scene_path)) throw std::runtime_error("paired scene-color capture already exists");
            const auto scene_bmp=edf::native::CaptureNativeBmp(EnsureSceneBackendLocked(state),*scene.color.backend_surface,scene.color.format);
            std::ofstream scene_file(scene_path,std::ios::binary);
            scene_file.write(reinterpret_cast<const char*>(scene_bmp.data()),scene_bmp.size()); scene_file.close();
            if(!scene_file) throw std::runtime_error("paired scene-color capture write failed");
            REXLOG_INFO("Native paired scene color capture: {}, output={}, same indexed frame; HDR BMP is diagnostic, not display reference",
              scene_path.string(),path.string());
          }
          for (const auto& [owner,target]:state.render_targets) {
            const auto& sample=target.native.sampled;
            if (!sample.content_valid || sample.width>40 || sample.height>22) continue;
            const auto value=edf::native::ReadNativeColorPixel(EnsureSceneBackendLocked(state),*sample.backend,sample.format,sample.width/2,sample.height/2);
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
          direct=edf::native::CreateNativeOpaqueFrameTarget(EnsureSceneBackendLocked(state),creation.width,creation.height);
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
  if (REXCVAR_GET(edf_native_shader_bridge)) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    if (state.active_scene==ctx.r3.u32) state.active_scene=0;
    if (state.active_output==ctx.r3.u32) state.active_output=0;
    if (state.scenes.erase(ctx.r3.u32)) edf::native::BindActiveTarget(state);
  }
  __imp__sub_8219E140(ctx,base);
}

REX_EXTERN(__imp__sub_821B8C30);
REX_HOOK_RAW(sub_821B8C30) {
  const auto owner = ctx.r3.u32;
  __imp__sub_821B8C30(ctx,base);
  if (REXCVAR_GET(edf_native_shader_bridge) && ctx.r3.u8) {
    try { edf::native::RegisterRenderTarget(edf::native::GuestReader(base),owner); }
    catch (const std::exception& error) { REXLOG_ERROR("Native render target: {}",error.what()); }
  }
}
REX_EXTERN(__imp__sub_821B8828);
REX_HOOK_RAW(sub_821B8828) {
  const auto owner = ctx.r3.u32;
  if(auto* recorder=edf::native::post_finish_recorder; recorder && recorder->targets.contains(owner))
    recorder->seen.BeginTarget(owner);
  __imp__sub_821B8828(ctx,base);
  if (REXCVAR_GET(edf_native_shader_bridge)) {
    try { edf::native::BeginRenderTarget(owner); }
    catch (const std::exception& error) { REXLOG_ERROR("Native render target begin: {}",error.what()); }
  }
}
REX_EXTERN(__imp__sub_821B88B0);
REX_HOOK_RAW(sub_821B88B0) {
  if (REXCVAR_GET(edf_native_shader_bridge)) {
    try {
      const edf::native::GuestReader reader(base);
      if (*reader.Bytes(reader.Add(ctx.r3.u32,40),1)) edf::native::EndRenderTarget(ctx.r3.u32);
    } catch (const std::exception& error) { REXLOG_ERROR("Native render target end: {}",error.what()); }
  }
  __imp__sub_821B88B0(ctx,base);
}

// D3D texture allocation: dimensions in r3-r5, mip count r6, usage r7,
// format r8, pool r9, resource type r10; returns the new resource pointer.
REX_EXTERN(__imp__sub_8213B730);
REX_HOOK_RAW(sub_8213B730) {
  if (!REXCVAR_GET(edf_native_shader_bridge)) { __imp__sub_8213B730(ctx, base); return; }
  const edf::native::TextureCreation creation{ctx.r3.u32, ctx.r4.u32, ctx.r5.u32,
    ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32, uint32_t(ctx.lr)};
  {
    edf::native::HookTiming timing(edf::native::HookPhase::TextureAllocate,edf::native::texture_loader_depth!=0);
    __imp__sub_8213B730(ctx, base);
  }
  if (ctx.r3.u32) {
    auto& state = edf::native::State();
    // Metadata publication only: no native upload/draw or context operation.
    // Keep address-reuse invalidation atomic without joining frame pacing.
    std::lock_guard lock(state.mutex);
    state.textures.erase(ctx.r3.u32);
    state.texture_creations.insert_or_assign(ctx.r3.u32, creation);
  }
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
  if(!REXCVAR_GET(edf_native_shader_bridge)) { __imp__sub_82137410(ctx,base); return; }
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
  if (REXCVAR_GET(edf_native_shader_bridge)) {
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
  if(!REXCVAR_GET(edf_native_shader_bridge)) { __imp__sub_821375C0(ctx,base); return; }
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
  if(REXCVAR_GET(edf_native_shader_bridge)) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    state.index_bindings.insert_or_assign(device,resource);
  }
}

namespace {
void BindNativeShaderResource(PPCContext& ctx,uint8_t* base,bool pixel) {
  const auto device=ctx.r3.u32,shader=ctx.r4.u32;
  const edf::native::GuestReader reader(base);
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
void PublishNativeShaderBinding(uint32_t device,uint32_t shader,bool pixel) {
  if(!REXCVAR_GET(edf_native_shader_bridge)) return;
  auto& state=edf::native::State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  state.shader_bindings.Set(device,shader,pixel);
}
void PublishNativeMaterialParameters(uint8_t* base,uint32_t instance) {
  if(!REXCVAR_GET(edf_native_shader_bridge)) return;
  auto& state=edf::native::State();
  // CPU-owned metadata publication is serialized by the registry mutex.
  std::lock_guard lock(state.mutex);
  state.material_parameters.Publish(edf::native::GuestReader(base),instance);
}
void RetireNativeMaterialParameters(uint8_t* base,uint32_t instance,bool array) {
  if(!REXCVAR_GET(edf_native_shader_bridge)) return;
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
  if(!REXCVAR_GET(edf_native_shader_bridge) || !handle) return;
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
  if(!REXCVAR_GET(edf_native_shader_bridge)) return;
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
  if(!REXCVAR_GET(edf_native_shader_bridge) || !REXCVAR_GET(edf_native_material_activation) ||
     (edf::native::native_queued_scene_group && edf::native::native_queued_scene_group->material_compatibility_only)) {
    edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::TextureBinding);
    __imp__sub_8213BA98(ctx,base); return;
  }
  const auto device=ctx.r3.u32;
  const edf::native::GuestReader reader(base);
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
REX_HOOK_RAW(sub_821498C8) {
  const auto device=ctx.r3.u32,shader=ctx.r4.u32;
  if(REXCVAR_GET(edf_native_shader_bridge) && REXCVAR_GET(edf_native_material_activation) &&
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
  if(REXCVAR_GET(edf_native_shader_bridge) && REXCVAR_GET(edf_native_material_activation) &&
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
  if(REXCVAR_GET(edf_native_shader_bridge)) {
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
  if (REXCVAR_GET(edf_native_shader_bridge)) {
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
          REXCVAR_GET(edf_native_seam_draws) && uint32_t(ctx.lr)==0x821D97E8;
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
              preacquired_mesh=state.meshes.TryAcquireOwned(EnsureSceneBackendLocked(state),bindings.shader(),
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
        auto& mesh_backend=EnsureSceneBackendLocked(state);
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
          const bool seam_draws=REXCVAR_GET(edf_native_seam_draws);
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
                REXCVAR_GET(edf_native_scene_material_owned) && !REXCVAR_GET(edf_native_scene_material_audit);
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
                  const bool owned=REXCVAR_GET(edf_native_scene_material_owned);
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
                  if(REXCVAR_GET(edf_native_scene_material_owned) &&
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
                  group->view=view; group->targets=targets; group->objects.push_back(SelectNativeSceneInstanceLocked(state,object));
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
              if(REXCVAR_GET(edf_native_scene_queued) && state.indexed_submitted%100000==0)
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
  const bool native_host=REXCVAR_GET(edf_native_host);
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

// XUI's device wrapper stores raw created shader handles directly through r5.
// These identities are diagnostic until the embedded programs are recovered;
// never register an arbitrary game effect as a substitute for an XUI shader.
REX_EXTERN(__imp__sub_8241E180);
REX_HOOK_RAW(sub_8241E180) {
  const auto source=ctx.r4.u32,output=ctx.r5.u32,caller=uint32_t(ctx.lr);
  __imp__sub_8241E180(ctx,base);
  if (REXCVAR_GET(edf_native_shader_bridge) && ctx.r3.s32>=0) {
    try { edf::native::RecordEmbeddedShader(edf::native::GuestReader(base),output,source,false,caller); }
    catch (const std::exception& error) { REXLOG_ERROR("Native middleware VS identity: {}",error.what()); }
  }
}
REX_EXTERN(__imp__sub_8241E1D0);
REX_HOOK_RAW(sub_8241E1D0) {
  const auto source=ctx.r4.u32,output=ctx.r5.u32,caller=uint32_t(ctx.lr);
  __imp__sub_8241E1D0(ctx,base);
  if (REXCVAR_GET(edf_native_shader_bridge) && ctx.r3.s32>=0) {
    try { edf::native::RecordEmbeddedShader(edf::native::GuestReader(base),output,source,true,caller); }
    catch (const std::exception& error) { REXLOG_ERROR("Native middleware PS identity: {}",error.what()); }
  }
}
// The movie decoder calls LockRect directly, not through the XUI wrapper.
// Capture only locks nested in its decode call; ordinary resource locks do not
// establish video identity. 8213AE38 writes {row_pitch,pixel_address} to r5.
REX_EXTERN(__imp__sub_8213B5A0);
REX_HOOK_RAW(sub_8213B5A0) {
  const auto texture=ctx.r3.u32,level=ctx.r4.u32,output=ctx.r5.u32,rect=ctx.r6.u32;
  __imp__sub_8213B5A0(ctx,base);
  if (auto* capture=edf::native::active_movie_decode) {
    try {
      if (level || rect || capture->planes.size()>=3) throw std::runtime_error("unexpected movie plane lock");
      const edf::native::GuestReader reader(base);
      capture->planes.push_back({texture,reader.Word(output),reader.Word(reader.Add(output,4))});
    } catch (...) { capture->failed=true; }
  }
}
REX_EXTERN(__imp__sub_8242AF30);
REX_HOOK_RAW(sub_8242AF30) {
  if (!REXCVAR_GET(edf_native_shader_bridge)) { __imp__sub_8242AF30(ctx,base); return; }
  const auto owner=ctx.r3.u32;
  edf::native::MovieDecodeLocks capture;
  struct CaptureScope {
    edf::native::MovieDecodeLocks* previous;
    explicit CaptureScope(edf::native::MovieDecodeLocks& current)
      : previous(edf::native::active_movie_decode) { edf::native::active_movie_decode=&current; }
    ~CaptureScope() { edf::native::active_movie_decode=previous; }
  };
  { CaptureScope scope(capture); __imp__sub_8242AF30(ctx,base); }
  // Only S_OK publishes the newly decoded buffer at owner+52. Other positive
  // statuses include end-of-stream, and must not upload unwritten pixels.
  if (ctx.r3.s32!=0) return;
  auto& state=edf::native::State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  try {
    if (capture.failed || capture.planes.size()!=3) throw std::runtime_error("incomplete movie decode locks");
    const edf::native::GuestReader reader(base);
    const auto buffer=reader.Word(reader.Add(owner,52));
    const auto width=reader.Word(reader.Add(owner,56)),height=reader.Word(reader.Add(owner,60));
    if (buffer>1 || !width || !height || width>4096 || height>4096 || (width&1) || (height&1))
      throw std::runtime_error("invalid movie buffer dimensions/index");
    for (size_t i=0;i<3;++i) {
      const auto& plane=capture.planes[i];
      const auto w=i ? width/2 : width,h=i ? height/2 : height;
      if (plane.texture!=reader.Word(reader.Add(owner,20+uint32_t(i)*8+buffer*4)) || plane.pitch<w || plane.pitch>16384)
        throw std::runtime_error("movie lock does not match published YUV buffer");
      const auto creation=state.texture_creations.find(plane.texture);
      if (creation==state.texture_creations.end() || creation->second.format!=0x28000002 ||
          creation->second.width!=w || creation->second.height!=h)
        throw std::runtime_error("movie plane is not the verified linear 8-bit allocation");
      const auto* pixels=reader.Bytes(plane.pixels,size_t(plane.pitch)*(h-1)+w);
      auto& native=state.textures[plane.texture];
      native.content_valid=false;
      if (!native.backend || native.width!=w || native.height!=h) {
        edf::native::NativeTexture replacement;
        replacement.content_valid=false;
        edf::native::NativeBackendTextureDesc desc{};
        desc.width=w; desc.height=h; desc.levels=1;
        desc.format=DXGI_FORMAT_R8_UNORM; desc.sampled=true;
        replacement.backend=edf::native::EnsureSceneBackendLocked(state).CreateTexture(desc,{});
        if(!replacement.backend)
          throw std::runtime_error("native movie plane allocation failed");
        replacement.resource=edf::native::NativeD3D11TextureResource(*replacement.backend);
        replacement.view=edf::native::NativeD3D11TextureView(*replacement.backend);
        replacement.width=w; replacement.height=h; replacement.mip_count=1;
        replacement.format=DXGI_FORMAT_R8_UNORM;
        native=std::move(replacement);
      }
      std::vector<uint8_t> packed(size_t(w)*h);
      for(uint32_t row=0;row<h;++row)
        std::memcpy(packed.data()+size_t(row)*w,pixels+size_t(row)*plane.pitch,w);
      edf::native::SceneRecorderLocked(state).UpdateTexture(*native.backend,packed);
      native.content_valid=true;
      if (state.movie_uploads<3) REXLOG_INFO("Native movie plane upload: owner={:#x}, buffer={}, plane={}, texture={:#x}, {}x{}, pitch={}, pixels={:#x}",
        owner,buffer,i,plane.texture,w,h,plane.pitch,plane.pixels);
    }
    ++state.movie_uploads;
  } catch (const std::exception& error) {
    for (const auto& plane:capture.planes) if (const auto found=state.textures.find(plane.texture);found!=state.textures.end())
      found->second.content_valid=false;
    if (++state.movie_upload_errors<=8) REXLOG_ERROR("Native movie upload: {}",error.what());
  }
}
REX_EXTERN(__imp__sub_821FD8F8);
REX_EXTERN(__imp__edf_native_immediate_cpu_tail);
REX_HOOK_RAW(sub_821FD8F8) {
  edf::native::HookTiming native_timing(edf::native::HookPhase::ImmediateNative);
  bool native_submitted=false;
  if (REXCVAR_GET(edf_native_shader_bridge)) {
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
    if (!state.active_target && state.scenes.contains(state.active_output)) try {
      const auto& pair=bound_shaders();
      const auto vs=state.embedded_shaders.find(pair.vertex),ps=state.embedded_shaders.find(pair.pixel);
      movie_draw=vs!=state.embedded_shaders.end() && ps!=state.embedded_shaders.end() &&
        !vs->second.pixel && vs->second.source==0x82060B70 && ps->second.pixel &&
        (ps->second.source==0x82064428 || ps->second.source==0x820641F0);
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
        auto& movie_pixel=ps->second.source==0x820641F0 ? *state.movie_pixel_sd : *state.movie_pixel;
        // CPU constant setters 82149248/82149358 copy float4 rows to
        // device+(112+register)*16 / device+(368+register)*16 respectively.
        auto registers=[&](uint32_t offset,size_t bytes) {return std::span<const uint8_t>{reader.Bytes(reader.Add(ctx.r3.u32,offset),bytes),bytes};};
        const auto& movie_plan=*state.movie_bindings[ps->second.source==0x820641F0?1:0];
        movie_plan.SetConstants(*state.movie_vertex,movie_pixel,registers(1792,160),registers(5888,16));
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
            cached=state.samplers.emplace(key,&EnsureSceneBackendLocked(state).CreateSampler(desc)).first;
          }
          movie_plan.SetSampler(movie_pixel,i,cached->second);
        }
        const auto key=edf::native::ReadAuditedRenderStateWords(reader,ctx.r3.u32);
        if ((key[1]&3)!=0) throw std::runtime_error("movie requires an unbound depth/stencil surface");
        auto render=state.render_states.find(key);
        if(render==state.render_states.end()) render=state.render_states.emplace(key,edf::native::CreateNativeRenderState(state.device.Get(),key)).first;
        if(REXCVAR_GET(edf_native_seam_draws)) {
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
            const auto bytes=CaptureOutputBmp(state,scene);
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
      const auto vs=state.embedded_shaders.find(pair.vertex),ps=state.embedded_shaders.find(pair.pixel);
      xui_draw=vs!=state.embedded_shaders.end() && ps!=state.embedded_shaders.end() &&
        !vs->second.pixel && vs->second.source==0x820608B0 && ps->second.pixel &&
        (ps->second.source==0x82060EC8 || ps->second.source==0x82060DB0 || ps->second.source==0x82061848);
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
        const bool solid=ps->second.source==0x82060DB0;
        const bool mask=ps->second.source==0x82061848;
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
        vertex_plan.SetConstants(vertex,vertex_registers,registers(5872,16),
          native_canvas?float(target.sampled.width)/1280.0f:1.0f,
          native_canvas?float(target.sampled.height)/720.0f:1.0f);
        const auto& pixel_plan=*state.xui_pixel_bindings[solid?1:mask?2:0];
        pixel_plan.SetConstants(pixel,registers(5904,16),solid?registers(5888,16):std::span<const uint8_t>{});
        if(!solid) {
        pixel_plan.SetTexture(pixel,texture->second.backend);
        const auto sampler_key=edf::native::SamplerStateKey(edf::native::ReadSamplerWords(reader,device,0));
        auto sampler=state.samplers.find(sampler_key);
        if(sampler==state.samplers.end()) {
          const auto desc=edf::native::DecodeNativeGuestSampler(sampler_key);
          sampler=state.samplers.emplace(sampler_key,&EnsureSceneBackendLocked(state).CreateSampler(desc)).first;
        }
        pixel_plan.SetSampler(pixel,sampler->second);
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
          if(state.xui_batch_draws%500000==0)
            REXLOG_INFO("Native XUI batch audit: draws={}, runs={}, longest_run={}, collapsible={} ({:.1f}% share the state of the draw before), of those {} also change constants ({:.1f}%)",
              state.xui_batch_draws,state.xui_batch_runs,state.xui_batch_longest,
              state.xui_batch_collapsible,
              100.0*double(state.xui_batch_collapsible)/double(state.xui_batch_draws),
              state.xui_constants_differ,
              state.xui_batch_collapsible?100.0*double(state.xui_constants_differ)/double(state.xui_batch_collapsible):0.0);
        }
        xui_decode.Finish();
        edf::native::HookTiming xui_bind(edf::native::HookPhase::XuiBind);
        auto render=state.render_states.find(key);
        if(render==state.render_states.end())
          render=state.render_states.emplace(key,edf::native::CreateNativeRenderState(state.device.Get(),key)).first;
        const bool xui_seam=REXCVAR_GET(edf_native_seam_draws);
        if(!xui_seam) {
          edf::native::BindActiveTarget(state);
          edf::native::BindGuestRenderState(render->second,*state.context.Get(),reader,ctx.r3.u32,&state.bind_generation); viewport.Bind(*state.context.Get());
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
        state.font_bindings->SetConstants(*state.font_vertex,*state.font_pixel,vs,ps,
          font_canvas?float(scene.output.sampled.width)/1280.0f:1.0f,
          font_canvas?float(scene.output.sampled.height)/720.0f:1.0f);
        state.font_bindings->SetTexture(*state.font_pixel,texture->second.backend);
        const auto sampler_key=edf::native::SamplerStateKey(edf::native::ReadSamplerWords(reader,device,0));
        auto sampler=state.samplers.find(sampler_key);
        if(sampler==state.samplers.end()) {
          const auto desc=edf::native::DecodeNativeGuestSampler(sampler_key);
          sampler=state.samplers.emplace(sampler_key,&EnsureSceneBackendLocked(state).CreateSampler(desc)).first;
        }
        state.font_bindings->SetSampler(*state.font_pixel,sampler->second);
        auto render=state.render_states.find(key);
        if(render==state.render_states.end())
          render=state.render_states.emplace(key,edf::native::CreateNativeRenderState(state.device.Get(),key)).first;
        const size_t bytes=size_t(ctx.r5.u32)*16;
        if(REXCVAR_GET(edf_native_seam_draws)) {
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
          edf::native::BindGuestRenderState(render->second,*state.context.Get(),reader,ctx.r3.u32,&state.bind_generation); viewport.Bind(*state.context.Get());
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
          const auto bytes=CaptureOutputBmp(state,scene);
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
      auto vertex=state.shaders.find(pair.vertex),pixel=state.shaders.find(pair.pixel);
      if(vertex!=state.shaders.end() && pixel!=state.shaders.end()) {
        const auto& vs=vertex->second.bindings->shader();
        auto& ps=*pixel->second.bindings;
        const bool solid=vs.source_fingerprint==0xc885203e230fe745ull &&
          ps.shader().source_fingerprint==0xc885203e230fe745ull &&
          vs.entry.name=="VS_3D" && ps.shader().entry.name=="PS_Main";
        const bool textured=vs.source_fingerprint==0xc885203e230fe745ull &&
          ps.shader().source_fingerprint==0xc885203e230fe745ull &&
          vs.entry.name=="VS_3DTex" && ps.shader().entry.name=="PS_Tex";
        const bool particle=vs.source_fingerprint==0x777f4cf51fb1b019ull &&
          ps.shader().source_fingerprint==0x777f4cf51fb1b019ull &&
          vs.entry.name=="Vs_Particle" &&
          (ps.shader().entry.name=="Ps_Particle" || ps.shader().entry.name=="Ps_ZParticle");
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
          auto& bindings=edf::native::VertexBindingsForDraw(vertex->second,viewport.reverse_depth);
          if(!bindings.HasAllTextureInputs() || !ps.HasAllTextureInputs())
            throw std::runtime_error("Utility 3D missing texture inputs");
          if(edf::native::SamplesTarget(bindings,scene.color) || edf::native::SamplesTarget(ps,scene.color))
            throw std::runtime_error("native scene immediate samples its target");
          const auto owned_indices=state.generated_indices.Get(strip?edf::native::NativeIndexPattern::Strip:
            edf::native::NativeIndexPattern::Quads,ctx.r5.u32);
          const auto indices=owned_indices->bytes();
          const size_t bytes=size_t(ctx.r5.u32)*stride;
          const std::span<const uint8_t> vertices{reader.Bytes(ctx.r6.u32,bytes),bytes};
          edf::native::HookTiming acquire_timing(edf::native::HookPhase::ImmediateAcquire);
          auto& mesh=state.immediate_meshes.Acquire(EnsureSceneBackendLocked(state),bindings.shader(),
            edf::native::ImmediateStreamKey(vertices.size(),declaration,pair.vertex,ctx.r4.u32,viewport.reverse_depth),
            {elements,element_count*12},stride,
            vertices,indices,2,owned_declaration,owned_indices,{},{},{},{},0,{},
            // Where this mesh's dynamic vertices are rewritten when the draw
            // is recorded; the immediate context does it otherwise.
            REXCVAR_GET(edf_native_seam_draws)?&edf::native::SceneRecorderLocked(state):nullptr);
          acquire_timing.Finish();
          const auto key=edf::native::ReadAuditedRenderStateWords(reader,ctx.r3.u32);
          auto render=state.render_states.find(key);
          if(render==state.render_states.end()) render=state.render_states.emplace(key,
            edf::native::CreateNativeRenderState(state.device.Get(),key)).first;
          if(REXCVAR_GET(edf_native_seam_draws)) {
            edf::native::HookTiming record_timing(edf::native::HookPhase::ImmediateRecord);
            auto& recorder=edf::native::RecordDrawSetup(state,reader,ctx.r3.u32,{
              bindings,ps,viewport,key,
              mesh.input_layout().elements(),mesh.input_layout().fingerprint(),
              (uint64_t(pair.vertex)<<1)|uint64_t(viewport.reverse_depth?1:0),pair.pixel,
              edf::native::NativeBackendTopology::TriangleList});
            mesh.DrawTransient(recorder,vertices,0,uint32_t(indices.size()/2));
          } else {
            edf::native::BindActiveTarget(state);
            edf::native::BindGuestRenderState(render->second,*state.context.Get(),reader,ctx.r3.u32,&state.bind_generation); viewport.Bind(*state.context.Get());
            bindings.Bind(*state.context.Get()); ps.Bind(*state.context.Get());
            mesh.Draw(*state.context.Get(),0,uint32_t(indices.size()/2));
          }
          native_submitted=true; scene.frame_complete=false;
          auto& reported=state.scene_immediate_variants_reported[solid?0:textured?(strip?1:2):ps.shader().entry.name=="Ps_ZParticle"?4:3];
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
      const auto vertex=state.shaders.find(pair.vertex),pixel=state.shaders.find(pair.pixel);
      if(vertex!=state.shaders.end() && pixel!=state.shaders.end()) {
        auto& vs=*vertex->second.bindings; auto& ps=*pixel->second.bindings;
        constexpr uint64_t utility_source=0xc885203e230fe745ull;
        const bool textured=vs.shader().entry.name=="VS_2DTex" && ps.shader().entry.name=="PS_Tex";
        utility_draw=vs.shader().source_fingerprint==utility_source && ps.shader().source_fingerprint==utility_source &&
          (textured || (vs.shader().entry.name=="VS_2D" && ps.shader().entry.name=="PS_Main"));
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
          auto& bindings=edf::native::VertexBindingsForDraw(vertex->second,viewport.reverse_depth);
          if(!bindings.HasAllTextureInputs()) throw std::runtime_error("Utility vertex shader has missing native texture inputs");
          if(!ps.HasAllTextureInputs()) throw std::runtime_error("Utility has missing native texture inputs");
          if(edf::native::SamplesTarget(ps,target) || edf::native::SamplesTarget(bindings,target))
            throw std::runtime_error("Utility samples its active surface");
          const auto owned_indices=state.generated_indices.Get(lines?edf::native::NativeIndexPattern::Lines:
            edf::native::NativeIndexPattern::Quads,ctx.r5.u32);
          const auto indices=owned_indices->bytes();
          const size_t bytes=size_t(ctx.r5.u32)*stride;
          const std::span<const uint8_t> vertices{reader.Bytes(ctx.r6.u32,bytes),bytes};
          auto& mesh=state.immediate_meshes.Acquire(EnsureSceneBackendLocked(state),bindings.shader(),
            edf::native::ImmediateStreamKey(vertices.size(),declaration_handle,pair.vertex,ctx.r4.u32,viewport.reverse_depth),
            native_declaration->bytes(),stride,
            vertices,indices,2,native_declaration,owned_indices,{},{},{},{},0,{},
            // Where this mesh's dynamic vertices are rewritten when the draw
            // is recorded; the immediate context does it otherwise.
            REXCVAR_GET(edf_native_seam_draws)?&edf::native::SceneRecorderLocked(state):nullptr);
          auto render=state.render_states.find(snapshot.render);
          if(render==state.render_states.end())
            render=state.render_states.emplace(snapshot.render,edf::native::CreateNativeRenderState(state.device.Get(),snapshot.render)).first;
          if(REXCVAR_GET(edf_native_seam_draws)) {
            auto& recorder=edf::native::RecordDrawSetup(state,reader,ctx.r3.u32,{
              bindings,ps,viewport,snapshot.render,
              mesh.input_layout().elements(),mesh.input_layout().fingerprint(),
              // The reversed-depth variant is a different compiled shader under
              // the same guest handle, so it belongs in the identity.
              (uint64_t(pair.vertex)<<1)|uint64_t(viewport.reverse_depth?1:0),pair.pixel,
              lines?edf::native::NativeBackendTopology::LineList
                   :edf::native::NativeBackendTopology::TriangleList});
            if(lines) mesh.DrawLinesTransient(recorder,vertices,0,uint32_t(indices.size()/2));
            else mesh.DrawTransient(recorder,vertices,0,uint32_t(indices.size()/2));
          } else {
            edf::native::BindActiveTarget(state);
            edf::native::BindGuestRenderState(render->second,*state.context.Get(),reader,ctx.r3.u32,&state.bind_generation); viewport.Bind(*state.context.Get());
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
        const bool post_seam=REXCVAR_GET(edf_native_seam_draws);
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
           state.indexed_submitted>state.scene_indexed_start &&
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
        if(post_seam) {
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
        target.content_valid=initialized;
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
  const bool native_host=REXCVAR_GET(edf_native_host);
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

REXCVAR_DEFINE_BOOL(edf_native_static_world_pass,false,"EDF2027",
  "Draw the static opaque world pass natively in published group order; unsupported groups run their guest group callback (development)");
namespace edf::native {
namespace {
// The scene publication, queue, ownership and preload flags a native pass
// drawing published scene state depends on; the first one off, or null.
const char* NativeScenePassMissingFlag() {
  const std::pair<const char*,bool> required[]{
    {"edf_native_frame_dispatch",REXCVAR_GET(edf_native_frame_dispatch)},
    {"edf_native_scene_tree",REXCVAR_GET(edf_native_scene_tree)},
    {"edf_native_scene_tree_published",REXCVAR_GET(edf_native_scene_tree_published)},
    {"edf_native_scene_queued",REXCVAR_GET(edf_native_scene_queued)},
    {"edf_native_scene_visibility",REXCVAR_GET(edf_native_scene_visibility)},
    {"edf_native_scene_sources_owned",REXCVAR_GET(edf_native_scene_sources_owned)},
    {"edf_native_scene_membership_owned",REXCVAR_GET(edf_native_scene_membership_owned)},
    {"edf_native_scene_selection_owned",REXCVAR_GET(edf_native_scene_selection_owned)},
    {"edf_native_scene_camera_owned",REXCVAR_GET(edf_native_scene_camera_owned)},
    {"edf_native_scene_geometry_owned",REXCVAR_GET(edf_native_scene_geometry_owned)},
    {"edf_native_scene_material_owned",REXCVAR_GET(edf_native_scene_material_owned)},
    {"edf_native_scene_preload",REXCVAR_GET(edf_native_scene_preload)},
    {"edf_native_scene_group_order",REXCVAR_GET(edf_native_scene_group_order)}};
  for(const auto& [name,enabled]:required) if(!enabled) return name;
  return nullptr;
}
}
bool NativeStaticWorldPassEnabled() {
  if(!REXCVAR_GET(edf_native_static_world_pass)) return false;
  if(const auto* name=NativeScenePassMissingFlag()) {
    static std::atomic<bool> reported=false;
    if(!reported.exchange(true))
      REXLOG_INFO("Native static world pass disabled: requires {} (original group traversal retained)",name);
    return false;
  }
  return true;
}
bool NativeModelPassEnabled() {
  if(!REXCVAR_GET(edf_native_model_pass)) return false;
  const char* name=!REXCVAR_GET(edf_native_model_publication)?"edf_native_model_publication":
    !REXCVAR_GET(edf_native_shader_bridge)?"edf_native_shader_bridge":NativeScenePassMissingFlag();
  if(name) {
    static std::atomic<bool> reported=false;
    if(!reported.exchange(true))
      REXLOG_INFO("Native model pass disabled: requires {} (original model draws retained)",name);
    return false;
  }
  return true;
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
}
// Replaces 821C3BB8(owner+240) for one world owner. Native groups draw from the
// scene publication with explicit pass inputs; the device state they owe is
// handed off before any guest group and at the end, so later stages inherit
// what the sequential guest groups would have left. Guest calls run unlocked.
void RenderNativeStaticWorldPass(PPCContext& ctx,uint8_t* base,uint32_t owner) {
  const GuestReader reader(base);
  static NativeStaticWorldPassCounters counters;
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
  struct Owed { uint32_t material; std::shared_ptr<const NativeSceneMaterialProgram> program; };
  struct World { uint32_t vertex; VertexParameterRange parameter; std::array<uint8_t,64> bytes; };
  std::vector<Owed> owed;
  std::optional<World> world;
  NativeMaterialRenderPass owed_start;
  const auto handoff=[&] {
    if(owed.empty()) return;
    std::vector<NativeStaticWorldHandoffGroup> groups;
    groups.reserve(owed.size());
    for(const auto& group:owed) {
      uint32_t slots=0;
      for(const auto& operation:group.program->sampler_operations) slots|=1u<<operation.slot;
      groups.push_back({group.program->inputs.state_overrides,slots});
    }
    auto work=frame;
    const auto writes=HandOffNativeStaticWorld(reader,device,owed_start,cursor.material.samplers,groups,[&](size_t index) {
      // 821B94E8 through the activation hook: CPU program, host shader
      // bindings and setter publications, exactly as the guest group would.
      work.r3.u64=owed[index].material; work.lr=0x821D979C; sub_821B94E8(work,base);
      ++counters.replays;
    },[&](size_t index) {
      // The skipped group's binds and uploads, so intermediate textures and
      // shaders retire and registers only it wrote hold its values.
      ActivateNativeMaterial(work,base,owed[index].material,device,false);
      ++counters.binds;
    });
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
    std::shared_ptr<const NativeSceneGroupMaterial> material;
    std::optional<NativeSceneGeometrySource> setup;
    {
      std::lock_guard lock(state.mutex);
      for(const auto& published:publication->group_materials) if(published->group==group) { material=published; break; }
      if(const auto* source=NativeSceneSourcesForPass(state).FindGroup(group)) {
        const auto latest=state.scene_adapter.GroupGeometry(group,source->revision);
        for(const auto& published:publication->group_geometry)
          if(published==latest && published->group==group && published->setup &&
             published->geometry->backend()==state.scene_backend.get()) { setup=published->setup; break; }
      }
    }
    if(!material || !material->program || !setup) return F::Program;
    const auto& program=*material->program;
    if(!program.CanDeferCpuActivation()) return F::Scissor;
    if(AssessNativeStaticGroup(reader,device,frame.r1.u32,*setup)!=NativeStaticGroupEligibility::Supported) return F::Eligibility;
    NativeSceneMaterialPassState next;
    auto constants=material->constants;
    try {
      next=cursor.material.After(program);
      DecodeNativeRenderState(next.render.words);
      for(auto& constant:constants) {
        if(native_scene_pass_camera->Apply(constant) || !constant.global ||
           (constant.name!="m_WaterTime" && constant.name!="g_SignalBrightness")) continue;
        if(!native_scene_pass_animation) throw std::runtime_error("missing native animation pass");
        native_scene_pass_animation->Apply(constant);
      }
    } catch(const std::exception& error) { report(error.what()); return F::PassState; }
    // Resolve every instance before recording: a decline returns the whole
    // group, with its selections restored, to the guest callback.
    const auto instances=queues->Take(group);
    uint64_t batches=0;
    {
      std::lock_guard submission(state.submissions);
      std::lock_guard lock(state.mutex);
      std::vector<NativeStaticInstanceResolution> resolved;
      resolved.reserve(instances.size());
      const NativeStaticPassInputs pass{device,constants,cursor.material.render,cursor.material.samplers,
        NativeStaticPassView{*cursor.viewport,cursor.targets}};
      // One Resolve per group: its instances share geometry, program and these
      // pass inputs, so they share one material and record as instanced draws.
      std::optional<NativeStaticGroupMaterial> shared;
      try {
        for(const auto instance:instances) {
          auto result=ResolveNativePublishedStaticInstanceLocked(state,reader,*publication,
            {group,setup->count,material,setup},instance,pass,nullptr,&shared);
          if(!result) {
            if(const auto* reason=NativeStaticWorldDeclineReason(result.decline)) report(reason);
            break;
          }
          resolved.push_back(std::move(result));
        }
      } catch(const std::exception& error) { report(error.what()); resolved.clear(); }
      if(resolved.size()!=instances.size()) {
        for(auto at=instances.rbegin();at!=instances.rend();++at) queues->Push(group,*at);
        return F::Instance;
      }
      NativeQueuedSceneGroup batch;
      batch.targets=cursor.targets;
      batch.objects.reserve(resolved.size());
      const auto draws=state.scene_native_draws;
      // The shared capture gives every instance one view, so this is one flush.
      try {
        for(auto& result:resolved) {
          if(!batch.objects.empty() && (batch.view.view!=result.view.view || batch.view.projection!=result.view.projection ||
             batch.view.view_projection!=result.view.view_projection)) FlushNativeQueuedSceneLocked(state,batch);
          batch.view=result.view; batch.objects.push_back(result.object);
        }
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
      state.scene_adapter.RememberGroupMaterial(group,resolved.back().material);
      world=World{resolved.back().vertex,*resolved.back().world_parameter,resolved.back().world};
    }
    if(owed.empty()) owed_start=cursor.material.render;
    owed.push_back({setup->material,material->program});
    cursor.material=std::move(next);
    ++counters.native_groups; counters.instances+=instances.size(); counters.batches+=batches;
    LogNativeStaticWorldGroup(group,batches);
    return {};
  };
  const auto fallback=[&](uint32_t group,NativeStaticWorldFallback reason) {
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
  const auto passes=++counters.passes;
  if(passes<=4 || passes%1000==0) {
    uint64_t fallbacks=0;
    for(const auto count:counters.fallbacks) fallbacks+=count;
    const auto& f=counters.fallbacks;
    REXLOG_INFO("Native static world pass: passes={} native_groups={} empty_groups={} instances={} draws={} batches={} fallback_groups={} "
      "guest_queue={} program={} scissor={} eligibility={} pass_state={} instance={} handoffs={} replays={} binds={} original={}",
      passes,counters.native_groups,counters.empty_groups,counters.instances,counters.draws,counters.batches,fallbacks,
      f[0],f[1],f[2],f[3],f[4],f[5],counters.handoffs,counters.replays,counters.binds,counters.original);
  }
}
}

namespace edf::native {
// Replaces one 821C9C20 object (rigid path: per record 821A17D8 then 821B2C28,
// which binds stream/declaration/indices per batch and runs 821B94E8 +
// 821FE358 per material pass). Everything is resolved from the layout and
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
  // Publication gate: rigid, layout current, pose published for this tick.
  auto& models=::ModelPublications();
  const auto published=models.Find(instance);
  if(published && render_dependent) models.MarkRenderDependent(instance,published.generation);
  const auto identity=ReadGuestWords<2>(reader,instance);
  const auto poses=models.AcquirePoses();
  const auto gate=GateNativeModelPass(published,published && models.Current(instance,identity[0],identity[1],vector),
    reader.Bytes(reader.Add(instance,12),1)[0],poses.get(),native_render_budget.tick,
    render_dependent || (published && models.RenderDependent(instance,published.generation)));
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
  const auto device=reader.Word(reader.Add(reader.Word(0x8257BFB4),8));
  // The 821C9C20 and 821B2C28 frames the setters and activations run under.
  if(ctx.r1.u32<4096+128+176) return decline(D::Eligibility);
  const uint32_t stack=ctx.r1.u32-128-176;
  const auto batch_of=[&](const NativeModelDraw& draw) -> const NativeModelBatchLayout& {
    return layout.meshes[draw.mesh].batches[draw.batch];
  };
  const auto source_of=[&](const NativeModelDraw& draw) {
    const auto& batch=batch_of(draw);
    return NativeSceneGeometrySource{batch.vertex.owner,batch.index.owner,batch.declaration,batch.stride,batch.draw_count,
      draw.pass,reader.Word(reader.Word(reader.Add(draw.pass,108)))};
  };
  // Static-group eligibility per pass: the same descriptor shape and CPU
  // activation contract. Guest reads only.
  try {
    for(const auto& draw:plan)
      if(AssessNativeStaticGroup(reader,device,stack,source_of(draw))!=NativeStaticGroupEligibility::Supported)
        return decline(D::Eligibility);
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
    // Material program of one pass record (the static group material shape):
    // the program is rebuilt only when its recorded program bytes or host
    // identities changed; constant values are re-read at every use, since the
    // guest activation uploads them live. That includes the pass-owned camera
    // and animation globals (the static pass substitutes its published camera;
    // a model draw uploads whatever the globals hold now), and excludes
    // g_mWorld, which each record supplies. Failures retry once per tick.
    const auto refresh=[&](Bridge::ModelPassLoad& load) {
      const auto& schema=*load.schema;
      const auto& published=load.published->constants;
      if(published.size()!=load.layout.size()) throw std::runtime_error("native model material constants do not match their layout");
      std::optional<std::vector<NativeSceneMaterialInputs::Constant>> constants;
      for(size_t i=0;i<load.layout.size();++i) {
        const auto& slot=load.layout[i];
        if(published[i].global && published[i].name=="g_mWorld") continue;
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
    const auto program_of=[&](uint32_t pass) -> std::shared_ptr<const NativeSceneGroupMaterial> {
      auto& load=state.model_pass_loads[pass];
      if(load.published && NativeSceneMaterialHostCurrent(state,*load.published->program,pass,load.schema) &&
         load.reads.Unchanged(window)) {
        try { refresh(load); ++state.model_pass_reused; return load.published; }
        catch(const std::exception& error) { report(error.what()); }
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
    };
    // Retained geometry of one batch under one vertex shader, reloaded through
    // the guarded observed-set copy, as the static preload does, when changed.
    const auto geometry_of=[&](const NativeSceneGeometrySource& source,const NativeModelBatchLayout& batch)
        -> std::shared_ptr<const NativeIndexedMesh::RetainedDraw> {
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
        result=program.Resolve(desc,viewport.reverse_depth,material->constants,cursor.render,cursor.samplers,
          REXCVAR_GET(edf_native_anisotropic_filtering));
      } catch(const std::exception& error) { report(error.what()); return decline(D::PassState); }
      // g_mWorld exactly as 821A17D8 stores it: the record's bone, column-major.
      try { ApplyNativeScenePublishedWorld(result.capture,NativeModelWorldRegisters(matrices[layout.meshes[draw.mesh].bone])); }
      catch(const std::exception& error) { report(error.what()); return decline(D::World); }
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
  // 1. 821A17D8 of the last record: g_mWorld's storage holds its bone.
  if(!layout.meshes.empty()) reader.StoreCpuWords(world_storage,NativeModelWorldWords(matrices[layout.meshes.back().bone]));
  // 2. 821B2C28's stream/declaration/index setters for the last batch.
  const NativeModelBatchLayout* last=nullptr;
  for(const auto& mesh:layout.meshes) if(!mesh.batches.empty()) last=&mesh.batches.back();
  if(last) {
    NativeSceneGeometryInstallState progress;
    auto work=frame;
    ::InstallNativeStaticGeometry(work,base,device,NativeSceneGeometrySource{last->vertex.owner,last->index.owner,
      last->declaration,last->stride,last->draw_count,last->passes.empty()?0:last->passes.back(),0},progress);
  }
  // 3. Material activations: the last pass and each sampler slot's last
  // binder through 821B94E8, then every pass's combined render words, dirty
  // masks and sampler words, exactly as for static world groups.
  if(!owed.empty()) {
    auto work=frame;
    const auto writes=HandOffNativeStaticWorld(reader,device,start.render,cursor.samplers,owed,[&](size_t index) {
      work.r3.u64=plan[index].pass; work.lr=0x821B2ECC; sub_821B94E8(work,base);
      ++counters.replays;
    });
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
