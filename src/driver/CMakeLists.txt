# tc-vm / tc-aot are the only tool entry points (design §14, §15 decision ⑦).
add_executable(tc-vm tc-vm.c)
target_link_libraries(tc-vm PRIVATE tc_core)
target_compile_options(tc-vm PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc-aot tc-aot.c)
target_link_libraries(tc-aot PRIVATE tc_core)
target_compile_definitions(tc-aot PRIVATE
    TC_A64_RUNTIME_LIB="${TC_A64_RT_LIB}"
    TC_A64_LINKER_SCRIPT="${TC_A64_LINK_SCRIPT}")
add_dependencies(tc-aot tc_a64_runtime)
target_compile_options(tc-aot PRIVATE -Wall -Wextra -Werror -Wpedantic)
