# edf2017 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(edf2017 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(EDF2017_SOURCES
    src/main.cpp
    src/input_hooks.cpp
    src/launcher_cvars.cpp
)

if(WIN32)
    add_executable(edf2017 WIN32 ${EDF2017_SOURCES})
else()
    add_executable(edf2017 ${EDF2017_SOURCES})
endif()

rexglue_setup_target(edf2017 GPU_PLUGINS xenos)

if(BUILD_TESTING)
    add_executable(edf2017_tests tests/unit_tests.cpp)
    target_include_directories(edf2017_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME edf2017_unit_tests COMMAND edf2017_tests)
endif()

# ---------------------------------------------------------------------------
# Release package: out/package/EDF2017-PC-<version>/  (no game data, ever)
# ---------------------------------------------------------------------------
set(EDF2017_VERSION 0.2.0)
set(EDF2017_PKG_DIR ${CMAKE_SOURCE_DIR}/out/package/EDF2017-PC-${EDF2017_VERSION})
set(EDF2017_SDK_BIN ${CMAKE_PREFIX_PATH}/bin)
set(EDF2017_TOOLS "" CACHE PATH "Directory containing the extract-xiso executable")
if(WIN32)
    set(EDF2017_XISO extract-xiso.exe)
else()
    set(EDF2017_XISO extract-xiso)
endif()
# Keep gamecontrollerdb.txt next to the dev binary too (the SDL input driver loads it at startup).
add_custom_command(TARGET edf2017 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/gamecontrollerdb.txt $<TARGET_FILE_DIR:edf2017>/
    VERBATIM)

add_custom_target(package_release
    COMMAND ${CMAKE_COMMAND} -DREQUIRED_FILE=${EDF2017_TOOLS}/${EDF2017_XISO}
            -P ${CMAKE_SOURCE_DIR}/cmake/RequireFile.cmake
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${EDF2017_PKG_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${EDF2017_PKG_DIR}/LICENSES
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:edf2017> ${EDF2017_PKG_DIR}/
    COMMAND ${CMAKE_COMMAND} -E copy ${EDF2017_SDK_BIN}/rexruntime${CMAKE_SHARED_LIBRARY_SUFFIX}
                                     ${EDF2017_SDK_BIN}/rexgpu-xenos${CMAKE_SHARED_LIBRARY_SUFFIX}
                                     ${EDF2017_SDK_BIN}/TracyClient${CMAKE_SHARED_LIBRARY_SUFFIX} ${EDF2017_PKG_DIR}/
    COMMAND ${CMAKE_COMMAND} -E copy ${EDF2017_TOOLS}/${EDF2017_XISO} ${EDF2017_PKG_DIR}/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/gamecontrollerdb.txt ${EDF2017_PKG_DIR}/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/README.md ${EDF2017_PKG_DIR}/README.md
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/LICENSES ${EDF2017_PKG_DIR}/LICENSES
    DEPENDS edf2017
    COMMENT "Packaging EDF2017-PC-${EDF2017_VERSION} (port + tools only, no game data)"
    VERBATIM)
