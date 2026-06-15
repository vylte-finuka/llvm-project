//===--- MaratineASTConsumer.h - AST Consumer for Maratine ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_MARATINE_MARATINEASTCONSUMER_H
#define LLVM_FRONTEND_MARATINE_MARATINEASTCONSUMER_H

#include "clang/AST/ASTConsumer.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/CodeGen/CodeGenAction.h"

namespace clang {
namespace maratine {

class MaratineASTConsumer : public ASTConsumer {
public:
  explicit MaratineASTConsumer(CompilerInstance &CI)
    : Consumer(llvm::make_unique<EmitLLVMOnlyAction>(CI.getDiagnostics())) {}

  bool HandleTopLevelDecl(DeclGroupRef DG) override {
    return Consumer->HandleTopLevelDecl(DG);
  }

  void HandleTranslationUnit(ASTContext &Ctx) override {
    Consumer->HandleTranslationUnit(Ctx);
  }

private:
  std::unique_ptr<FrontendAction> Consumer;
};

} // namespace maratine
} // namespace clang

#endif // LLVM_FRONTEND_MARATINE_MARATINEASTCONSUMER_H