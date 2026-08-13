# TC-AOT: TC → C99 ahead-of-time compiler
set(TC_AOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(TC_VM_DIR ${CMAKE_SOURCE_DIR}/src/vm)

set(TC_AOT_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/src/libtc
    ${TC_AOT_DIR}
    ${TC_VM_DIR}/runtime
    ${TC_VM_DIR}/lexer
    ${TC_VM_DIR}/parser
    ${TC_VM_DIR}/analyzer
    ${TC_VM_DIR}/executor
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/aot/bin)

add_executable(tc-aot
    main.c
    tc_aot_codegen.c
    tc_aot_emit_rhs.c
    tc_aot_emit_stmt.c
    tc_aot_emit_func.c
)

target_link_libraries(tc-aot PRIVATE libtc)
target_include_directories(tc-aot PRIVATE ${TC_AOT_INCLUDE_DIRS})
target_compile_definitions(tc-aot PRIVATE
    TC_AOT_RT_DIR="${TC_AOT_DIR}"
    TC_VM_DIR="${TC_VM_DIR}"
)
if(TC_HAVE_FENV AND NOT TC_FORCE_NO_FENV)
    target_compile_definitions(tc-aot PRIVATE TC_AOT_HAVE_FENV=1)
endif()
target_compile_options(tc-aot PRIVATE -std=c99 -Wall -Wextra -pedantic)

add_custom_target(check-aot
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/aot/run_tests.sh
    DEPENDS tc-aot tc-vm
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running TC-AOT differential tests"
)
