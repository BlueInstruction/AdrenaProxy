cmake_minimum_required(VERSION 3.20)
project(AdrenaProxy VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT WIN32 AND NOT CMAKE_CROSSCOMPILING)
    message(WARNING "AdrenaProxy targets Windows x86_64 — cross-compile required on non-Windows.")
endif()

option(ADRENA_ENABLE_LOGGING   "Enable debug logging"     ON)
option(ADRENA_ENABLE_SGSR2     "Enable SGSR2 temporal"    ON)
option(ADRENA_ENABLE_FG        "Enable Frame Generation"  ON)
option(ADRENA_ENABLE_OVERLAY   "Enable ImGui overlay"     ON)

include(FetchContent)

if(ADRENA_ENABLE_OVERLAY)
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

    list(APPEND IMGUI_SOURCES ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp)

    add_library(imgui_lib STATIC ${IMGUI_SOURCES})
    target_include_directories(imgui_lib PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_compile_definitions(imgui_lib PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
endif()

include(cmake/embed_shaders.cmake)

set(PROXY_SOURCES
    src/dllmain.cpp
    src/proxy_factory.cpp
    src/proxy_swapchain.cpp
    src/sgsr1_pass.cpp
    src/config.cpp
    src/gpu_detect.cpp
    src/logger.cpp
    src/overlay_menu.cpp
)

if(ADRENA_ENABLE_SGSR2)
    list(APPEND PROXY_SOURCES src/sgsr2_pass.cpp)
endif()

if(ADRENA_ENABLE_FG)
    list(APPEND PROXY_SOURCES src/fg_pass.cpp)
endif()

add_library(adxgi SHARED ${PROXY_SOURCES})

embed_hlsl_shaders(adxgi)

target_include_directories(adxgi PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/generated
)

target_compile_definitions(adxgi PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    ADRENA_PROXY_VERSION="${PROJECT_VERSION}"
)

if(ADRENA_ENABLE_LOGGING)
    target_compile_definitions(adxgi PRIVATE ADRENA_LOGGING_ENABLED)
endif()

if(ADRENA_ENABLE_OVERLAY)
    target_link_libraries(adxgi PRIVATE imgui_lib)
    target_compile_definitions(adxgi PRIVATE ADRENA_OVERLAY_ENABLED)
endif()

if(ADRENA_ENABLE_SGSR2)
    target_compile_definitions(adxgi PUBLIC ADRENA_SGSR2_ENABLED)
endif()

if(ADRENA_ENABLE_FG)
    target_compile_definitions(adxgi PUBLIC ADRENA_FG_ENABLED)
endif()

# DX12 overlay for both MSVC and MinGW
if(ADRENA_ENABLE_OVERLAY)
    target_compile_definitions(adxgi PRIVATE ADRENA_DX12_OVERLAY=1)
endif()

target_link_libraries(adxgi PRIVATE
    dxgi d3d11 d3d12 d3dcompiler user32 kernel32 psapi dxguid dwmapi
)

set_target_properties(adxgi PROPERTIES
    OUTPUT_NAME "dxgi"
    PREFIX ""
    SUFFIX ".dll"
)

target_sources(adxgi PRIVATE ${CMAKE_SOURCE_DIR}/src/dxgi.def)

add_custom_command(TARGET adxgi POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/adrena_proxy.ini
        $<TARGET_FILE_DIR:adxgi>/adrena_proxy.ini
    COMMENT "Copying adrena_proxy.ini"
)
