set(ADRENA_FSR2_SDK_PATH "" CACHE PATH "Root of AMD FidelityFX FSR2 SDK (optional)")

if(NOT "${ADRENA_FSR2_SDK_PATH}" STREQUAL "")
    add_adrena_plugin(plugin_fsr2 fsr2_plugin.cpp)
    target_include_directories(plugin_fsr2 PRIVATE
        "${ADRENA_FSR2_SDK_PATH}/include"
    )
    target_link_directories(plugin_fsr2 PRIVATE
        "${ADRENA_FSR2_SDK_PATH}/lib/x64"
    )
    target_link_libraries(plugin_fsr2 PRIVATE
        ffx_fsr2_api_x64
        ffx_fsr2_api_dx12_x64
    )
    message(STATUS "FSR2 plugin: SDK found at ${ADRENA_FSR2_SDK_PATH}")
else()
    message(STATUS "FSR2 plugin: skipped (set ADRENA_FSR2_SDK_PATH to enable)")
endif()
