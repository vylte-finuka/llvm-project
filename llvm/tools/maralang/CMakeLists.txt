add_llvm_tool(maralang
  Maralang.cpp
)

set(_maralang_llvm_libs LLVMCore LLVMSupport LLVMDemangle)

if(TARGET clangBasic)
  list(APPEND _maralang_llvm_libs
    clangBasic clangDriver clangFrontend clangFrontendTool clangLex clangParse clangSema
    clangEdit clangAnalysis clangAST clangASTMatchers clangRewriteFrontend
    clangSerialization)
endif()

target_link_libraries(maralang PRIVATE ${_maralang_llvm_libs})
