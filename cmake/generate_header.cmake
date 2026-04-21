# generate_header.cmake
#
# Convert a binary file (typically compiled SPIR-V) into a C++ header
# that exposes it as a `static const unsigned char <name>_code[]` plus
# `<name>_size`.  Runs as a script when invoked with -P, and also
# provides the adrena_embed_binary() function for normal CMake usage.
#
# Usage (script):
#   cmake -DINPUT_FILE=foo.spv -DOUTPUT_FILE=foo_spv.h -DARRAY_NAME=foo_spv \
#         -P generate_header.cmake
#
# Usage (function):
#   include(cmake/generate_header.cmake)
#   adrena_embed_binary(foo.spv ${CMAKE_CURRENT_BINARY_DIR}/foo_spv.h foo_spv)

function(_adrena_write_spirv_header input_file output_file array_name)
    if(NOT EXISTS "${input_file}")
        message(FATAL_ERROR "generate_header: input file not found: ${input_file}")
    endif()

    file(READ "${input_file}" SPIRV_DATA HEX)
    string(REGEX MATCHALL ".." BYTES "${SPIRV_DATA}")

    set(ARRAY_CONTENT "")
    set(COUNTER 0)
    set(TOTAL_BYTES 0)
    foreach(BYTE ${BYTES})
        string(APPEND ARRAY_CONTENT "0x${BYTE}, ")
        math(EXPR TOTAL_BYTES "${TOTAL_BYTES} + 1")
        math(EXPR COUNTER "${COUNTER} + 1")
        if(${COUNTER} EQUAL 16)
            string(APPEND ARRAY_CONTENT "\n    ")
            set(COUNTER 0)
        endif()
    endforeach()
    string(REGEX REPLACE ",[ \n ]+$" "" ARRAY_CONTENT "${ARRAY_CONTENT}")

    file(WRITE "${output_file}"
        "// AUTO-GENERATED — do not edit\n"
        "#ifndef ADRENA_EMBED_${array_name}_H\n"
        "#define ADRENA_EMBED_${array_name}_H\n\n"
        "namespace adrena::shaders {\n\n"
        "inline constexpr unsigned int ${array_name}_size = ${TOTAL_BYTES};\n"
        "inline constexpr unsigned char ${array_name}_code[] = {\n    ${ARRAY_CONTENT}\n};\n\n"
        "} // namespace adrena::shaders\n\n"
        "#endif // ADRENA_EMBED_${array_name}_H\n"
    )
endfunction()

function(adrena_embed_binary input_file output_file array_name)
    _adrena_write_spirv_header("${input_file}" "${output_file}" "${array_name}")
endfunction()

# Script mode: invoked via `cmake -P`.
if(CMAKE_SCRIPT_MODE_FILE STREQUAL "${CMAKE_CURRENT_LIST_FILE}")
    if(NOT DEFINED INPUT_FILE OR NOT DEFINED OUTPUT_FILE OR NOT DEFINED ARRAY_NAME)
        message(FATAL_ERROR
            "generate_header.cmake: script mode requires "
            "-DINPUT_FILE= -DOUTPUT_FILE= -DARRAY_NAME=")
    endif()
    _adrena_write_spirv_header("${INPUT_FILE}" "${OUTPUT_FILE}" "${ARRAY_NAME}")
endif()
