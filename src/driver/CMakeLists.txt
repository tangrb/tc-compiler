# tc-vm / tc-aot are the only tool entry points (design §14, §15 decision ⑦).
add_executable(tc-vm tc-vm.c)
target_link_libraries(tc-vm PRIVATE tc_core)
target_compile_options(tc-vm PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc-aot tc-aot.c)
target_link_libraries(tc-aot PRIVATE tc_core)
target_compile_options(tc-aot PRIVATE -Wall -Wextra -Werror -Wpedantic)
