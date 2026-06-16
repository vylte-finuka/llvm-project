//===--- MaratineAST.cpp - AST node factories for Mara/Maratine ---------===//
//
// Vyft Ltd — Proprietary — 2026
//
//===----------------------------------------------------------------------===//

#include "MaratineAST.h"
#include "clang/AST/ASTContext.h"

using namespace clang;
using namespace clang::maratine;

// ---------------------------------------------------------------------------
// MaratineImportDecl
// ---------------------------------------------------------------------------
MaratineImportDecl *
MaratineImportDecl::Create(ASTContext &C,
                           SourceLocation HashLoc,
                           SourceLocation LAngle,
                           SourceLocation RAngle,
                           StringRef ModulePath,
                           ArrayRef<StringRef> Deps) {
  void *Mem = C.Allocate(sizeof(MaratineImportDecl),
                         alignof(MaratineImportDecl));
  return new (Mem) MaratineImportDecl(HashLoc, LAngle, RAngle, ModulePath, Deps);
}

MaratineImportDecl::MaratineImportDecl(SourceLocation HL, SourceLocation LA,
                                       SourceLocation RA, StringRef MP,
                                       ArrayRef<StringRef> Deps)
  : Decl(MaratineImport, nullptr, HL, RA),
    HashLoc(HL), LAngle(LA), RAngle(RA), ModulePath(MP),
    Dependencies(Deps.begin(), Deps.end()) {}

// ---------------------------------------------------------------------------
// MaratineLetDecl
// ---------------------------------------------------------------------------
MaratineLetDecl *
MaratineLetDecl::Create(ASTContext &C,
                        SourceLocation KwLoc,
                        bool IsConst,
                        VarDecl *Var,
                        Expr *Init) {
  void *Mem = C.Allocate(sizeof(MaratineLetDecl), alignof(MaratineLetDecl));
  return new (Mem) MaratineLetDecl(KwLoc, IsConst, Var, Init);
}

MaratineLetDecl::MaratineLetDecl(SourceLocation KL, bool IC,
                                 VarDecl *V, Expr *I)
  : Decl(MaratineLet, nullptr, KL, KL),
    KwLoc(KL), IsConst(IC), Var(V), Init(I) {}

// ---------------------------------------------------------------------------
// MaratineFunctionDecl
// ---------------------------------------------------------------------------
MaratineFunctionDecl *
MaratineFunctionDecl::Create(ASTContext &C,
                             SourceLocation RelLoc,
                             bool IsPublic,
                             StringRef InheritType,
                             SourceLocation LBracket,
                             SourceLocation RBracket,
                             FunctionDecl *FnDecl,
                             Stmt *Body) {
  void *Mem = C.Allocate(sizeof(MaratineFunctionDecl),
                         alignof(MaratineFunctionDecl));
  return new (Mem) MaratineFunctionDecl(RelLoc, IsPublic, InheritType,
                                        LBracket, RBracket, FnDecl, Body);
}

MaratineFunctionDecl::MaratineFunctionDecl(SourceLocation RL, bool IP,
                                           StringRef IT,
                                           SourceLocation LB, SourceLocation RB,
                                           FunctionDecl *Fn, Stmt *B)
  : Decl(MaratineFunction, nullptr, RL, RB),
    RelLoc(RL), IsPublic(IP), InheritType(IT),
    LBracket(LB), RBracket(RB), FnDecl(Fn), Body(B) {}

// ---------------------------------------------------------------------------
// MaratineIfStmt
// ---------------------------------------------------------------------------
MaratineIfStmt *
MaratineIfStmt::Create(ASTContext &C,
                       SourceLocation IfLoc,
                       Expr *Cond,
                       Stmt *Then,
                       Stmt *Else) {
  void *Mem = C.Allocate(sizeof(MaratineIfStmt), alignof(MaratineIfStmt));
  return new (Mem) MaratineIfStmt(IfLoc, Cond, Then, Else);
}

MaratineIfStmt::MaratineIfStmt(SourceLocation IL, Expr *C, Stmt *T, Stmt *E)
  : Stmt(MaratineIfStmtClass), IfLoc(IL), Cond(C), Then(T), Else(E) {}

// ---------------------------------------------------------------------------
// MaratineLoopStmt
// ---------------------------------------------------------------------------
MaratineLoopStmt *
MaratineLoopStmt::Create(ASTContext &C,
                         SourceLocation LoopLoc,
                         Expr *Cond,
                         Stmt *Body) {
  void *Mem = C.Allocate(sizeof(MaratineLoopStmt), alignof(MaratineLoopStmt));
  return new (Mem) MaratineLoopStmt(LoopLoc, Cond, Body);
}

MaratineLoopStmt::MaratineLoopStmt(SourceLocation LL, Expr *C, Stmt *B)
  : Stmt(MaratineLoopStmtClass), LoopLoc(LL), Cond(C), Body(B) {}

// ---------------------------------------------------------------------------
// MaratineExprStmt
// ---------------------------------------------------------------------------
MaratineExprStmt *
MaratineExprStmt::Create(ASTContext &C,
                         SourceLocation KwLoc,
                         bool IsLog,
                         Expr *E) {
  void *Mem = C.Allocate(sizeof(MaratineExprStmt), alignof(MaratineExprStmt));
  return new (Mem) MaratineExprStmt(KwLoc, IsLog, E);
}

MaratineExprStmt::MaratineExprStmt(SourceLocation KL, bool IL, Expr *E)
  : Stmt(MaratineExprStmtClass), KwLoc(KL), IsLog(IL), SubExpr(E) {}
