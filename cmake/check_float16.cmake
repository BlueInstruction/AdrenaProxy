include(CheckCXXSourceCompiles)

macro(check_float16 HAVE_FLOAT16)
    check_cxx_source_compiles("
        int main() {
            _Float16 f = 1.0f;
            (void)f;
            return 0;
        }
    " ${HAVE_FLOAT16})

    if(${HAVE_FLOAT16})
        message(STATUS "_Float16 is supported.")
        set(${HAVE_FLOAT16} TRUE  CACHE INTERNAL "Support for _Float16")
    else()
        message(STATUS "_Float16 is not supported.")
        set(${HAVE_FLOAT16} FALSE CACHE INTERNAL "Support for _Float16")
    endif()
endmacro()
