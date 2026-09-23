#pragma once
// The native renderer's cvars (edf_native_*), defined in native_cvars.cpp and grouped as there.
// Cvars defined elsewhere (launcher, host surface, SDK) are declared where they are used.
#include <rex/cvar.h>
#include <cstdint>
#include <string>

// Renderer preset and native ownership switches.
REXCVAR_DECLARE(bool, edf_native_shader_bridge);
REXCVAR_DECLARE(bool, edf_native_scene_material_owned);
REXCVAR_DECLARE(bool, edf_native_scene_activation_owned);
REXCVAR_DECLARE(bool, edf_native_scene_geometry_owned);
REXCVAR_DECLARE(bool, edf_native_scene_instance_owned);
REXCVAR_DECLARE(bool, edf_native_scene_pass_owned);
REXCVAR_DECLARE(bool, edf_native_scene_geometry_deferred);
REXCVAR_DECLARE(bool, edf_native_scene_material_deferred);
REXCVAR_DECLARE(bool, edf_native_scene_view_owned);
REXCVAR_DECLARE(bool, edf_native_scene_camera_owned);
REXCVAR_DECLARE(bool, edf_native_scene_sources_owned);
REXCVAR_DECLARE(bool, edf_native_scene_selection_owned);
REXCVAR_DECLARE(bool, edf_native_scene_membership_owned);
REXCVAR_DECLARE(bool, edf_native_owned_render_state);
REXCVAR_DECLARE(bool, edf_native_scene_queued);
REXCVAR_DECLARE(bool, edf_native_scene_preload);
REXCVAR_DECLARE(bool, edf_native_scene_visibility);
REXCVAR_DECLARE(bool, edf_native_bucket_dispatch);
REXCVAR_DECLARE(bool, edf_native_scene_static_walk);
REXCVAR_DECLARE(bool, edf_native_model_publication);
REXCVAR_DECLARE(bool, edf_native_render_registry);
REXCVAR_DECLARE(bool, edf_native_model_pass);
REXCVAR_DECLARE(bool, edf_native_model_pass_skinned);
REXCVAR_DECLARE(bool, edf_native_seam_draws);
REXCVAR_DECLARE(bool, edf_native_post_finish);
REXCVAR_DECLARE(bool, edf_native_scene_tree);
REXCVAR_DECLARE(bool, edf_native_scene_tree_published);
REXCVAR_DECLARE(bool, edf_native_scene_group_order);
REXCVAR_DECLARE(bool, edf_native_full_frame);
REXCVAR_DECLARE(bool, edf_native_map_effect_list);
REXCVAR_DECLARE(bool, edf_native_frame_dispatch);
REXCVAR_DECLARE(bool, edf_native_material_activation);
REXCVAR_DECLARE(bool, edf_native_static_world_pass);
REXCVAR_DECLARE(std::string, edf_native_renderer);

// Backends, devices and caches.
REXCVAR_DECLARE(std::string, edf_native_backend);
REXCVAR_DECLARE(std::string, edf_native_scene_backend);
REXCVAR_DECLARE(std::string, edf_native_cache_dir);
REXCVAR_DECLARE(int32_t, edf_native_upload_megabytes);
REXCVAR_DECLARE(int32_t, edf_native_geometry_workers);
REXCVAR_DECLARE(int32_t, edf_native_frame_operations);
REXCVAR_DECLARE(bool, edf_native_d3d12_debug_layer);
REXCVAR_DECLARE(bool, edf_native_backend_present);
REXCVAR_DECLARE(int32_t, edf_native_shader_workers);
REXCVAR_DECLARE(bool, edf_native_backend_preview);
REXCVAR_DECLARE(int32_t, edf_native_frame_latency);

// Render size, sampling, anti-aliasing and FSR.
REXCVAR_DECLARE(int32_t, edf_native_anisotropic_filtering);
REXCVAR_DECLARE(int32_t, edf_native_render_width);
REXCVAR_DECLARE(int32_t, edf_native_msaa);
REXCVAR_DECLARE(bool, edf_native_scene_depth_srv);
REXCVAR_DECLARE(std::string, edf_native_fsr);
REXCVAR_DECLARE(double, edf_native_fsr_sharpness);
REXCVAR_DECLARE(bool, edf_native_motion_vectors);
REXCVAR_DECLARE(int32_t, edf_native_motion_vectors_debug);
REXCVAR_DECLARE(int32_t, edf_native_render_height);
REXCVAR_DECLARE(bool, edf_native_pixel_centers);

// Reuse, batching and caches between draws and frames.
REXCVAR_DECLARE(int32_t, edf_native_geometry_verify_interval);
REXCVAR_DECLARE(int32_t, edf_native_geometry_verify_initial);
REXCVAR_DECLARE(bool, edf_native_render_registry_idle_skip);
REXCVAR_DECLARE(bool, edf_native_reuse_material);
REXCVAR_DECLARE(int32_t, edf_native_preload_workers);
REXCVAR_DECLARE(bool, edf_native_registry_overlap);
REXCVAR_DECLARE(bool, edf_native_owned_mesh_hit);
REXCVAR_DECLARE(bool, edf_native_world_instancing);
REXCVAR_DECLARE(bool, edf_native_world_constant_reuse);
REXCVAR_DECLARE(bool, edf_native_transient_batching);
REXCVAR_DECLARE(bool, edf_native_prepared_geometry);

// Frame pacing, unlocked frame rate and threads.
REXCVAR_DECLARE(int32_t, edf_native_wait_stall_ms);
REXCVAR_DECLARE(bool, edf_native_unlock_framerate);
REXCVAR_DECLARE(bool, edf_native_camera_interpolation);
REXCVAR_DECLARE(bool, edf_native_model_interpolation);
REXCVAR_DECLARE(int32_t, edf_native_thread_qos);

// Guest memory, fences and frame publication.
REXCVAR_DECLARE(bool, edf_native_untiled_scene);
REXCVAR_DECLARE(bool, edf_native_guest_heap_reads);
REXCVAR_DECLARE(bool, edf_native_fence_probe);
REXCVAR_DECLARE(bool, edf_native_validate_wait);
REXCVAR_DECLARE(bool, edf_native_publish_frames);
REXCVAR_DECLARE(bool, edf_native_preview_window);

// Audits (diagnostic).
REXCVAR_DECLARE(bool, edf_native_render_state_audit);
REXCVAR_DECLARE(bool, edf_native_material_sampler_audit);
REXCVAR_DECLARE(bool, edf_native_material_state_audit);
REXCVAR_DECLARE(bool, edf_native_scene_material_audit);
REXCVAR_DECLARE(bool, edf_native_scene_reject_compatibility);
REXCVAR_DECLARE(bool, edf_native_worker_callback_audit);
REXCVAR_DECLARE(bool, edf_native_retirement_audit);
REXCVAR_DECLARE(bool, edf_native_scene_adapter_audit);
REXCVAR_DECLARE(bool, edf_native_scene_transform_audit);
REXCVAR_DECLARE(bool, edf_native_scene_visibility_audit);
REXCVAR_DECLARE(bool, edf_native_bucket_dispatch_audit);
REXCVAR_DECLARE(bool, edf_native_scene_static_walk_audit);
REXCVAR_DECLARE(bool, edf_native_model_publication_audit);
REXCVAR_DECLARE(bool, edf_native_render_registry_audit);
REXCVAR_DECLARE(bool, edf_native_model_source_audit);
REXCVAR_DECLARE(bool, edf_native_render_registry_idle_audit);
REXCVAR_DECLARE(bool, edf_native_batch_audit);
REXCVAR_DECLARE(int32_t, edf_native_contract_limit);
REXCVAR_DECLARE(std::string, edf_native_contract_export);
REXCVAR_DECLARE(bool, edf_native_contract_coverage);
REXCVAR_DECLARE(int32_t, edf_native_shared_constant_audit);
REXCVAR_DECLARE(bool, edf_native_mesh_watch_audit);
REXCVAR_DECLARE(bool, edf_native_post_finish_audit);
REXCVAR_DECLARE(bool, edf_native_scene_group_order_audit);
REXCVAR_DECLARE(bool, edf_native_map_effect_census);

// Captures, probes and A/B validation (development).
REXCVAR_DECLARE(std::string, edf_native_scene_capture);
REXCVAR_DECLARE(int32_t, edf_native_output_capture_interval);
REXCVAR_DECLARE(int32_t, edf_native_output_capture_limit);
REXCVAR_DECLARE(int32_t, edf_native_output_capture_start_frame);
REXCVAR_DECLARE(bool, edf_native_output_capture_scene_color);
REXCVAR_DECLARE(int32_t, edf_native_shadow_render);
REXCVAR_DECLARE(int32_t, edf_native_shadow_render_start_frame);
REXCVAR_DECLARE(int32_t, edf_native_shadow_render_limit);
REXCVAR_DECLARE(std::string, edf_native_shadow_render_prefix);
REXCVAR_DECLARE(bool, edf_native_shadow_render_constants);
REXCVAR_DECLARE(bool, edf_native_capture_indexed_state);
REXCVAR_DECLARE(int32_t, edf_native_probe_x);
REXCVAR_DECLARE(int32_t, edf_native_probe_y);
REXCVAR_DECLARE(int32_t, edf_native_probe_frame);
REXCVAR_DECLARE(int32_t, edf_native_probe_width);
REXCVAR_DECLARE(int32_t, edf_native_probe_height);
REXCVAR_DECLARE(int32_t, edf_native_probe_draw_limit);
REXCVAR_DECLARE(bool, edf_native_probe_negative);
REXCVAR_DECLARE(bool, edf_native_reuse_off);
REXCVAR_DECLARE(int32_t, edf_native_reuse_off_alternate);
REXCVAR_DECLARE(int32_t, edf_native_ab_alternate);

// Timings and traces (development).
REXCVAR_DECLARE(int32_t, edf_native_loop_trace);
REXCVAR_DECLARE(int32_t, edf_native_motion_trace);
REXCVAR_DECLARE(int32_t, edf_native_instance_motion_trace);
REXCVAR_DECLARE(std::string, edf_native_frame_trace);
REXCVAR_DECLARE(int32_t, edf_native_hook_sample_period);
REXCVAR_DECLARE(bool, edf_native_hook_timings);
REXCVAR_DECLARE(bool, edf_native_gpu_timings);
REXCVAR_DECLARE(bool, edf_native_coverage_census);
REXCVAR_DECLARE(int32_t, edf_native_coverage_census_interval);
REXCVAR_DECLARE(bool, edf_native_frame_times);
REXCVAR_DECLARE(bool, edf_native_loading_trace);
REXCVAR_DECLARE(bool, edf_native_load_timings);
REXCVAR_DECLARE(bool, edf_native_load_trace);
