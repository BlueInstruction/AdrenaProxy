add_adrena_plugin(plugin_multi_fg multi_fg_plugin.cpp)

# Link against the core adrena library so the plugin can read the live
# Config + SharedState singletons.  The core library already owns the
# D3D12 linkage so we don't need to re-link d3d12.lib here.
target_link_libraries(plugin_multi_fg PRIVATE adrena_core)
