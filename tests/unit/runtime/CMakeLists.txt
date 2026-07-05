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

# ------------------------------------------------------------------ #
#  test-semantics — 语义核心模块单元测试                                #
# ------------------------------------------------------------------ #

add_executable(test-semantics
    test_semantics.c
    ${TC_VM_DIR}/runtime/tc_semantics.c
    ${TC_VM_DIR}/runtime/tc_types.c
    ${TC_VM_DIR}/runtime/tc_diagnostic.c
)

target_include_directories(test-semantics PRIVATE ${TC_VM_INCLUDE_DIRS})
target_compile_options(test-semantics PRIVATE -std=c99 -Wall -Wextra -pedantic)

add_custom_target(check-semantics
    COMMAND test-semantics
    DEPENDS test-semantics
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running Semantics unit tests"
)

# ------------------------------------------------------------------ #
#  test-types — 类型工具函数单元测试                                    #
# ------------------------------------------------------------------ #

add_executable(test-types
    test_types.c
    ${TC_VM_DIR}/runtime/tc_types.c
)

target_include_directories(test-types PRIVATE ${TC_VM_INCLUDE_DIRS})
target_compile_options(test-types PRIVATE -std=c99 -Wall -Wextra -pedantic)

add_custom_target(check-types
    COMMAND test-types
    DEPENDS test-types
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running Types unit tests"
)

# ------------------------------------------------------------------ #
#  test-io — 统一 I/O 模块单元测试                                     #
# ------------------------------------------------------------------ #

add_executable(test-io
    test_io.c
    ${TC_VM_DIR}/runtime/tc_io.c
    ${TC_VM_DIR}/runtime/tc_semantics.c
    ${TC_VM_DIR}/runtime/tc_types.c
    ${TC_VM_DIR}/runtime/tc_diagnostic.c
)

target_include_directories(test-io PRIVATE ${TC_VM_INCLUDE_DIRS})
target_compile_options(test-io PRIVATE -std=c99 -Wall -Wextra -pedantic)

add_custom_target(check-io
    COMMAND test-io
    DEPENDS test-io
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running IO unit tests"
)
