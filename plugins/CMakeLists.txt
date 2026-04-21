cmake_minimum_required(VERSION 3.20)

# ── Plugin build helper ────────────────────────────────────────────────────
# Called by each plugin subdirectory to produce a *.adrena.dll.
function(add_adrena_plugin TARGET SRC)
    add_library(${TARGET} SHARED ${SRC})
    target_include_directories(${TARGET} PRIVATE
        ${CMAKE_SOURCE_DIR}/adrena_core/include
    )
    set_target_properties(${TARGET} PROPERTIES
        OUTPUT_NAME "${TARGET}"
        SUFFIX      ".adrena.dll"
        PREFIX      ""
    )
    target_compile_features(${TARGET} PRIVATE cxx_std_17)
endfunction()

add_subdirectory(example)
add_subdirectory(fsr2)
add_subdirectory(xess)
add_subdirectory(multi_fg)
add_subdirectory(vulkan_mfg)
