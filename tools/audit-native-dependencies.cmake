# Read-only PE dependency audit. Run in a VS developer environment (dumpbin).
# This checks loader imports, not dynamic LoadLibrary calls or GPU behavior.
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
        message(STATUS "Native packaged import: ${name}")
    endif()
endforeach()
list(LENGTH resolved dependency_count)
message(STATUS "Native PE import audit passed: ${dependency_count} non-system DLLs; no Xenos import or staged DLL. Windows imports are prerequisites; dynamic loading/runtime coverage requires separate validation.")
