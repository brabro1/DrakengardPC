cmake_minimum_required(VERSION 3.21)

project(DrakengardPC LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Game-derived C++ produced by tools/recomp.py from the user's own disc. It is
# never committed; the build only reads it from the work directory.
set(DRAKENGARD_GENERATED_DIR "${CMAKE_SOURCE_DIR}/work/recomp/generated"
    CACHE PATH "Directory holding ps2_recomp output for SLUS-20732")
set(DRAKENGARD_UNITY_BATCH 32 CACHE STRING "Generated functions per unity translation unit")

if(NOT EXISTS "${DRAKENGARD_GENERATED_DIR}/register_functions.cpp")
    message(FATAL_ERROR
        "No generated code in ${DRAKENGARD_GENERATED_DIR}.\n"
        "Run: python -I tools/recomp.py <path to your Drakengard (USA) ISO>")
endif()

# Only the runtime libraries are needed from PS2Recomp here; the recompiler and
# analyzer are built separately (build/ps2recomp) by tools/recomp.py.
set(PS2X_BUILD_RECOMP OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_ANALYZER OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_STUDIO OFF CACHE BOOL "" FORCE)
set(PS2X_ENABLE_SCCACHE OFF CACHE BOOL "" FORCE)
# PS2Recomp's "aggressive" logging compiles a call tracker into every generated
# function and writes each guest call to ./ps2_log.txt (over 1 GB per minute of
# boot here). Keep it opt-in for call-level debugging only.
option(DRAKENGARD_TRACE_CALLS "Log every guest function entry/exit to ps2_log.txt (very slow)" OFF)
set(PS2X_ENABLE_AGRESSIVE_LOGS ${DRAKENGARD_TRACE_CALLS} CACHE BOOL "" FORCE)
# GPU GS renderer: paraLLEl-GS (Vulkan compute, LGPL-3.0+), a git submodule. When present,
# PS2Recomp's runtime builds its GSParallelBackend and uses it unless PS2X_GS_RENDERER=cpu.
option(DRAKENGARD_GPU_GS "Build the Vulkan GS renderer (paraLLEl-GS)" ON)
if(DRAKENGARD_GPU_GS)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/external/parallel-gs/Granite/CMakeLists.txt")
        message(FATAL_ERROR
            "external/parallel-gs is not checked out. Run:\n"
            "  git submodule update --init external/parallel-gs\n"
            "  (cd external/parallel-gs && bash checkout_submodules.sh)\n"
            "or configure with -DDRAKENGARD_GPU_GS=OFF for the CPU renderer only.")
    endif()
    set(PARALLEL_GS_STANDALONE ON CACHE BOOL "" FORCE)
    add_subdirectory(external/parallel-gs EXCLUDE_FROM_ALL)
endif()
add_subdirectory(external/PS2Recomp EXCLUDE_FROM_ALL)
if(NOT MSVC)
    # Line tables for the runtime (not the generated code) so tools/profile_report.py
    # can attribute DKG_PROFILE samples to source functions.
    target_compile_options(ps2_runtime PRIVATE -g1)
    target_compile_options(ps2_iop PRIVATE -g1)
endif()

# The software GS rasteriser is the hottest host code (DKG_PROFILE: ~55% of the guest
# thread at the title screen). Optimise it harder; AVX2 is opt-in because it raises the
# CPU requirement (Haswell / Zen 1 or newer).
option(DRAKENGARD_HOST_AVX2 "Build the software GS with AVX2" ON)
if(NOT MSVC)
    set(DRAKENGARD_GS_FLAGS -O3)
    if(DRAKENGARD_HOST_AVX2)
        list(APPEND DRAKENGARD_GS_FLAGS -mavx2 -mfma -mbmi -mbmi2 -mlzcnt)
    endif()
    file(GLOB DRAKENGARD_GS_SOURCES "${CMAKE_SOURCE_DIR}/external/PS2Recomp/ps2xRuntime/src/lib/gs/*.cpp")
    set_source_files_properties(${DRAKENGARD_GS_SOURCES}
        DIRECTORY external/PS2Recomp/ps2xRuntime
        PROPERTIES COMPILE_OPTIONS "${DRAKENGARD_GS_FLAGS}")
endif()

file(GLOB DRAKENGARD_GENERATED_SOURCES CONFIGURE_DEPENDS "${DRAKENGARD_GENERATED_DIR}/*.cpp")
list(LENGTH DRAKENGARD_GENERATED_SOURCES DRAKENGARD_GENERATED_COUNT)
message(STATUS "Generated translation units: ${DRAKENGARD_GENERATED_COUNT}")

add_library(drakengard_generated STATIC ${DRAKENGARD_GENERATED_SOURCES})
target_include_directories(drakengard_generated PUBLIC "${DRAKENGARD_GENERATED_DIR}")
target_link_libraries(drakengard_generated PUBLIC ps2_runtime)
set_target_properties(drakengard_generated PROPERTIES
    UNITY_BUILD ON
    UNITY_BUILD_BATCH_SIZE "${DRAKENGARD_UNITY_BATCH}")
target_precompile_headers(drakengard_generated PRIVATE
    "${CMAKE_SOURCE_DIR}/external/PS2Recomp/ps2xRuntime/include/ps2_runtime_macros.h"
    "${CMAKE_SOURCE_DIR}/external/PS2Recomp/ps2xRuntime/include/ps2_runtime.h"
    "${DRAKENGARD_GENERATED_DIR}/ps2_recompiled_functions.h"
    "${DRAKENGARD_GENERATED_DIR}/ps2_recompiled_stubs.h"
    "${CMAKE_SOURCE_DIR}/external/PS2Recomp/ps2xRuntime/include/ps2_syscalls.h"
    "${CMAKE_SOURCE_DIR}/external/PS2Recomp/ps2xRuntime/include/ps2_stubs.h")
if(NOT MSVC)
    # Translated code is enormous straight-line C++; -O2 keeps build time sane.
    target_compile_options(drakengard_generated PRIVATE -O2 -msse4.1 -Wno-unused-label -Wno-unused-variable)
endif()

# Ahead-of-time compiled VU1 microprograms (tools/vu_recomp.py, game-derived, in work/).
set(DRAKENGARD_VU1_DIR "${CMAKE_SOURCE_DIR}/work/recomp/vu1")
file(GLOB DRAKENGARD_VU1_SOURCES CONFIGURE_DEPENDS "${DRAKENGARD_VU1_DIR}/*.cpp")
if(DRAKENGARD_VU1_SOURCES)
    add_library(drakengard_vu1 STATIC ${DRAKENGARD_VU1_SOURCES})
    target_link_libraries(drakengard_vu1 PUBLIC ps2_runtime)
    if(NOT MSVC)
        # Values must match the interpreter: no FMA contraction, no fast-math.
        target_compile_options(drakengard_vu1 PRIVATE -O2 -msse4.1 -ffp-contract=off
            -Wno-unused-label -Wno-unused-variable -Wno-unused-but-set-variable)
    endif()
endif()

add_executable(DrakengardPC
    src/main.cpp
    src/disc/iso9660.cpp
    src/game_identity.cpp
    src/overrides/drakengard_overrides.cpp
    src/debug/trace_hooks.cpp
    src/debug/sampler.cpp
)
target_include_directories(DrakengardPC PRIVATE src)
target_link_libraries(DrakengardPC PRIVATE drakengard_generated ps2_runtime)
if(DRAKENGARD_VU1_SOURCES)
    target_link_libraries(DrakengardPC PRIVATE drakengard_vu1)
    target_compile_definitions(DrakengardPC PRIVATE DRAKENGARD_HAVE_VU1_AOT=1)
endif()
if(NOT MSVC)
    target_compile_options(DrakengardPC PRIVATE -msse4.1)
endif()
if(MINGW)
    # Large guest-call recursion depth during boot; match the PS2Recomp tests.
    target_link_options(DrakengardPC PRIVATE -Wl,--stack,8388608 -static-libgcc -static-libstdc++)
    target_link_libraries(DrakengardPC PRIVATE psapi)
endif()
