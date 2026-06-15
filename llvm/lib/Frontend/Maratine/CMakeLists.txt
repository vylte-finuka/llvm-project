#===--- MaratineFrontend CMakeLists.txt --------------------------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===#

add_library(MaratineFrontend
  MaratineLexer.cpp
  MaratineParser.cpp
  MaratineAST.cpp
  MaratineASTConsumer.cpp
  MaratineSema.cpp
  MaratineCodeGenAction.cpp
  MaratinePlugin.cpp
)

target_include_directories(MaratineFrontend
  PUBLIC
    ${LLVM_MAIN_INCLUDE_DIR}
    ${CLANG_MAIN_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(MaratineFrontend
  PRIVATE
    clangBasic
    clangLex
    clangParse
    clangSema
    clangAnalysis
    clangAST
    clangASTMatchers
    clangDriver
    clangEdit
    clangFrontend
    clangSerialization
    LLVMCore
    LLVMSupport
    LLVMOption
)

# Export the target so the driver can link against it
install(TARGETS MaratineFrontend DESTINATION lib)

add_executable(maralang Maralang.cpp)

target_include_directories(maralang PRIVATE
  ${LLVM_MAIN_INCLUDE_DIR}
  ${CLANG_MAIN_INCLUDE_DIR}
)

target_link_libraries(maralang PRIVATE
  MaratineFrontend   # <-- our new frontend library
  clangBasic
  clangDriver
  clangFrontend
  clangSerialization
  LLVMCore
  LLVMSupport
  LLVMOption
)