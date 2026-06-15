//===--- MaratineSema.h - Semantic analysis for Maratine language ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_MARATINE_MARATINESEMA_H
#define LLVM_FRONTEND_MARATINE_MARATINESEMA_H

#include "clang/Sema/Sema.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Stmt.h"

namespace clang {
namespace maratine {

class MaratineSema : public Sema {
public:
  MaratineSema(Preprocessor &PP, SourceManager &SM, LangOptions &LO,
               DiagnosticsEngine &Diags)
    : Sema(PP, SM, LO, Diags,
           /*ExternalSource=*/nullptr,
           /*DelayedTemplates=*/true) {}

  // Override to handle MaratineImportDecl
  bool ActOnImportDecl(MaratineImportDecl *D);

  // Override to handle MaratineLetDecl (type checking, initializer)
  bool ActOnLetDecl(MaratineLetDecl *D);

  // Override to handle MaratineFunctionDecl (body checking)
  bool ActOnFunctionDecl(MaratineFunctionDecl *D);

  // Override to handle MaratineExprStmt (log/ret)
  bool ActOnExprStmt(MaratineExprStmt *S);
};

} // namespace maratine
} // namespace clang

#endif // LLVM_FRONTEND_MARATINE_MARATINESEMA_H