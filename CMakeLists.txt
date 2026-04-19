cmake_minimum_required(VERSION 3.20)
project(AdrenaProxy VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT WIN32 AND NOT CMAKE_CROSSCOMPILING)
    message(WARNING "AdrenaProxy targets Windows x86_64 — cross-compile required on non-Windows hosts.")
endif()

option(ADRENA_ENABLE_LOGGING   "Enable debug logging"     ON)
option(ADRENA_ENABLE_SGSR2     "Enable SGSR2 temporal"    ON)
option(ADRENA_ENABLE_OVERLAY   "Enable ImGui overlay"     ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(ADRENA_ENABLE_OVERLAY)
    include(FetchContent)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.6-docking
    )
    FetchContent_MakeAvailable(imgui)

    set(IMGUI_SOURCES
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    )

    if(MSVC)
        list(APPEND IMGUI_SOURCES ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp)
    endif()

    add_library(imgui_lib STATIC ${IMGUI_SOURCES})
    target_include_directories(imgui_lib PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_compile_definitions(imgui_lib PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
endif()

include(cmake/embed_shaders.cmake)

add_subdirectory(adrena_core)
add_subdirectory(dlss_proxy)
add_subdirectory(dxgi_proxy)
add_subdirectory(version_proxy)

# ── Plugins (example, FSR2, XeSS — optional SDK-gated) ─────────────────────
add_subdirectory(plugins)

# ── Standalone Config GUI (ImGui + DX11 desktop app) ───────────────────────
add_subdirectory(config_gui)
