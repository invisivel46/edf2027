"""One allowlist shared by the offline runner and dispatch acceptance gate."""
CPU = """edf_renderer_scalar_contract_tests edf_renderer_scalar_analysis_tests
edf_renderer_scalar_pipeline_tests edf_renderer_scalar_effects_tests
edf_renderer_setter_contract_tests edf_renderer_setter_analysis_tests
edf_native_frame_dispatch_tests edf_native_shader_binding_tests
edf_native_material_render_state_tests edf_native_material_sampler_tests
edf_native_capture_policy_tests edf_native_pacing_tests edf_scripted_input_tests
edf_native_backend_tests edf_native_upload_ring_tests edf_native_decode_worker_tests
edf_native_effect_tests edf_native_display_gamma_tests
edf_native_worker_callback_audit_tests edf_native_model_constructor_tests
edf_native_bucket_dispatch_tests edf_native_map_effect_tests edf_native_ab_alternate_tests
edf_native_full_frame_tests
edf_renderer_runtime_gate_tests edf_renderer_image_compare_tests
edf_renderer_ab_capture_tests edf_renderer_ab_postprocess_tests""".split()
RENDER = """edf_native_scene_tests edf_native_backend_completion_tests
edf_native_backend_ui_tests edf_native_backend_compositor_tests
edf_native_backend_conformance_tests edf_native_static_pass_replay_tests
edf_native_immediate_tail_tests""".split()

# --quick mapping: files owned by exactly the listed suites. A changed file in
# no entry and outside QUICK_IGNORED selects every suite (shared production
# code, CMake, generated code and fixtures cannot be attributed safely).
SOURCES = {
    'edf_renderer_scalar_contract_tests': ['tests/renderer_scalar_contract_tests.cpp', 'tools/analyze-renderer-scalars.py'],
    'edf_renderer_scalar_analysis_tests': ['tests/test_renderer_scalar_analysis.py'],
    'edf_renderer_scalar_pipeline_tests': ['tests/renderer_scalar_pipeline_tests.cpp', 'tools/renderer_scalar_effects.py'],
    'edf_renderer_scalar_effects_tests': ['tests/test_renderer_scalar_effects.py', 'tools/renderer_scalar_effects.py'],
    'edf_renderer_setter_contract_tests': ['tests/renderer_setter_contract_tests.cpp', 'tools/renderer_setter_analysis.py'],
    'edf_renderer_setter_analysis_tests': ['tests/test_renderer_setter_analysis.py', 'tools/renderer_setter_analysis.py'],
    'edf_native_frame_dispatch_tests': ['tests/native_frame_dispatch_tests.cpp'],
    'edf_native_shader_binding_tests': ['tests/native_shader_binding_tests.cpp'],
    'edf_native_material_render_state_tests': ['tests/native_material_render_state_tests.cpp'],
    'edf_native_material_sampler_tests': ['tests/native_material_sampler_tests.cpp'],
    'edf_native_capture_policy_tests': ['tests/native_capture_policy_tests.cpp'],
    'edf_native_pacing_tests': ['tests/native_pacing_tests.cpp'],
    'edf_scripted_input_tests': ['tests/scripted_input_reload_tests.cpp'],
    'edf_native_backend_tests': ['tests/native_backend_tests.cpp'],
    'edf_native_upload_ring_tests': ['tests/native_upload_ring_tests.cpp'],
    'edf_native_decode_worker_tests': ['tests/native_decode_workers_tests.cpp'],
    'edf_native_effect_tests': ['tests/native_effect_tests.cpp'],
    'edf_native_display_gamma_tests': ['tests/native_display_gamma_tests.cpp'],
    'edf_native_worker_callback_audit_tests': ['tests/native_worker_callback_audit_tests.cpp',
                                               'tools/extract-worker-callback-audit-test.cmake'],
    'edf_native_model_constructor_tests': ['tests/native_model_constructor_tests.cpp',
                                           'tools/extract-model-constructor-test.cmake'],
    'edf_native_bucket_dispatch_tests': ['tests/native_bucket_dispatch_tests.cpp'],
    'edf_native_map_effect_tests': ['tests/native_map_effect_tests.cpp'],
    'edf_native_ab_alternate_tests': ['tests/native_ab_alternate_tests.cpp'],
    'edf_native_full_frame_tests': ['tests/native_full_frame_tests.cpp'],
    'edf_renderer_runtime_gate_tests': ['tests/test_renderer_runtime_gate.py', 'tools/renderer-runtime-gate.py'],
    'edf_renderer_image_compare_tests': ['tests/test_compare_renderer_images.py', 'tools/compare-renderer-images.py'],
    'edf_renderer_ab_capture_tests': ['tools/test_compare_renderer_ab_captures.py', 'tools/compare-renderer-ab-captures.py'],
    'edf_renderer_ab_postprocess_tests': ['tools/test_renderer_ab_postprocess.py', 'tools/renderer-ab-postprocess.py'],
    'edf_native_scene_tests': ['tests/native_scene_tests.cpp'],
    'edf_native_backend_completion_tests': ['tests/native_backend_completion_tests.cpp'],
    'edf_native_backend_ui_tests': ['tests/native_backend_ui_tests.cpp'],
    'edf_native_backend_compositor_tests': ['tests/native_backend_compositor_tests.cpp'],
    'edf_native_backend_conformance_tests': ['tests/native_backend_conformance_tests.cpp'],
    'edf_native_static_pass_replay_tests': ['tests/native_static_pass_replay_tests.cpp',
                                            'tests/fixtures/static-pass-replay-v1.txt',
                                            'tests/native_static_group_gpu.cpp', 'tests/native_static_group_gpu.h'],
    'edf_native_immediate_tail_tests': ['tests/native_immediate_tail_tests.cpp',
                                        'tests/native_static_group_gpu.cpp', 'tests/native_static_group_gpu.h'],
}
# Changes here never select a suite: prose, the knowledge base and build output.
QUICK_IGNORED_PREFIXES = ('docs/', 'knowledge/', 'out/')
QUICK_IGNORED_SUFFIXES = ('.md',)


def quick_select(changed):
    """Suites to run for changed repo-relative paths, in allowlist order."""
    owners = {}
    for suite, paths in SOURCES.items():
        for path in paths:
            owners.setdefault(path, set()).add(suite)
    chosen = set()
    for path in changed:
        path = path.replace('\\', '/')
        if path in owners:
            chosen |= owners[path]
        elif path.startswith(QUICK_IGNORED_PREFIXES) or path.endswith(QUICK_IGNORED_SUFFIXES):
            continue
        else:
            return CPU + RENDER
    return [suite for suite in CPU + RENDER if suite in chosen]
