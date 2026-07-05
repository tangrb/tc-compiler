# TC-VM 构建配置
set(TC_VM_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(TC_VM_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/src/libtc
    ${TC_VM_DIR}/runtime
    ${TC_VM_DIR}/lexer
    ${TC_VM_DIR}/parser
    ${TC_VM_DIR}/analyzer
    ${TC_VM_DIR}/executor
    ${TC_VM_DIR}/driver
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/vm/bin)

add_executable(tc-vm
    ${TC_VM_DIR}/driver/tc_driver.c
    ${TC_VM_DIR}/driver/tc_repl.c
    ${TC_VM_DIR}/driver/main.c
)

target_link_libraries(tc-vm PRIVATE libtc)
target_include_directories(tc-vm PRIVATE ${TC_VM_INCLUDE_DIRS})
target_compile_options(tc-vm PRIVATE -std=c99 -Wall -Wextra -pedantic)

add_custom_target(check-vm
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/vm/run_tests.sh
    DEPENDS tc-vm
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running TC-VM conformance tests"
)
