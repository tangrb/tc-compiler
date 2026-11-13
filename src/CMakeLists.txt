# TC 0.0.46 implementation.
#
# S0 freezes the contracts and builds the private tc_core library plus the two
# tool entry points.  No install rule, no exported headers, no host ABI.

add_library(tc_core STATIC
    ir/type.c
    ir/instr.c
    runtime/alloc.c
    runtime/profiles.c
    runtime/module_loader.c
    frontend/diagnostic.c
    frontend/diagnostic_table.c
    frontend/optimizer.c
    frontend/contract.c
    driver/cli.c
)

target_include_directories(tc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(tc_core PRIVATE -Wall -Wextra -Werror -Wpedantic)
set_target_properties(tc_core PROPERTIES
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE OFF)

add_subdirectory(driver)
