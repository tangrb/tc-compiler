set(TC_VM_DIR ${CMAKE_SOURCE_DIR}/src/vm)

set(TC_VM_INCLUDE_DIRS
    ${TC_VM_DIR}/runtime
    ${TC_VM_DIR}/lexer
    ${TC_VM_DIR}/parser
    ${TC_VM_DIR}/analyzer
    ${TC_VM_DIR}/executor
    ${TC_VM_DIR}/embed
    ${TC_VM_DIR}/driver
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/bin)

# ------------------------------------------------------------------ #
#  test-semantics — 语义核心模块单元测试                                #
# ------------------------------------------------------------------ #

add_executable(test-semantics
    test_semantics.c
    ${TC_VM_DIR}/runtime/tc_semantics.c
    ${TC_VM_DIR}/runtime/tc_sem_int.c
    ${TC_VM_DIR}/runtime/tc_sem_fp.c
    ${TC_VM_DIR}/runtime/tc_sem_cast.c
    ${TC_VM_DIR}/runtime/tc_sem_bitwise.c
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
#  test-diagnostic — 诊断域与所有权单元测试                            #
# ------------------------------------------------------------------ #

add_executable(test-diagnostic
    test_diagnostic.c
    ${TC_VM_DIR}/runtime/tc_diagnostic.c
    ${TC_VM_DIR}/runtime/tc_types.c
)
target_include_directories(test-diagnostic PRIVATE ${TC_VM_INCLUDE_DIRS})
target_compile_definitions(test-diagnostic PRIVATE TC_DIAGNOSTIC_TESTING=1)
target_compile_options(test-diagnostic PRIVATE -std=c99 -Wall -Wextra -pedantic)

add_custom_target(check-diagnostic
    COMMAND test-diagnostic
    DEPENDS test-diagnostic
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running Diagnostic unit tests"
)

# ------------------------------------------------------------------ #
#  test-libtc — transactional ownership and repeatable consumption    #
# ------------------------------------------------------------------ #

add_executable(test-libtc
    test_libtc.c
    ${CMAKE_SOURCE_DIR}/src/aot/tc_aot_codegen.c
    ${CMAKE_SOURCE_DIR}/src/aot/tc_aot_emit_rhs.c
    ${CMAKE_SOURCE_DIR}/src/aot/tc_aot_emit_stmt.c
    ${CMAKE_SOURCE_DIR}/src/aot/tc_aot_emit_func.c
)
target_link_libraries(test-libtc PRIVATE libtc)
target_include_directories(test-libtc PRIVATE
    ${TC_VM_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src/libtc
    ${CMAKE_SOURCE_DIR}/src/aot
)
target_compile_options(test-libtc PRIVATE -std=c99 -Wall -Wextra -pedantic)

add_custom_target(check-libtc
    COMMAND test-libtc
    DEPENDS test-libtc
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running libtc unit tests"
)

# ------------------------------------------------------------------ #
#  test-module — Phase 2 模块系统（导入/DAG/签名/Self）单元测试         #
# ------------------------------------------------------------------ #

add_executable(test-module test_module.c)
target_link_libraries(test-module PRIVATE libtc)
target_include_directories(test-module PRIVATE
    ${TC_VM_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src/libtc
)
target_compile_options(test-module PRIVATE -std=c99 -Wall -Wextra -pedantic)

add_custom_target(check-module
    COMMAND test-module
    DEPENDS test-module
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running Module system unit tests"
)

# ------------------------------------------------------------------ #
#  test-io — 统一 I/O 模块单元测试                                     #
# ------------------------------------------------------------------ #

add_executable(test-io
    test_io.c
    ${TC_VM_DIR}/runtime/tc_io.c
    ${TC_VM_DIR}/runtime/tc_semantics.c
    ${TC_VM_DIR}/runtime/tc_sem_int.c
    ${TC_VM_DIR}/runtime/tc_sem_fp.c
    ${TC_VM_DIR}/runtime/tc_sem_cast.c
    ${TC_VM_DIR}/runtime/tc_sem_bitwise.c
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

# ------------------------------------------------------------------ #
#  test-bitwise — 按位运算语义单元测试                                  #
# ------------------------------------------------------------------ #

add_executable(test-bitwise
    test_bitwise.c
    ${TC_VM_DIR}/runtime/tc_semantics.c
    ${TC_VM_DIR}/runtime/tc_sem_int.c
    ${TC_VM_DIR}/runtime/tc_sem_fp.c
    ${TC_VM_DIR}/runtime/tc_sem_cast.c
    ${TC_VM_DIR}/runtime/tc_sem_bitwise.c
    ${TC_VM_DIR}/runtime/tc_types.c
    ${TC_VM_DIR}/runtime/tc_diagnostic.c
)

target_include_directories(test-bitwise PRIVATE ${TC_VM_INCLUDE_DIRS})
target_compile_options(test-bitwise PRIVATE -std=c99 -Wall -Wextra -pedantic)

add_custom_target(check-bitwise
    COMMAND test-bitwise
    DEPENDS test-bitwise
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running Bitwise unit tests"
)

# ------------------------------------------------------------------ #
#  test-shift — 移位运算语义单元测试                                    #
# ------------------------------------------------------------------ #

add_executable(test-shift
    test_shift.c
    ${TC_VM_DIR}/runtime/tc_semantics.c
    ${TC_VM_DIR}/runtime/tc_sem_int.c
    ${TC_VM_DIR}/runtime/tc_sem_fp.c
    ${TC_VM_DIR}/runtime/tc_sem_bitwise.c
    ${TC_VM_DIR}/runtime/tc_types.c
    ${TC_VM_DIR}/runtime/tc_diagnostic.c
)

target_include_directories(test-shift PRIVATE ${TC_VM_INCLUDE_DIRS})
target_compile_options(test-shift PRIVATE -std=c99 -Wall -Wextra -pedantic)

add_custom_target(check-shift
    COMMAND test-shift
    DEPENDS test-shift
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running Shift unit tests"
)

# ------------------------------------------------------------------ #
#  test-symbol — 符号表与块级作用域单元测试                             #
# ------------------------------------------------------------------ #

add_executable(test-symbol
    test_symbol.c
    ${TC_VM_DIR}/runtime/tc_symbol.c
    ${TC_VM_DIR}/runtime/tc_types.c
    ${TC_VM_DIR}/runtime/tc_diagnostic.c
)

target_include_directories(test-symbol PRIVATE ${TC_VM_INCLUDE_DIRS})
target_compile_options(test-symbol PRIVATE -std=c99 -Wall -Wextra -pedantic)

add_custom_target(check-symbol
    COMMAND test-symbol
    DEPENDS test-symbol
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running Symbol table unit tests"
)

# ------------------------------------------------------------------ #
#  test-warning — 编译警告列表单元测试                                  #
# ------------------------------------------------------------------ #

add_executable(test-warning
    test_warning.c
    ${TC_VM_DIR}/runtime/tc_warning.c
    ${TC_VM_DIR}/runtime/tc_types.c
)

target_include_directories(test-warning PRIVATE ${TC_VM_INCLUDE_DIRS})
target_compile_options(test-warning PRIVATE -std=c99 -Wall -Wextra -pedantic)

add_custom_target(check-warning
    COMMAND test-warning
    DEPENDS test-warning
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running Warning list unit tests"
)

# ------------------------------------------------------------------ #
#  test-analyzer — 静态分析器单元测试                                   #
# ------------------------------------------------------------------ #

add_executable(test-analyzer
    test_analyzer.c
    ${TC_VM_DIR}/analyzer/tc_analyzer.c
    ${TC_VM_DIR}/analyzer/tc_analyzer_pass1.c
    ${TC_VM_DIR}/analyzer/tc_analyzer_pass2.c
    ${TC_VM_DIR}/analyzer/tc_analyze_6a.c
    ${TC_VM_DIR}/analyzer/tc_analyze_6e.c
    ${TC_VM_DIR}/analyzer/tc_analyzer_dfa.c
    ${TC_VM_DIR}/analyzer/tc_cfg.c
    ${TC_VM_DIR}/analyzer/tc_const_eval.c
    ${TC_VM_DIR}/analyzer/tc_module.c
    ${TC_VM_DIR}/analyzer/tc_scope.c
    ${TC_VM_DIR}/analyzer/tc_struct_check.c
    ${TC_VM_DIR}/analyzer/tc_ptr_check.c
    ${TC_VM_DIR}/analyzer/tc_memblock_check.c
    ${TC_VM_DIR}/analyzer/tc_type_check.c
    ${TC_VM_DIR}/analyzer/tc_func_check.c
    ${TC_VM_DIR}/analyzer/tc_callgraph.c
    ${TC_VM_DIR}/parser/tc_parser.c
    ${TC_VM_DIR}/parser/tc_parser_rhs.c
    ${TC_VM_DIR}/parser/tc_parser_free.c
    ${TC_VM_DIR}/lexer/tc_lexer.c
    ${TC_VM_DIR}/runtime/tc_symbol.c
    ${TC_VM_DIR}/runtime/tc_semantics.c
    ${TC_VM_DIR}/runtime/tc_sem_int.c
    ${TC_VM_DIR}/runtime/tc_sem_fp.c
    ${TC_VM_DIR}/runtime/tc_sem_cast.c
    ${TC_VM_DIR}/runtime/tc_sem_bitwise.c
    ${TC_VM_DIR}/runtime/tc_warning.c
    ${TC_VM_DIR}/runtime/tc_types.c
    ${TC_VM_DIR}/runtime/tc_diagnostic.c
)

target_include_directories(test-analyzer PRIVATE ${TC_VM_INCLUDE_DIRS})
target_compile_options(test-analyzer PRIVATE -std=c99 -Wall -Wextra -pedantic)

add_custom_target(check-analyzer
    COMMAND test-analyzer
    DEPENDS test-analyzer
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running Analyzer unit tests"
)

# ------------------------------------------------------------------ #
#  test-cfg — 显式控制流图与可达性单元测试                         #
# ------------------------------------------------------------------ #

add_executable(test-cfg test_cfg.c)
target_link_libraries(test-cfg PRIVATE libtc)
target_include_directories(test-cfg PRIVATE ${TC_VM_INCLUDE_DIRS})
target_compile_options(test-cfg PRIVATE -std=c99 -Wall -Wextra -pedantic)

add_custom_target(check-cfg
    COMMAND test-cfg
    DEPENDS test-cfg
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running CFG unit tests"
)

# ------------------------------------------------------------------ #
#  test-executor — structured VM control and fixed-slot lifecycle     #
# ------------------------------------------------------------------ #

add_executable(test-executor test_executor.c)
target_link_libraries(test-executor PRIVATE libtc)
target_include_directories(test-executor PRIVATE ${TC_VM_INCLUDE_DIRS})
target_compile_options(test-executor PRIVATE -std=c99 -Wall -Wextra -pedantic)

add_custom_target(check-executor
    COMMAND test-executor
    DEPENDS test-executor
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running Executor unit tests"
)

# ------------------------------------------------------------------ #
#  test-stmt-index — stmt_index 子树 span 单元测试                      #
# ------------------------------------------------------------------ #

add_executable(test-stmt-index
    test_stmt_index.c
    ${TC_VM_DIR}/parser/tc_parser.c
    ${TC_VM_DIR}/parser/tc_parser_rhs.c
    ${TC_VM_DIR}/parser/tc_parser_free.c
    ${TC_VM_DIR}/lexer/tc_lexer.c
    ${TC_VM_DIR}/runtime/tc_types.c
    ${TC_VM_DIR}/runtime/tc_diagnostic.c
)

target_include_directories(test-stmt-index PRIVATE ${TC_VM_INCLUDE_DIRS})
target_compile_options(test-stmt-index PRIVATE -std=c99 -Wall -Wextra -pedantic)

add_custom_target(check-stmt-index
    COMMAND test-stmt-index
    DEPENDS test-stmt-index
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running stmt_index unit tests"
)

# ------------------------------------------------------------------ #
#  test-type-check — Phase 3 类型验证 / CFG 诊断单元测试               #
# ------------------------------------------------------------------ #

add_executable(test-type-check test_type_check.c)
target_link_libraries(test-type-check PRIVATE libtc)
target_include_directories(test-type-check PRIVATE ${TC_VM_INCLUDE_DIRS})
target_compile_options(test-type-check PRIVATE -std=c99 -Wall -Wextra -pedantic)

add_custom_target(check-type-check
    COMMAND test-type-check
    DEPENDS test-type-check
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running Type-check unit tests"
)

# ------------------------------------------------------------------ #
#  test-embed — TC-Embed 嵌入运行时单元测试                             #
# ------------------------------------------------------------------ #

add_executable(test-embed test_embed.c)
target_link_libraries(test-embed PRIVATE libtc)
target_include_directories(test-embed PRIVATE ${TC_VM_INCLUDE_DIRS})
target_compile_options(test-embed PRIVATE -std=c99 -Wall -Wextra -pedantic)

add_custom_target(check-embed
    COMMAND test-embed
    DEPENDS test-embed
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running Embed unit tests"
)

# ------------------------------------------------------------------ #
#  test-embed-aot — TC-Embed AOT 模式单元测试                          #
# ------------------------------------------------------------------ #

add_executable(test-embed-aot test_embed_aot.c
    ${CMAKE_SOURCE_DIR}/src/aot/tc_aot_codegen.c
    ${CMAKE_SOURCE_DIR}/src/aot/tc_aot_emit_rhs.c
    ${CMAKE_SOURCE_DIR}/src/aot/tc_aot_emit_stmt.c
    ${CMAKE_SOURCE_DIR}/src/aot/tc_aot_emit_func.c
)
target_link_libraries(test-embed-aot PRIVATE libtc)
target_include_directories(test-embed-aot PRIVATE
    ${TC_VM_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src/libtc
    ${CMAKE_SOURCE_DIR}/src/aot
)
target_compile_definitions(test-embed-aot PRIVATE
    TC_SRC_DIR="${CMAKE_SOURCE_DIR}"
)
target_compile_options(test-embed-aot PRIVATE -std=c99 -Wall -Wextra -pedantic)

add_custom_target(check-embed-aot
    COMMAND test-embed-aot
    DEPENDS test-embed-aot
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running Embed AOT unit tests"
)
