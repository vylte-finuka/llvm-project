# CMake generated Testfile for 
# Source directory: D:/Downloads/Vyft_product/Maratinelang/maratinec/maratine/test
# Build directory: D:/Downloads/Vyft_product/Maratinelang/maratinec/maratine/build/test
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[test_hello_mara]=] "D:/Downloads/Vyft_product/Maratinelang/maratinec/maratine/build/maratine-cc.exe" "D:/Downloads/Vyft_product/Maratinelang/maratinec/maratine/test/fixtures/hello.mart" "-emit" "llvm" "-o" "D:/Downloads/Vyft_product/Maratinelang/maratinec/maratine/build/test/hello.ll")
set_tests_properties([=[test_hello_mara]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/Downloads/Vyft_product/Maratinelang/maratinec/maratine/test/CMakeLists.txt;3;add_test;D:/Downloads/Vyft_product/Maratinelang/maratinec/maratine/test/CMakeLists.txt;0;")
add_test([=[test_hello_tokens]=] "D:/Downloads/Vyft_product/Maratinelang/maratinec/maratine/build/maratine-cc.exe" "D:/Downloads/Vyft_product/Maratinelang/maratinec/maratine/test/fixtures/hello.mart" "-dump-tokens")
set_tests_properties([=[test_hello_tokens]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/Downloads/Vyft_product/Maratinelang/maratinec/maratine/test/CMakeLists.txt;11;add_test;D:/Downloads/Vyft_product/Maratinelang/maratinec/maratine/test/CMakeLists.txt;0;")
add_test([=[test_hello_ast]=] "D:/Downloads/Vyft_product/Maratinelang/maratinec/maratine/build/maratine-cc.exe" "D:/Downloads/Vyft_product/Maratinelang/maratinec/maratine/test/fixtures/hello.mart" "-dump-ast")
set_tests_properties([=[test_hello_ast]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/Downloads/Vyft_product/Maratinelang/maratinec/maratine/test/CMakeLists.txt;18;add_test;D:/Downloads/Vyft_product/Maratinelang/maratinec/maratine/test/CMakeLists.txt;0;")
