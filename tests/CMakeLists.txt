# Contract tests: contract units, code-table/traceability checks, harness and
# golden self-tests, CLI contract.  All tests are deterministic and offline.

find_package(Python3 COMPONENTS Interpreter REQUIRED)

file(MAKE_DIRECTORY ${PROJECT_SOURCE_DIR}/.tmp)
add_compile_definitions(TC_TEST_TMP_ROOT="${PROJECT_SOURCE_DIR}/.tmp")

add_executable(tc_contract_test unit/contract_test.c)
target_link_libraries(tc_contract_test PRIVATE tc_core)
target_include_directories(tc_contract_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(tc_contract_test PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc_frontend_test unit/frontend_test.c)
target_link_libraries(tc_frontend_test PRIVATE tc_core)
target_include_directories(tc_frontend_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(tc_frontend_test PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc_sem_test unit/sem_test.c)
target_link_libraries(tc_sem_test PRIVATE tc_core)
target_include_directories(tc_sem_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(tc_sem_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
target_compile_definitions(tc_sem_test PRIVATE
    TC_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(tc_ir_test unit/ir_test.c)
target_link_libraries(tc_ir_test PRIVATE tc_core)
target_include_directories(tc_ir_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(tc_ir_test PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc_linemap_test unit/linemap_test.c)
target_link_libraries(tc_linemap_test PRIVATE tc_core)
target_include_directories(tc_linemap_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(tc_linemap_test PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc_debug_fixture unit/debug_fixture.c)
target_link_libraries(tc_debug_fixture PRIVATE tc_core)
target_include_directories(tc_debug_fixture PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(tc_debug_fixture PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc_ct_test unit/ct_test.c)
target_link_libraries(tc_ct_test PRIVATE tc_core)
target_include_directories(tc_ct_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(tc_ct_test PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc_vm_test unit/vm_test.c)
target_link_libraries(tc_vm_test PRIVATE tc_core)
target_include_directories(tc_vm_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(tc_vm_test PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc_gdb_journal_test unit/gdb_journal_test.c)
target_link_libraries(tc_gdb_journal_test PRIVATE tc_core)
target_include_directories(tc_gdb_journal_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(tc_gdb_journal_test PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc_gdb_rsp_test unit/gdb_rsp_test.c)
target_link_libraries(tc_gdb_rsp_test PRIVATE tc_core)
target_include_directories(tc_gdb_rsp_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(tc_gdb_rsp_test PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc_aot_test unit/aot_test.c)
target_link_libraries(tc_aot_test PRIVATE tc_core)
target_include_directories(tc_aot_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(tc_aot_test PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc_oom_test unit/oom_test.c)
target_link_libraries(tc_oom_test PRIVATE tc_core)
target_include_directories(tc_oom_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(tc_oom_test PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc_project_manifest_test unit/project_manifest_test.c)
target_link_libraries(tc_project_manifest_test PRIVATE tc_core)
target_include_directories(tc_project_manifest_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(tc_project_manifest_test PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc_project_init_fault_test project/init_fault_driver.c)
target_link_libraries(tc_project_init_fault_test PRIVATE tc_core)
target_include_directories(tc_project_init_fault_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(tc_project_init_fault_test PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc_path_test unit/path_test.c)
target_link_libraries(tc_path_test PRIVATE tc_core)
target_include_directories(tc_path_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(tc_path_test PRIVATE
    TC_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(tc_path_test PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc_dump_codes unit/dump_codes.c)
target_link_libraries(tc_dump_codes PRIVATE tc_core)
target_include_directories(tc_dump_codes PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(tc_dump_codes PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_test(NAME contract.unit COMMAND tc_contract_test)
add_test(NAME frontend.unit COMMAND tc_frontend_test)
add_test(NAME sem.unit COMMAND tc_sem_test)
add_test(NAME ct.unit COMMAND tc_ct_test)
add_test(NAME ir.unit COMMAND tc_ir_test)
add_test(NAME linemap.unit COMMAND tc_linemap_test)
add_test(NAME vm.unit COMMAND tc_vm_test)
add_test(NAME gdb_journal.unit COMMAND tc_gdb_journal_test)
add_test(NAME gdb_rsp.unit COMMAND tc_gdb_rsp_test)
add_test(NAME vm.gdb.protocol
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/tools/vm_gdb_protocol_test.py
                 $<TARGET_FILE:tc-vm>)
add_test(NAME vm.gdb.semantics
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/tools/vm_gdb_semantics_test.py
                 $<TARGET_FILE:tc-vm>)
set_tests_properties(gdb_journal.unit gdb_rsp.unit vm.gdb.protocol vm.gdb.semantics
                    PROPERTIES LABELS "unit;toolchain;fast" TIMEOUT 180)
add_test(NAME aot.unit COMMAND tc_aot_test)
add_test(NAME oom.unit COMMAND tc_oom_test)
add_test(NAME project.manifest
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/project/manifest_test.py --check
                 $<TARGET_FILE:tc_project_manifest_test>)
add_test(NAME project.init
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/project/tc_cli_test.py
                 $<TARGET_FILE:tc>)
add_test(NAME project.test_runner
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/project/test_runner_test.py --check
                 $<TARGET_FILE:tc>)
add_test(NAME project.paths_unit COMMAND tc_path_test)
add_test(NAME project.paths
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/project/path_test.py --check
                 $<TARGET_FILE:tc-vm>)
add_test(NAME project.init_fault
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/project/init_fault_test.py
                 $<TARGET_FILE:tc_project_init_fault_test>)
add_test(NAME release.audit_selftest
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/tools/s10_release_audit.py
                 --selftest)
add_test(NAME contract.dump_codes COMMAND tc_dump_codes)
add_test(NAME conformance.s2s3_cases
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/conformance/static/s2s3_cases.py --check)
add_test(NAME conformance.s1_cases
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/conformance/static/s1_cases.py --check)

add_test(NAME conformance.s4_cases
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/conformance/static/s4_cases.py --check)

add_test(NAME conformance.s5_cases
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/conformance/static/s5_cases.py --check)

add_test(NAME conformance.s6_cases
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/conformance/runtime/s6_cases.py --check)

add_test(NAME conformance.s6_static_cases
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/conformance/static/s6_static_cases.py
                 --check)

add_test(
    NAME docs.cleanup
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/docs_cleanup_check.py)

add_test(
    NAME cli.contract
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/cli_contract_test.py
            $<TARGET_FILE:tc-vm> $<TARGET_FILE:tc-aot> $<TARGET_FILE:tc>)
add_test(
    NAME t1.observe
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/t1_observe_test.py
            $<TARGET_FILE:tc-vm> $<TARGET_FILE:tc-aot>)
set_tests_properties(t1.observe PROPERTIES LABELS "unit;toolchain;fast")
set_tests_properties(contract.unit cli.contract t1.observe oom.unit release.audit_selftest
                    project.manifest project.init project.init_fault
                    project.test_runner
                    project.paths_unit project.paths
                    PROPERTIES LABELS "unit;toolchain;fast")

# --- traceability / golden / harness checks ----------------------------------

set(TC_TRACE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/traceability)
set(TC_GOLDEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/golden)

add_test(NAME trace.gen_codes
         COMMAND ${Python3_EXECUTABLE} ${TC_TRACE_DIR}/gen_codes.py --check)
add_test(NAME trace.gen_c_table
         COMMAND ${Python3_EXECUTABLE} ${TC_TRACE_DIR}/gen_c_table.py --check)
add_test(NAME trace.codes
         COMMAND ${Python3_EXECUTABLE} ${TC_TRACE_DIR}/check_codes.py
                 --c-table-dump $<TARGET_FILE:tc_dump_codes>)
add_test(NAME trace.coverage
         COMMAND ${Python3_EXECUTABLE} ${TC_TRACE_DIR}/coverage.py --check)
add_test(NAME trace.case_ids
         COMMAND ${Python3_EXECUTABLE} ${TC_TRACE_DIR}/check_case_ids.py)
add_test(NAME trace.golden
         COMMAND ${Python3_EXECUTABLE} ${TC_GOLDEN_DIR}/validate.py)

set_tests_properties(
    trace.gen_codes trace.gen_c_table trace.codes trace.coverage trace.case_ids
    trace.golden
    PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME harness.selftest
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/harness/selftest.py)
add_test(NAME trace.harness_codes
         COMMAND ${Python3_EXECUTABLE} ${TC_TRACE_DIR}/check_harness_codes.py)
set_tests_properties(harness.selftest trace.harness_codes conformance.s1_cases
                     conformance.s2s3_cases conformance.s4_cases
                     conformance.s5_cases conformance.s6_cases
                     conformance.s6_static_cases
                     PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME golden.selftest
         COMMAND ${Python3_EXECUTABLE} ${TC_GOLDEN_DIR}/selftest.py)
add_test(NAME golden.run
         COMMAND ${Python3_EXECUTABLE} ${TC_GOLDEN_DIR}/run.py
                 $<TARGET_FILE:tc-vm>)
set_tests_properties(golden.selftest golden.run PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# --- S7 AArch64 backend, ABI, toolchain and differential --------------------
#
# These tests need an AArch64 ELF toolchain and qemu.  qemu-user is not
# available on every host (design §11 allows qemu-system as the fallback).  A
# gate build must not silently omit them: configure fails unless the tools are
# present, or the user explicitly disables the suite with
# -DTC_ENABLE_AOT_TESTS=OFF.

find_program(TC_QEMU_SYSTEM_AARCH64 NAMES qemu-system-aarch64)
find_program(TC_QEMU_SYSTEM_ARM NAMES qemu-system-arm)
find_program(TC_QEMU_SYSTEM_RISCV32 NAMES qemu-system-riscv32)
find_program(TC_LLD NAMES ld.lld HINTS /opt/homebrew/opt/llvm/bin
             /usr/local/opt/llvm/bin /opt/homebrew/bin)
find_program(TC_LLVM_DWARFDUMP NAMES llvm-dwarfdump
             HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin)
find_program(TC_GDB NAMES gdb gdb-multiarch)
option(TC_ENABLE_AOT_TESTS "Register S7 AArch64/qemu conformance tests" ON)
if(TC_ENABLE_AOT_TESTS AND
   (NOT TC_QEMU_SYSTEM_AARCH64 OR NOT TC_QEMU_SYSTEM_ARM OR
    NOT TC_QEMU_SYSTEM_RISCV32 OR NOT TC_LLD OR NOT TC_LLVM_DWARFDUMP OR
    NOT TC_GDB))
    message(FATAL_ERROR
        "S7/S9/S10 and T2/T3 AOT tests need qemu-system-aarch64, "
        "qemu-system-arm, qemu-system-riscv32, ld.lld, llvm-dwarfdump "
        "and gdb; install them or configure with -DTC_ENABLE_AOT_TESTS=OFF")
endif()
if(TC_ENABLE_AOT_TESTS)
    add_test(NAME toolchain.debug_elf
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/debug_elf_test.py
                     $<TARGET_FILE:tc-vm> $<TARGET_FILE:tc-aot>
                     ${TC_LLVM_DWARFDUMP} "${TC_GDB}")
    set_tests_properties(toolchain.debug_elf PROPERTIES LABELS "toolchain;aot" TIMEOUT 120)
    add_test(NAME p4a.debug
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/tools/p4a_debug_cases.py
                     --check $<TARGET_FILE:tc_debug_fixture>
                     ${TC_LLVM_DWARFDUMP})
    set_tests_properties(p4a.debug PROPERTIES LABELS "toolchain;aot")
    add_test(NAME t2.debug
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/t2_debug_test.py
                     $<TARGET_FILE:tc-vm> $<TARGET_FILE:tc-aot>
                     ${TC_LLVM_DWARFDUMP})
    set_tests_properties(t2.debug PROPERTIES LABELS "toolchain;aot")
    add_test(NAME t2.gdb
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/t2_gdb_test.py
                     $<TARGET_FILE:tc-aot> ${TC_QEMU_SYSTEM_AARCH64}
                     "${TC_GDB}")
    set_tests_properties(t2.gdb PROPERTIES LABELS "toolchain;aot")
    add_test(NAME vm.gdb.e2e
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/vm_gdb_e2e.py
                     $<TARGET_FILE:tc-vm> "${TC_GDB}")
    set_tests_properties(vm.gdb.e2e PROPERTIES LABELS "toolchain;aot" TIMEOUT 120)
    add_test(NAME project.paths_aot
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/project/path_aot_test.py --check
                     $<TARGET_FILE:tc-aot> ${TC_LLD} ${TC_QEMU_SYSTEM_AARCH64})
    set_tests_properties(project.paths_aot PROPERTIES LABELS "toolchain;aot")
    add_test(NAME conformance.s7_diff
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/diff/s7_cases.py
                     --check)
    add_test(NAME conformance.s7_fuzz
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/diff/s7_fuzz.py
                     --check)
    add_test(NAME conformance.s7_abi
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/abi/s7_abi_cases.py
                     --check)
    add_test(NAME conformance.s7_tools
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/tools/s7_toolchain_cases.py
                     --check)
    add_test(NAME determinism.s7
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/determinism/s7_determinism.py
                     --check)
    add_test(NAME conformance.s7_static_parity
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/tools/s7_static_parity.py
                     --check)
    add_test(NAME conformance.s8_diff
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/diff/s8_cases.py
                     --check)
    add_test(NAME conformance.s8_abi
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/abi/s8_abi_cases.py
                     --check)
    add_test(NAME conformance.s8_abi_fuzz
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/diff/s8_abi_fuzz.py
                     --check)
    add_test(NAME conformance.s8_layout_fuzz
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/diff/s8_layout_fuzz.py
                     --check)
    add_test(NAME conformance.s8_mem_fuzz
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/diff/s8_mem_fuzz.py
                     --check)
    add_test(NAME conformance.s8_fp_fuzz
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/diff/s8_fp_fuzz.py
                     --check)
    add_test(NAME conformance.s8_interop
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/abi/s8_interop_cases.py
                     --check)
    add_test(NAME conformance.s8_checked
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/checked/s8_checked_cases.py
                     --check)
    add_test(NAME conformance.s8_consistency
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/consistency/s8_consistency_cases.py
                     --check)
    add_test(NAME conformance.s8_artifact
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/tools/s8_artifact_cases.py
                     --check)
    add_test(NAME determinism.s8
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/determinism/s8_determinism.py
                     --check)
    # --- S9 freestanding / ARM32 ------------------------------------------
    add_test(NAME conformance.s9_baremetal
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/platform/s9_baremetal_cases.py
                     --check)
    add_test(NAME conformance.s9_arm32
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/platform/s9_arm32_cases.py
                     --check)
    add_test(NAME conformance.s9_arm32_interop
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/abi/s9_arm32_interop.py
                     --check)
    add_test(NAME conformance.abi_reverse
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/abi/reverse_interop_cases.py
                     --check)
    add_test(NAME conformance.s9_static_parity
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/tools/s9_static_parity.py
                     --check)
    add_test(NAME conformance.s9_profiles
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/platform/s9_profiles.py
                     --check)
    add_test(NAME determinism.s9
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/determinism/s9_determinism.py
                     --check)
    # --- S10 RISC-V 32 target admission and long-term regression ----------
    add_test(NAME conformance.s10_riscv32
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/platform/s10_riscv32_cases.py
                     --check)
    add_test(NAME conformance.s10_interop
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/abi/s10_riscv32_interop.py
                     --check)
    add_test(NAME conformance.s10_reverse_interop
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/abi/s10_reverse_interop.py
                     --check)
    add_test(NAME conformance.s10_static_parity
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/tools/s10_static_parity.py
                     --check)
    add_test(NAME determinism.s10
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/determinism/s10_determinism.py
                     --check)
    add_test(NAME conformance.s10_longterm
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/longterm/s10_longterm.py
                     --check)
    add_test(NAME conformance.s10_abi_fuzz
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/abi/s10_abi_fuzz.py
                     --check --seeds 8)
    add_test(NAME conformance.s10_limits
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/platform/s10_limits_cases.py
                     --check)
    add_test(NAME conformance.s10_static_fuzz
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/tools/s8_static_fuzz.py
                     --check --profile=riscv32-baremetal)
    add_test(NAME conformance.s10_frontend_scale
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/tools/s10_frontend_scale.py
                     --check)
    add_test(NAME conformance.s10_checked
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/checked/s10_checked_cases.py
                     --check)
    add_test(NAME conformance.s10_long_branch
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/conformance/platform/s10_long_branch.py
                     --check)
    add_test(NAME release.s10_audit
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/s10_release_audit.py
                     --check)
    set_tests_properties(release.s10_audit PROPERTIES LABELS "toolchain;aot;slow")
    set_tests_properties(
        conformance.s7_diff conformance.s7_fuzz conformance.s7_abi
        conformance.s7_tools determinism.s7 conformance.s7_static_parity
        conformance.s8_diff conformance.s8_abi conformance.s8_checked
        conformance.s8_consistency conformance.s8_artifact determinism.s8
        conformance.s8_abi_fuzz conformance.s8_layout_fuzz
        conformance.s8_mem_fuzz conformance.s8_fp_fuzz
        conformance.s8_interop conformance.s9_baremetal
        conformance.s9_arm32 conformance.s9_arm32_interop
        conformance.abi_reverse conformance.s9_static_parity
        conformance.s9_profiles determinism.s9 conformance.s10_riscv32
        conformance.s10_interop conformance.s10_reverse_interop
        conformance.s10_static_parity determinism.s10
        conformance.s10_longterm conformance.s10_abi_fuzz
        conformance.s10_limits conformance.s10_static_fuzz
        conformance.s10_frontend_scale conformance.s10_checked
        conformance.s10_long_branch release.s10_audit
        PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        ENVIRONMENT "TC_PROJECT=$<TARGET_FILE:tc>;TC_VM=$<TARGET_FILE:tc-vm>;TC_AOT=$<TARGET_FILE:tc-aot>;TC_AOT_QEMU_SYSTEM=${TC_QEMU_SYSTEM_AARCH64};TC_AOT_QEMU_SYSTEM_ARM=${TC_QEMU_SYSTEM_ARM};TC_AOT_QEMU_SYSTEM_RISCV32=${TC_QEMU_SYSTEM_RISCV32};TC_AOT_LINKER=${TC_LLD}")
endif()

# --- LEX/SYN front-end conformance ------------------------------------------
#
# The adapter is configured with this build tree's tool paths.  The runner is
# a hard gate: every selected case must pass; not_implemented is never allowed.

set(TC_VM_PATH ${CMAKE_BINARY_DIR}/src/driver/tc-vm)
set(TC_AOT_PATH ${CMAKE_BINARY_DIR}/src/driver/tc-aot)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/tc_adapter.py.in
    ${CMAKE_BINARY_DIR}/tc_adapter.py
    @ONLY)

add_test(
    NAME adapter.selftest
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/adapters/adapter_selftest.py
            ${CMAKE_BINARY_DIR}/tc_adapter.py)
set_tests_properties(adapter.selftest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(
    NAME determinism.s1
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/determinism/s1_determinism.py
            $<TARGET_FILE:tc-vm>
            ${CMAKE_BINARY_DIR}/tc_adapter.py
            ${CMAKE_CURRENT_SOURCE_DIR}/conformance
            ${CMAKE_BINARY_DIR}/determinism)
set_tests_properties(determinism.s1 PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(
    NAME determinism.s2s3
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/determinism/s2s3_determinism.py
            $<TARGET_FILE:tc-vm>
            ${CMAKE_BINARY_DIR}/tc_adapter.py
            ${CMAKE_CURRENT_SOURCE_DIR}/conformance
            ${CMAKE_BINARY_DIR}/determinism)
set_tests_properties(determinism.s2s3 PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(
    NAME determinism.s5
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/determinism/s5_determinism.py
            $<TARGET_FILE:tc-vm>
            ${CMAKE_BINARY_DIR}/tc_adapter.py
            ${CMAKE_CURRENT_SOURCE_DIR}/conformance
            ${CMAKE_BINARY_DIR}/determinism)
set_tests_properties(determinism.s5 PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(
    NAME determinism.s6
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/determinism/s6_determinism.py
            $<TARGET_FILE:tc-vm>
            ${CMAKE_BINARY_DIR}/tc_adapter.py
            ${CMAKE_CURRENT_SOURCE_DIR}/conformance
            ${CMAKE_BINARY_DIR}/determinism)
set_tests_properties(determinism.s6 PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(
    NAME determinism.s4
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/determinism/s4_determinism.py
            $<TARGET_FILE:tc-vm>
            ${CMAKE_BINARY_DIR}/tc_adapter.py
            ${CMAKE_CURRENT_SOURCE_DIR}/conformance
            ${CMAKE_BINARY_DIR}/determinism)
set_tests_properties(determinism.s4 PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(
    NAME conformance.s8_static_fuzz
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/conformance/tools/s8_static_fuzz.py
            --check)
set_tests_properties(conformance.s8_static_fuzz PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                     ENVIRONMENT "TC_VM=$<TARGET_FILE:tc-vm>;TC_AOT=$<TARGET_FILE:tc-aot>")

add_test(
    NAME harness.conformance
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/harness/runner.py
            ${CMAKE_CURRENT_SOURCE_DIR}/conformance
            --adapter ${CMAKE_BINARY_DIR}/tc_adapter.py
            --json-out ${CMAKE_BINARY_DIR}/harness-report.json)
set_tests_properties(harness.conformance PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

get_property(tc_test_names DIRECTORY PROPERTY TESTS)
foreach(tc_test_name IN LISTS tc_test_names)
    get_test_property(${tc_test_name} ENVIRONMENT tc_test_environment)
    if(tc_test_environment STREQUAL "tc_test_environment-NOTFOUND")
        set(tc_test_environment "")
    endif()
    set_tests_properties(${tc_test_name} PROPERTIES
        ENVIRONMENT "${tc_test_environment};TMPDIR=${PROJECT_SOURCE_DIR}/.tmp")
endforeach()
