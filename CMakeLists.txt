cmake_minimum_required(VERSION 3.16)

project(TC-Compiler C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# 跨平台 feature-test 宏：glibc / MSYS2 MinGW 在严格 ANSI（-std=c99 -pedantic）
# 下隐藏 strdup/strndup/clock_gettime/timespec/mkdtemp/mkstemps 等 POSIX/GNU 声明，
# 导致 Linux/Windows 编译失败而 macOS（Apple SDK 不隐藏）通过。
# _POSIX_C_SOURCE=200809L 解锁 POSIX.1-2008 声明，_DEFAULT_SOURCE 解锁 BSD/GNU
# 默认集（__USE_MISC：mkdtemp/mkstemps）。两宏在 MSYS2 上无副作用。
# Apple 例外：_POSIX_C_SOURCE 会反过来隐藏 BSD 专属 API（如 funopen），
# 需用 _DARWIN_C_SOURCE 重新放开，保持 macOS 行为不变。
if(APPLE)
    add_compile_definitions(_DARWIN_C_SOURCE=1)
endif()
add_compile_definitions(_POSIX_C_SOURCE=200809L)
add_compile_definitions(_DEFAULT_SOURCE)

# glibc 的数学库（trunc 等）不在默认链接中，需显式 -lm；
# Apple libSystem 内含 libm，MinGW 隐式提供 libm，均无需此选项。
if(NOT APPLE)
    link_libraries(m)
endif()

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
