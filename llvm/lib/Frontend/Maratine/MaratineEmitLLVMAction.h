//===--- MaratineEmitLLVMAction.h - Emit LLVM IR for Mara/Maratine ------===//
//
// Vyft Ltd — Proprietary — 2026
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_MARATINE_MARATINEEMITLLVMACTION_H
#define LLVM_FRONTEND_MARATINE_MARATINEEMITLLVMACTION_H

#include "MaratineASTConsumer.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/CodeGen/CodeGenAction.h"

namespace clang {
namespace maratine {

// Frontend action that emits LLVM IR for Mara source files.
// Delegates AST consumption to MaratineASTConsumer which drives
// the standard Clang code-generation backend.
class MaratineEmitLLVMAction : public ASTFrontendAction {
protected:
  std::unique_ptr<ASTConsumer>
  CreateASTConsumer(CompilerInstance &CI, StringRef /*InFile*/) override {
    return std::make_unique<MaratineASTConsumer>(CI);
  }
};

} // namespace maratine
} // namespace clang

#endif // LLVM_FRONTEND_MARATINE_MARATINEEMITLLVMACTION_H
