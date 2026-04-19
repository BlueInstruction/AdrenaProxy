add_library(dxgi_proxy SHARED
    dxgi_main.cpp
    proxy_factory.cpp
    proxy_swapchain.cpp
)

target_link_libraries(dxgi_proxy PRIVATE
    adrena_core
)

if(ADRENA_ENABLE_OVERLAY)
    target_link_libraries(dxgi_proxy PRIVATE imgui_lib)
endif()

set_target_properties(dxgi_proxy PROPERTIES
    OUTPUT_NAME "adrenaproxy_dxgi"
    PREFIX ""
    SUFFIX ".dll"
)

if(MSVC)
    set_target_properties(dxgi_proxy PROPERTIES
        LINK_FLAGS "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/dxgi.def"
    )
else()
    target_sources(dxgi_proxy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/dxgi.def)
endif()
