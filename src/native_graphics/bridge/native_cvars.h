#pragma once
// The native renderer's cvars (edf_native_*), defined in native_cvars.cpp and grouped as there.
// Cvars defined elsewhere (launcher, host surface, SDK) are declared where they are used.
//
// EDF_NATIVE_CVAR(type, name, default) is the cvar's accessor, inline: REXCVAR_GET(name) is then a load of
// its storage in every translation unit, as it was in the bridge when the bridge defined the cvars, and not a
// call into native_cvars.cpp (the renderer reads them on every hook call). It is token for token the accessor
// the SDK's REXCVAR_DEFINE_* writes, which native_cvars.cpp prefixes with `inline`, so the storage is one
// function-local static program-wide, registered by native_cvars.cpp; the default here must be the one there
// (tools/audit-native-cvar-defaults.cmake checks). edf_native_scene_backend and edf_native_unlock_framerate
// stay plain declarations: test executables define them themselves.
#include <rex/cvar.h>
#include <cstdint>
#include <string>

#ifdef EDF_NATIVE_CVARS_DEFINE
#define EDF_NATIVE_CVAR(type, name, default_val) static_assert(true)  // native_cvars.cpp defines it
#else
#define EDF_NATIVE_CVAR(type, name, default_val) \
  inline type& FLAGS_##name##_storage_() {       \
    static type storage = (default_val);         \
    return storage;                              \
  }                                              \
  static_assert(true)
#endif

// Renderer preset and native ownership switches.
EDF_NATIVE_CVAR(bool, edf_native_shader_bridge, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_material_owned, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_activation_owned, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_geometry_owned, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_instance_owned, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_pass_owned, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_geometry_deferred, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_material_deferred, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_view_owned, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_camera_owned, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_sources_owned, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_selection_owned, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_membership_owned, false);
EDF_NATIVE_CVAR(bool, edf_native_owned_render_state, true);
EDF_NATIVE_CVAR(bool, edf_native_scene_queued, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_preload, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_visibility, false);
EDF_NATIVE_CVAR(bool, edf_native_bucket_dispatch, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_static_walk, false);
EDF_NATIVE_CVAR(bool, edf_native_model_publication, false);
EDF_NATIVE_CVAR(bool, edf_native_render_registry, false);
EDF_NATIVE_CVAR(bool, edf_native_model_pass, false);
EDF_NATIVE_CVAR(bool, edf_native_model_pass_skinned, false);
EDF_NATIVE_CVAR(bool, edf_native_seam_draws, true);
EDF_NATIVE_CVAR(bool, edf_native_post_finish, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_tree, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_tree_published, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_group_order, false);
EDF_NATIVE_CVAR(bool, edf_native_full_frame, false);
EDF_NATIVE_CVAR(bool, edf_native_map_effect_list, false);
EDF_NATIVE_CVAR(bool, edf_native_frame_dispatch, false);
EDF_NATIVE_CVAR(bool, edf_native_material_activation, false);
EDF_NATIVE_CVAR(bool, edf_native_static_world_pass, false);
EDF_NATIVE_CVAR(std::string, edf_native_renderer, "native");

// Backends, devices and caches.
EDF_NATIVE_CVAR(std::string, edf_native_backend, "d3d12");
REXCVAR_DECLARE(std::string, edf_native_scene_backend);
EDF_NATIVE_CVAR(std::string, edf_native_cache_dir, "");
EDF_NATIVE_CVAR(int32_t, edf_native_upload_megabytes, 256);
EDF_NATIVE_CVAR(int32_t, edf_native_geometry_workers, 4);
EDF_NATIVE_CVAR(int32_t, edf_native_frame_operations, 8192);
EDF_NATIVE_CVAR(bool, edf_native_d3d12_debug_layer, false);
EDF_NATIVE_CVAR(bool, edf_native_backend_present, true);
EDF_NATIVE_CVAR(int32_t, edf_native_shader_workers, -1);
EDF_NATIVE_CVAR(bool, edf_native_backend_preview, false);
EDF_NATIVE_CVAR(int32_t, edf_native_frame_latency, 2);

// Render size, sampling, anti-aliasing and FSR.
EDF_NATIVE_CVAR(int32_t, edf_native_anisotropic_filtering, -1);
EDF_NATIVE_CVAR(int32_t, edf_native_render_width, 0);
EDF_NATIVE_CVAR(int32_t, edf_native_msaa, 0);
EDF_NATIVE_CVAR(bool, edf_native_scene_depth_srv, false);
EDF_NATIVE_CVAR(std::string, edf_native_fsr, "off");
EDF_NATIVE_CVAR(double, edf_native_fsr_sharpness, 0.2);
EDF_NATIVE_CVAR(bool, edf_native_motion_vectors, false);
EDF_NATIVE_CVAR(int32_t, edf_native_motion_vectors_debug, 0);
EDF_NATIVE_CVAR(int32_t, edf_native_render_height, 0);
EDF_NATIVE_CVAR(bool, edf_native_pixel_centers, true);

// Reuse, batching and caches between draws and frames.
EDF_NATIVE_CVAR(int32_t, edf_native_geometry_verify_interval, 256);
EDF_NATIVE_CVAR(int32_t, edf_native_geometry_verify_initial, 8);
EDF_NATIVE_CVAR(bool, edf_native_render_registry_idle_skip, true);
EDF_NATIVE_CVAR(bool, edf_native_reuse_material, true);
EDF_NATIVE_CVAR(int32_t, edf_native_preload_workers, -1);
EDF_NATIVE_CVAR(bool, edf_native_registry_overlap, true);
EDF_NATIVE_CVAR(bool, edf_native_owned_mesh_hit, true);
EDF_NATIVE_CVAR(bool, edf_native_world_instancing, true);
EDF_NATIVE_CVAR(bool, edf_native_world_constant_reuse, true);
EDF_NATIVE_CVAR(bool, edf_native_transient_batching, true);
EDF_NATIVE_CVAR(bool, edf_native_prepared_geometry, true);

// Frame pacing, unlocked frame rate and threads.
EDF_NATIVE_CVAR(int32_t, edf_native_wait_stall_ms, 5000);
REXCVAR_DECLARE(bool, edf_native_unlock_framerate);
EDF_NATIVE_CVAR(bool, edf_native_camera_interpolation, true);
EDF_NATIVE_CVAR(bool, edf_native_model_interpolation, true);
EDF_NATIVE_CVAR(int32_t, edf_native_thread_qos, 2);

// Guest memory, fences and frame publication.
EDF_NATIVE_CVAR(bool, edf_native_untiled_scene, true);
EDF_NATIVE_CVAR(bool, edf_native_guest_heap_reads, true);
EDF_NATIVE_CVAR(bool, edf_native_fence_probe, false);
EDF_NATIVE_CVAR(bool, edf_native_validate_wait, false);
EDF_NATIVE_CVAR(bool, edf_native_publish_frames, false);
EDF_NATIVE_CVAR(bool, edf_native_preview_window, false);

// Audits (diagnostic).
EDF_NATIVE_CVAR(bool, edf_native_render_state_audit, false);
EDF_NATIVE_CVAR(bool, edf_native_material_sampler_audit, false);
EDF_NATIVE_CVAR(bool, edf_native_material_state_audit, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_material_audit, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_reject_compatibility, false);
EDF_NATIVE_CVAR(bool, edf_native_worker_callback_audit, false);
EDF_NATIVE_CVAR(bool, edf_native_retirement_audit, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_adapter_audit, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_transform_audit, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_visibility_audit, false);
EDF_NATIVE_CVAR(bool, edf_native_bucket_dispatch_audit, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_static_walk_audit, false);
EDF_NATIVE_CVAR(bool, edf_native_model_publication_audit, false);
EDF_NATIVE_CVAR(bool, edf_native_render_registry_audit, false);
EDF_NATIVE_CVAR(bool, edf_native_model_source_audit, false);
EDF_NATIVE_CVAR(bool, edf_native_render_registry_idle_audit, false);
EDF_NATIVE_CVAR(bool, edf_native_batch_audit, false);
EDF_NATIVE_CVAR(int32_t, edf_native_contract_limit, 4096);
EDF_NATIVE_CVAR(std::string, edf_native_contract_export, "");
EDF_NATIVE_CVAR(bool, edf_native_contract_coverage, false);
EDF_NATIVE_CVAR(int32_t, edf_native_shared_constant_audit, 0);
EDF_NATIVE_CVAR(bool, edf_native_mesh_watch_audit, false);
EDF_NATIVE_CVAR(bool, edf_native_post_finish_audit, false);
EDF_NATIVE_CVAR(bool, edf_native_scene_group_order_audit, false);
EDF_NATIVE_CVAR(bool, edf_native_map_effect_census, false);

// Captures, probes and A/B validation (development).
EDF_NATIVE_CVAR(std::string, edf_native_scene_capture, "");
EDF_NATIVE_CVAR(int32_t, edf_native_output_capture_interval, 0);
EDF_NATIVE_CVAR(int32_t, edf_native_output_capture_limit, 32);
EDF_NATIVE_CVAR(int32_t, edf_native_output_capture_start_frame, 0);
EDF_NATIVE_CVAR(bool, edf_native_output_capture_scene_color, false);
EDF_NATIVE_CVAR(int32_t, edf_native_shadow_render, 0);
EDF_NATIVE_CVAR(int32_t, edf_native_shadow_render_start_frame, 0);
EDF_NATIVE_CVAR(int32_t, edf_native_shadow_render_limit, 16);
EDF_NATIVE_CVAR(std::string, edf_native_shadow_render_prefix, "native-shadow/shadow");
EDF_NATIVE_CVAR(bool, edf_native_shadow_render_constants, false);
EDF_NATIVE_CVAR(bool, edf_native_capture_indexed_state, false);
EDF_NATIVE_CVAR(int32_t, edf_native_probe_x, -1);
EDF_NATIVE_CVAR(int32_t, edf_native_probe_y, -1);
EDF_NATIVE_CVAR(int32_t, edf_native_probe_frame, 0);
EDF_NATIVE_CVAR(int32_t, edf_native_probe_width, 1);
EDF_NATIVE_CVAR(int32_t, edf_native_probe_height, 1);
EDF_NATIVE_CVAR(int32_t, edf_native_probe_draw_limit, 4096);
EDF_NATIVE_CVAR(bool, edf_native_probe_negative, false);
EDF_NATIVE_CVAR(bool, edf_native_reuse_off, false);
EDF_NATIVE_CVAR(int32_t, edf_native_reuse_off_alternate, 0);
EDF_NATIVE_CVAR(int32_t, edf_native_ab_alternate, 0);

// Timings and traces (development).
EDF_NATIVE_CVAR(int32_t, edf_native_loop_trace, 0);
EDF_NATIVE_CVAR(int32_t, edf_native_motion_trace, 0);
EDF_NATIVE_CVAR(int32_t, edf_native_instance_motion_trace, 0);
EDF_NATIVE_CVAR(std::string, edf_native_frame_trace, "");
EDF_NATIVE_CVAR(int32_t, edf_native_hook_sample_period, 0);
EDF_NATIVE_CVAR(bool, edf_native_hook_timings, false);
EDF_NATIVE_CVAR(bool, edf_native_gpu_timings, false);
EDF_NATIVE_CVAR(bool, edf_native_coverage_census, false);
EDF_NATIVE_CVAR(int32_t, edf_native_coverage_census_interval, 30);
EDF_NATIVE_CVAR(bool, edf_native_frame_times, false);
EDF_NATIVE_CVAR(bool, edf_native_loading_trace, false);
EDF_NATIVE_CVAR(bool, edf_native_load_timings, false);
EDF_NATIVE_CVAR(bool, edf_native_load_trace, false);
