# Vyft Ltd - Maratine Tests

# Test simple du compilateur
add_test(
    NAME test_hello_world
    COMMAND maratine-cc 
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hello.mart
            -o ${CMAKE_CURRENT_BINARY_DIR}/hello.ll
)

# Tests du lexer
add_test(
    NAME test_lexer
    COMMAND maratine-cc
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/lexer_test.mart
            -dump-tokens
)

# Tests du parser
add_test(
    NAME test_parser
    COMMAND maratine-cc
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/parser_test.mart
            -dump-ast
)
