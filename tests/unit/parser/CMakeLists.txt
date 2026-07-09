set(TC_VM_DIR ${CMAKE_SOURCE_DIR}/src/vm)

set(TC_VM_INCLUDE_DIRS
    ${TC_VM_DIR}/runtime
    ${TC_VM_DIR}/lexer
    ${TC_VM_DIR}/parser
    ${TC_VM_DIR}/analyzer
    ${TC_VM_DIR}/executor
    ${TC_VM_DIR}/driver
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/bin)

add_executable(test-parser
    test_parser.c
    ${TC_VM_DIR}/parser/tc_parser.c
    ${TC_VM_DIR}/lexer/tc_lexer.c
    ${TC_VM_DIR}/runtime/tc_types.c
    ${TC_VM_DIR}/runtime/tc_diagnostic.c
)

target_include_directories(test-parser PRIVATE ${TC_VM_INCLUDE_DIRS})
target_compile_options(test-parser PRIVATE -std=c99 -Wall -Wextra -pedantic)

add_custom_target(check-parser
    COMMAND test-parser
    DEPENDS test-parser
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running Parser unit tests"
)
