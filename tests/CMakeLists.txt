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

add_executable(tc_dump_codes unit/dump_codes.c)
target_link_libraries(tc_dump_codes PRIVATE tc_core)
target_compile_options(tc_dump_codes PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_test(NAME contract.unit COMMAND tc_contract_test)
add_test(NAME frontend.unit COMMAND tc_frontend_test)
add_test(NAME sem.unit COMMAND tc_sem_test)
add_test(NAME contract.dump_codes COMMAND tc_dump_codes)
add_test(NAME conformance.s2s3_cases
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/conformance/static/s2s3_cases.py --check)
add_test(NAME conformance.s1_cases
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/conformance/static/s1_cases.py --check)

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
add_test(NAME trace.golden
         COMMAND ${Python3_EXECUTABLE} ${TC_GOLDEN_DIR}/validate.py)

set_tests_properties(
    trace.gen_codes trace.gen_c_table trace.codes trace.coverage trace.golden
    PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME harness.selftest
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/harness/selftest.py)
add_test(NAME trace.harness_codes
         COMMAND ${Python3_EXECUTABLE} ${TC_TRACE_DIR}/check_harness_codes.py)
set_tests_properties(harness.selftest trace.harness_codes conformance.s1_cases
                     conformance.s2s3_cases
                     PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME golden.selftest
         COMMAND ${Python3_EXECUTABLE} ${TC_GOLDEN_DIR}/selftest.py)
set_tests_properties(golden.selftest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

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
    NAME harness.conformance
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/harness/runner.py
            ${CMAKE_CURRENT_SOURCE_DIR}/conformance
            --adapter ${CMAKE_BINARY_DIR}/tc_adapter.py
            --json-out ${CMAKE_BINARY_DIR}/harness-report.json)
set_tests_properties(harness.conformance PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
