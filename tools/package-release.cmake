# Builds the release package (CMakeLists.txt package_release): a folder and a zip with the
# game executable, every DLL it loads that Windows does not ship, the controller database,
# the README and the licenses - and nothing else. Never game data: the player supplies
# their own disc. A second zip holds the PDB, for symbolizing crash dumps from this build.
#
#   cmake -DSOURCE_DIR=<repo> -DPACKAGE_ROOT=<out/package> -DVERSION=<x.y.z-pre>
#         -DEXECUTABLE=<edf2027.exe> -DDLLS=<a.dll|b.dll|...> [-DPDB=<edf2027.pdb>]
#         -DAUDIT_SCRIPT=<tools/audit-native-dependencies.cmake> -P package-release.cmake
#
# docs/release.md lists what the package contains and why.
foreach(required SOURCE_DIR PACKAGE_ROOT VERSION EXECUTABLE DLLS AUDIT_SCRIPT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "package-release.cmake needs ${required}")
    endif()
endforeach()
include(${CMAKE_CURRENT_LIST_DIR}/version-info.cmake)
edf_git_identity("${SOURCE_DIR}" hash dirty)
set(name "EDF2027-PC-${VERSION}-${hash}")
if(dirty)
    string(APPEND name "-dirty")
    message(WARNING "Packaging a build with uncommitted changes: the package is named ${name}")
endif()
set(stage "${PACKAGE_ROOT}/${name}")
set(zip "${PACKAGE_ROOT}/${name}.zip")
set(symbols_zip "${PACKAGE_ROOT}/${name}-symbols.zip")

# --- Stage ---------------------------------------------------------------------------------
file(REMOVE_RECURSE "${stage}")
file(REMOVE "${zip}" "${symbols_zip}")
file(MAKE_DIRECTORY "${stage}")
string(REPLACE "|" ";" dlls "${DLLS}")
foreach(file IN ITEMS "${EXECUTABLE}" ${dlls}
        "${SOURCE_DIR}/gamecontrollerdb.txt" "${SOURCE_DIR}/README.md")
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "Package input is missing: ${file}")
    endif()
    file(COPY "${file}" DESTINATION "${stage}")
endforeach()
file(COPY "${SOURCE_DIR}/LICENSE" DESTINATION "${stage}")
file(COPY "${SOURCE_DIR}/LICENSES" DESTINATION "${stage}")

# --- Check: exactly the expected files -------------------------------------------------------
# Anything else (a PDB, a test executable, a stale DLL, a log, game data) fails the package.
get_filename_component(exe_name "${EXECUTABLE}" NAME)
set(expected "${exe_name}" gamecontrollerdb.txt README.md LICENSE)
foreach(dll IN LISTS dlls)
    get_filename_component(dll_name "${dll}" NAME)
    list(APPEND expected "${dll_name}")
endforeach()
file(GLOB license_files RELATIVE "${SOURCE_DIR}" "${SOURCE_DIR}/LICENSES/*")
list(APPEND expected ${license_files})
file(GLOB_RECURSE staged RELATIVE "${stage}" "${stage}/*")
foreach(file IN LISTS staged)
    list(FIND expected "${file}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Unexpected file in the package: ${file}")
    endif()
    if(file MATCHES "[.](pdb|ilk|lib|exp|xex|iso|xiso|sgo|toml|log|dmp)$")
        message(FATAL_ERROR "Refusing to package ${file}")
    endif()
endforeach()
foreach(file IN LISTS expected)
    if(NOT EXISTS "${stage}/${file}")
        message(FATAL_ERROR "Expected file missing from the package: ${file}")
    endif()
endforeach()
# The licenses a player is owed for what the package redistributes.
foreach(license FidelityFX-SDK.txt SDL3.txt ReXGlue-SDK.txt Microsoft-redistributables.txt gamecontrollerdb.txt)
    if(NOT EXISTS "${stage}/LICENSES/${license}")
        message(FATAL_ERROR "LICENSES/${license} is missing")
    endif()
endforeach()

# --- Audit the staged folder's import closure -----------------------------------------------
execute_process(COMMAND "${CMAKE_COMMAND}" -DEXECUTABLE=${stage}/${exe_name} -P "${AUDIT_SCRIPT}"
    RESULT_VARIABLE audit_result OUTPUT_VARIABLE audit_out ERROR_VARIABLE audit_err)
message(STATUS "${audit_out}${audit_err}")
if(NOT audit_result EQUAL 0)
    message(FATAL_ERROR "The staged package failed audit_native_dependencies")
endif()

# --- Zip -------------------------------------------------------------------------------------
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${zip}" --format=zip "${name}"
    WORKING_DIRECTORY "${PACKAGE_ROOT}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Could not write ${zip}")
endif()
if(PDB AND EXISTS "${PDB}")
    set(symbols_stage "${PACKAGE_ROOT}/${name}-symbols")
    file(REMOVE_RECURSE "${symbols_stage}")
    file(COPY "${PDB}" DESTINATION "${symbols_stage}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${symbols_zip}" --format=zip "${name}-symbols"
        WORKING_DIRECTORY "${PACKAGE_ROOT}" RESULT_VARIABLE result)
    file(REMOVE_RECURSE "${symbols_stage}")
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Could not write ${symbols_zip}")
    endif()
else()
    message(WARNING "No PDB (EDF2027_RELEASE_PDB is OFF?): crash dumps from this package cannot be symbolized")
endif()

# --- Report ----------------------------------------------------------------------------------
list(SORT staged)
set(total 0)
foreach(file IN LISTS staged)
    file(SIZE "${stage}/${file}" size)
    math(EXPR total "${total} + ${size}")
    message(STATUS "  ${file}  ${size}")
endforeach()
file(SIZE "${zip}" zip_size)
message(STATUS "Package: ${zip} (${zip_size} bytes; ${total} bytes unpacked)")
if(EXISTS "${symbols_zip}")
    file(SIZE "${symbols_zip}" symbols_size)
    message(STATUS "Symbols: ${symbols_zip} (${symbols_size} bytes)")
endif()
