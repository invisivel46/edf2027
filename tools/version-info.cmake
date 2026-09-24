# The build's identity: the release version (CMakeLists.txt EDF2027_VERSION) plus the git
# commit it was built from and whether tracked files differed from it. Shared by
# write-version.cmake (the header the game logs and shows in its F1 menu) and
# package-release.cmake (the zip's name), so the two always agree.
#
# edf_git_identity(<source dir> <hash var> <dirty var>): hash is the short commit hash, or
# "nogit" outside a git checkout; dirty is 1 when a tracked file differs from HEAD
# (untracked files and the codegen stamp do not count), else 0.
function(edf_git_identity source_dir hash_var dirty_var)
    set(hash nogit)
    set(dirty 0)
    find_program(EDF_GIT_EXECUTABLE NAMES git)
    if(EDF_GIT_EXECUTABLE)
        execute_process(COMMAND "${EDF_GIT_EXECUTABLE}" -C "${source_dir}" rev-parse --short=7 HEAD
            RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(result EQUAL 0 AND out)
            set(hash "${out}")
            # The build's own codegen rewrites its stamp and partition files on every
            # configure; those do not make a build differ from its commit.
            execute_process(COMMAND "${EDF_GIT_EXECUTABLE}" -C "${source_dir}" status --porcelain --untracked-files=no
                    -- . ":(exclude)generated/default/codegen.build.stamp" ":(exclude)generated/default/codegen.partition.json"
                RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(result EQUAL 0 AND out)
                set(dirty 1)
            endif()
        endif()
    endif()
    set(${hash_var} "${hash}" PARENT_SCOPE)
    set(${dirty_var} "${dirty}" PARENT_SCOPE)
endfunction()

# "0.3.0-beta+abc1234" or "0.3.0-beta+abc1234.dirty" (SemVer build metadata).
function(edf_version_full version hash dirty out_var)
    set(full "${version}+${hash}")
    if(dirty)
        string(APPEND full ".dirty")
    endif()
    set(${out_var} "${full}" PARENT_SCOPE)
endfunction()
