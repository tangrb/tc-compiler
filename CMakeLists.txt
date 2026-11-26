cmake_minimum_required(VERSION 3.16)

# Language and compiler contracts remain at 0.0.46; the toolchain release is
# 0.0.47.  Build products are the private tc_core static library and the three
# tools tc / tc-vm / tc-aot.  Nothing is installed and no public API/ABI is
# exported.
#
# Target architecture: docs/TC原生编译器设计说明书-0.0.46.md §14.
project(TC-Compiler VERSION 0.0.47 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
