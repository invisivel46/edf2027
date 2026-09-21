# EDF2027 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(edf2027 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(EDF2027_NATIVE_GRAPHICS_ONLY "Build native graphics tools without ReXGlue or Xenos" OFF)
add_subdirectory(src/native_graphics)
if(EDF2027_NATIVE_GRAPHICS_ONLY)
    return()
endif()

if(NOT WIN32)
    message(FATAL_ERROR "The native game renderer currently requires Windows/D3D12. Native analysis tools remain available with EDF2027_NATIVE_GRAPHICS_ONLY=ON.")
endif()

include(generated/rexglue.cmake)

# Sources
set(EDF2027_SOURCES
    src/main.cpp
    src/input_hooks.cpp
    src/launcher_cvars.cpp
    src/native_kbm.cpp
)

if(WIN32)
    add_library(edf_native_sdk_ui STATIC src/native_graphics/native_immediate_drawer.cpp src/native_graphics/native_backend_immediate_drawer.cpp)
    target_link_libraries(edf_native_sdk_ui PUBLIC edf_native_d3d11 rex::runtime)
    add_executable(edf2027 WIN32 ${EDF2027_SOURCES})
    target_sources(edf2027 PRIVATE src/native_graphics/guest_shader_bridge.cpp)
    target_sources(edf2027 PRIVATE src/native_graphics/native_fallback_imports.cpp)
    target_sources(edf2027 PRIVATE src/native_graphics/native_preview_window.cpp)
    target_sources(edf2027 PRIVATE src/native_graphics/native_d3d12_preview.cpp)
    target_sources(edf2027 PRIVATE src/native_graphics/native_host_surface.cpp src/native_graphics/native_backend_host.cpp)
    target_link_libraries(edf2027 PRIVATE comctl32)
    target_link_libraries(edf2027 PRIVATE gdi32)
    target_link_libraries(edf2027 PRIVATE edf_native_d3d11)
    target_link_libraries(edf2027 PRIVATE edf_native_d3d12)
    target_link_libraries(edf2027 PRIVATE edf_native_sdk_ui)
    # Keep the explicit D3D11 fallback dormant: the default runtime must not
    # load D3D11 merely because its fallback factory is linked into the binary.
    target_link_libraries(edf2027 PRIVATE delayimp)
    target_link_options(edf2027 PRIVATE "LINKER:/DELAYLOAD:d3d11.dll")
else()
    add_executable(edf2027 ${EDF2027_SOURCES})
endif()

# The host owns native D3D12 rendering. Do not request an SDK GPU emulation plugin.
rexglue_setup_target(edf2027)

# Allow an independent candidate while the default executable is being playtested.
set(EDF2027_EXECUTABLE_NAME "edf2027" CACHE STRING "Game executable basename (no directory or extension required)")
if(NOT EDF2027_EXECUTABLE_NAME MATCHES "^[A-Za-z0-9][A-Za-z0-9_.-]*$")
    message(FATAL_ERROR "EDF2027_EXECUTABLE_NAME must be a simple executable basename")
endif()
set_target_properties(edf2027 PROPERTIES OUTPUT_NAME "${EDF2027_EXECUTABLE_NAME}")

# Read-only import/staging check, including transitive non-system dependencies.
# Run from the VS developer environment so CMake can use dumpbin.
add_custom_target(audit_native_dependencies
    COMMAND ${CMAKE_COMMAND} -DEXECUTABLE=$<TARGET_FILE:edf2027>
        -P ${CMAKE_SOURCE_DIR}/tools/audit-native-dependencies.cmake
    DEPENDS edf2027
    COMMENT "Auditing native game's packaged PE dependency closure"
    VERBATIM)

# Route direct and registered file-read/word-fill imports through native
# producer adapters. Keep this private to generated code: each adapter
# itself calls the SDK's original symbol without recursion. Do not edit codegen.
target_compile_definitions(edf2027_recomp PRIVATE __imp__NtReadFile=edf_native_NtReadFile)
target_compile_definitions(edf2027_recomp PRIVATE __imp__RtlFillMemoryUlong=edf_native_RtlFillMemoryUlong)
option(EDF2027_OBSERVE_SCALAR_STORES "Diagnostic generated-store notifications (native providers still need coverage)" OFF)
if(EDF2027_OBSERVE_SCALAR_STORES)
    target_precompile_headers(edf2027_recomp PRIVATE
        "${CMAKE_SOURCE_DIR}/src/native_graphics/guest_raw_store_observer.h")
endif()

set(_native_indexed_tail ${CMAKE_CURRENT_BINARY_DIR}/native_indexed_cpu_tail.cpp)
add_custom_command(OUTPUT ${_native_indexed_tail}
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${_native_indexed_tail}
        -P ${CMAKE_SOURCE_DIR}/tools/extract-native-indexed-tail.cmake
    DEPENDS generated/default/edf2017_recomp.48.cpp tools/extract-native-indexed-tail.cmake tools/native-draw-prefix.cmake VERBATIM)
target_sources(edf2027 PRIVATE ${_native_indexed_tail})
set(_native_immediate_tail ${CMAKE_CURRENT_BINARY_DIR}/native_immediate_cpu_tail.cpp)
add_custom_command(OUTPUT ${_native_immediate_tail}
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${_native_immediate_tail}
        -P ${CMAKE_SOURCE_DIR}/tools/extract-native-immediate-tail.cmake
    DEPENDS generated/default/edf2017_recomp.42.cpp generated/default/edf2017_recomp.10.cpp tools/extract-native-immediate-tail.cmake tools/native-draw-prefix.cmake VERBATIM)
target_sources(edf2027 PRIVATE ${_native_immediate_tail})
set(_native_derived_tail ${CMAKE_CURRENT_BINARY_DIR}/native_derived_cpu_tail.cpp)
add_custom_command(OUTPUT ${_native_derived_tail}
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${_native_derived_tail}
        -P ${CMAKE_SOURCE_DIR}/tools/extract-native-derived-tail.cmake
    DEPENDS generated/default/edf2017_recomp.54.cpp tools/extract-native-derived-tail.cmake VERBATIM)
target_sources(edf2027 PRIVATE ${_native_derived_tail})
set(_native_shader_output_tail ${CMAKE_CURRENT_BINARY_DIR}/native_shader_output_cpu_tail.cpp)
add_custom_command(OUTPUT ${_native_shader_output_tail}
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${_native_shader_output_tail}
        -P ${CMAKE_SOURCE_DIR}/tools/extract-native-shader-output.cmake
    DEPENDS generated/default/edf2017_recomp.66.cpp tools/extract-native-shader-output.cmake VERBATIM)
target_sources(edf2027 PRIVATE ${_native_shader_output_tail})
set(_native_shader_upload_tail ${CMAKE_CURRENT_BINARY_DIR}/native_shader_upload_cpu_tail.cpp)
add_custom_command(OUTPUT ${_native_shader_upload_tail}
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${_native_shader_upload_tail}
        -P ${CMAKE_SOURCE_DIR}/tools/extract-native-shader-upload.cmake
    DEPENDS generated/default/edf2017_recomp.50.cpp tools/extract-native-shader-upload.cmake VERBATIM)
target_sources(edf2027 PRIVATE ${_native_shader_upload_tail})
set(_native_main_state_tail ${CMAKE_CURRENT_BINARY_DIR}/native_main_state_cpu_tail.cpp)
add_custom_command(OUTPUT ${_native_main_state_tail}
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${_native_main_state_tail}
        -P ${CMAKE_SOURCE_DIR}/tools/extract-native-main-state.cmake
    DEPENDS generated/default/edf2017_recomp.17.cpp tools/extract-native-main-state.cmake VERBATIM)
target_sources(edf2027 PRIVATE ${_native_main_state_tail})
set(_native_shader_cache_tail ${CMAKE_CURRENT_BINARY_DIR}/native_shader_cache_cpu_tail.cpp)
add_custom_command(OUTPUT ${_native_shader_cache_tail}
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${_native_shader_cache_tail}
        -P ${CMAKE_SOURCE_DIR}/tools/extract-native-shader-cache.cmake
    DEPENDS generated/default/edf2017_recomp.32.cpp tools/extract-native-shader-cache.cmake VERBATIM)
target_sources(edf2027 PRIVATE ${_native_shader_cache_tail})
set(_native_buffer_unlock ${CMAKE_CURRENT_BINARY_DIR}/native_buffer_unlock.cpp)
add_custom_command(OUTPUT ${_native_buffer_unlock}
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${_native_buffer_unlock}
        -P ${CMAKE_SOURCE_DIR}/tools/extract-native-buffer-unlock.cmake
    DEPENDS generated/default/edf2017_recomp.44.cpp generated/default/edf2017_recomp.71.cpp
        generated/default/edf2017_recomp.16.cpp tools/extract-native-buffer-unlock.cmake VERBATIM)
target_sources(edf2027 PRIVATE ${_native_buffer_unlock})
set(_native_device_reset ${CMAKE_CURRENT_BINARY_DIR}/native_device_reset.cpp)
add_custom_command(OUTPUT ${_native_device_reset}
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${_native_device_reset}
        -P ${CMAKE_SOURCE_DIR}/tools/extract-native-device-reset.cmake
    DEPENDS generated/default/edf2017_recomp.38.cpp generated/default/edf2017_recomp.7.cpp tools/extract-native-device-reset.cmake VERBATIM)
target_sources(edf2027 PRIVATE ${_native_device_reset})
set(_native_cache_range ${CMAKE_CURRENT_BINARY_DIR}/native_cache_range.cpp)
add_custom_command(OUTPUT ${_native_cache_range}
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${_native_cache_range}
        -P ${CMAKE_SOURCE_DIR}/tools/extract-native-cache-range.cmake
    DEPENDS generated/default/edf2017_recomp.4.cpp tools/extract-native-cache-range.cmake VERBATIM)
target_sources(edf2027 PRIVATE ${_native_cache_range})
set(_native_buffer_lock ${CMAKE_CURRENT_BINARY_DIR}/native_buffer_lock.cpp)
add_custom_command(OUTPUT ${_native_buffer_lock}
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${_native_buffer_lock}
        -P ${CMAKE_SOURCE_DIR}/tools/extract-native-buffer-lock.cmake
    DEPENDS generated/default/edf2017_recomp.74.cpp generated/default/edf2017_recomp.24.cpp
        generated/default/edf2017_recomp.76.cpp tools/extract-native-buffer-lock.cmake VERBATIM)
target_sources(edf2027 PRIVATE ${_native_buffer_lock})
set(_native_ring_submit ${CMAKE_CURRENT_BINARY_DIR}/native_ring_submit.cpp)
add_custom_command(OUTPUT ${_native_ring_submit}
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${_native_ring_submit}
        -P ${CMAKE_SOURCE_DIR}/tools/extract-native-ring-submit.cmake
    DEPENDS generated/default/edf2017_recomp.33.cpp generated/default/edf2017_recomp.31.cpp generated/default/edf2017_recomp.19.cpp tools/extract-native-ring-submit.cmake VERBATIM)
target_sources(edf2027 PRIVATE ${_native_ring_submit})
set(_native_worker_audit ${CMAKE_CURRENT_BINARY_DIR}/native_worker_audit.cpp)
add_custom_command(OUTPUT ${_native_worker_audit}
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${_native_worker_audit}
        -P ${CMAKE_SOURCE_DIR}/tools/extract-native-worker-audit.cmake
    DEPENDS generated/default/edf2017_recomp.14.cpp generated/default/edf2017_recomp.73.cpp tools/extract-native-worker-audit.cmake VERBATIM)
target_sources(edf2027 PRIVATE ${_native_worker_audit})
set(_native_worker_init ${CMAKE_CURRENT_BINARY_DIR}/native_worker_init.cpp)
add_custom_command(OUTPUT ${_native_worker_init}
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${_native_worker_init}
        -P ${CMAKE_SOURCE_DIR}/tools/extract-native-worker-init.cmake
    DEPENDS generated/default/edf2017_recomp.51.cpp tools/extract-native-worker-init.cmake VERBATIM)
target_sources(edf2027 PRIVATE ${_native_worker_init})
set(_native_device_defaults ${CMAKE_CURRENT_BINARY_DIR}/native_device_defaults.cpp)
add_custom_command(OUTPUT ${_native_device_defaults}
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${_native_device_defaults}
        -P ${CMAKE_SOURCE_DIR}/tools/extract-native-device-defaults.cmake
    DEPENDS generated/default/edf2017_recomp.19.cpp tools/extract-native-device-defaults.cmake VERBATIM)
target_sources(edf2027 PRIVATE ${_native_device_defaults})
set(_native_present ${CMAKE_CURRENT_BINARY_DIR}/native_present.cpp)
add_custom_command(OUTPUT ${_native_present}
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${_native_present}
        -P ${CMAKE_SOURCE_DIR}/tools/extract-native-present.cmake
    DEPENDS generated/default/edf2017_recomp.26.cpp generated/default/edf2017_recomp.15.cpp
        generated/default/edf2017_recomp.27.cpp generated/default/edf2017_recomp.45.cpp
        generated/default/edf2017_recomp.71.cpp generated/default/edf2017_recomp.54.cpp
        generated/default/edf2017_recomp.66.cpp generated/default/edf2017_recomp.7.cpp
        generated/default/edf2017_recomp.41.cpp generated/default/edf2017_recomp.10.cpp
        generated/default/edf2017_recomp.69.cpp generated/default/edf2017_recomp.32.cpp
        generated/default/edf2017_recomp.40.cpp generated/default/edf2017_recomp.2.cpp
        generated/default/edf2017_recomp.56.cpp generated/default/edf2017_recomp.39.cpp
        generated/default/edf2017_recomp.65.cpp generated/default/edf2017_recomp.8.cpp generated/default/edf2017_recomp.25.cpp
        tools/extract-native-present.cmake tools/extract-native-counter-reset.cmake VERBATIM)
target_sources(edf2027 PRIVATE ${_native_present})

if(BUILD_TESTING)
    add_test(NAME edf_native_hook_boundary_audit
      COMMAND ${CMAKE_COMMAND}
        -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/src/native_graphics/guest_shader_bridge.cpp
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/audit-native-hook-boundaries.cmake)
    add_executable(edf_native_display_gamma_tests tests/native_display_gamma_tests.cpp)
    set(_worker_callback_fixture ${CMAKE_CURRENT_BINARY_DIR}/native_worker_callback_audit_fixture.inc)
    add_custom_command(OUTPUT ${_worker_callback_fixture}
        COMMAND ${CMAKE_COMMAND} -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/src/native_graphics/guest_shader_bridge.cpp
            -DOUTPUT=${_worker_callback_fixture} -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/extract-worker-callback-audit-test.cmake
        DEPENDS src/native_graphics/guest_shader_bridge.cpp tools/extract-worker-callback-audit-test.cmake VERBATIM)
    add_executable(edf_native_worker_callback_audit_tests tests/native_worker_callback_audit_tests.cpp ${_worker_callback_fixture})
    target_include_directories(edf_native_worker_callback_audit_tests PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    add_test(NAME edf_native_worker_callback_audit_tests COMMAND edf_native_worker_callback_audit_tests)
    set(_model_constructor_fixture ${CMAKE_CURRENT_BINARY_DIR}/native_model_constructor_fixture.inc)
    add_custom_command(OUTPUT ${_model_constructor_fixture}
        COMMAND ${CMAKE_COMMAND} -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/src/native_graphics/guest_shader_bridge.cpp
            -DOUTPUT=${_model_constructor_fixture} -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/extract-model-constructor-test.cmake
        DEPENDS src/native_graphics/guest_shader_bridge.cpp tools/extract-model-constructor-test.cmake VERBATIM)
    add_executable(edf_native_model_constructor_tests tests/native_model_constructor_tests.cpp ${_model_constructor_fixture})
    target_include_directories(edf_native_model_constructor_tests PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME edf_native_model_constructor_tests COMMAND edf_native_model_constructor_tests)
    target_include_directories(edf_native_display_gamma_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME edf_native_display_gamma_tests COMMAND edf_native_display_gamma_tests)
    if(WIN32)
        set(_native_present_fixture ${CMAKE_CURRENT_BINARY_DIR}/native_present_fixture.cpp)
        add_custom_command(OUTPUT ${_native_present_fixture}
            COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${_native_present_fixture}
                -DTEST_MODE=ON -P ${CMAKE_SOURCE_DIR}/tools/extract-native-present.cmake
            DEPENDS generated/default/edf2017_recomp.26.cpp generated/default/edf2017_recomp.58.cpp
                generated/default/edf2017_recomp.6.cpp generated/default/edf2017_recomp.15.cpp
                generated/default/edf2017_recomp.27.cpp generated/default/edf2017_recomp.17.cpp generated/default/edf2017_recomp.25.cpp
                generated/default/edf2017_recomp.45.cpp generated/default/edf2017_recomp.71.cpp
                generated/default/edf2017_recomp.68.cpp generated/default/edf2017_recomp.28.cpp generated/default/edf2017_recomp.54.cpp
                generated/default/edf2017_recomp.66.cpp generated/default/edf2017_recomp.7.cpp generated/default/edf2017_recomp.41.cpp
                generated/default/edf2017_recomp.24.cpp generated/default/edf2017_recomp.34.cpp
                generated/default/edf2017_recomp.42.cpp generated/default/edf2017_recomp.10.cpp
                generated/default/edf2017_recomp.69.cpp generated/default/edf2017_recomp.75.cpp generated/default/edf2017_recomp.13.cpp
                generated/default/edf2017_recomp.32.cpp generated/default/edf2017_recomp.40.cpp
                generated/default/edf2017_recomp.2.cpp generated/default/edf2017_recomp.56.cpp
                generated/default/edf2017_recomp.39.cpp generated/default/edf2017_recomp.65.cpp generated/default/edf2017_recomp.8.cpp
                generated/default/edf2017_recomp.23.cpp tools/extract-native-pix-test.cmake
                tools/extract-native-present.cmake tools/extract-native-counter-reset.cmake VERBATIM)
        add_executable(edf_native_present_tail_tests tests/native_present_tail_tests.cpp ${_native_present_fixture})
        target_include_directories(edf_native_present_tail_tests PRIVATE ${CMAKE_SOURCE_DIR}/generated/default ${CMAKE_SOURCE_DIR}/src)
        target_link_libraries(edf_native_present_tail_tests PRIVATE rex::runtime)
        add_test(NAME edf_native_present_tail_tests COMMAND edf_native_present_tail_tests)
        add_executable(edf_native_sdk_ui_tests tests/native_sdk_ui_tests.cpp)
        set(_native_tail_fixture ${CMAKE_CURRENT_BINARY_DIR}/native_immediate_tail_fixture.cpp)
        set(_native_tail_shards ${CMAKE_SOURCE_DIR}/generated/default/edf2017_recomp.74.cpp ${CMAKE_SOURCE_DIR}/generated/default/edf2017_recomp.46.cpp)
        foreach(_shard 42 10 12 17 25 50 48 7 16 15 75 13 31 4 22 14 20 72 73 58 6 54 66 65 34 24 5 32 38 39 68 28 62 11 52 33 3 29 19 51 44 71)
            list(APPEND _native_tail_shards ${CMAKE_SOURCE_DIR}/generated/default/edf2017_recomp.${_shard}.cpp)
        endforeach()
        add_custom_command(OUTPUT ${_native_tail_fixture}
            COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${_native_tail_fixture}
                -P ${CMAKE_SOURCE_DIR}/tools/extract-native-tail-test.cmake
            DEPENDS ${_native_tail_shards} tools/extract-native-tail-test.cmake src/native_graphics/guest_shader_bridge.cpp VERBATIM)
        add_executable(edf_native_immediate_tail_tests tests/native_immediate_tail_tests.cpp ${_native_tail_fixture} ${_native_indexed_tail} ${_native_derived_tail} ${_native_shader_output_tail} ${_native_shader_upload_tail} ${_native_main_state_tail} ${_native_device_reset} ${_native_cache_range} ${_native_ring_submit})
        target_sources(edf_native_immediate_tail_tests PRIVATE ${_native_immediate_tail})
        target_sources(edf_native_immediate_tail_tests PRIVATE ${_native_shader_cache_tail})
        target_sources(edf_native_immediate_tail_tests PRIVATE ${_native_buffer_unlock})
        target_include_directories(edf_native_immediate_tail_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/generated/default)
        target_sources(edf_native_immediate_tail_tests PRIVATE ${_native_buffer_lock})
        target_sources(edf_native_immediate_tail_tests PRIVATE ${_native_worker_init})
        target_sources(edf_native_immediate_tail_tests PRIVATE ${_native_device_defaults})
        target_link_libraries(edf_native_immediate_tail_tests PRIVATE rex::runtime)
        add_test(NAME edf_native_immediate_tail_tests COMMAND edf_native_immediate_tail_tests)
        target_link_libraries(edf_native_sdk_ui_tests PRIVATE edf_native_sdk_ui)
        add_test(NAME edf_native_sdk_ui_tests COMMAND edf_native_sdk_ui_tests)
        add_executable(edf_native_backend_sdk_ui_tests tests/native_backend_sdk_ui_tests.cpp
            src/native_graphics/native_fallback_imports.cpp)
        target_link_libraries(edf_native_backend_sdk_ui_tests PRIVATE edf_native_sdk_ui delayimp)
        target_link_options(edf_native_backend_sdk_ui_tests PRIVATE "LINKER:/DELAYLOAD:d3d11.dll")
        add_test(NAME edf_native_backend_sdk_ui_tests COMMAND edf_native_backend_sdk_ui_tests)
        add_executable(edf_native_backend_host_tests tests/native_backend_host_tests.cpp
            src/native_graphics/native_backend_host.cpp)
        target_link_libraries(edf_native_backend_host_tests PRIVATE edf_native_d3d11 rex::runtime comctl32)
        add_test(NAME edf_native_backend_host_tests COMMAND edf_native_backend_host_tests)
        set_tests_properties(edf_native_backend_host_tests PROPERTIES TIMEOUT 30)
        add_executable(edf_native_host_lifetime_tests tests/native_host_lifetime_tests.cpp
            src/native_graphics/native_host_surface.cpp)
        target_link_libraries(edf_native_host_lifetime_tests PRIVATE edf_native_d3d11 rex::runtime comctl32)
        add_test(NAME edf_native_host_lifetime_tests COMMAND edf_native_host_lifetime_tests)
        set_tests_properties(edf_native_host_lifetime_tests PROPERTIES TIMEOUT 30)
        add_executable(edf_native_guest_memory_tests tests/native_guest_memory_tests.cpp)
        target_include_directories(edf_native_guest_memory_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
        target_link_libraries(edf_native_guest_memory_tests PRIVATE rex::runtime)
        add_test(NAME edf_native_guest_memory_tests COMMAND edf_native_guest_memory_tests)
        set_tests_properties(edf_native_guest_memory_tests PROPERTIES TIMEOUT 30)
    endif()
    add_executable(edf2027_tests tests/unit_tests.cpp)
    target_include_directories(edf2027_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME edf2027_unit_tests COMMAND edf2027_tests)
endif()

# ---------------------------------------------------------------------------
# Release package: out/package/EDF2027-PC-<version>/  (no game data, ever)
# ---------------------------------------------------------------------------
set(EDF2027_VERSION 0.2.0)
set(EDF2027_PKG_DIR ${CMAKE_SOURCE_DIR}/out/package/EDF2027-PC-${EDF2027_VERSION}
    CACHE PATH "Release package output directory")
# Derive the SDK's bin/ from where find_package landed (<sdk>/lib/cmake/rexglue).
# CMAKE_PREFIX_PATH is a list and may hold more than the SDK, so it cannot be used here.
get_filename_component(EDF2027_SDK_ROOT ${rexglue_DIR}/../../.. ABSOLUTE)
set(EDF2027_SDK_BIN ${EDF2027_SDK_ROOT}/bin)
# Keep gamecontrollerdb.txt next to the dev binary too (the SDL input driver loads it at startup).
add_custom_command(TARGET edf2027 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/gamecontrollerdb.txt $<TARGET_FILE_DIR:edf2027>/
    VERBATIM)

# SDK DX12/Vulkan post-processing is not used by the native renderer.
message(STATUS "EDF2027 renderer: native D3D12 scene/composition/presentation/completion; explicit delay-loaded D3D11 fallback; no GPU emulation plugin staged")
# Some SDK builds directly import FidelityFX from rexruntime.dll, even when no
# GPU plugin/upscaler is active. Satisfy that loader dependency without exposing
# unsupported native upscaling controls. This is not the Xenos plugin.
set(EDF2027_SDK_AUX_DLLS)
if(TARGET rex::amd_fidelityfx_dx12)
    list(APPEND EDF2027_SDK_AUX_DLLS $<TARGET_FILE:rex::amd_fidelityfx_dx12>)
    add_custom_command(TARGET edf2027 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${EDF2027_SDK_AUX_DLLS} $<TARGET_FILE_DIR:edf2027>/
        VERBATIM)
    foreach(_sdk_test edf_native_sdk_ui_tests edf_native_host_lifetime_tests edf_native_guest_memory_tests edf_native_immediate_tail_tests)
        if(TARGET ${_sdk_test})
            add_custom_command(TARGET ${_sdk_test} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${EDF2027_SDK_AUX_DLLS} $<TARGET_FILE_DIR:${_sdk_test}>/
                VERBATIM)
        endif()
    endforeach()
endif()

# Which runtime DLLs an SDK install ships depends on how it was configured (Tracy is
# absent from some), so package only the ones that are actually there.
set(EDF2027_RUNTIME_DLLS)
foreach(_dll rexruntime TracyClient)
    set(_dll_path ${EDF2027_SDK_BIN}/${_dll}${CMAKE_SHARED_LIBRARY_SUFFIX})
    if(EXISTS ${_dll_path})
        list(APPEND EDF2027_RUNTIME_DLLS ${_dll_path})
    endif()
endforeach()

add_custom_target(package_release
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${EDF2027_PKG_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${EDF2027_PKG_DIR}/LICENSES
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:edf2027> ${EDF2027_PKG_DIR}/
    COMMAND ${CMAKE_COMMAND} -E copy ${EDF2027_RUNTIME_DLLS} ${EDF2027_SDK_AUX_DLLS}
                                     ${CMAKE_SOURCE_DIR}/gamecontrollerdb.txt ${EDF2027_PKG_DIR}/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/README.md ${EDF2027_PKG_DIR}/README.md
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/LICENSES ${EDF2027_PKG_DIR}/LICENSES
    DEPENDS edf2027
    COMMENT "Packaging EDF2027-PC-${EDF2027_VERSION} (port only, no game data)"
    VERBATIM)
