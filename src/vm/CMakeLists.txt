# TC-VM 构建配置文件
# 作者：唐荣兵
# 联系邮箱：yanhuang8923@qq.com

set(TC_VM_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(TC_VM_INCLUDE_DIRS
    ${TC_VM_DIR}/runtime
    ${TC_VM_DIR}/lexer
    ${TC_VM_DIR}/parser
    ${TC_VM_DIR}/analyzer
    ${TC_VM_DIR}/executor
    ${TC_VM_DIR}/driver
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/vm/bin)

add_executable(tc-vm
    ${TC_VM_DIR}/runtime/types.c
    ${TC_VM_DIR}/runtime/diagnostic.c
    ${TC_VM_DIR}/runtime/semantics.c
    ${TC_VM_DIR}/lexer/lexer.c
    ${TC_VM_DIR}/parser/parser.c
    ${TC_VM_DIR}/analyzer/analyzer.c
    ${TC_VM_DIR}/executor/executor.c
    ${TC_VM_DIR}/driver/driver.c
    ${TC_VM_DIR}/driver/repl.c
    ${TC_VM_DIR}/driver/main.c
)

target_include_directories(tc-vm PRIVATE ${TC_VM_INCLUDE_DIRS})

target_compile_options(tc-vm PRIVATE
    -Wall
    -Wextra
    -pedantic
)

add_custom_target(check-vm
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/vm/run_tests.sh
    DEPENDS tc-vm
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running TC-VM conformance tests"
)
