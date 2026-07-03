add_subdirectory(unit/lexer)

add_custom_target(check-unit DEPENDS check-lexer)
