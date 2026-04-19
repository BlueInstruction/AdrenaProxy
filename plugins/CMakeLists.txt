#  Out-of-tree upscaler plugins that ship as standalone DLLs and are
#  discovered at runtime by PluginManager::ScanDefaultDirectory().
#
#  Each plugin is built into build/bin/plugins/ so dropping that folder
#  next to adrenaproxy_sgsr.dll "just works" without any CMake changes
#  on the consumer side.

set(ADRENA_PLUGIN_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/plugins)

# Helper: declare a plugin DLL with the standard naming/layout convention.
function(adrena_add_plugin name)
    set(options)
    set(oneValueArgs)
    set(multiValueArgs SOURCES LIBS)
    cmake_parse_arguments(P "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_library(${name} SHARED ${P_SOURCES})
    target_link_libraries(${name} PRIVATE adrena_core ${P_LIBS})
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/adrena_core/include)
    set_target_properties(${name} PROPERTIES
        OUTPUT_NAME "adrena_plugin_${name}"
        PREFIX ""
        SUFFIX ".dll"
        RUNTIME_OUTPUT_DIRECTORY ${ADRENA_PLUGIN_OUTPUT_DIR}
        LIBRARY_OUTPUT_DIRECTORY ${ADRENA_PLUGIN_OUTPUT_DIR})
    target_compile_definitions(${name} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endfunction()

add_subdirectory(fsr2)
add_subdirectory(fsr3)
add_subdirectory(fsr4)
add_subdirectory(xess)
