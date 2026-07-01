# TC-AOT: ahead-of-time compiler (not yet implemented).
# When ready, add tc-aot executable here.

add_custom_target(aot-not-implemented
    COMMAND ${CMAKE_COMMAND} -E echo "tc-aot: not yet implemented"
    COMMAND ${CMAKE_COMMAND} -E false
    COMMENT "TC-AOT placeholder"
)

add_custom_target(check-aot
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/aot/run_tests.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running TC-AOT tests"
)
