# The frozen 0.0.46 language contracts and 0.0.47 toolchain build the private
# tc_core library plus three tool entry points.  No install rule, no exported
# headers, no host ABI.

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
    backend/arm32/emit.c
    backend/arm32/emit_abi.c
    backend/arm32/emit_ops.c
    backend/riscv32/emit.c
    backend/riscv32/emit_abi.c
    backend/riscv32/emit_ops.c
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
    runtime/wide.c
    # Reference VM: memory/objects and interpreter.
    vm/memory.c
    vm/interp.c
    # Project model.
    project/name.c
    project/manifest.c
    project/init.c

    # Code shared by the tool entry points.
    driver/cli.c
    driver/frontend_driver.c
    driver/aot_driver.c
    driver/profile_select.c
)

target_include_directories(tc_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(UNIX AND NOT APPLE)
    # fmod/fmodf for the exact SEM constant evaluator.
    target_link_libraries(tc_core PUBLIC m)
endif()
target_compile_options(tc_core PRIVATE -Wall -Wextra -Werror -Wpedantic
    -ffp-contract=off)
set_target_properties(tc_core PROPERTIES
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE OFF)

# Freestanding target runtimes used by `tc-aot --emit=obj/elf/flat`.
#
# A clang that can target aarch64-none-elf / thumbv7em-none-eabihf and an
# archiver that preserves ELF members are required; falling back to the host
# `ar` silently produces an unusable archive on platforms whose ar only
# understands Mach-O.  Each target compiles the shared runtime core and the
# semantic core once; only the platform seam, startup and (for ARM32) the ARM
# EABI helpers differ.  ARM32 uses the Cortex-M7 FPv5-D unit with the base
# AAPCS32 softfp calling convention: float64 values travel in core-register
# pairs while arithmetic uses the hardware unit.
find_program(TC_TARGET_CLANG NAMES clang
    HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin /opt/homebrew/bin)
if(NOT TC_TARGET_CLANG)
    message(FATAL_ERROR
        "clang is required to build the freestanding target runtimes")
endif()
find_program(TC_LLVM_ARCHIVER NAMES llvm-ar aarch64-elf-ar
    HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin /opt/homebrew/bin)
if(NOT TC_LLVM_ARCHIVER)
    message(FATAL_ERROR
        "llvm-ar or aarch64-elf-ar is required to build the target runtimes")
endif()
find_program(TC_AARCH64_LINKER NAMES ld.lld
    HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin /opt/homebrew/bin)
if(NOT TC_AARCH64_LINKER)
    set(TC_AARCH64_LINKER "ld.lld")
endif()

set(TC_RUNTIME_COMMON_SOURCES
    backend/common/runtime.c
    runtime/semantics.c
    runtime/wide.c
    runtime/format.c
    runtime/input.c
    frontend/arena.c
    frontend/num.c
    ir/type.c)
set(TC_RUNTIME_DEPENDENCIES
    ${CMAKE_CURRENT_SOURCE_DIR}/ir/type.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ir/type_width.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ir/region.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ir/instr.h
    ${CMAKE_CURRENT_SOURCE_DIR}/base/assert.h
    ${CMAKE_CURRENT_SOURCE_DIR}/diagnostic/codes.h
    ${CMAKE_CURRENT_SOURCE_DIR}/diagnostic/codes.inc
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/rt_memory.h
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/wide.h
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/semantics.h
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/format.h
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/input.h
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/profile.h
    ${CMAKE_CURRENT_SOURCE_DIR}/backend/common/rt_abi.h
    ${CMAKE_CURRENT_SOURCE_DIR}/backend/common/rt_platform.h
    ${CMAKE_CURRENT_SOURCE_DIR}/backend/common/rt_image.h)

function(tc_compile_runtime_objects out_var directory)
    set(objects)
    foreach(source IN LISTS TC_RUNTIME_COMMON_SOURCES)
        get_filename_component(name ${source} NAME_WE)
        set(object ${directory}/${name}.o)
        add_custom_command(
            OUTPUT ${object}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${directory}
            COMMAND ${TC_TARGET_CLANG} ${ARGN}
                    -Wall -Wextra -Werror -I ${CMAKE_CURRENT_SOURCE_DIR}
                    -c ${CMAKE_CURRENT_SOURCE_DIR}/${source}
                    -o ${object}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${source}
                    ${TC_RUNTIME_DEPENDENCIES}
            COMMENT "Compiling freestanding ${directory} member ${source}"
            VERBATIM)
        list(APPEND objects ${object})
    endforeach()
    set(${out_var} ${objects} PARENT_SCOPE)
endfunction()

function(tc_compile_runtime_member out_var object source directory)
    add_custom_command(
        OUTPUT ${object}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${directory}
        COMMAND ${TC_TARGET_CLANG} ${ARGN}
                -Wall -Wextra -Werror -I ${CMAKE_CURRENT_SOURCE_DIR}
                -c ${CMAKE_CURRENT_SOURCE_DIR}/${source}
                -o ${object}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${source}
                ${TC_RUNTIME_DEPENDENCIES}
        COMMENT "Compiling freestanding ${directory} member ${source}"
        VERBATIM)
    set(${out_var} ${object} PARENT_SCOPE)
endfunction()

set(TC_AARCH64_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR}/aarch64-runtime)
set(TC_AARCH64_RUNTIME_LIB ${TC_AARCH64_RUNTIME_DIR}/libtc_aarch64_runtime.a)
set(TC_AARCH64_BAREMETAL_RUNTIME_LIB
    ${TC_AARCH64_RUNTIME_DIR}/libtc_aarch64_baremetal_runtime.a)
set(TC_AARCH64_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/backend/aarch64/link.ld)
set(TC_ARM32_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR}/arm32-runtime)
set(TC_ARM32_RUNTIME_LIB ${TC_ARM32_RUNTIME_DIR}/libtc_arm32_runtime.a)
set(TC_ARM32_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/backend/arm32/link.ld)
set(TC_RISCV32_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR}/riscv32-runtime)
set(TC_RISCV32_RUNTIME_LIB
    ${TC_RISCV32_RUNTIME_DIR}/libtc_riscv32_runtime.a)
set(TC_RISCV32_LINKER_SCRIPT
    ${CMAKE_CURRENT_SOURCE_DIR}/backend/riscv32/link.ld)
target_compile_definitions(tc_core PRIVATE
    TC_AARCH64_RUNTIME_LIB="${TC_AARCH64_RUNTIME_LIB}"
    TC_AARCH64_BAREMETAL_RUNTIME_LIB="${TC_AARCH64_BAREMETAL_RUNTIME_LIB}"
    TC_ARM32_RUNTIME_LIB="${TC_ARM32_RUNTIME_LIB}"
    TC_RISCV32_RUNTIME_LIB="${TC_RISCV32_RUNTIME_LIB}"
    TC_AARCH64_LINKER_SCRIPT="${TC_AARCH64_LINKER_SCRIPT}"
    TC_ARM32_LINKER_SCRIPT="${TC_ARM32_LINKER_SCRIPT}"
    TC_RISCV32_LINKER_SCRIPT="${TC_RISCV32_LINKER_SCRIPT}"
    TC_TARGET_ASSEMBLER="${TC_TARGET_CLANG}"
    TC_TARGET_LINKER="${TC_AARCH64_LINKER}")

set(TC_AARCH64_FLAGS
    -target aarch64-none-elf -ffreestanding -nostdlib -O2 -std=c11
    -ffp-contract=off -fno-vectorize -fno-slp-vectorize
    -fno-stack-protector -mstrict-align)
tc_compile_runtime_objects(TC_AARCH64_COMMON_OBJECTS
    ${TC_AARCH64_RUNTIME_DIR}/common ${TC_AARCH64_FLAGS})
tc_compile_runtime_member(TC_AARCH64_SEMIHOST_PLATFORM
    ${TC_AARCH64_RUNTIME_DIR}/platform_semihost.o
    backend/aarch64/platform_semihost.c ${TC_AARCH64_RUNTIME_DIR}/common
    ${TC_AARCH64_FLAGS})
tc_compile_runtime_member(TC_AARCH64_BAREMETAL_PLATFORM
    ${TC_AARCH64_RUNTIME_DIR}/platform_baremetal.o
    backend/aarch64/platform_baremetal.c ${TC_AARCH64_RUNTIME_DIR}/common
    ${TC_AARCH64_FLAGS})
tc_compile_runtime_member(TC_AARCH64_START
    ${TC_AARCH64_RUNTIME_DIR}/start.o backend/aarch64/start.S
    ${TC_AARCH64_RUNTIME_DIR}/common ${TC_AARCH64_FLAGS})
add_custom_command(
    OUTPUT ${TC_AARCH64_RUNTIME_LIB}
    COMMAND ${TC_LLVM_ARCHIVER} rcs ${TC_AARCH64_RUNTIME_LIB}
            ${TC_AARCH64_COMMON_OBJECTS} ${TC_AARCH64_SEMIHOST_PLATFORM}
            ${TC_AARCH64_START}
    DEPENDS ${TC_AARCH64_COMMON_OBJECTS} ${TC_AARCH64_SEMIHOST_PLATFORM}
            ${TC_AARCH64_START}
    COMMENT "Building AArch64 semihosting runtime archive"
    VERBATIM)
add_custom_command(
    OUTPUT ${TC_AARCH64_BAREMETAL_RUNTIME_LIB}
    COMMAND ${TC_LLVM_ARCHIVER} rcs ${TC_AARCH64_BAREMETAL_RUNTIME_LIB}
            ${TC_AARCH64_COMMON_OBJECTS} ${TC_AARCH64_BAREMETAL_PLATFORM}
            ${TC_AARCH64_START}
    DEPENDS ${TC_AARCH64_COMMON_OBJECTS} ${TC_AARCH64_BAREMETAL_PLATFORM}
            ${TC_AARCH64_START}
    COMMENT "Building AArch64 bare-metal runtime archive"
    VERBATIM)
add_custom_target(tc_aarch64_runtime ALL
    DEPENDS ${TC_AARCH64_RUNTIME_LIB} ${TC_AARCH64_BAREMETAL_RUNTIME_LIB})

set(TC_ARM32_FLAGS
    -target thumbv7em-none-eabihf -mcpu=cortex-m7 -mfloat-abi=softfp
    -mfpu=fpv5-d16 -ffreestanding -nostdlib -O2 -std=c11
    -ffp-contract=off -fno-vectorize -fno-slp-vectorize
    -fno-stack-protector)
tc_compile_runtime_objects(TC_ARM32_COMMON_OBJECTS
    ${TC_ARM32_RUNTIME_DIR}/common ${TC_ARM32_FLAGS})
tc_compile_runtime_member(TC_ARM32_PLATFORM
    ${TC_ARM32_RUNTIME_DIR}/platform_mps2.o backend/arm32/platform_mps2.c
    ${TC_ARM32_RUNTIME_DIR}/common ${TC_ARM32_FLAGS})
tc_compile_runtime_member(TC_ARM32_AEABI
    ${TC_ARM32_RUNTIME_DIR}/aeabi.o backend/arm32/aeabi.c
    ${TC_ARM32_RUNTIME_DIR}/common ${TC_ARM32_FLAGS})
tc_compile_runtime_member(TC_ARM32_HELPERS
    ${TC_ARM32_RUNTIME_DIR}/rt_helpers.o backend/arm32/rt_helpers.c
    ${TC_ARM32_RUNTIME_DIR}/common ${TC_ARM32_FLAGS})
tc_compile_runtime_member(TC_ARM32_AEABI_GLUE
    ${TC_ARM32_RUNTIME_DIR}/aeabi_glue.o backend/arm32/aeabi_glue.S
    ${TC_ARM32_RUNTIME_DIR}/common ${TC_ARM32_FLAGS})
tc_compile_runtime_member(TC_ARM32_START
    ${TC_ARM32_RUNTIME_DIR}/start.o backend/arm32/start.S
    ${TC_ARM32_RUNTIME_DIR}/common ${TC_ARM32_FLAGS})
add_custom_command(
    OUTPUT ${TC_ARM32_RUNTIME_LIB}
    COMMAND ${TC_LLVM_ARCHIVER} rcs ${TC_ARM32_RUNTIME_LIB}
            ${TC_ARM32_COMMON_OBJECTS} ${TC_ARM32_PLATFORM} ${TC_ARM32_AEABI}
            ${TC_ARM32_HELPERS} ${TC_ARM32_AEABI_GLUE} ${TC_ARM32_START}
    DEPENDS ${TC_ARM32_COMMON_OBJECTS} ${TC_ARM32_PLATFORM} ${TC_ARM32_AEABI}
            ${TC_ARM32_HELPERS} ${TC_ARM32_AEABI_GLUE} ${TC_ARM32_START}
    COMMENT "Building ARM32 freestanding runtime archive"
    VERBATIM)
add_custom_target(tc_arm32_runtime ALL DEPENDS ${TC_ARM32_RUNTIME_LIB})

set(TC_RISCV32_FLAGS
    -target riscv32-unknown-elf -march=rv32imafdc -mabi=ilp32
    -mno-relax -msmall-data-limit=0 -mstrict-align
    -ffreestanding -nostdlib -O2 -std=c11
    -ffp-contract=off -fno-vectorize -fno-slp-vectorize
    -fno-stack-protector)
tc_compile_runtime_objects(TC_RISCV32_COMMON_OBJECTS
    ${TC_RISCV32_RUNTIME_DIR}/common ${TC_RISCV32_FLAGS})
tc_compile_runtime_member(TC_RISCV32_PLATFORM
    ${TC_RISCV32_RUNTIME_DIR}/platform_virt.o
    backend/riscv32/platform_virt.c ${TC_RISCV32_RUNTIME_DIR}/common
    ${TC_RISCV32_FLAGS})
tc_compile_runtime_member(TC_RISCV32_HELPERS
    ${TC_RISCV32_RUNTIME_DIR}/rt_helpers.o backend/riscv32/rt_helpers.c
    ${TC_RISCV32_RUNTIME_DIR}/common ${TC_RISCV32_FLAGS})
tc_compile_runtime_member(TC_RISCV32_COMPILER_RT
    ${TC_RISCV32_RUNTIME_DIR}/compiler_rt.o backend/riscv32/compiler_rt.c
    ${TC_RISCV32_RUNTIME_DIR}/common ${TC_RISCV32_FLAGS})
tc_compile_runtime_member(TC_RISCV32_SEMI
    ${TC_RISCV32_RUNTIME_DIR}/semi.o backend/riscv32/semi.S
    ${TC_RISCV32_RUNTIME_DIR}/common ${TC_RISCV32_FLAGS})
tc_compile_runtime_member(TC_RISCV32_START
    ${TC_RISCV32_RUNTIME_DIR}/start.o backend/riscv32/start.S
    ${TC_RISCV32_RUNTIME_DIR}/common ${TC_RISCV32_FLAGS})
add_custom_command(
    OUTPUT ${TC_RISCV32_RUNTIME_LIB}
    COMMAND ${TC_LLVM_ARCHIVER} rcs ${TC_RISCV32_RUNTIME_LIB}
            ${TC_RISCV32_COMMON_OBJECTS} ${TC_RISCV32_PLATFORM}
            ${TC_RISCV32_HELPERS} ${TC_RISCV32_COMPILER_RT}
            ${TC_RISCV32_SEMI} ${TC_RISCV32_START}
    DEPENDS ${TC_RISCV32_COMMON_OBJECTS} ${TC_RISCV32_PLATFORM}
            ${TC_RISCV32_HELPERS} ${TC_RISCV32_COMPILER_RT}
            ${TC_RISCV32_SEMI} ${TC_RISCV32_START}
    COMMENT "Building RISC-V 32 freestanding runtime archive"
    VERBATIM)
add_custom_target(tc_riscv32_runtime ALL
    DEPENDS ${TC_RISCV32_RUNTIME_LIB})

add_subdirectory(driver)
