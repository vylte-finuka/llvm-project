//===--- MaratineAST.h - AST nodes for Maratine language -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_MARATINE_MARATINEAST_H
#define LLVM_FRONTEND_MARATINE_MARATINEAST_H

#include "clang/AST/AST.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Stmt.h"
#include "llvm/ADT/ArrayRef.h"

namespace clang {
namespace maratine {

/// Represents a module import: #base <parent***child***module[deps]>;
class MaratineImportDecl : public Decl {
  FriendDecl<MaratineImportDecl>;
  SourceLocation HashLoc;
  SourceLocation LAngle, RAngle;
  StringRef ModulePath;   // e.g. "std***ComTpe***SlulFrmt"
  SmallVector<StringRef, 4> Dependencies; // e.g. ["MathSafety","DrvManSpec"]
public:
  static bool classof(const Decl *D) {
    return D->getKind() == MaratineImport;
  }
  static MaratineImportDecl *Create(ASTContext &C, SourceLocation HashLoc,
                                    SourceLocation LAngle, SourceLocation RAngle,
                                    StringRef ModulePath,
                                    ArrayRef<StringRef> Deps);
  SourceLocation getHashLoc() const { return HashLoc; }
  StringRef getModulePath() const { return ModulePath; }
  ArrayRef<StringRef> getDependencies() const { return Dependencies; }
};

/// Variable declaration: let <name>: <type> = <expr>;
/// Also used for var and const (mutability/constness stored in the underlying VarDecl).
class MaratineLetDecl : public Decl {
  FriendDecl<MaratineLetDecl>;
  SourceLocation LetLoc;
  SourceLocation Colon;
  SourceLocation Equals;
  Decl *VarDecl;   // underlying VarDecl from Clang AST (holds type)
  Expr *Init;      // initializer expression
public:
  static bool classof(const Decl *D) {
    return D->getKind() == MaratineLet;
  }
  static MaratineLetDecl *Create(ASTContext &C, SourceLocation LetLoc,
                                 SourceLocation Colon, SourceLocation Equals,
                                 Decl *Var, Expr *Init);
  SourceLocation getLetLoc() const { return LetLoc; }
  Decl *getVarDecl() const { return VarDecl; }
  Expr *getInit() const { return Init; }
};

/// Function declaration: rel cl <name>: <params> [ <body> ];
class MaratineFunctionDecl : public Decl {
  FriendDecl<MaratineFunctionDecl>;
  SourceLocation RelLoc;
  SourceLocation CLoc;
  SourceLocation Colon;
  SourceLocation LBracket, RBracket;
  FunctionDecl *FnDecl; // the real Clang FunctionDecl (params, etc.)
  Stmt *Body;           // compound statement
public:
  static bool classof(const Decl *D) {
    return D->getKind() == MaratineFunction;
  }
  static MaratineFunctionDecl *Create(ASTContext &C,
                                      SourceLocation Rel, SourceLocation CL,
                                      SourceLocation Colon,
                                      SourceLocation LB, SourceLocation RB,
                                      FunctionDecl *Fn, Stmt *Body);
  SourceLocation getRelLoc() const { return RelLoc; }
  SourceLocation getCLoc() const { return CLoc; }
  FunctionDecl getFunction() const { return *FnDecl; }
  Stmt *getBody() const { return Body; }
};

/// Expression statement for `log:` and `ret:`.
class MaratineExprStmt : public Stmt {
  FriendStmt<MaratineExprStmt>;
  SourceLocation Colon; // for log: or ret:
  Expr *SubExpr;
  bool IsRet;           // true if this stmt originated from `ret:`
public:
  static bool classof(const Stmt *S) {
    return S->getStmtClass() == MaratineExprStmtClass;
  }
  static MaratineExprStmt *Create(ASTContext &C, SourceLocation Colon,
                                  Expr *E, bool IsRet = false);
  SourceLocation getColonLoc() const { return Colon; }
  Expr *getSubExpr() const { return SubExpr; }
  bool isRet() const { return IsRet; }
};

} // namespace maratine
} // namespace clang

#endif // LLVM_FRONTEND_MARATINE_MARATINEAST_H