# libtc — TC 编译/执行静态库
set(TC_VM_DIR ${CMAKE_SOURCE_DIR}/src/vm)

set(TC_LIBTC_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${TC_VM_DIR}/runtime
    ${TC_VM_DIR}/lexer
    ${TC_VM_DIR}/parser
    ${TC_VM_DIR}/analyzer
    ${TC_VM_DIR}/executor
)

set(TC_LIBTC_SOURCES
    tc_lib.c
    ${TC_VM_DIR}/runtime/tc_types.c
    ${TC_VM_DIR}/runtime/tc_diagnostic.c
    ${TC_VM_DIR}/runtime/tc_symbol.c
    ${TC_VM_DIR}/runtime/tc_warning.c
    ${TC_VM_DIR}/runtime/tc_semantics.c
    ${TC_VM_DIR}/runtime/tc_sem_int.c
    ${TC_VM_DIR}/runtime/tc_sem_fp.c
    ${TC_VM_DIR}/runtime/tc_sem_cast.c
    ${TC_VM_DIR}/runtime/tc_sem_bitwise.c
    ${TC_VM_DIR}/runtime/tc_io.c
    ${TC_VM_DIR}/lexer/tc_lexer.c
    ${TC_VM_DIR}/parser/tc_parser.c
    ${TC_VM_DIR}/parser/tc_parser_rhs.c
    ${TC_VM_DIR}/parser/tc_parser_free.c
    ${TC_VM_DIR}/analyzer/tc_analyzer.c
    ${TC_VM_DIR}/analyzer/tc_analyzer_pass1.c
    ${TC_VM_DIR}/analyzer/tc_analyzer_pass2.c
    ${TC_VM_DIR}/analyzer/tc_analyzer_dfa.c
    ${TC_VM_DIR}/analyzer/tc_analyzer_repl.c
    ${TC_VM_DIR}/analyzer/tc_cfg.c
    ${TC_VM_DIR}/analyzer/tc_const_eval.c
    ${TC_VM_DIR}/executor/tc_executor.c
)

add_library(libtc STATIC ${TC_LIBTC_SOURCES})
target_include_directories(libtc PUBLIC ${TC_LIBTC_INCLUDE_DIRS})
target_compile_options(libtc PRIVATE -std=c99 -Wall -Wextra -pedantic)
