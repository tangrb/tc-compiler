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
    # AArch64 subset backend (assembly emission; toolchain driver below).
    backend/aarch64/emit.c
    backend/common/toolchain.c
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
    driver/aot_driver.c
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

# Freestanding AArch64 runtime archive used by `tc-aot --emit=obj/elf`.
# Apple clang can assemble/link AArch64 ELF objects; no host C library is used.
find_program(TC_LLVM_ARCHIVER NAMES llvm-ar
    HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin /opt/homebrew/bin)
if(NOT TC_LLVM_ARCHIVER)
    set(TC_LLVM_ARCHIVER ${CMAKE_AR})
endif()
set(TC_AARCH64_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR}/aarch64-runtime)
set(TC_AARCH64_RUNTIME_LIB ${TC_AARCH64_RUNTIME_DIR}/libtc_aarch64_runtime.a)
set(TC_AARCH64_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/backend/aarch64/link.ld)
target_compile_definitions(tc_core PRIVATE
    TC_AARCH64_RUNTIME_LIB="${TC_AARCH64_RUNTIME_LIB}"
    TC_AARCH64_LINKER_SCRIPT="${TC_AARCH64_LINKER_SCRIPT}")
add_custom_command(
    OUTPUT ${TC_AARCH64_RUNTIME_LIB}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${TC_AARCH64_RUNTIME_DIR}
    COMMAND ${CMAKE_C_COMPILER} -target aarch64-none-elf -ffreestanding
            -nostdlib -fno-zero-initialized-in-bss -O2 -std=c11
            -fno-vectorize -fno-slp-vectorize -fno-stack-protector
            -Wall -Wextra -Werror -I ${CMAKE_CURRENT_SOURCE_DIR}
            -c ${CMAKE_CURRENT_SOURCE_DIR}/backend/aarch64/runtime.c
            -o ${TC_AARCH64_RUNTIME_DIR}/runtime.o
    COMMAND ${CMAKE_C_COMPILER} -target aarch64-none-elf -ffreestanding
            -nostdlib -c ${CMAKE_CURRENT_SOURCE_DIR}/backend/aarch64/start.S
            -o ${TC_AARCH64_RUNTIME_DIR}/start.o
    COMMAND ${TC_LLVM_ARCHIVER} rcs ${TC_AARCH64_RUNTIME_LIB}
            ${TC_AARCH64_RUNTIME_DIR}/runtime.o ${TC_AARCH64_RUNTIME_DIR}/start.o
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/backend/aarch64/runtime.c
            ${CMAKE_CURRENT_SOURCE_DIR}/backend/aarch64/start.S
            ${CMAKE_CURRENT_SOURCE_DIR}/ir/type.h
            ${CMAKE_CURRENT_SOURCE_DIR}/base/assert.h
            ${CMAKE_CURRENT_SOURCE_DIR}/diagnostic/codes.h
            ${CMAKE_CURRENT_SOURCE_DIR}/diagnostic/codes.inc
    COMMENT "Building AArch64 freestanding runtime archive"
    VERBATIM)
add_custom_target(tc_aarch64_runtime ALL DEPENDS ${TC_AARCH64_RUNTIME_LIB})

add_subdirectory(driver)
