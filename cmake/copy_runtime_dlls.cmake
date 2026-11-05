# Copies the DLLs an executable needs from the toolchain folder next to it (libc++, FFmpeg,
# ...), so DrakengardPC.exe and the launcher start without the MSYS2 folder on PATH.
# Usage: cmake -DEXE=<exe> -DSEARCH_DIRS=<dir> -DOBJDUMP=<llvm-objdump> -P copy_runtime_dlls.cmake
cmake_minimum_required(VERSION 3.21)

set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "objdump")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${OBJDUMP}")

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${EXE}"
    RESOLVED_DEPENDENCIES_VAR resolved
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    DIRECTORIES ${SEARCH_DIRS}
    PRE_EXCLUDE_REGEXES "^api-ms-.*" "^ext-ms-.*"
    POST_EXCLUDE_REGEXES ".*[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\].*")

get_filename_component(dest "${EXE}" DIRECTORY)
set(copied 0)
foreach(dll IN LISTS resolved)
    get_filename_component(name "${dll}" NAME)
    file(SIZE "${dll}" srcSize)
    set(dstSize -1)
    if(EXISTS "${dest}/${name}")
        file(SIZE "${dest}/${name}" dstSize)
    endif()
    if(NOT srcSize EQUAL dstSize)
        file(COPY_FILE "${dll}" "${dest}/${name}")
        math(EXPR copied "${copied} + 1")
    endif()
endforeach()
list(LENGTH resolved total)
message(STATUS "Runtime DLLs next to ${EXE}: ${total} (${copied} updated)")
