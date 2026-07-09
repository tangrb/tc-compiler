add_subdirectory(unit/lexer)
add_subdirectory(unit/parser)
add_subdirectory(unit/runtime)

add_custom_target(check-unit
    COMMAND echo "=== Running Lexer unit tests ===" && ${CMAKE_COMMAND} -E env ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target check-lexer
    COMMAND echo "=== Running Extended Lexer unit tests ===" && ${CMAKE_COMMAND} -E env ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target check-lexer-extended
    COMMAND echo "=== Running Parser unit tests ===" && ${CMAKE_COMMAND} -E env ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target check-parser
    COMMAND echo "=== Running Semantics unit tests ===" && ${CMAKE_COMMAND} -E env ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target check-semantics
    COMMAND echo "=== Running Types unit tests ===" && ${CMAKE_COMMAND} -E env ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target check-types
    COMMAND echo "=== Running IO unit tests ===" && ${CMAKE_COMMAND} -E env ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target check-io
    COMMAND echo "=== Running Bitwise unit tests ===" && ${CMAKE_COMMAND} -E env ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target check-bitwise
    COMMAND echo "=== Running Shift unit tests ===" && ${CMAKE_COMMAND} -E env ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target check-shift
    COMMAND echo "=== Running Symbol table unit tests ===" && ${CMAKE_COMMAND} -E env ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target check-symbol
    COMMAND echo "=== Running Warning list unit tests ===" && ${CMAKE_COMMAND} -E env ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target check-warning
    COMMAND echo "=== Running Analyzer unit tests ===" && ${CMAKE_COMMAND} -E env ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target check-analyzer
    COMMAND echo "=== Running stmt_index unit tests ===" && ${CMAKE_COMMAND} -E env ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target check-stmt-index
    COMMENT "Running all unit tests"
)
