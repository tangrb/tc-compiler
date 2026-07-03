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

add_executable(test-lexer
    test_lexer.c
    ${TC_VM_DIR}/lexer/lexer.c
    ${TC_VM_DIR}/runtime/types.c
    ${TC_VM_DIR}/runtime/diagnostic.c
)

target_include_directories(test-lexer PRIVATE ${TC_VM_INCLUDE_DIRS})
target_compile_options(test-lexer PRIVATE -std=c99 -Wall -Wextra -pedantic)

add_custom_target(check-lexer
    COMMAND test-lexer
    DEPENDS test-lexer
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running Lexer unit tests"
)
