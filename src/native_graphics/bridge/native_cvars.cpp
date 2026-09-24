// The native renderer's cvars (edf_native_*), moved from guest_shader_bridge.cpp unchanged: names,
// defaults, descriptions and ranges. Declared for other translation units in native_cvars.h.
//
// Registration (rex/cvar.h): each REXCVAR_DEFINE_* is a function-local static for the value, which
// is constant-initialized on first use from any translation unit, and a namespace-scope FlagRegistrar
// that registers the name during static initialization. The command line and environment (the SDK's
// main: rex::cvar::Init, ApplyEnvironment) and the config file (ReXApp: LoadConfig) are applied after
// every translation unit's static initialization, and a value read before them is the default
// whichever file defines the cvar, so the file that defines them changes no value anyone reads.
// Registration order across files only orders the registry, which SerializeToTOML walks: a saved
// config may list its non-default edf_native_* lines in another order.
//
// `inline` makes each accessor the inline one native_cvars.h gives every other file (EDF_NATIVE_CVAR), so
// reads elsewhere are direct loads of the one storage; edf_native_scene_backend and
// edf_native_unlock_framerate are defined out of line (native_cvars.h says why).
#define EDF_NATIVE_CVARS_DEFINE
#include "native_cvars.h"

// Renderer preset and native ownership switches.
// The preset (native_renderer_preset.h, ResolveNativeRendererPreset) and the switches it can add: each moves one
// part of the frame from the guest renderer to native code. Read through EDF_NATIVE_FLAG where the preset applies.
inline REXCVAR_DEFINE_BOOL(edf_native_shader_bridge, false, "EDF2027",
                   "Validate native shader resources against live guest loads (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_material_owned,false,"EDF2027",
                   "Construct queued rigid scene materials from published programs and native pass inputs");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_activation_owned,false,"EDF2027",
                   "Activate queued scene shader bindings from published material inputs");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_geometry_owned,false,"EDF2027",
                   "Use published geometry for the first native group draw, retaining the indexed CPU tail");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_instance_owned,false,"EDF2027",
                   "Use lifecycle-owned world-only instance register metadata");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_pass_owned,false,"EDF2027",
                   "Carry explicit render and sampler pass state between native groups");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_geometry_deferred,false,"EDF2027",
                   "Submit eligible native groups before installing compatibility geometry bindings");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_material_deferred,false,"EDF2027",
                   "Submit eligible native groups before CPU material activation and restore state at handoff");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_view_owned,false,"EDF2027",
                   "Carry viewport, scissor and native target selection across native material groups");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_camera_owned,false,"EDF2027",
                   "Consume immutable producer camera matrices at native render entry");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_sources_owned,false,"EDF2027",
                   "Select native static sources, LOD, visibility and world from one scene publication");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_selection_owned,false,"EDF2027",
                   "Resolve published static instances without mutating the current scene database");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_membership_owned,false,"EDF2027",
                   "Select spatial lists and hierarchy from the same publication as native scene sources and assets");
inline REXCVAR_DEFINE_BOOL(edf_native_owned_render_state,true,"EDF2027",
                   "Use native setter-owned render words, scissor enable and blend factors; false enables diagnostic legacy reads");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_queued,false,"EDF2027",
                   "Render supported queued static-world groups from retained native scene objects (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_preload,false,"EDF2027",
                   "Publish static-group geometry and owned material inputs without draw callbacks (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_visibility,false,"EDF2027",
                   "Use native visibility and static LOD selection within the opt-in native scene path (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_bucket_dispatch,false,"EDF2027",
                   "Insert sort-mode 1/2 objects into the guest depth buckets natively instead of sub_821C0C00 (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_static_walk,false,"EDF2027",
                   "Publish a per-world static walk plan at the simulation step and drive the native visibility walk from it: planned membership, vtable slot and source candidates; mode, hidden flag and vtable stay live (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_model_publication,false,"EDF2027",
                   "Capture model draw layouts at first sight and publish per-tick pose snapshots; draws are unchanged (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_render_registry,false,"EDF2027",
                   "Track render objects from the base constructor/destructor and update subscription, and publish a per-tick renderable snapshot at the end of 821A4DE8 for the full-frame renderer; draws are unchanged (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_model_pass,false,"EDF2027",
                   "Draw rigid published models natively at 821C9C20; any unsupported object runs the original draw. Requires edf_native_model_publication and the static world pass scene flags (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_model_pass_skinned,false,"EDF2027",
                   "Also draw palette-skinned models (instance+12 set) in the native model pass, binding the packed bone palette as g_mWorldArray; requires edf_native_model_pass (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_seam_draws, true, "EDF2027",
                   "Record the scene's draws through the backend interface instead of calling the D3D11 context directly. True by default, and required by --edf_native_scene_backend=d3d12: a draw issued straight to the D3D11 context cannot bind a resource that lives on another device. False keeps the old direct path, which only works with the d3d11 scene backend and exists as the A/B control - with both on d3d11 the two draw the same thing on the same device, so a difference is a wiring mistake rather than a backend one");
inline REXCVAR_DEFINE_BOOL(edf_native_post_finish,false,"EDF2027",
  "Replace the post chain 820B09B0 (and the bloom quad of 821A8F20) of the finish stage 820B0B80 with a native loop issuing the planned passes; with edf_native_post_finish_audit on, only after a clean audited guest frame. Any preflight failure, missing target or exception runs the original for that frame");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_tree,false,"EDF2027",
  "Use native spatial tree traversal and culling; leaf callbacks remain explicit.");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_tree_published,false,"EDF2027",
  "Read immutable spatial hierarchy published by the world producer.");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_group_order,false,"EDF2027",
  "Publish each world owner's static group walk order (owner+240) at simulation step.");
inline REXCVAR_DEFINE_BOOL(edf_native_full_frame,false,"EDF2027",
  "Full-frame native renderer: the render helper 821A5080 runs a native frame (scene begin, native static world, native post, then the guest HUD phase loop on the output) instead of the guest helper. Needs edf_native_host and edf_native_shader_bridge; edf_native_ab_alternate guest-side frames keep the guest helper (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_map_effect_list,false,"EDF2027",
  "Walk the map-effect list (sub_820B35A0) natively; each object still goes through the hooked sub_821C0C00.");
inline REXCVAR_DEFINE_BOOL(edf_native_frame_dispatch,false,"EDF2027",
  "Own outer render phase dispatch in native code; remaining phase callbacks are retained.");
inline REXCVAR_DEFINE_BOOL(edf_native_material_activation,false,"EDF2027",
  "Run material activation from a native operation list with native sampler resolution");
inline REXCVAR_DEFINE_BOOL(edf_native_static_world_pass,false,"EDF2027",
  "Draw the static opaque world pass natively in published group order; unsupported groups run their guest group callback (development)");
inline REXCVAR_DEFINE_STRING(edf_native_renderer,"native","EDF2027",
  "Native renderer preset (default native; off restores the guest renderer): off, world (static world pass and every scene flag it requires), full (world plus model publication, the rigid model pass and the native post finish) or native (full plus the full-frame renderer, edf_native_full_frame). Adds to the individual edf_native_* cvars and never turns one off; read once at startup");

// Backends, devices and caches.
// Which backend draws the scene and presents it, and how its devices, workers, upload ring and disk caches are set up.
inline REXCVAR_DEFINE_STRING(edf_native_backend, "d3d12", "EDF2027",
                     "Backend used by everything that draws through the renderer's backend interface: d3d12 (default), d3d12-warp, d3d11, d3d11-warp, or empty for none. Built on first use, so selecting one costs nothing until something draws through it. An unknown name is refused rather than silently falling back");
REXCVAR_DEFINE_STRING(edf_native_scene_backend, "d3d12", "EDF2027",
                     "Scene rendering backend: d3d12 (default) or d3d11 for comparison. D3D12 publishes a fenced GPU snapshot through the host compositor, preserving display gamma and overlays. D3D11 is an explicit fallback; backend initialization failures do not silently change this setting");
inline REXCVAR_DEFINE_STRING(edf_native_cache_dir, "", "EDF2027",
                     "Directory for the renderer's persistent caches: compiled shader bytecode (shaders/<sha256>.dxbc) and the scene backend's D3D12 pipeline manifest (d3d12_pipelines.bin), which the next run prebuilds in the background. Empty (default) uses native_cache beside the executable; off disables both, leaving only this run's in-memory caches. Entries are keyed by SHA-256 of everything their output depends on, so a stale file is a miss, never a wrong shader or pipeline");
inline REXCVAR_DEFINE_INT32(edf_native_upload_megabytes, 256, "EDF2027",
                    "Upload-ring megabytes for a D3D12 backend. Every recorded draw stages its constants here and the ring is retired by fence, so it has to hold every frame still in flight. A frame that does not fit is refused with the high water it reached, which is what to set this from");
inline REXCVAR_DEFINE_INT32(edf_native_geometry_workers, 4, "EDF2027",
    "D3D12 geometry recording workers (0 direct, 1 serial packets, 2..32 parallel); restart required");
inline REXCVAR_DEFINE_INT32(edf_native_frame_operations, 8192, "EDF2027",
                    "Recorded operations after which the scene's frame is submitted and a new one opened, rather than waiting for the guest's swap. The guest swaps once a frame but begins render targets far more often than that - measured at 1,000 begins across 8 swaps - so a frame tied only to the swap accumulates without bound during loading, which is one command list, one ring's worth of uploads, and eventually a GPU with more work in one submission than it will accept");
inline REXCVAR_DEFINE_BOOL(edf_native_d3d12_debug_layer, false, "EDF2027",
                   "Turn the D3D12 debug layer, and GPU-based validation with it, on for every backend this process builds - including the hardware one. Very slow. Worth it when something removes the device: the plain layer names an invalid call, and GPU-based validation names what a shader did with a valid one, which is the half that presents as a hang with nothing in the log");
inline REXCVAR_DEFINE_BOOL(edf_native_backend_present, true, "EDF2027",
                   "Use a separate presenting backend for the D3D11 fallback. D3D12 always uses its native host and presenter");
inline REXCVAR_DEFINE_INT32(edf_native_shader_workers, -1, "EDF2027",
                    "Threads used to compile a shader registration's entries: -1 picks one per core up to eight, 0 compiles inline on the calling thread. Compilation is the load cost worth threading - the entries are a real batch and each takes milliseconds, unlike the per-draw work, which has neither property");
inline REXCVAR_DEFINE_BOOL(edf_native_backend_preview, false, "EDF2027",
                   "Open a second window drawn and presented entirely by the selected backend. Needs --edf_native_backend and --edf_native_publish_frames. The renderer's own window is untouched");
inline REXCVAR_DEFINE_INT32(edf_native_frame_latency,2,"EDF2027",
  "D3D12 frame credits: 1 drains each frame, 2 overlaps next-frame preparation; restart required.");

// Render size, sampling, anti-aliasing and FSR.
// Render target size and samples (native_display_layout.h), sampler quality, pixel centres, and FSR with its motion vectors.
inline REXCVAR_DEFINE_INT32(edf_native_anisotropic_filtering, -1, "EDF2027",
                    "Native material filtering: -1 game default, 0 off, 1..5 for 1x..16x; preserves point/base-only sampling");
inline REXCVAR_DEFINE_INT32(edf_native_render_width, 0, "EDF2027",
                    "Native render width at startup; 0 takes the width from the window's shape (native_display_layout.h), a positive width with a positive height renders exactly that size (restart required)");
inline REXCVAR_DEFINE_INT32(edf_native_msaa, 0, "EDF2027",
                    "Native scene samples: 0 game default, 1 off, 2 or 4 MSAA (restart required)");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_depth_srv, false, "EDF2027",
                   "Create a single-sampled native scene depth shader-readable (typeless, with a depth SRV) for FSR; ignored with MSAA (restart required)");
inline REXCVAR_DEFINE_STRING(edf_native_fsr, "off", "EDF2027",
                     "FSR 3.1 on the native full-frame scene (native_fsr.h): off, or native_aa (1.0x temporal anti-aliasing); quality, balanced, performance and ultra_performance are accepted and run as native_aa until render scaling exists. On at startup it forces 1x scene MSAA and the sampled scene depth (edf_native_msaa, edf_native_scene_depth_srv; restart-time), so turning it on later needs a restart unless those already hold. Off while edf_native_ab_alternate, edf_native_reuse_off_alternate or edf_native_shadow_render is set");
inline REXCVAR_DEFINE_DOUBLE(edf_native_fsr_sharpness, 0.2, "EDF2027",
                     "FSR sharpening (RCAS) strength, 0 (off) to 1").range(0.0,1.0);
inline REXCVAR_DEFINE_BOOL(edf_native_motion_vectors, false, "EDF2027",
                   "FSR motion vectors: camera reprojection from the scene depth plus a velocity re-render of moving models, per view before post; forces a single-sampled, shader-readable scene depth (restart required)");
inline REXCVAR_DEFINE_INT32(edf_native_motion_vectors_debug, 0, "EDF2027",
                    "Motion vector debug view over the output before the HUD: 0 off, 1 motion as colour, 2 history-valid mask").range(0,2);
inline REXCVAR_DEFINE_INT32(edf_native_render_height, -1, "EDF2027",
                    "Native render height at startup: -1 (default) the window's size, 0 the engine's original 720 lines (with width 0, exactly 1280x720 on a 16:9 window), otherwise the line count; paired with render width (restart required)");
inline REXCVAR_DEFINE_BOOL(edf_native_pixel_centers,true,"EDF2027",
                   "Apply the guest PA_SU_VTX_CNTL half-pixel offset to the audited retail post passes; false restores the unshifted viewport for regression diagnosis");

// Reuse, batching and caches between draws and frames.
// What the renderer may carry over from the previous draw or frame instead of rebuilding it (native_reuse.h).
inline REXCVAR_DEFINE_INT32(edf_native_geometry_verify_interval,256,"EDF2027",
                    "Sample the guest geometry byte comparison once per N snapshot observations of an owner once its revision proves the retained candidate; 0 compares every draw. A detected miss permanently restores full comparison");
inline REXCVAR_DEFINE_INT32(edf_native_geometry_verify_initial,8,"EDF2027",
                    "Snapshot observations of each subscription lifetime that always compare guest geometry bytes before the sampled schedule applies (0..65536)");
inline REXCVAR_DEFINE_BOOL(edf_native_render_registry_idle_skip,true,"EDF2027",
                   "On an unlocked render-only iteration (no simulation step) the render registry applies its events and reads only new, resubscribed and retrying objects plus a round-robin probe of the update members (and those a probe found changing on such iterations), not every scene+100 member, animated object and round-robin refresh; false re-reads them every iteration");
inline REXCVAR_DEFINE_BOOL(edf_native_reuse_material, true, "EDF2027",
                   "Skip re-binding the shader pair, textures and samplers when the previous indexed draw already bound the same ones and nothing has bound since. Set false if repeated objects ever show another material's textures; that is what a wrong guard here looks like");
inline REXCVAR_DEFINE_INT32(edf_native_preload_workers, -1, "EDF2027",
                    "Threads that help the engine thread check the static preloads' groups each step (821A4DE8, between frames): -1 picks 3 on 8+ cores, 1 on 4+, else 0; 0 checks every group on the engine thread. Read once, at the first preload").range(-1,16);
inline REXCVAR_DEFINE_BOOL(edf_native_registry_overlap, true, "EDF2027",
                   "Run the render registry's per-step tick on its own thread beside the step's scene publication and preloads (joined before 821A4DE8 returns); false runs it after them on the engine thread");
inline REXCVAR_DEFINE_BOOL(edf_native_owned_mesh_hit,true,"EDF2027",
                   "Reuse consecutive mesh hits with identical owned geometry snapshots");
inline REXCVAR_DEFINE_BOOL(edf_native_world_instancing,true,"EDF2027",
                   "Combine compatible queued world-matrix draws into GPU instances");
inline REXCVAR_DEFINE_BOOL(edf_native_world_constant_reuse,true,"EDF2027",
                   "Retain shared vertex constants when only an instance world matrix changes");
inline REXCVAR_DEFINE_BOOL(edf_native_transient_batching,true,"EDF2027",
                   "Record a UI/immediate list draw (XUI brush, font run, Utility 2D quad or line) as the continuation of the draw before it when the two differ only in their vertices; the Utility 2D path then records its quads non-indexed. Set false to record every draw as its own");
inline REXCVAR_DEFINE_BOOL(edf_native_prepared_geometry,true,"EDF2027",
                   "Reuse prepared queued geometry after guarded snapshot validation");

// Frame pacing, unlocked frame rate and threads.
// The experimental unlocked render loop, its interpolation, waits and the engine/render-helper thread QoS.
inline REXCVAR_DEFINE_INT32(edf_native_wait_stall_ms,5000,"EDF2027",
                    "Native wait no-progress deadline in host milliseconds (1..600000); failure does not fake GPU completion");
REXCVAR_DEFINE_BOOL(edf_native_unlock_framerate,false,"EDF2027",
                   "Experimental independent render loop with 60 Hz step dispatch; motion interpolation and timing validation are in progress");
inline REXCVAR_DEFINE_BOOL(edf_native_camera_interpolation,true,"EDF2027",
                   "Interpolate published camera poses in experimental unlocked mode; false permits diagnostic comparison");
inline REXCVAR_DEFINE_BOOL(edf_native_model_interpolation,true,"EDF2027",
                   "Interpolate model pose uploads in experimental unlocked mode; false permits diagnostic comparison");
inline REXCVAR_DEFINE_INT32(edf_native_thread_qos,2,"EDF2027",
  "Engine and render helper thread QoS: 0 OS default (a hidden or occluded window gets low QoS, which on hybrid CPUs "
  "prefers efficiency cores), 1 opt out of execution-speed throttling (HighQoS), 2 also prefer performance-core CPU sets").range(0,2);

// Guest memory, fences and frame publication.
// How the bridge reads guest memory and fences, and publishes native frames to the host and preview windows.
inline REXCVAR_DEFINE_BOOL(edf_native_untiled_scene, true, "EDF2027",
                   "Deprecated compatibility setting; native untiled scene lifecycle is always used");
inline REXCVAR_DEFINE_BOOL(edf_native_guest_heap_reads, true, "EDF2027",
                   "Validate committed readable SDK regions; Windows validation remains fallback (false forces OS checks)");
inline REXCVAR_DEFINE_BOOL(edf_native_fence_probe, false, "EDF2027",
                   "Observe native event-query completion at guest fence boundaries; never writes guest counters");
inline REXCVAR_DEFINE_BOOL(edf_native_validate_wait, false, "EDF2027",
                   "Development: await native completion after original guest waits; requires native bridge and fence probe");
inline REXCVAR_DEFINE_BOOL(edf_native_publish_frames, false, "EDF2027",
                   "Publish native movie/partial scene GPU snapshots for host presentation; requires native bridge");
inline REXCVAR_DEFINE_BOOL(edf_native_preview_window, false, "EDF2027",
                   "Show native frames in a separate development window; requires frame publication");

// Audits (diagnostic).
// Compare a native path with the guest one it replaces and log mismatches; none changes what is drawn.
inline REXCVAR_DEFINE_BOOL(edf_native_render_state_audit,false,"EDF2027",
                   "Compare setter-owned render-state snapshots with live draw state (diagnostic; does not bypass reads)");
inline REXCVAR_DEFINE_BOOL(edf_native_material_sampler_audit,false,"EDF2027",
                   "Compare explicit native material sampler programs with original activation (diagnostic)");
inline REXCVAR_DEFINE_BOOL(edf_native_material_state_audit,false,"EDF2027",
                   "Compare explicit native material render-state programs with original activation (diagnostic)");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_material_audit,false,"EDF2027",
                   "Compare published programs with explicit pass-time constants and visible group setup (diagnostic)");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_reject_compatibility,false,"EDF2027",
                   "Diagnostic: reject counted static-group compatibility boundaries before calling them");
inline REXCVAR_DEFINE_BOOL(edf_native_worker_callback_audit,false,"EDF2027",
                   "Log up to 256 render-worker callback registrations for writer provenance (diagnostic)");
inline REXCVAR_DEFINE_BOOL(edf_native_retirement_audit,false,"EDF2027",
                   "Sample completed native setter retirement branches and descriptor identity (diagnostic)");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_adapter_audit,false,"EDF2027",
                   "Log bounded world-object dispatch samples for native scene lifetime integration (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_transform_audit,false,"EDF2027",
                   "Compare event-published native static transforms with every queued guest matrix (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_visibility_audit,false,"EDF2027",
                   "Compare native visibility against original culling routines and live bounds (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_bucket_dispatch_audit,false,"EDF2027",
                   "Compare the native sort-mode 1/2 bucket key and insert with sub_821C0C00 (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_static_walk_audit,false,"EDF2027",
                   "Publish the static walk plan and compare its classification with the live walk reads, counting mismatches; the walk itself stays live (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_model_publication_audit,false,"EDF2027",
                   "Compare published model layouts and poses with live memory at model draw entry (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_render_registry_audit,false,"EDF2027",
                   "Walk scene+84 and scene+100 each tick and count mismatches against the render registry's records and subscriptions; requires edf_native_render_registry (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_model_source_audit,false,"EDF2027",
                   "Full-frame Models pass: fetch the program and geometry of every draw kept at the current source generation afresh from the providers and log each one that differs (a provider input the source generation missed) (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_render_registry_idle_audit,false,"EDF2027",
                   "Follow each render-only light registry tick with a compare-only read of every update member and log the entries a full tick would change (what the light tick left unread); publishes nothing (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_batch_audit, false, "EDF2027",
                   "Measure runs of consecutive indexed draws that differ only in per-instance constants; the mean run length is the draw-call reduction instancing would give");
inline REXCVAR_DEFINE_INT32(edf_native_contract_limit, 4096, "EDF2027",
                    "Distinct draw contracts the coverage ledger retains (1..1048576); reaching it is counted, never silently dropped");
inline REXCVAR_DEFINE_STRING(edf_native_contract_export, "", "EDF2027",
                     "Write the captured draw-contract catalog to this path; the offline geometry check replays it");
inline REXCVAR_DEFINE_BOOL(edf_native_contract_coverage, false, "EDF2027",
                   "Also record every submitted draw contract, so a run can enumerate what the content exercises; costs a set lookup per draw");
inline REXCVAR_DEFINE_INT32(edf_native_shared_constant_audit, 0, "EDF2027",
                    "Audit Common.fx globals a stage's native shader consumes but the material never lists for that stage, for this many activations; 0 disables (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_mesh_watch_audit,false,"EDF2027",
                   "Audit physical mesh write versions against exact bytes; never skips validation (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_post_finish_audit,false,"EDF2027",
  "Compare the native finish plan with the setters, targets and quads 820B0B80 actually issues, and log mismatches (development). With edf_native_post_finish on, audited guest frames and native frames alternate");
inline REXCVAR_DEFINE_BOOL(edf_native_scene_group_order_audit,false,"EDF2027",
  "Compare the published static group order with a live walk at world-pass entry (development).");
inline REXCVAR_DEFINE_BOOL(edf_native_map_effect_census,false,"EDF2027",
  "Tally map-effect objects by (vtable, mode, slot-4 method) before each map-effect walk; log the top classes every 600 frames.");

// Captures, probes and A/B validation (development).
// Image and draw captures, the invalid-RGB probe, shadow renders and the A/B and reuse-off alternations.
inline REXCVAR_DEFINE_STRING(edf_native_scene_capture, "", "EDF2027",
                     "Optional prefix for partial scene, font/movie and output-frame BMP diagnostics (not presentation)");
inline REXCVAR_DEFINE_INT32(edf_native_output_capture_interval,0,"EDF2027",
                    "Additional output capture interval in indexed frames; 0 disables periodic captures (development)");
inline REXCVAR_DEFINE_INT32(edf_native_output_capture_limit,32,"EDF2027",
                    "Maximum diagnostic output BMPs per run, clamped to 0..128; requires scene capture prefix (development)");
inline REXCVAR_DEFINE_INT32(edf_native_output_capture_start_frame,0,"EDF2027",
                    "Positive indexed frame starts interval captures and disables startup milestones; 0 preserves defaults (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_output_capture_scene_color,false,"EDF2027",
                   "Also capture scene color at each selected output frame to diagnose post-processing differences (development)");
inline REXCVAR_DEFINE_INT32(edf_native_shadow_render,0,"EDF2027",
                    "Shadow render: every Nth indexed output frame, after the native full frame, also render the guest helper path for the same state into offscreen targets and write both pre-HUD images and draw lists (tools/shadow-diff.py); 0 off (development)").range(0,100000);
inline REXCVAR_DEFINE_INT32(edf_native_shadow_render_start_frame,0,"EDF2027",
                    "First indexed output frame a shadow render may take; shadow frames are start, start+N, ... (development)").range(0,100000000);
inline REXCVAR_DEFINE_INT32(edf_native_shadow_render_limit,16,"EDF2027",
                    "Maximum shadow frames written per run (development)").range(0,4096);
inline REXCVAR_DEFINE_STRING(edf_native_shadow_render_prefix,"native-shadow/shadow","EDF2027",
                     "Path prefix of shadow render output: <prefix>.<frame>.native.bmp/.guest.bmp, .native.draws.jsonl/.guest.draws.jsonl and .shadow.json; its directory is created (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_shadow_render_constants,false,"EDF2027",
                   "Shadow render: also write every draw's constant bytes and its transient vertex uploads of up to 4 KB into the draw lists, so tools/shadow-diff.py can say which registers and vertices differ (large; development)");
inline REXCVAR_DEFINE_BOOL(edf_native_capture_indexed_state,false,"EDF2027",
                   "Trace up to 256 indexed draw states per selected capture frame; requires scene capture prefix (development)");
inline REXCVAR_DEFINE_INT32(edf_native_probe_x, -1, "EDF2027", "Optional native scene invalid-RGB probe pixel X");
inline REXCVAR_DEFINE_INT32(edf_native_probe_y, -1, "EDF2027", "Optional native scene invalid-RGB probe pixel Y");
inline REXCVAR_DEFINE_INT32(edf_native_probe_frame, 0, "EDF2027",
                    "First indexed output candidate to probe; 0 retains first-scene diagnostics");
inline REXCVAR_DEFINE_INT32(edf_native_probe_width, 1, "EDF2027", "Invalid-RGB diagnostic region width");
inline REXCVAR_DEFINE_INT32(edf_native_probe_height, 1, "EDF2027", "Invalid-RGB diagnostic region height");
inline REXCVAR_DEFINE_INT32(edf_native_probe_draw_limit, 4096, "EDF2027", "Maximum invalid-RGB diagnostic draws, capped at 65536");
inline REXCVAR_DEFINE_BOOL(edf_native_probe_negative, false, "EDF2027",
                   "Also stop the invalid-RGB probe on a scene channel at or below -1; the tone curve maps a large negative to white");
inline REXCVAR_DEFINE_BOOL(edf_native_reuse_off,false,"EDF2027",
                   "Correctness diagnostics: disable every cross-frame reuse of the full-frame renderer (native_reuse.h): the models' draw states, carried objects, material rows and caches, source memo and pose-blend cache; the static world's frame, group memos, material cache, instance reuse, selection cache, flattened tree, cluster cull and uniform recording; the sky's material, layout and hierarchy caches; the registry's render-only light ticks, frame-pose memo and unchanged-entry/constant pointer sharing; shared effect activation, transient batching (implies edf_native_transient_batching off) and the UI lookup memos. Output should be identical, only slower (development)");
inline REXCVAR_DEFINE_INT32(edf_native_reuse_off_alternate,0,"EDF2027",
  "Correctness diagnostics: render with reuse off (edf_native_reuse_off) in runs of N indexed output frames from the capture start frame, the edf_native_ab_alternate rule: even runs (the reference, logged reuse_alternate frame=F native=0) reuse off, odd runs (judged, native=1) reuse on; 0 off, ignored while edf_native_ab_alternate is on (development)").range(0,1000);
inline REXCVAR_DEFINE_INT32(edf_native_ab_alternate,0,"EDF2027",
  "A/B diagnostics: alternate guest and native passes in runs of N indexed output frames from the capture start frame; odd runs are native, 0 off (development)").range(0,1000);

// Timings and traces (development).
// CPU hook and load timings (hook_timing.h), GPU pass timings, frame times, the coverage census and the traces.
inline REXCVAR_DEFINE_INT32(edf_native_loop_trace,0,"EDF2027",
                    "Trace the first N engine heartbeat/update/helper calls with thread and timing for frame-rate decoupling; 0 disables (development)").range(0,10000);
inline REXCVAR_DEFINE_INT32(edf_native_motion_trace,0,"EDF2027",
                    "Trace the first N scene camera submissions and matrix fingerprints; 0 disables (development)").range(0,10000);
inline REXCVAR_DEFINE_INT32(edf_native_instance_motion_trace,0,"EDF2027",
                    "Observe transforms for N scene frames, up to 256 instance and 64 palette sources; 0 disables (development)").range(0,10000);
inline REXCVAR_DEFINE_STRING(edf_native_frame_trace, "", "EDF2027",
  "Optional CSV of swap-boundary wall times; restart to change the output path.");
inline REXCVAR_DEFINE_INT32(edf_native_hook_sample_period,0,"EDF2027",
  "Sample one in N bridge timing scopes (0 disables); independent of full hook/load instrumentation").range(0,4096);
inline REXCVAR_DEFINE_BOOL(edf_native_hook_timings, false, "EDF2027",
                   "Log inclusive CPU wall times for native/original graphics hook phases (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_gpu_timings, false, "EDF2027",
                   "Log GPU time per full-frame pass (sky, models, static_world, effects, transparent, post, view overlays, HUD phases) and per frame from scene-backend timestamps, read back frames later without stalling (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_coverage_census, false, "EDF2027",
                   "Full-frame coverage census: count, per class and reason, what the native passes draw and every object, pass or view they skip that the guest render helper would have drawn; logs 'Native coverage' summaries every edf_native_coverage_census_interval seconds and at exit (tools/coverage-report.py; development)");
inline REXCVAR_DEFINE_INT32(edf_native_coverage_census_interval,30,"EDF2027",
  "Seconds between edf_native_coverage_census summaries").range(1,3600);
inline REXCVAR_DEFINE_BOOL(edf_native_frame_times, false, "EDF2027",
                   "Log present-to-present frame-time percentiles and one line per spike frame (over 25 ms or twice the rolling median) with its pipeline, shader, geometry and texture creations, declined passes and largest hook phases (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_first_use_log, false, "EDF2027",
                   "Log one 'Native first use' line per shader compile, shader disk-cache read, shader wait, pipeline built or waited for on the thread that needed it, and boot precompile batch, with its duration, key and swap number (tools/frame-time-report.py sorts them by mission phase; development)");
inline REXCVAR_DEFINE_BOOL(edf_native_loading_trace, false, "EDF2027",
                   "Sample end-frame publication eligibility and cumulative UI draws; does not capture pixels (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_load_timings, false, "EDF2027",
                   "Log each texture/shader load phase CPU duration, including nested work (development)");
inline REXCVAR_DEFINE_BOOL(edf_native_load_trace, false, "EDF2027",
                   "Loading-screen trace: log the load's phase transitions (mission begin, BeginLoading/EndLoading requests, "
                   "the engine's resource transition, LoadMap, the loading presenter's start and exit) and, per phase and "
                   "every 250 ms while loading, the time per category (file reads and bytes, texture snapshot/guest/create, "
                   "shaders, model construction/publication/retirement, preloads, guest and bridge-mutex waits) split "
                   "engine thread / other threads, plus the engine thread's CPU time; aggregated, no per-call lines (development)");
