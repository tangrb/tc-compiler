cmake_minimum_required(VERSION 3.16)

project(TC-Compiler C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_subdirectory(src/vm)
add_subdirectory(src/aot)

add_custom_target(check DEPENDS check-vm)
