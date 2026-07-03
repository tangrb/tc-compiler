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
    compile.c
    ${TC_VM_DIR}/runtime/types.c
    ${TC_VM_DIR}/runtime/diagnostic.c
    ${TC_VM_DIR}/runtime/symbol.c
    ${TC_VM_DIR}/runtime/warning.c
    ${TC_VM_DIR}/runtime/semantics.c
    ${TC_VM_DIR}/lexer/lexer.c
    ${TC_VM_DIR}/parser/parser.c
    ${TC_VM_DIR}/analyzer/analyzer.c
    ${TC_VM_DIR}/executor/executor.c
)

add_library(libtc STATIC ${TC_LIBTC_SOURCES})
target_include_directories(libtc PUBLIC ${TC_LIBTC_INCLUDE_DIRS})
target_compile_options(libtc PRIVATE -std=c99 -Wall -Wextra -pedantic)
