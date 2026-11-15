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
    # IR representation and shape helpers.
    ir/type.c
    ir/instr.c
    ir/pointer_state.c
    # Runtime seams and platform profiles.
    runtime/alloc.c
    runtime/module_loader.c
    runtime/profile.c
    # Code shared by the two tool entry points.
    driver/cli.c
    driver/frontend_driver.c
    driver/profile_select.c
)

target_include_directories(tc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(tc_core PRIVATE -Wall -Wextra -Werror -Wpedantic)
set_target_properties(tc_core PROPERTIES
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE OFF)

add_subdirectory(driver)
