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
    frontend/arena.c
    frontend/num.c
    frontend/lex.c
    frontend/parse.c
    frontend/module.c
    frontend/sem.c
    frontend/ct.c
    frontend/irgen.c
    # AArch64 backend (assembly emission; toolchain driver below).
    backend/aarch64/emit.c
    backend/aarch64/emit_abi.c
    backend/aarch64/emit_ops.c
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
# A clang that can target aarch64-none-elf and an archiver that preserves ELF
# members are required; falling back to the host `ar` silently produces an
# unusable archive on platforms whose ar only understands Mach-O.
find_program(TC_AARCH64_CLANG NAMES clang
    HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin /opt/homebrew/bin)
if(NOT TC_AARCH64_CLANG)
    message(FATAL_ERROR
        "clang is required to build the AArch64 freestanding runtime")
endif()
find_program(TC_LLVM_ARCHIVER NAMES llvm-ar aarch64-elf-ar
    HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin /opt/homebrew/bin)
if(NOT TC_LLVM_ARCHIVER)
    message(FATAL_ERROR
        "llvm-ar or aarch64-elf-ar is required to build the AArch64 runtime")
endif()
find_program(TC_AARCH64_LINKER NAMES ld.lld
    HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin /opt/homebrew/bin)
if(NOT TC_AARCH64_LINKER)
    set(TC_AARCH64_LINKER "ld.lld")
endif()
set(TC_AARCH64_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR}/aarch64-runtime)
set(TC_AARCH64_RUNTIME_LIB ${TC_AARCH64_RUNTIME_DIR}/libtc_aarch64_runtime.a)
set(TC_AARCH64_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/backend/aarch64/link.ld)
target_compile_definitions(tc_core PRIVATE
    TC_AARCH64_RUNTIME_LIB="${TC_AARCH64_RUNTIME_LIB}"
    TC_AARCH64_LINKER_SCRIPT="${TC_AARCH64_LINKER_SCRIPT}"
    TC_AARCH64_ASSEMBLER="${TC_AARCH64_CLANG}"
    TC_AARCH64_LINKER="${TC_AARCH64_LINKER}")
set(TC_AARCH64_RUNTIME_SOURCES
    backend/aarch64/runtime.c
    runtime/semantics.c
    runtime/format.c
    runtime/input.c
    frontend/arena.c
    frontend/num.c
    ir/type.c)
set(TC_AARCH64_RUNTIME_OBJECTS)
foreach(source IN LISTS TC_AARCH64_RUNTIME_SOURCES)
    get_filename_component(name ${source} NAME_WE)
    set(object ${TC_AARCH64_RUNTIME_DIR}/${name}.o)
    add_custom_command(
        OUTPUT ${object}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${TC_AARCH64_RUNTIME_DIR}
        COMMAND ${TC_AARCH64_CLANG} -target aarch64-none-elf -ffreestanding
                -nostdlib -O2 -std=c11
                -fno-vectorize -fno-slp-vectorize -fno-stack-protector
                -mstrict-align
                -Wall -Wextra -Werror -I ${CMAKE_CURRENT_SOURCE_DIR}
                -c ${CMAKE_CURRENT_SOURCE_DIR}/${source}
                -o ${object}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${source}
                ${CMAKE_CURRENT_SOURCE_DIR}/ir/type.h
                ${CMAKE_CURRENT_SOURCE_DIR}/ir/type_width.h
                ${CMAKE_CURRENT_SOURCE_DIR}/ir/region.h
                ${CMAKE_CURRENT_SOURCE_DIR}/ir/instr.h
                ${CMAKE_CURRENT_SOURCE_DIR}/base/assert.h
                ${CMAKE_CURRENT_SOURCE_DIR}/diagnostic/codes.h
                ${CMAKE_CURRENT_SOURCE_DIR}/diagnostic/codes.inc
                ${CMAKE_CURRENT_SOURCE_DIR}/runtime/rt_memory.h
                ${CMAKE_CURRENT_SOURCE_DIR}/backend/aarch64/rt_abi.h
        COMMENT "Compiling freestanding AArch64 runtime member ${source}"
        VERBATIM)
    list(APPEND TC_AARCH64_RUNTIME_OBJECTS ${object})
endforeach()
add_custom_command(
    OUTPUT ${TC_AARCH64_RUNTIME_LIB}
    COMMAND ${TC_AARCH64_CLANG} -target aarch64-none-elf -ffreestanding
            -nostdlib -c ${CMAKE_CURRENT_SOURCE_DIR}/backend/aarch64/start.S
            -o ${TC_AARCH64_RUNTIME_DIR}/start.o
    COMMAND ${TC_LLVM_ARCHIVER} rcs ${TC_AARCH64_RUNTIME_LIB}
            ${TC_AARCH64_RUNTIME_OBJECTS} ${TC_AARCH64_RUNTIME_DIR}/start.o
    DEPENDS ${TC_AARCH64_RUNTIME_OBJECTS}
            ${CMAKE_CURRENT_SOURCE_DIR}/backend/aarch64/start.S
    COMMENT "Building AArch64 freestanding runtime archive"
    VERBATIM)
add_custom_target(tc_aarch64_runtime ALL DEPENDS ${TC_AARCH64_RUNTIME_LIB})

add_subdirectory(driver)
