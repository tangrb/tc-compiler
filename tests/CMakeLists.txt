# S0 tests: L0 contract units, code-table/traceability checks, harness and
# golden self-tests, CLI contract.  All tests are deterministic and offline.

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_executable(tc_contract_test unit/contract_test.c)
target_link_libraries(tc_contract_test PRIVATE tc_core)
target_compile_options(tc_contract_test PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(tc_dump_codes unit/dump_codes.c)
target_link_libraries(tc_dump_codes PRIVATE tc_core)
target_compile_options(tc_dump_codes PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_test(NAME contract.unit COMMAND tc_contract_test)
add_test(NAME contract.dump_codes COMMAND tc_dump_codes)

add_test(
    NAME cli.contract
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/cli_contract_test.py
            $<TARGET_FILE:tc-vm> $<TARGET_FILE:tc-aot>)

# --- S0 traceability / golden / harness checks -------------------------------

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
set_tests_properties(harness.selftest trace.harness_codes
                     PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME golden.selftest
         COMMAND ${Python3_EXECUTABLE} ${TC_GOLDEN_DIR}/selftest.py)
set_tests_properties(golden.selftest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
