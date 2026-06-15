add_llvm_tool(maralang
  Maralang.cpp
  LLVMCore LLVMSupport LLVMDemangle
  clangBasic clangDriver clangFrontend clangFrontendTool clangLex clangParse clangSema
  clangEdit clangAnalysis clangAST clangASTMatchers clangRewriteFrontend
  clangSerialization
)