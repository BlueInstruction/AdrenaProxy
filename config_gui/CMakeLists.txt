if(NOT ADRENA_ENABLE_OVERLAY)
    message(STATUS "Config GUI: skipped (requires ADRENA_ENABLE_OVERLAY)")
    return()
endif()

add_executable(adrena_config WIN32 config_gui_main.cpp)

target_link_libraries(adrena_config PRIVATE
    imgui_lib
    d3d11 dxgi
)

target_compile_features(adrena_config PRIVATE cxx_std_17)
target_compile_definitions(adrena_config PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

set_target_properties(adrena_config PROPERTIES
    OUTPUT_NAME "adrena_config"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
