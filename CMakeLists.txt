# forza2 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(forza2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# The build runs `rexglue codegen` on assets/default.xex first. The game is not
# part of the repository, so without it codegen is swapped for a no-op and the
# committed generated/ sources are compiled as they are. Drop your default.xex
# into assets/ (the launcher's import puts it in game/) to regenerate.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/default.xex")
    if(WIN32)
        set(_forza2_codegen_stub "${CMAKE_CURRENT_SOURCE_DIR}/cmake/codegen_skipped.cmd")
    else()
        set(_forza2_codegen_stub "${CMAKE_CURRENT_SOURCE_DIR}/cmake/codegen_skipped.sh")
    endif()
    foreach(_config "" _DEBUG _RELEASE _RELWITHDEBINFO)
        set_target_properties(rex::rexglue PROPERTIES IMPORTED_LOCATION${_config} "${_forza2_codegen_stub}")
    endforeach()
    message(STATUS "forza2: no assets/default.xex - building the committed generated/ code")
endif()

# Sources
set(FORZA2_SOURCES
    src/main.cpp
    src/profile.cpp
    src/crash_report.cpp
    src/stream_guard.cpp
    src/diag_hooks.cpp
    src/log_filter.cpp
    src/frame_stats.cpp
    src/shader_warmup.cpp
    src/scripted_input.cpp
    src/dlc.cpp
)

if(WIN32)
    add_executable(forza2 WIN32 ${FORZA2_SOURCES})
else()
    add_executable(forza2 ${FORZA2_SOURCES})
endif()

rexglue_setup_target(forza2 GPU_PLUGINS xenos)
if(WIN32)
    target_link_libraries(forza2 PRIVATE dbghelp)
endif()

# Launcher: profiles, settings, ISO import (launcher/).
add_subdirectory(launcher)

# Port settings ship next to the exe (the runtime reads <exe dir>/forza2.toml).
add_custom_command(TARGET forza2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config/forza2.toml $<TARGET_FILE_DIR:forza2>/forza2.toml)
