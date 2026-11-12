cmake_minimum_required(VERSION 3.15)

project(talaz_bedrock CXX)

set(CMAKE_CXX_STANDARD 20)
# for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.9
        GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)

# src/core and src/mods are the same everywhere; src/android hooks the Android game
# through mcpelauncher, src/windows the Windows one. Each mod is one file, picked up here
file(GLOB TALAZ_SOURCES CONFIGURE_DEPENDS src/core/*.cpp src/mods/*.cpp)
if(WIN32)
    file(GLOB TALAZ_PLATFORM CONFIGURE_DEPENDS src/windows/*.cpp)
    list(APPEND TALAZ_PLATFORM ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
    set(TALAZ_PLATFORM_DIR src/windows)
else()
    file(GLOB TALAZ_PLATFORM CONFIGURE_DEPENDS src/android/*.cpp src/android/mods/*.cpp)
    list(APPEND TALAZ_PLATFORM ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
    set(TALAZ_PLATFORM_DIR src/android)
endif()
add_library(talaz_bedrock SHARED
        ${TALAZ_SOURCES}
        ${TALAZ_PLATFORM}
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp)

target_include_directories(talaz_bedrock PRIVATE src src/core ${TALAZ_PLATFORM_DIR} ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
# The game ships its own libc++_shared, so keep ours static and our symbols private
# The name the in-game menu shows
set(CLIENT_NAME "Talaz" CACHE STRING "Name the in-game menu shows")
target_compile_definitions(talaz_bedrock PRIVATE IMGUI_IMPL_OPENGL_ES3 IMGUI_DISABLE_DEMO_WINDOWS
        CLIENT_NAME="${CLIENT_NAME}"
        FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")
if(WIN32)
    # one DLL with nothing to install beside it: the C++ runtime goes in
    set_target_properties(talaz_bedrock PROPERTIES PREFIX "lib")
    target_link_options(talaz_bedrock PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_libraries(talaz_bedrock PRIVATE d3d11 d3d12 dxgi d3dcompiler)
    add_executable(talaz-inject tools/inject.cpp)
    target_link_options(talaz-inject PRIVATE -static -municode)
    return()
endif()
target_compile_options(talaz_bedrock PRIVATE -fvisibility=hidden)
# Only the two entry points leave the library, so our static libc++ never
# stands in for the game's own
# The launcher's linker skips packed RELR relocations, which leaves static
# constructors pointing nowhere, so keep relocations in the plain format
target_link_options(talaz_bedrock PRIVATE
        -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map
        -Wl,--pack-dyn-relocs=none)
set_source_files_properties(${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp PROPERTIES
        COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/src/android/gl.h")
target_link_libraries(talaz_bedrock PRIVATE EGL log)
