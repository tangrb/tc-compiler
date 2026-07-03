add_subdirectory(unit/lexer)

add_custom_target(check-unit
    COMMAND echo "=== Running Lexer unit tests ===" && ${CMAKE_COMMAND} -E env ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target check-lexer
    COMMAND echo "=== Running Extended Lexer unit tests ===" && ${CMAKE_COMMAND} -E env ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target check-lexer-extended
    COMMENT "Running all unit tests"
)
