# Vyft Ltd — Maratine Compiler Tests — Proprietary — 2026

add_test(
    NAME test_hello_mara
    COMMAND maratine-cc
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hello.mart
            -emit llvm
            -o ${CMAKE_CURRENT_BINARY_DIR}/hello.ll
)

add_test(
    NAME test_hello_tokens
    COMMAND maratine-cc
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hello.mart
            -dump-tokens
)

add_test(
    NAME test_hello_ast
    COMMAND maratine-cc
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hello.mart
            -dump-ast
)
