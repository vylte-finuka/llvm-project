//===--- MaratineSema.cpp - Semantic analysis for Mara/Maratine language -===//
//
// Vyft Ltd — Proprietary — 2026
//
//===----------------------------------------------------------------------===//

#include "MaratineSema.h"
#include "MaratineAST.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Decl.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/FileSystem.h"

using namespace clang;
using namespace clang::maratine;

bool MaratineSema::ActOnImportDecl(MaratineImportDecl *D) {
  // Replace *** path separator with directory separator and search for the module.
  StringRef ModPath = D->getModulePath();
  SmallString<256> FSPath;
  for (char C : ModPath) {
    if (C == '*')
      FSPath.push_back(llvm::sys::path::get_separator().front());
    else
      FSPath.push_back(C);
  }

  bool Found = false;
  auto &HS = getPreprocessor().getHeaderSearchInfo();
  for (unsigned I = 0, N = HS.search_dir_size(); I < N; ++I) {
    SmallString<256> Candidate(HS.search_dir_begin()[I].getName());
    llvm::sys::path::append(Candidate, FSPath.str());
    if (llvm::sys::fs::is_directory(Candidate)) {
      Found = true;
      break;
    }
  }

  if (!Found) {
    Diags.Report(D->getHashLoc(), diag::err_fe_invalid_code_complete_file)
        << D->getModulePath();
    return true;
  }

  return false;
}

bool MaratineSema::ActOnLetDecl(MaratineLetDecl *D) {
  if (!D->getVarDecl()) return true;

  if (D->getInit()) {
    ExprResult InitRes = DefaultLvalueConversion(D->getInit());
    if (InitRes.isInvalid()) return true;

    ExprResult Conv = ImpCastExprToType(InitRes.get(),
                                        D->getVarDecl()->getType(),
                                        CK_BitCast);
    if (Conv.isInvalid()) {
      Diags.Report(D->getKwLoc(), diag::err_typecheck_convert_incompatible)
          << D->getVarDecl()->getType()
          << D->getInit()->getType()
          << 1 << 0 << 0;
      return true;
    }
  }

  PushOnScopeChains(D->getVarDecl(), getCurScope(), /*AddToContext=*/true);
  return false;
}

bool MaratineSema::ActOnFunctionDecl(MaratineFunctionDecl *D) {
  if (!D->getFunction()) return true;

  PushDeclContext(getCurScope(), D->getFunction());
  bool Err = false;

  if (D->getBody()) {
    // Walk statements in the body — delegate each to the appropriate Act* method.
    if (auto *CS = dyn_cast<CompoundStmt>(D->getBody())) {
      for (auto *S : CS->body()) {
        if (auto *IS = dyn_cast<MaratineIfStmt>(S))
          Err |= ActOnIfStmt(IS);
        else if (auto *LS = dyn_cast<MaratineLoopStmt>(S))
          Err |= ActOnLoopStmt(LS);
        else if (auto *ES = dyn_cast<MaratineExprStmt>(S))
          Err |= ActOnExprStmt(ES);
      }
    }
  }

  PopDeclContext();
  return Err;
}

bool MaratineSema::ActOnIfStmt(MaratineIfStmt *S) {
  if (!S->getCond()) return true;

  ExprResult Cond = CheckBooleanCondition(S->getIfLoc(), S->getCond());
  if (Cond.isInvalid()) {
    Diags.Report(S->getIfLoc(), diag::err_typecheck_bool_condition)
        << S->getCond()->getType();
    return true;
  }

  return false;
}

bool MaratineSema::ActOnLoopStmt(MaratineLoopStmt *S) {
  if (!S->getCond()) return false; // infinite loop: loop [ … ] is valid

  ExprResult Cond = CheckBooleanCondition(S->getLoopLoc(), S->getCond());
  if (Cond.isInvalid()) {
    Diags.Report(S->getLoopLoc(), diag::err_typecheck_bool_condition)
        << S->getCond()->getType();
    return true;
  }

  return false;
}

bool MaratineSema::ActOnExprStmt(MaratineExprStmt *S) {
  if (S->isRet()) {
    // ret expr;  — no colon in Mara syntax
    FunctionDecl *FnDecl = getCurFunctionDecl();
    if (!FnDecl) {
      Diags.Report(S->getKwLoc(), diag::err_return_in_constructor_handler);
      return true;
    }

    if (S->getSubExpr()) {
      ExprResult Conv = ImpCastExprToType(S->getSubExpr(),
                                          FnDecl->getReturnType(),
                                          CK_BitCast);
      if (Conv.isInvalid()) {
        Diags.Report(S->getKwLoc(), diag::err_typecheck_convert_incompatible)
            << FnDecl->getReturnType()
            << S->getSubExpr()->getType()
            << 1 << 0 << 0;
        return true;
      }
    }
  }
  // log: expr;  — any expression is valid
  return false;
}
