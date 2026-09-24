# Read-only PE dependency audit. Run in a VS developer environment (dumpbin).
# This checks loader imports, not dynamic LoadLibrary calls or GPU behavior.
if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)  # normalized paths in GET_RUNTIME_DEPENDENCIES matching
endif()
if(NOT DEFINED EXECUTABLE OR NOT EXISTS "${EXECUTABLE}")
    message(FATAL_ERROR "EXECUTABLE must name an existing native game executable")
endif()
file(REAL_PATH "${EXECUTABLE}" executable)
get_filename_component(directory "${executable}" DIRECTORY)
file(GLOB staged_dlls "${directory}/*.dll")
foreach(staged IN LISTS staged_dlls)
    get_filename_component(name "${staged}" NAME)
    string(TOLOWER "${name}" name)
    if(name MATCHES "xenos")
        message(FATAL_ERROR "GPU emulation DLL is staged: ${staged}")
    endif()
endforeach()
find_program(audit_dumpbin NAMES dumpbin REQUIRED)
function(check_native_imports image)
    execute_process(COMMAND "${audit_dumpbin}" /dependents "${image}"
        RESULT_VARIABLE result OUTPUT_VARIABLE imports ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Cannot inspect native imports for ${image}: ${error}")
    endif()
    # The explicit fallback may delay-load D3D11. It must not appear in the
    # eager import closure of the executable or any packaged dependency.
    string(FIND "${imports}" "Image has the following delay load dependencies:" delay_offset)
    if(NOT delay_offset EQUAL -1)
        string(SUBSTRING "${imports}" 0 ${delay_offset} imports)
    endif()
    string(TOLOWER "${imports}" imports)
    if(imports MATCHES "[\r\n][ \t]+d3d11[.]dll[\r\n]")
        message(FATAL_ERROR "Eager D3D11 import in native D3D12 package: ${image}")
    endif()
endfunction()
check_native_imports("${executable}")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "dumpbin")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${audit_dumpbin}")
# System DLLs are an OS prerequisite, not redistributable package contents.
# Stop at that boundary: recursively auditing Windows itself encounters optional
# OS-feature imports unrelated to loading the game's packaged libraries.
file(GLOB system_dlls "$ENV{SystemRoot}/System32/*.dll")
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${executable}"
    RESOLVED_DEPENDENCIES_VAR resolved
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    CONFLICTING_DEPENDENCIES_PREFIX conflicts
    # Windows API-set contracts are resolved by the loader, not packaged DLLs.
    PRE_EXCLUDE_REGEXES "^[Aa][Pp][Ii]-[Mm][Ss]-" "^[Ee][Xx][Tt]-[Mm][Ss]-"
    POST_EXCLUDE_FILES ${system_dlls})
if(unresolved OR conflicts_FILENAMES)
    message(FATAL_ERROR "Incomplete/ambiguous native import closure: unresolved=${unresolved}; conflicting=${conflicts_FILENAMES}")
endif()
file(TO_CMAKE_PATH "$ENV{SystemRoot}/System32" system_directory)
string(TOLOWER "${system_directory}" system_directory)
string(TOLOWER "${directory}" local_directory)
foreach(dependency IN LISTS resolved)
    file(REAL_PATH "${dependency}" dependency)
    get_filename_component(name "${dependency}" NAME)
    string(TOLOWER "${name}" name)
    if(name MATCHES "xenos")
        message(FATAL_ERROR "GPU emulation import in native dependency closure: ${dependency}")
    endif()
    get_filename_component(parent "${dependency}" DIRECTORY)
    string(TOLOWER "${parent}" parent)
    if(NOT parent STREQUAL local_directory AND NOT parent STREQUAL system_directory)
        message(FATAL_ERROR "Native import resolved outside package/System32: ${dependency}")
    endif()
    if(parent STREQUAL local_directory)
        check_native_imports("${dependency}")
        message(STATUS "Native packaged import: ${name}")
    endif()
endforeach()
list(LENGTH resolved dependency_count)
message(STATUS "Native PE import audit passed: ${dependency_count} non-system DLLs; no eager D3D11 import, Xenos import or staged DLL. The explicit D3D11 fallback is delay-loaded; dynamic loading/runtime coverage requires separate validation.")
