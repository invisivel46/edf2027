# Opt-in optimization experiments beyond the baseline Release flags.
#
# Every knob here is empty/OFF by default, so the default build is unchanged.
# The baseline optimization level comes from CMAKE_CXX_FLAGS_<CONFIG>
# (pinned to "-O2 -DNDEBUG" for win-amd64-release in CMakePresets.json);
# these options append to it, and a later -O on the command line wins.
#
#   EDF_GUEST_OPT_PROFILE  recompiled guest code (edf2027_recomp, 81 TUs + PCH)
#   EDF_HOST_OPT_PROFILE   host code (edf2027 and the edf_native_* libraries)
#       ""      unchanged
#       O3      -O3
#       v3      -march=x86-64-v3 -ffp-contract=off  (AVX2, FMA, BMI1/2, LZCNT,
#               MOVBE, F16C; Haswell / Zen or newer). std::fma, which the
#               generated code uses for every PPC fmadd/fmsub/fnmadd/fnmsub,
#               becomes one vfmadd instead of a call into the CRT's fma();
#               fma is exactly rounded either way, so results are identical.
#               -ffp-contract=off keeps the compiler from fusing any a*b+c
#               that is not an explicit std::fma, so no float result changes.
#       O3-v3   both
#     Any non-empty profile also adds -fno-strict-aliasing -fwrapv
#     -fno-math-errno: the first two only restate what the guest code needs
#     (clang already defaults to no TBAA for the MSVC target), the third only
#     drops errno writes from libm calls, which the guest cannot observe.
#
#   EDF_LTO=thin           ThinLTO for guest and host targets, linked by lld.
#   EDF_PGO=generate|use   Clang IR PGO for guest and host targets.
#       generate           instrumented build; the game rewrites
#                          EDF_PGO_RAW_FILE every EDF_PGO_DUMP_SECONDS (env,
#                          default 30) and at a clean exit.
#       use                optimize with EDF_PGO_PROFILE (llvm-profdata merge
#                          output). Name each new profile differently: the
#                          path is on the compile line, its contents are not
#                          a Ninja dependency.
#
# Only Clang (the GNU-style clang++ driver the presets select) is supported.

set(EDF_GUEST_OPT_PROFILE "" CACHE STRING "Extra optimization profile for recompiled guest code: '', O3, v3, O3-v3")
set_property(CACHE EDF_GUEST_OPT_PROFILE PROPERTY STRINGS "" O3 v3 O3-v3)
set(EDF_HOST_OPT_PROFILE "" CACHE STRING "Extra optimization profile for host code: '', O3, v3, O3-v3")
set_property(CACHE EDF_HOST_OPT_PROFILE PROPERTY STRINGS "" O3 v3 O3-v3)
set(EDF_LTO "" CACHE STRING "Link-time optimization: '' or thin")
set_property(CACHE EDF_LTO PROPERTY STRINGS "" thin)
set(EDF_PGO "" CACHE STRING "Profile-guided optimization: '', generate or use")
set_property(CACHE EDF_PGO PROPERTY STRINGS "" generate use)
set(EDF_PGO_RAW_FILE "${CMAKE_BINARY_DIR}/pgo/edf2027.profraw" CACHE FILEPATH
    "Raw profile the EDF_PGO=generate build writes")
set(EDF_PGO_PROFILE "${CMAKE_BINARY_DIR}/pgo/edf2027.profdata" CACHE FILEPATH
    "Merged profile the EDF_PGO=use build reads")

if(NOT EDF_GUEST_OPT_PROFILE AND NOT EDF_HOST_OPT_PROFILE AND NOT EDF_LTO AND NOT EDF_PGO)
    return()
endif()

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
    message(FATAL_ERROR "EDF_*_OPT_PROFILE / EDF_LTO / EDF_PGO expect the GNU-style clang++ driver")
endif()

function(_edf_opt_profile_flags out profile)
    set(flags "")
    if(profile STREQUAL "")
    elseif(profile STREQUAL "O3")
        set(flags -O3)
    elseif(profile STREQUAL "v3")
        set(flags -march=x86-64-v3 -ffp-contract=off)
    elseif(profile STREQUAL "O3-v3")
        set(flags -O3 -march=x86-64-v3 -ffp-contract=off)
    else()
        message(FATAL_ERROR "Unknown optimization profile '${profile}' (expected '', O3, v3 or O3-v3)")
    endif()
    if(flags)
        list(APPEND flags -fno-strict-aliasing -fwrapv -fno-math-errno)
    endif()
    set(${out} ${flags} PARENT_SCOPE)
endfunction()

set(_edf_guest_targets)
if(TARGET edf2027_recomp)
    list(APPEND _edf_guest_targets edf2027_recomp)
endif()
set(_edf_host_targets)
foreach(_t edf2027 edf_native_sdk_ui edf_native_effects edf_native_d3d11 edf_native_d3d12)
    if(TARGET ${_t})
        list(APPEND _edf_host_targets ${_t})
    endif()
endforeach()

# Target options land after CMAKE_CXX_FLAGS_<CONFIG> on the compile line and
# also reach the target's PCH, so the PCH and its TUs always agree.
_edf_opt_profile_flags(_edf_guest_flags "${EDF_GUEST_OPT_PROFILE}")
_edf_opt_profile_flags(_edf_host_flags "${EDF_HOST_OPT_PROFILE}")
foreach(_t IN LISTS _edf_guest_targets)
    target_compile_options(${_t} PRIVATE ${_edf_guest_flags})
endforeach()
foreach(_t IN LISTS _edf_host_targets)
    target_compile_options(${_t} PRIVATE ${_edf_host_flags})
endforeach()

if(EDF_LTO STREQUAL "thin")
    foreach(_t IN LISTS _edf_guest_targets _edf_host_targets)
        target_compile_options(${_t} PRIVATE -flto=thin)
    endforeach()
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/lto-cache")
    # The cache keeps relinks after a small change incremental; prune it to 2 GB.
    target_link_options(edf2027 PRIVATE -flto=thin
        "LINKER:/lldltocache:${CMAKE_BINARY_DIR}/lto-cache"
        "LINKER:/lldltocachepolicy:cache_size_bytes=2g"
        "LINKER:/opt:lldltojobs=2")
elseif(EDF_LTO)
    message(FATAL_ERROR "EDF_LTO must be '' or thin")
endif()

if(EDF_PGO STREQUAL "generate")
    foreach(_t IN LISTS _edf_guest_targets _edf_host_targets)
        target_compile_options(${_t} PRIVATE -fprofile-generate)
    endforeach()
    get_filename_component(_edf_pgo_dir "${EDF_PGO_RAW_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${_edf_pgo_dir}")
    target_sources(edf2027 PRIVATE ${CMAKE_SOURCE_DIR}/src/pgo_profile_writer.cpp)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/pgo_profile_writer.cpp PROPERTIES
        COMPILE_DEFINITIONS "EDF_PGO_RAW_FILE=\"${EDF_PGO_RAW_FILE}\"")
    target_link_options(edf2027 PRIVATE -fprofile-generate)
elseif(EDF_PGO STREQUAL "use")
    if(NOT EXISTS "${EDF_PGO_PROFILE}")
        message(FATAL_ERROR "EDF_PGO=use needs EDF_PGO_PROFILE (${EDF_PGO_PROFILE}); "
            "run llvm-profdata merge -o <it> <the .profraw> first")
    endif()
    foreach(_t IN LISTS _edf_guest_targets _edf_host_targets)
        # A stale profile (codegen or host changes since training) only loses
        # coverage for the changed functions; keep the build quiet about it.
        target_compile_options(${_t} PRIVATE "-fprofile-use=${EDF_PGO_PROFILE}"
            -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date -Wno-profile-instr-missing)
    endforeach()
    # Ninja does not track the profile's contents, only the command line:
    # give each new profile a new file name to recompile against it.
elseif(EDF_PGO)
    message(FATAL_ERROR "EDF_PGO must be '', generate or use")
endif()

message(STATUS "EDF optimization: guest='${EDF_GUEST_OPT_PROFILE}' host='${EDF_HOST_OPT_PROFILE}' lto='${EDF_LTO}' pgo='${EDF_PGO}'")
