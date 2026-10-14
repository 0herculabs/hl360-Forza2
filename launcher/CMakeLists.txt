# Forza2Launcher: profiles, settings and ISO import. Built next to forza2.exe.
#
# SDL3 comes from the ReXGlue SDK package; Dear ImGui is fetched at a pinned
# release (the SDK ships ImGui headers only, compiled into the runtime).

include(FetchContent)
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.1.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(imgui)
# miniz (single-file release) reads the Project Forza Plus 2 mod archives.
# It is C; the top-level project only enables C++.
enable_language(C)
FetchContent_Declare(miniz
    URL https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
    URL_HASH SHA256=ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(miniz)

find_package(SDL3 REQUIRED CONFIG)

add_executable(Forza2Launcher WIN32
    src/main.cpp
    src/installer.cpp
    src/settings.cpp
    src/sha256.cpp
    src/forza_plus.cpp
    ${miniz_SOURCE_DIR}/miniz.c
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp)

# The SDK include dir also carries an (older) imgui.h: keep ours first.
target_include_directories(Forza2Launcher BEFORE PRIVATE
    ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${miniz_SOURCE_DIR})
target_link_libraries(Forza2Launcher PRIVATE SDL3::SDL3-static)
# C++17: path::u8string() returns std::string there (C++20 made it u8string).
set_target_properties(Forza2Launcher PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

# Same folder as the game, plus the file list imports are verified against.
set_target_properties(Forza2Launcher PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:forza2>)
add_custom_command(TARGET Forza2Launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/data/game_files.sha256
        $<TARGET_FILE_DIR:forza2>/game_files.sha256)

# Box art shown in the launcher's left column. It is copyrighted, so it is not
# in the repository: drop your own data/cover.png to have it copied (without
# it the launcher draws its own panel).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/cover.png")
    add_custom_command(TARGET Forza2Launcher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/data/cover.png $<TARGET_FILE_DIR:forza2>/cover.png)
endif()
