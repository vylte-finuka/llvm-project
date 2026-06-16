//===--- MaratineCodeGenAction.h - Code generation action for Maratine --===//
//
// Vyft Ltd — Proprietary — 2026
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_MARATINE_MARATINECODEGENACTION_H
#define LLVM_FRONTEND_MARATINE_MARATINECODEGENACTION_H

#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Frontend/CompilerInstance.h"

namespace clang {
namespace maratine {

// Drives LLVM IR emission for Mara source files.
// Extends ASTFrontendAction so the Maratine parser can attach its AST
// consumer before handing off to the Clang code-generation backend.
class MaratineCodeGenAction : public ASTFrontendAction {
public:
  MaratineCodeGenAction() = default;
  ~MaratineCodeGenAction() override = default;

  std::unique_ptr<ASTConsumer>
  CreateASTConsumer(CompilerInstance &CI, StringRef InFile) override;

  bool BeginSourceFileAction(CompilerInstance &CI) override;
  void EndSourceFileAction() override;
};

} // namespace maratine
} // namespace clang

#endif // LLVM_FRONTEND_MARATINE_MARATINECODEGENACTION_H
