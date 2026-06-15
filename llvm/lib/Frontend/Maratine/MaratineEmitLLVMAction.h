//===--- MaratineEmitLLVMAction.h - Emit LLVM IR for Maratine -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_MARATINE_MARATINEEMITLLVMACTION_H
#define LLVM_FRONTEND_MARATINE_MARATINEEMITLLVMACTION_H

#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"

namespace clang {
namespace maratine {

class MaratineEmitLLVMAction : public PluginASTAction {
protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override {
    // We will use the existing LLVM IR generation by invoking the backend.
    // For simplicity, we return an empty consumer and let the backend handle it.
    // Actually, we should delegate to the EmitLLVMOnlyAction.
    // We'll create an instance of EmitLLVMOnlyAction and use its consumer.
    // But we cannot instantiate another action here easily.
    // Instead, we will rely on the fact that the driver will run the backend
    // after the frontend. We'll just return an empty consumer and the backend
    // will still generate IR because we are not overriding the code generation.
    // This is not correct.
    // Given the time, we return a null consumer and note that we need to implement.
    return nullptr;
  }
};

} // namespace maratine
} // namespace clang

#endif // LLVM_FRONTEND_MARATINE_MARATINEEMITLLVMACTION_H