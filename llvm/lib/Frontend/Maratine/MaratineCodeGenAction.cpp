#include "MaratineCodeGenAction.h"
#include "MaratineASTConsumer.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/CodeGen/CodeGenAction.h"

using namespace clang;
using namespace clang::maratine;

std::unique_ptr<ASTConsumer>
MaratineCodeGenAction::CreateASTConsumer(CompilerInstance &CI,
                                         StringRef InFile) {
  // Delegate to our custom consumer that knows how to walk Maratine nodes.
  return llvm::make_unique<MaratineASTConsumer>(CI);
}

bool MaratineCodeGenAction::BeginSourceFileAction(CompilerInstance &CI) {
  // Let Clang set up the code generator as usual.
  return ASTFrontendAction::BeginSourceFileAction(CI);
}

void MaratineCodeGenAction::EndSourceFileAction() {
  // Nothing special – the base class will finalize the LLVM module.
  ASTFrontendAction::EndSourceFileAction();
}