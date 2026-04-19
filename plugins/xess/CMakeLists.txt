set(ADRENA_XESS_SDK_PATH "" CACHE PATH "Root of Intel XeSS SDK (optional)")

if(NOT "${ADRENA_XESS_SDK_PATH}" STREQUAL "")
    add_adrena_plugin(plugin_xess xess_plugin.cpp)
    target_include_directories(plugin_xess PRIVATE
        "${ADRENA_XESS_SDK_PATH}/inc"
    )
    target_link_directories(plugin_xess PRIVATE
        "${ADRENA_XESS_SDK_PATH}/lib"
    )
    target_link_libraries(plugin_xess PRIVATE libxess)
    message(STATUS "XeSS plugin: SDK found at ${ADRENA_XESS_SDK_PATH}")
else()
    message(STATUS "XeSS plugin: skipped (set ADRENA_XESS_SDK_PATH to enable)")
endif()
