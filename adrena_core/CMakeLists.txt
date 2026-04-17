set(CORE_SOURCES
    scr/config.cpp
    scr/logger.cpp
    scr/gpu_detect.cpp
    scr/shared_state.cpp
    scr/sgsr1_pass.cpp
    scr/overlay_menu.cpp
)

if(ADRENA_ENABLE_SGSR2)
    list(APPEND CORE_SOURCES scr/sgsr2_pass.cpp)
endif()

add_library(adrena_core STATIC ${CORE_SOURCES})

embed_hlsl_shaders(adrena_core)

target_include_directories(adrena_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/generated
)

target_compile_definitions(adrena_core PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    ADRENA_PROXY_VERSION="${PROJECT_VERSION}"
)

if(ADRENA_ENABLE_LOGGING)
    target_compile_definitions(adrena_core PUBLIC ADRENA_LOGGING_ENABLED)
endif()

if(ADRENA_ENABLE_SGSR2)
    target_compile_definitions(adrena_core PUBLIC ADRENA_SGSR2_ENABLED)
endif()

if(ADRENA_ENABLE_OVERLAY)
    target_compile_definitions(adrena_core PUBLIC ADRENA_OVERLAY_ENABLED)
    if(MSVC)
        target_compile_definitions(adrena_core PUBLIC ADRENA_DX12_OVERLAY=1)
    endif()
endif()

target_link_libraries(adrena_core PUBLIC
    dxgi d3d11 d3d12 d3dcompiler user32 kernel32 psapi dxguid dwmapi
)

if(ADRENA_ENABLE_OVERLAY)
    target_link_libraries(adrena_core PUBLIC imgui_lib)
endif()
