# Contract tests: contract units, code-table/traceability checks, harness and
# golden self-tests, CLI contract.  All tests are deterministic and offline.

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_executable(tc_contract_test unit/contract_test.c)
target_link_libraries(tc_contract_test PRIVATE tc_core)
target_compile_options(tc_contract_test PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc_frontend_test unit/frontend_test.c)
target_link_libraries(tc_frontend_test PRIVATE tc_core)
target_compile_options(tc_frontend_test PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc_sem_test unit/sem_test.c)
target_link_libraries(tc_sem_test PRIVATE tc_core)
target_compile_options(tc_sem_test PRIVATE -Wall -Wextra -Werror -Wpedantic)
target_compile_definitions(tc_sem_test PRIVATE
    TC_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(tc_ir_test unit/ir_test.c)
target_link_libraries(tc_ir_test PRIVATE tc_core)
target_compile_options(tc_ir_test PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc_ct_test unit/ct_test.c)
target_link_libraries(tc_ct_test PRIVATE tc_core)
target_compile_options(tc_ct_test PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc_vm_test unit/vm_test.c)
target_link_libraries(tc_vm_test PRIVATE tc_core)
target_compile_options(tc_vm_test PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc_aot_test unit/aot_test.c)
target_link_libraries(tc_aot_test PRIVATE tc_core)
target_compile_options(tc_aot_test PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc_dump_codes unit/dump_codes.c)
target_link_libraries(tc_dump_codes PRIVATE tc_core)
target_compile_options(tc_dump_codes PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_test(NAME contract.unit COMMAND tc_contract_test)
add_test(NAME frontend.unit COMMAND tc_frontend_test)
add_test(NAME sem.unit COMMAND tc_sem_test)
add_test(NAME ct.unit COMMAND tc_ct_test)
add_test(NAME ir.unit COMMAND tc_ir_test)
add_test(NAME vm.unit COMMAND tc_vm_test)
add_test(NAME aot.unit COMMAND tc_aot_test)
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
            $<TARGET_FILE:tc-vm> $<TARGET_FILE:tc-aot>)

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
find_program(TC_LLD NAMES ld.lld HINTS /opt/homebrew/opt/llvm/bin
             /usr/local/opt/llvm/bin /opt/homebrew/bin)
option(TC_ENABLE_AOT_TESTS "Register S7 AArch64/qemu conformance tests" ON)
if(TC_ENABLE_AOT_TESTS AND
   (NOT TC_QEMU_SYSTEM_AARCH64 OR NOT TC_LLD))
    message(FATAL_ERROR
        "S7 AOT tests need qemu-system-aarch64 and ld.lld; install them or "
        "configure with -DTC_ENABLE_AOT_TESTS=OFF")
endif()
if(TC_ENABLE_AOT_TESTS)
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
    set_tests_properties(
        conformance.s7_diff conformance.s7_fuzz conformance.s7_abi
        conformance.s7_tools determinism.s7 conformance.s7_static_parity
        conformance.s8_diff conformance.s8_abi conformance.s8_checked
        conformance.s8_consistency conformance.s8_artifact determinism.s8
        PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        ENVIRONMENT "TC_VM=$<TARGET_FILE:tc-vm>;TC_AOT=$<TARGET_FILE:tc-aot>;TC_AOT_QEMU_SYSTEM=${TC_QEMU_SYSTEM_AARCH64};TC_AOT_LINKER=${TC_LLD}")
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
    NAME harness.conformance
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/harness/runner.py
            ${CMAKE_CURRENT_SOURCE_DIR}/conformance
            --adapter ${CMAKE_BINARY_DIR}/tc_adapter.py
            --json-out ${CMAKE_BINARY_DIR}/harness-report.json)
set_tests_properties(harness.conformance PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
