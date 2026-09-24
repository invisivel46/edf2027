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
#               The executable then needs such a CPU: edf_cpu_check (the entry
#               point, CMakeLists.txt) is deliberately left out of the targets
#               below and refuses older CPUs with a message box.
#     Any non-empty profile also adds -fno-strict-aliasing -fwrapv
#     -fno-math-errno: the first two only restate what the guest code needs
#     (clang already defaults to no TBAA for the MSVC target), the third only
#     drops errno writes from libm calls, which the guest cannot observe.
#
#   EDF_LTO=thin           ThinLTO for guest and host targets, linked by lld.
#   EDF_PGO=generate|use   Clang IR PGO for guest and host targets.
#       generate           instrumented build (preset win-amd64-pgo-train,
#                          driven by tools/pgo-train.ps1); the game rewrites
#                          EDF_PGO_RAW_FILE (env overrides the cache path)
#                          every EDF_PGO_DUMP_SECONDS (env, default 30).
#       use                optimize with EDF_PGO_PROFILE (llvm-profdata merge
#                          output; win-amd64-release uses the committed
#                          pgo/edf2027.profdata). Configure copies it into the
#                          build tree under a name carrying its MD5, so a new
#                          profile changes the compile line and Ninja rebuilds
#                          against it; a missing profile only warns and builds
#                          without PGO. docs/pgo.md covers training and when
#                          to retrain.
#     Both modes pass -mllvm -static-func-full-module-prefix=false: the profile
#     names internal-linkage functions by source file name only, not the full
#     path, so a profile trained in one checkout matches another.
#     EDF_PGO_STALE_WARNINGS=ON keeps clang's per-TU "profile data may be out
#     of date" summary (how many functions no longer match the profile).
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
set(EDF_PGO_PROFILE "${CMAKE_SOURCE_DIR}/pgo/edf2027.profdata" CACHE FILEPATH
    "Merged profile the EDF_PGO=use build reads")
option(EDF_PGO_STALE_WARNINGS "EDF_PGO=use: report functions whose profile no longer matches (-Wprofile-instr-out-of-date)" OFF)

# What the build actually does with PGO: EDF_PGO=use falls back to no PGO when
# the profile is missing.
set(EDF_PGO_ACTIVE "${EDF_PGO}")
if(EDF_PGO STREQUAL "use" AND NOT EXISTS "${EDF_PGO_PROFILE}")
    message(WARNING "EDF_PGO=use, but the profile ${EDF_PGO_PROFILE} does not exist: building WITHOUT "
        "profile-guided optimization (correct, about 10% slower). Restore pgo/edf2027.profdata from git, "
        "train one with tools/pgo-train.ps1, or pass -DEDF_PGO= to silence this.")
    set(EDF_PGO_ACTIVE "")
    # Configure again once the profile appears.
    get_filename_component(_edf_pgo_parent "${EDF_PGO_PROFILE}" DIRECTORY)
    if(IS_DIRECTORY "${_edf_pgo_parent}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_edf_pgo_parent}")
    endif()
endif()

if(NOT EDF_GUEST_OPT_PROFILE AND NOT EDF_HOST_OPT_PROFILE AND NOT EDF_LTO AND NOT EDF_PGO_ACTIVE)
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
# Never add edf_cpu_check here: it must run on CPUs that cannot run the rest.
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

set(_edf_pgo_name_flags "SHELL:-mllvm -static-func-full-module-prefix=false")
if(EDF_PGO_ACTIVE STREQUAL "generate")
    foreach(_t IN LISTS _edf_guest_targets _edf_host_targets)
        target_compile_options(${_t} PRIVATE -fprofile-generate ${_edf_pgo_name_flags})
    endforeach()
    get_filename_component(_edf_pgo_dir "${EDF_PGO_RAW_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${_edf_pgo_dir}")
    target_sources(edf2027 PRIVATE ${CMAKE_SOURCE_DIR}/src/pgo_profile_writer.cpp)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/pgo_profile_writer.cpp PROPERTIES
        COMPILE_DEFINITIONS "EDF_PGO_RAW_FILE=\"${EDF_PGO_RAW_FILE}\"")
    target_link_options(edf2027 PRIVATE -fprofile-generate)
elseif(EDF_PGO_ACTIVE STREQUAL "use")
    # Ninja tracks the compile line, not the profile's contents. Compile
    # against a copy named after the profile's MD5: a retrained profile gets a
    # new name, so every object is rebuilt against it. The profile is also a
    # configure dependency, so replacing it re-runs this on the next build.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${EDF_PGO_PROFILE}")
    file(MD5 "${EDF_PGO_PROFILE}" _edf_pgo_md5)
    string(SUBSTRING "${_edf_pgo_md5}" 0 12 _edf_pgo_md5)
    set(_edf_pgo_dir "${CMAKE_BINARY_DIR}/pgo")
    set(_edf_pgo_copy "${_edf_pgo_dir}/use-${_edf_pgo_md5}.profdata")
    if(NOT EXISTS "${_edf_pgo_copy}")
        file(GLOB _edf_pgo_old "${_edf_pgo_dir}/use-*.profdata")
        if(_edf_pgo_old)
            file(REMOVE ${_edf_pgo_old})
        endif()
        file(MAKE_DIRECTORY "${_edf_pgo_dir}")
        file(COPY_FILE "${EDF_PGO_PROFILE}" "${_edf_pgo_copy}")
    endif()
    # A stale profile (host changes since training) only loses the profile of
    # the functions that changed; they are optimized as without PGO. The guest
    # code (generated/) does not change, so its profile stays valid.
    set(_edf_pgo_warnings -Wno-profile-instr-unprofiled -Wno-profile-instr-missing)
    if(NOT EDF_PGO_STALE_WARNINGS)
        list(APPEND _edf_pgo_warnings -Wno-profile-instr-out-of-date)
    endif()
    foreach(_t IN LISTS _edf_guest_targets _edf_host_targets)
        target_compile_options(${_t} PRIVATE "-fprofile-use=${_edf_pgo_copy}" ${_edf_pgo_name_flags}
            ${_edf_pgo_warnings})
    endforeach()
    message(STATUS "EDF PGO: using ${EDF_PGO_PROFILE} (md5 ${_edf_pgo_md5})")
elseif(EDF_PGO AND NOT EDF_PGO STREQUAL "use")
    message(FATAL_ERROR "EDF_PGO must be '', generate or use")
endif()

message(STATUS "EDF optimization: guest='${EDF_GUEST_OPT_PROFILE}' host='${EDF_HOST_OPT_PROFILE}' lto='${EDF_LTO}' pgo='${EDF_PGO_ACTIVE}'")
