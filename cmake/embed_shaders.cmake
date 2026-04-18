function(embed_hlsl_shaders target)
    set(SHADER_DIR ${CMAKE_SOURCE_DIR}/shaders)
    set(GEN_DIR ${CMAKE_BINARY_DIR}/generated)
    file(MAKE_DIRECTORY ${GEN_DIR})

    set(SHADER_FILES
        sgsr1_official.hlsl
        sgsr1_rcas.hlsl
        sgsr2_convert.hlsl
        sgsr2_upscale.hlsl
        fg_interpolate.hlsl
    )

    set(OUTPUT_CPP ${GEN_DIR}/embedded_shaders.cpp)
    set(OUTPUT_H   ${GEN_DIR}/embedded_shaders.h)

    file(WRITE ${OUTPUT_H}
        "// AUTO-GENERATED — do not edit\n"
        "#pragma once\n"
        "#include <cstdint>\n\n"
        "namespace adrena::shaders {\n\n"
    )

    file(WRITE ${OUTPUT_CPP}
        "// AUTO-GENERATED — do not edit\n"
        "#include \"embedded_shaders.h\"\n\n"
        "namespace adrena::shaders {\n\n"
    )

    foreach(SHADER ${SHADER_FILES})
        set(SHADER_PATH ${SHADER_DIR}/${SHADER})
        if(NOT EXISTS ${SHADER_PATH})
            message(WARNING "Shader not found: ${SHADER_PATH}")
            continue()
        endif()

        file(READ ${SHADER_PATH} SHADER_CONTENT)

        string(REPLACE ".hlsl" "" VAR_BASE ${SHADER})
        string(MAKE_C_IDENTIFIER ${VAR_BASE} VAR_NAME)

        string(REPLACE "\\" "\\\\" SHADER_CONTENT "${SHADER_CONTENT}")
        string(REPLACE "\"" "\\\"" SHADER_CONTENT "${SHADER_CONTENT}")
        string(REPLACE "\n" "\\n\"\n\"" SHADER_CONTENT "${SHADER_CONTENT}")
        file(SIZE ${SHADER_PATH} SHADER_SIZE)

        file(APPEND ${OUTPUT_H}
            "extern const char* ${VAR_NAME};\n"
            "extern const uint32_t ${VAR_NAME}_size;\n\n"
        )
        file(APPEND ${OUTPUT_CPP}
            "const char* ${VAR_NAME} = \"${SHADER_CONTENT}\";\n"
            "const uint32_t ${VAR_NAME}_size = ${SHADER_SIZE};\n\n"
        )
    endforeach()

    file(APPEND ${OUTPUT_H} "} // namespace adrena::shaders\n")
    file(APPEND ${OUTPUT_CPP} "} // namespace adrena::shaders\n")

    target_sources(${target} PRIVATE ${OUTPUT_CPP})
    target_include_directories(${target} PRIVATE ${GEN_DIR})
endfunction()
