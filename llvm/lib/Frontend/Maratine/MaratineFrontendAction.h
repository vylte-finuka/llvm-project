//===--- MaratineFrontendAction.h - Frontend action selecting Maratine codegen --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_MARATINE_MARATINEFRONTEND_ACTION_H
#define LLVM_FRONTEND_MARATINE_MARATINEFRONTEND_ACTION_H

#include "clang/Frontend/FrontendAction.h"
#include "MaratineCodeGenAction.h"

namespace clang {
namespace maratine {

class MaratineFrontendAction : public CodeGenAction {
public:
  MaratineFrontendAction() = default;
  ~MaratineFrontendAction() override = default;

protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override {
    return llvm::make_unique<MaratineCodeGenAction>()->CreateASTConsumer(CI, InFile);
  }
};

} // namespace maratine
} // namespace clang

#endif // LLVM_FRONTEND_MARATINE_MARATINEFRONTEND_ACTION_H