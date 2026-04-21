# generate_combined_header.cmake
#
# Concatenate a list of header files into a single combined header,
# preserving their original order with source markers.  Primarily used
# to build a single-include public API header.
#
# Invocation (script mode):
#   cmake -DHEADER_FILES="a.h;b.h;c.h" -DOUTPUT_FILE=combined.h \
#         -P generate_combined_header.cmake

if(NOT DEFINED HEADER_FILES OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR
        "generate_combined_header.cmake: requires -DHEADER_FILES= -DOUTPUT_FILE=")
endif()

set(HEADER_FILES_LIST ${HEADER_FILES})
string(REPLACE " " ";" HEADER_FILES_LIST "${HEADER_FILES_LIST}")

file(WRITE ${OUTPUT_FILE} "// Combined header file\n\n")

foreach(HEADER_FILE ${HEADER_FILES_LIST})
    if(NOT EXISTS "${HEADER_FILE}")
        message(WARNING "generate_combined_header: skipping missing ${HEADER_FILE}")
        continue()
    endif()
    message(STATUS "Processing ${HEADER_FILE}")
    file(READ ${HEADER_FILE} HEADER_CONTENT)
    file(APPEND ${OUTPUT_FILE} "// Begin ${HEADER_FILE}\n")
    file(APPEND ${OUTPUT_FILE} "${HEADER_CONTENT}\n")
    file(APPEND ${OUTPUT_FILE} "// End ${HEADER_FILE}\n\n")
endforeach()

message(STATUS "Generated combined header: ${OUTPUT_FILE}")
