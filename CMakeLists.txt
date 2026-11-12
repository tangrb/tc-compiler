cmake_minimum_required(VERSION 3.16)

# TC-Compiler 0.0.46 clean-sheet skeleton.
#
# The historical implementation, tools, and embedding API have been removed
# from this branch.  This file only configures the target module
# layout; there are no compiler targets yet.
#
# Target architecture: docs/TC原生编译器设计说明书-0.0.46.md §14.
project(TC-Compiler VERSION 0.0.46 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_subdirectory(src)
