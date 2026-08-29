# EDF2027 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(edf2027 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(EDF2027_SOURCES
    src/main.cpp
    src/input_hooks.cpp
    src/launcher_cvars.cpp
)

if(WIN32)
    add_executable(edf2027 WIN32 ${EDF2027_SOURCES})
else()
    add_executable(edf2027 ${EDF2027_SOURCES})
endif()

rexglue_setup_target(edf2027 GPU_PLUGINS xenos)

if(BUILD_TESTING)
    add_executable(edf2027_tests tests/unit_tests.cpp)
    target_include_directories(edf2027_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME edf2027_unit_tests COMMAND edf2027_tests)
endif()

# ---------------------------------------------------------------------------
# Release package: out/package/EDF2027-PC-<version>/  (no game data, ever)
# ---------------------------------------------------------------------------
set(EDF2027_VERSION 0.2.0)
set(EDF2027_PKG_DIR ${CMAKE_SOURCE_DIR}/out/package/EDF2027-PC-${EDF2027_VERSION})
# Derive the SDK's bin/ from where find_package landed (<sdk>/lib/cmake/rexglue).
# CMAKE_PREFIX_PATH is a list and may hold more than the SDK, so it cannot be used here.
get_filename_component(EDF2027_SDK_ROOT ${rexglue_DIR}/../../.. ABSOLUTE)
set(EDF2027_SDK_BIN ${EDF2027_SDK_ROOT}/bin)
# Keep gamecontrollerdb.txt next to the dev binary too (the SDL input driver loads it at startup).
add_custom_command(TARGET edf2027 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/gamecontrollerdb.txt $<TARGET_FILE_DIR:edf2027>/
    VERBATIM)

# AMD FidelityFX (CAS/FSR upscaling) is optional: only an SDK configured with
# REXGLUE_ENABLE_FIDELITYFX exports these targets. The SDK's own POST_BUILD copy in
# rexglue_helpers.cmake tests the unnamespaced target names and so never fires for an
# installed SDK, where the export is rex::amd_fidelityfx_*; stage the DLLs ourselves.
# The settings dialog looks for the DX12 DLL beside the executable to decide whether to
# offer the upscaling controls, so a missing DLL degrades to "unavailable", not a crash.
set(EDF2027_FIDELITYFX)
foreach(_fx rex::amd_fidelityfx_dx12 rex::amd_fidelityfx_vk)
    if(TARGET ${_fx})
        list(APPEND EDF2027_FIDELITYFX $<TARGET_FILE:${_fx}>)
        add_custom_command(TARGET edf2027 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${_fx}> $<TARGET_FILE_DIR:edf2027>/
            VERBATIM)
    endif()
endforeach()
if(EDF2027_FIDELITYFX)
    message(STATUS "FidelityFX upscaling (CAS/FSR): enabled by the SDK")
else()
    message(STATUS "FidelityFX upscaling (CAS/FSR): not in this SDK, the controls stay disabled")
endif()

# Which runtime DLLs an SDK install ships depends on how it was configured (Tracy is
# absent from some), so package only the ones that are actually there.
set(EDF2027_RUNTIME_DLLS)
foreach(_dll rexruntime rexgpu-xenos TracyClient)
    set(_dll_path ${EDF2027_SDK_BIN}/${_dll}${CMAKE_SHARED_LIBRARY_SUFFIX})
    if(EXISTS ${_dll_path})
        list(APPEND EDF2027_RUNTIME_DLLS ${_dll_path})
    endif()
endforeach()

add_custom_target(package_release
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${EDF2027_PKG_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${EDF2027_PKG_DIR}/LICENSES
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:edf2027> ${EDF2027_PKG_DIR}/
    COMMAND ${CMAKE_COMMAND} -E copy ${EDF2027_RUNTIME_DLLS} ${EDF2027_FIDELITYFX}
                                     ${CMAKE_SOURCE_DIR}/gamecontrollerdb.txt ${EDF2027_PKG_DIR}/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/README.md ${EDF2027_PKG_DIR}/README.md
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/LICENSES ${EDF2027_PKG_DIR}/LICENSES
    DEPENDS edf2027
    COMMENT "Packaging EDF2027-PC-${EDF2027_VERSION} (port only, no game data)"
    VERBATIM)
