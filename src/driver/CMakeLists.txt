add_executable(tc tc.c)
target_link_libraries(tc PRIVATE tc_core)
target_include_directories(tc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_options(tc PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc-vm tc-vm.c)
target_link_libraries(tc-vm PRIVATE tc_core)
target_include_directories(tc-vm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_options(tc-vm PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc-aot tc-aot.c)
target_link_libraries(tc-aot PRIVATE tc_core)
target_include_directories(tc-aot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_dependencies(tc-aot tc_aarch64_runtime tc_arm32_runtime
                 tc_riscv32_runtime)
target_compile_options(tc-aot PRIVATE -Wall -Wextra -Werror -Wpedantic)
