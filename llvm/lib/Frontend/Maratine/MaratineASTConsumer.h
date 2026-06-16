//===--- MaratineASTConsumer.h - AST Consumer for Mara/Maratine ---------===//
//
// Vyft Ltd — Proprietary — 2026
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_MARATINE_MARATINEASTCONSUMER_H
#define LLVM_FRONTEND_MARATINE_MARATINEASTCONSUMER_H

#include "clang/AST/ASTConsumer.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Frontend/CompilerInstance.h"
#include <memory>

namespace clang {
namespace maratine {

// Walks the Maratine AST and delegates code-generation to Clang's
// standard EmitLLVMOnlyAction consumer.
class MaratineASTConsumer : public ASTConsumer {
public:
  explicit MaratineASTConsumer(CompilerInstance &CI);

  bool HandleTopLevelDecl(DeclGroupRef DG) override {
    return Inner->HandleTopLevelDecl(DG);
  }

  void HandleTranslationUnit(ASTContext &Ctx) override {
    Inner->HandleTranslationUnit(Ctx);
  }

private:
  std::unique_ptr<ASTConsumer> Inner;
};

} // namespace maratine
} // namespace clang

#endif // LLVM_FRONTEND_MARATINE_MARATINEASTCONSUMER_H
