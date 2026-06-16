//===--- MaratineCodeGenAction.cpp - Code generation for Mara/Maratine --===//
//
// Vyft Ltd — Proprietary — 2026
//
//===----------------------------------------------------------------------===//

#include "MaratineCodeGenAction.h"
#include "MaratineASTConsumer.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/CodeGen/CodeGenAction.h"

using namespace clang;
using namespace clang::maratine;

// MaratineASTConsumer constructor — wraps Clang's EmitLLVMOnlyAction consumer
MaratineASTConsumer::MaratineASTConsumer(CompilerInstance &CI) {
  EmitLLVMOnlyAction Action;
  Inner = Action.CreateASTConsumer(CI, {});
}

std::unique_ptr<ASTConsumer>
MaratineCodeGenAction::CreateASTConsumer(CompilerInstance &CI,
                                         StringRef /*InFile*/) {
  return std::make_unique<MaratineASTConsumer>(CI);
}

bool MaratineCodeGenAction::BeginSourceFileAction(CompilerInstance &CI) {
  return ASTFrontendAction::BeginSourceFileAction(CI);
}

void MaratineCodeGenAction::EndSourceFileAction() {
  ASTFrontendAction::EndSourceFileAction();
}
