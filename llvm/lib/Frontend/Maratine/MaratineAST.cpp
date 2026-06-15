//===--- MaratineAST.cpp - AST node factories for Maratine language --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MaratineAST.h"
#include "clang/AST/ASTContext.h"

using namespace clang;
using namespace clang::maratine;

MaratineImportDecl *
MaratineImportDecl::Create(ASTContext &C, SourceLocation HashLoc,
                           SourceLocation LAngle, SourceLocation RAngle,
                           StringRef ModulePath,
                           ArrayRef<StringRef> Deps) {
  void *Mem = C.Allocate(sizeof(MaratineImportDecl), alignof(MaratineImportDecl));
  return new (Mem) MaratineImportDecl(HashLoc, LAngle, RAngle,
                                      ModulePath, Deps);
}

MaratineImportDecl::MaratineImportDecl(SourceLocation HL, SourceLocation LA,
                                       SourceLocation RA, StringRef MP,
                                       ArrayRef<StringRef> Deps)
  : Decl(MaratineImport, nullptr, HL, RA),
    HashLoc(HL), LAngle(LA), RAngle(RA), ModulePath(MP),
    Dependencies(Deps.begin(), Deps.end()) {}

MaratineLetDecl *
MaratineLetDecl::Create(ASTContext &C, SourceLocation LetLoc,
                        SourceLocation Colon, SourceLocation Equals,
                        Decl *Var, Expr *Init) {
  void *Mem = C.Allocate(sizeof(MaratineLetDecl), alignof(MaratineLetDecl));
  return new (Mem) MaratineLetDecl(LetLoc, Colon, Equals, Var, Init);
}

MaratineLetDecl::MaratineLetDecl(SourceLocation LL, SourceLocation CL,
                                 SourceLocation EL, Decl *V, Expr *I)
  : Decl(MaratineLet, nullptr, LL, EL),
    LetLoc(LL), Colon(CL), Equals(EL), VarDecl(V), Init(I) {}

MaratineFunctionDecl *
MaratineFunctionDecl::Create(ASTContext &C,
                             SourceLocation Rel, SourceLocation CL,
                             SourceLocation Colon,
                             SourceLocation LB, SourceLocation RB,
                             FunctionDecl *Fn, Stmt *Body) {
  void *Mem = C.Allocate(sizeof(MaratineFunctionDecl), alignof(MaratineFunctionDecl));
  return new (Mem) MaratineFunctionDecl(Rel, CL, Colon, LB, RB, Fn, Body);
}

MaratineFunctionDecl::MaratineFunctionDecl(SourceLocation RL, SourceLocation CLL,
                                           SourceLocation COL, SourceLocation LBR,
                                           SourceLocation RBR, FunctionDecl *F,
                                           Stmt *B)
  : Decl(MaratineFunction, nullptr, RL, RBR),
    RelLoc(RL), CLoc(CLL), Colon(COL), LBracket(LBR), RBracket(RBR),
    FnDecl(F), Body(B) {}

MaratineExprStmt *
MaratineExprStmt::Create(ASTContext &C, SourceLocation Colon,
                         Expr *E, bool IsRet) {
  void *Mem = C.Allocate(sizeof(MaratineExprStmt), alignof(MaratineExprStmt));
  return new (Mem) MaratineExprStmt(Colon, E, IsRet);
}

MaratineExprStmt::MaratineExprStmt(SourceLocation CL, Expr *E, bool IR)
  : Stmt(MaratineExprStmtClass), Colon(CL), SubExpr(E), IsRet(IR) {}