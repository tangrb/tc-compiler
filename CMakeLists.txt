cmake_minimum_required(VERSION 3.16)

# TC-Compiler 0.0.46 clean-sheet skeleton plus the S0 frozen contracts.
#
# Historical implementation, tools, embedding API and public host library have
# been removed from this branch.  The only build products are the private
# `tc_core` static library and the two tools `tc-vm` / `tc-aot`; nothing is
# installed and no public API/ABI is exported.
#
# Target architecture: docs/TC原生编译器设计说明书-0.0.46.md §14.
project(TC-Compiler VERSION 0.0.46 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
