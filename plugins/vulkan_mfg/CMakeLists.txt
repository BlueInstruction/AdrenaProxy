add_adrena_plugin(plugin_vulkan_mfg vulkan_mfg_plugin.cpp)

# Pull in adrena_core so the plugin can access SharedState + Config.
target_link_libraries(plugin_vulkan_mfg PRIVATE adrena_core)

# vkcompbench-style SPIR-V embedding: if glslc is available on the build
# host, compile the reprojection shader and emit a C++ header containing
# the bytecode.  The plugin compiles cleanly without it — the header
# just stays empty and the compute dispatch is skipped at runtime.
find_program(GLSLC_EXECUTABLE NAMES glslc)

set(GEN_DIR ${CMAKE_BINARY_DIR}/generated/vulkan_mfg)
file(MAKE_DIRECTORY ${GEN_DIR})

if(GLSLC_EXECUTABLE)
    set(SHADER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/reproject.comp.glsl)
    set(SHADER_SPV ${GEN_DIR}/reproject.comp.spv)
    set(SHADER_HDR ${GEN_DIR}/reproject_spv.h)

    add_custom_command(
        OUTPUT ${SHADER_SPV}
        COMMAND ${GLSLC_EXECUTABLE}
                --target-env=vulkan1.2
                -O
                -fshader-stage=comp
                ${SHADER_SRC}
                -o ${SHADER_SPV}
        DEPENDS ${SHADER_SRC}
        COMMENT "Compiling vulkan_mfg reprojection compute shader"
    )

    add_custom_command(
        OUTPUT ${SHADER_HDR}
        COMMAND ${CMAKE_COMMAND}
                -DINPUT_FILE=${SHADER_SPV}
                -DOUTPUT_FILE=${SHADER_HDR}
                -DARRAY_NAME=reproject
                -P ${CMAKE_SOURCE_DIR}/cmake/generate_header.cmake
        DEPENDS ${SHADER_SPV}
        COMMENT "Embedding vulkan_mfg SPIR-V"
    )

    target_sources(plugin_vulkan_mfg PRIVATE ${SHADER_HDR})
    target_compile_definitions(plugin_vulkan_mfg PRIVATE ADRENA_HAS_VULKAN_MFG_SPV=1)
else()
    message(STATUS "vulkan_mfg: glslc not found; SPIR-V embedding skipped (plugin still builds)")
endif()

target_include_directories(plugin_vulkan_mfg PRIVATE ${GEN_DIR})
