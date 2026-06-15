//===--- MaratineSema.cpp - Semantic analysis for Maratine language --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MaratineSema.h"
#include "MaratineAST.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Decl.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/FileSystem.h"

using namespace clang;
using namespace clang::maratine;

bool MaratineSema::ActOnImportDecl(MaratineImportDecl *D) {
  // Resolve the module path: replace *** with the platform's path separator
  SmallString<128> FSPath;
  llvm::sys::path::native(D.getModulePath(), FSPath);
  // Look for a directory entry under the current include path
  bool Found = false;
  for (auto &I : getHeaderSearchInfo()) {
    SmallString<128> Candidate(I);
    llvm::sys::path::append(Candidate, FSPath);
    if (llvm::sys::fs::is_directory(Candidate)) {
      Found = true;
      // Add this directory to the header search path so that #include inside
      // the module works.
      getHeaderSearchInfo().addPath(Candidate, clang::frontend::Angled,
                                    false, false);
      break;
    }
  }
  if (!Found) {
    Diags.Report(D->getHashLoc(), diag::err_fe_expected_identifier)
      << "module not found: " << D.getModulePath();
    return true;
  }

  // Record dependencies (could be used for build‑system later)
  for (auto &Dep : D.getDependencies()) {
    // For now just verify that a header <Dep.h> exists somewhere.
    SmallString<128> DepPath;
    llvm::sys::path::native(Dep, DepPath);
    llvm::sys::path::append(DepPath, ".h");
    bool DepOk = false;
    for (auto &I : getHeaderSearchInfo()) {
      SmallString<128> Cand(I);
      llvm::sys::path::append(Cand, DepPath);
      if (llvm::sys::fs::exists(Cand)) {
        DepOk = true;
        break;
      }
    }
    if (!DepOk) {
      Diags.Report(D->getHashLoc(), diag::err_fe_expected_identifier)
        << "dependency not found: " << Dep;
      return true;
    }
  }
  return false;
}

bool MaratineSema::ActOnLetDecl(MaratineLetDecl *D) {
  // Ensure the initializer (if any) matches the declared type.
  if (D->getInit()) {
    ExprResult InitRes = UsualUnaryConversions(D->getInit());
    if (InitRes.isInvalid()) return true;
    Expr *Init = InitRes.get();
    // Perform implicit conversion check
    ExprResult Conv = ImpCastExprToType(Init, D->getVarDecl()->getType(),
                                        CK_BitCast);
    if (Conv.isInvalid()) {
      Diags.Report(D->getLetLoc(), diag::err_type_mismatch_in_init)
        << D->getVarDecl()->getType() << Init->getType();
      return true;
    }
    D->setInit(Conv.get());
  }
  // Finally, let the base Sema handle the VarDecl creation.
  return ActOnVarDecl(D->getVarDecl());
}

bool MaratineSema::ActOnFunctionDecl(MaratineFunctionDecl *D) {
  // Push a new function scope
  PushDeclContext(D->getFunction());
  bool Err = ActOnFunctionBody(D->getFunction(), D->getBody());
  PopDeclContext();
  return Err;
}

bool MaratineSema::ActOnExprStmt(MaratineExprStmt *S) {
  if (S->isRet()) {
    // `ret:` must appear inside a function and the expression must be
    // convertible to the function's return type.
    if (!getCurFunctionDecl()) {
      Diags.Report(S->getColonLoc(), diag::err_ret_outside_function);
      return true;
    }
    QualType RetTy = getCurFunctionDecl()->getReturnType();
    ExprResult Conv = ImpCastExprToType(S->getSubExpr(), RetTy, CK_BitCast);
    if (Conv.isInvalid()) {
      Diags.Report(S->getColonLoc(), diag::err_ret_type_mismatch)
        << RetTy << S->getSubExpr()->getType();
      return true;
    }
    // Replace the expression with the converted one (so codegen sees the correct type)
    const_cast<MaratineExprStmt*>(S)->setSubExpr(Conv.get());
  }
  // For `log:` we just allow any expression (side‑effects are fine).
  return false;
}