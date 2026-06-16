#===--- llvm/lib/Frontend/Maratine/CMakeLists.txt -----------------------===#
# Vyft Ltd — Proprietary — 2026
# Mara/Maratine language frontend — integrates into the LLVM/Clang build.
#===----------------------------------------------------------------------===#

add_clang_library(MaratineFrontend
  MaratineAST.cpp
  MaratineCodeGenAction.cpp
  MaratineLexer.cpp
  MaratineParser.cpp
  MaratinePlugin.cpp
  MaratineSema.cpp

  LINK_LIBS
    clangAST
    clangBasic
    clangCodeGen
    clangDriver
    clangFrontend
    clangLex
    clangParse
    clangSema
    clangSerialization
    LLVMCore
    LLVMPasses
    LLVMSupport
    LLVMTargetParser
)

target_include_directories(MaratineFrontend
  PUBLIC
    ${LLVM_MAIN_INCLUDE_DIR}
    ${CLANG_MAIN_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

install(TARGETS MaratineFrontend DESTINATION lib)
