add_library(dlss_proxy SHARED
    dlss_main.cpp
    ngx_param.cpp
)

target_link_libraries(dlss_proxy PRIVATE
    adrena_core
)

set_target_properties(dlss_proxy PROPERTIES
    OUTPUT_NAME "adrenaproxy_sgsr"
    PREFIX ""
    SUFFIX ".dll"
)
