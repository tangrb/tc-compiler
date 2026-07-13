cmake_minimum_required(VERSION 3.16)

project(TC-Compiler C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CheckCSourceCompiles)

check_c_source_compiles("
#include <fenv.h>
int main(void) {
    feclearexcept(FE_ALL_EXCEPT);
    return fetestexcept(FE_INVALID) ? 1 : 0;
}
" TC_HAVE_FENV)

option(TC_FORCE_NO_FENV "Disable fenv.h strict checks even when available (test no-fenv fallback)" OFF)

if(TC_FORCE_NO_FENV)
    message(STATUS "fenv.h: forced off (float strict uses no-fenv fallback)")
elseif(TC_HAVE_FENV)
    add_compile_definitions(TC_HAVE_FENV=1)
    message(STATUS "fenv.h: available (float strict exception checking enabled)")
else()
    message(STATUS "fenv.h: unavailable (float strict mode uses no-fenv fallback)")
endif()

add_subdirectory(src/libtc)
add_subdirectory(src/vm)
add_subdirectory(src/aot)
add_subdirectory(tests)

add_custom_target(check DEPENDS check-vm check-unit check-aot)
