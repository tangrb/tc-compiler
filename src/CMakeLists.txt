# TC 0.0.46 implementation.
#
# The frozen contracts build the private tc_core library plus the two tool
# entry points.  No install rule, no exported headers, no host ABI.

add_library(tc_core STATIC
    # Cross-cutting frozen contracts.
    contract/contract.c
    contract/optimizer.c
    # Closed diagnostic set, table and first-diagnostic selection.
    diagnostic/diagnostic.c
    diagnostic/table.c
    # LEX/SYN front end: source/arena, exact decimal conversion, LEX and SYN.
    frontend/input.c
    frontend/num.c
    frontend/lex.c
    frontend/parse.c
    frontend/module.c
    frontend/sem.c
    frontend/ct.c
    frontend/irgen.c
    # IR representation and shape helpers.
    ir/type.c
    ir/instr.c
    ir/pointer_state.c
    ir/print.c
    # Runtime semantic core, deterministic I/O, platform profiles and seams.
    runtime/alloc.c
    runtime/format.c
    runtime/input.c
    runtime/module_loader.c
    runtime/profile.c
    runtime/semantics.c
    # Reference VM: memory/objects and interpreter.
    vm/memory.c
    vm/interp.c
    # Code shared by the two tool entry points.
    driver/cli.c
    driver/frontend_driver.c
    driver/profile_select.c
)

target_include_directories(tc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(UNIX AND NOT APPLE)
    # fmod/fmodf for the exact SEM constant evaluator.
    target_link_libraries(tc_core PUBLIC m)
endif()
target_compile_options(tc_core PRIVATE -Wall -Wextra -Werror -Wpedantic
    -ffp-contract=off)
set_target_properties(tc_core PROPERTIES
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE OFF)

add_subdirectory(driver)
