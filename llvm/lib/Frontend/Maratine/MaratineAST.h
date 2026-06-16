//===--- MaratineAST.h - AST nodes for Mara/Maratine language -----------===//
//
// Vyft Ltd — Proprietary — 2026
//
//===----------------------------------------------------------------------===//
//
// AST node classes for the Mara language.
//
//   MaratineImportDecl  — #base <path***[ deps ]>;
//   MaratineLetDecl     — var/let name: <type> = expr;
//   MaratineFunctionDecl— rel op/cl name: [params] [ body ];
//   MaratineIfStmt      — if (cond) [ body ] else [ body ];
//   MaratineLoopStmt    — loop cond [ body ];
//   MaratineExprStmt    — log: expr;  /  ret expr;
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_MARATINE_MARATINEAST_H
#define LLVM_FRONTEND_MARATINE_MARATINEAST_H

#include "clang/AST/AST.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Stmt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

namespace clang {
namespace maratine {

// ---------------------------------------------------------------------------
// MaratineImportDecl — #base <std***path***[ DepA, DepB ]>;
// ---------------------------------------------------------------------------
class MaratineImportDecl : public Decl {
  friend class MaratineImportDecl;
  SourceLocation HashLoc;
  SourceLocation LAngle, RAngle;
  StringRef ModulePath;                    // e.g. "std***ComTpe***SlulFrmt"
  SmallVector<StringRef, 4> Dependencies;  // e.g. ["MathSafety", "DrvManSpec"]

  MaratineImportDecl(SourceLocation HL, SourceLocation LA, SourceLocation RA,
                     StringRef MP, ArrayRef<StringRef> Deps);

public:
  static bool classof(const Decl *D) {
    return D->getKind() == MaratineImport;
  }
  static MaratineImportDecl *Create(ASTContext &C,
                                    SourceLocation HashLoc,
                                    SourceLocation LAngle,
                                    SourceLocation RAngle,
                                    StringRef ModulePath,
                                    ArrayRef<StringRef> Deps);

  SourceLocation getHashLoc() const { return HashLoc; }
  StringRef      getModulePath() const { return ModulePath; }
  ArrayRef<StringRef> getDependencies() const { return Dependencies; }
};

// ---------------------------------------------------------------------------
// MaratineLetDecl — var/let name: <type> = expr;
//   IsConst == true  → let (immutable)
//   IsConst == false → var (mutable)
// ---------------------------------------------------------------------------
class MaratineLetDecl : public Decl {
  friend class MaratineLetDecl;
  SourceLocation KwLoc;   // location of var/let keyword
  bool           IsConst; // true for let, false for var
  VarDecl       *Var;     // underlying Clang VarDecl (holds type + name)
  Expr          *Init;    // initialiser expression (may be nullptr for var)

  MaratineLetDecl(SourceLocation KL, bool IC, VarDecl *V, Expr *I);

public:
  static bool classof(const Decl *D) {
    return D->getKind() == MaratineLet;
  }
  static MaratineLetDecl *Create(ASTContext &C,
                                 SourceLocation KwLoc,
                                 bool IsConst,
                                 VarDecl *Var,
                                 Expr *Init);

  SourceLocation getKwLoc()  const { return KwLoc; }
  bool           isConst()   const { return IsConst; }
  VarDecl       *getVarDecl() const { return Var; }
  Expr          *getInit()   const { return Init; }
};

// ---------------------------------------------------------------------------
// MaratineFunctionDecl — rel op/cl name: [params] [ body ];
//   IsPublic == true  → rel op (ExternalLinkage)
//   IsPublic == false → rel cl (InternalLinkage)
//   Inherits: non-empty InheritType → <[string InheritType]>t suffix
// ---------------------------------------------------------------------------
class MaratineFunctionDecl : public Decl {
  friend class MaratineFunctionDecl;
  SourceLocation RelLoc;
  bool           IsPublic;        // op vs cl
  StringRef      InheritType;     // set if this is a class-like body
  SourceLocation LBracket, RBracket; // positions of the [ ] body delimiters
  FunctionDecl  *FnDecl;          // Clang FunctionDecl (params, name, type)
  Stmt          *Body;            // compound statement (the [ … ] body)

  MaratineFunctionDecl(SourceLocation RL, bool IP, StringRef IT,
                       SourceLocation LB, SourceLocation RB,
                       FunctionDecl *Fn, Stmt *Body);

public:
  static bool classof(const Decl *D) {
    return D->getKind() == MaratineFunction;
  }
  static MaratineFunctionDecl *Create(ASTContext &C,
                                      SourceLocation RelLoc,
                                      bool IsPublic,
                                      StringRef InheritType,
                                      SourceLocation LBracket,
                                      SourceLocation RBracket,
                                      FunctionDecl *FnDecl,
                                      Stmt *Body);

  SourceLocation  getRelLoc()      const { return RelLoc; }
  bool            isPublic()       const { return IsPublic; }
  StringRef       getInheritType() const { return InheritType; }
  bool            hasInherit()     const { return !InheritType.empty(); }
  FunctionDecl   *getFunction()    const { return FnDecl; }
  Stmt           *getBody()        const { return Body; }
};

// ---------------------------------------------------------------------------
// MaratineIfStmt — if (cond) [ then ] else [ else ];
// ---------------------------------------------------------------------------
class MaratineIfStmt : public Stmt {
  friend class MaratineIfStmt;
  SourceLocation IfLoc;
  Expr  *Cond;
  Stmt  *Then;
  Stmt  *Else; // nullptr if no else branch

  MaratineIfStmt(SourceLocation IL, Expr *C, Stmt *T, Stmt *E);

public:
  static bool classof(const Stmt *S) {
    return S->getStmtClass() == MaratineIfStmtClass;
  }
  static MaratineIfStmt *Create(ASTContext &C,
                                SourceLocation IfLoc,
                                Expr *Cond,
                                Stmt *Then,
                                Stmt *Else);

  SourceLocation getIfLoc()  const { return IfLoc; }
  Expr          *getCond()   const { return Cond; }
  Stmt          *getThen()   const { return Then; }
  Stmt          *getElse()   const { return Else; }
  bool           hasElse()   const { return Else != nullptr; }
};

// ---------------------------------------------------------------------------
// MaratineLoopStmt — loop cond [ body ];
// ---------------------------------------------------------------------------
class MaratineLoopStmt : public Stmt {
  friend class MaratineLoopStmt;
  SourceLocation LoopLoc;
  Expr  *Cond;  // loop condition (nullptr → infinite loop with break)
  Stmt  *Body;

  MaratineLoopStmt(SourceLocation LL, Expr *C, Stmt *B);

public:
  static bool classof(const Stmt *S) {
    return S->getStmtClass() == MaratineLoopStmtClass;
  }
  static MaratineLoopStmt *Create(ASTContext &C,
                                  SourceLocation LoopLoc,
                                  Expr *Cond,
                                  Stmt *Body);

  SourceLocation getLoopLoc() const { return LoopLoc; }
  Expr          *getCond()    const { return Cond; }
  Stmt          *getBody()    const { return Body; }
};

// ---------------------------------------------------------------------------
// MaratineExprStmt — log: expr;  /  ret expr;
//   IsLog == true  → log: expr;
//   IsLog == false → ret expr;
// ---------------------------------------------------------------------------
class MaratineExprStmt : public Stmt {
  friend class MaratineExprStmt;
  SourceLocation KwLoc;  // location of log / ret keyword
  bool           IsLog;  // true = log, false = ret
  Expr          *SubExpr;

  MaratineExprStmt(SourceLocation KL, bool IL, Expr *E);

public:
  static bool classof(const Stmt *S) {
    return S->getStmtClass() == MaratineExprStmtClass;
  }
  static MaratineExprStmt *Create(ASTContext &C,
                                  SourceLocation KwLoc,
                                  bool IsLog,
                                  Expr *E);

  SourceLocation getKwLoc()  const { return KwLoc; }
  bool           isLog()     const { return IsLog; }
  bool           isRet()     const { return !IsLog; }
  Expr          *getSubExpr() const { return SubExpr; }
};

} // namespace maratine
} // namespace clang

#endif // LLVM_FRONTEND_MARATINE_MARATINEAST_H
