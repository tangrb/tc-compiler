# TC 0.0.46 implementation.
#
# S0 freezes the contracts and builds the private tc_core library plus the two
# tool entry points.  No install rule, no exported headers, no host ABI.

add_library(tc_core STATIC
    # Cross-cutting S0 contracts.
    contract/contract.c
    contract/optimizer.c
    # Closed diagnostic set, table and first-diagnostic selection.
    diagnostic/diagnostic.c
    diagnostic/table.c
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
    driver/profile_select.c
)

target_include_directories(tc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(tc_core PRIVATE -Wall -Wextra -Werror -Wpedantic)
set_target_properties(tc_core PROPERTIES
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE OFF)

add_subdirectory(driver)
