add_library(version_proxy SHARED
    version_main.cpp
)

target_link_libraries(version_proxy PRIVATE
    adrena_core
    version
)

set_target_properties(version_proxy PROPERTIES
    OUTPUT_NAME "adrenaproxy_version"
    PREFIX ""
    SUFFIX ".dll"
)

if(MSVC)
    set_target_properties(version_proxy PROPERTIES
        LINK_FLAGS "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/version.def"
    )
else()
    set_target_properties(version_proxy PROPERTIES
        LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version.def"
    )
endif()
