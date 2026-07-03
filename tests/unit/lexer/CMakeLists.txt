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

# Extended lexer tests
add_executable(test-lexer-extended
    test_lexer_extended.c
    ${TC_VM_DIR}/lexer/lexer.c
    ${TC_VM_DIR}/runtime/types.c
    ${TC_VM_DIR}/runtime/diagnostic.c
)

target_include_directories(test-lexer-extended PRIVATE ${TC_VM_INCLUDE_DIRS})
target_compile_options(test-lexer-extended PRIVATE -std=c99 -Wall -Wextra -pedantic)

add_custom_target(check-lexer-extended
    COMMAND test-lexer-extended
    DEPENDS test-lexer-extended
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running Extended Lexer unit tests"
)
