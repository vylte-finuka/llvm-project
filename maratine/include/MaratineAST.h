// Vyft Ltd — Mara/Maratine AST — Proprietary — 2026
//
// AST nodes for the standalone Mara compiler driver (maratine-cc).
// All classes implement LLVM RTTI (classof + kind enum) so that
// dyn_cast<> / isa<> / cast<> work correctly.

#ifndef LLVM_MARATINE_AST_H
#define LLVM_MARATINE_AST_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/SmallVector.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace llvm {
namespace maratine {

// ---------------------------------------------------------------------------
// Mara type system — only these 7 types exist
// ---------------------------------------------------------------------------
enum class MaraTypeKind {
  String, I32, I64, U64, Bool, Ptr, Array,
  Compound, // <[string TypeName]>
  Unknown
};

// ---------------------------------------------------------------------------
// Visibility: op (public) / cl (private)
// ---------------------------------------------------------------------------
enum class Visibility { Public, Private };

// ---------------------------------------------------------------------------
// Expr — base expression node with LLVM RTTI
// ---------------------------------------------------------------------------
class Expr {
public:
  enum ExprKind {
    EK_StringLiteral,
    EK_IntLiteral,
    EK_BoolLiteral,
    EK_NullLiteral,
    EK_ArrayLiteral,
    EK_VarRef,
    EK_FFICallExpr,
    EK_CallExpr,
    EK_BinaryExpr,
  };

  ExprKind getKind() const { return Kind; }
  virtual ~Expr() = default;

protected:
  explicit Expr(ExprKind K) : Kind(K) {}

private:
  ExprKind Kind;
};

// ---------------------------------------------------------------------------
// Expression leaf nodes
// ---------------------------------------------------------------------------
class StringLiteral : public Expr {
public:
  std::string Value;
  explicit StringLiteral(StringRef V) : Expr(EK_StringLiteral), Value(V.str()) {}
  static bool classof(const Expr *E) { return E->getKind() == EK_StringLiteral; }
};

class IntLiteral : public Expr {
public:
  int64_t      Value;
  MaraTypeKind IntTyKind; // I32 / I64 / U64
  explicit IntLiteral(int64_t V, MaraTypeKind K = MaraTypeKind::I32)
      : Expr(EK_IntLiteral), Value(V), IntTyKind(K) {}
  static bool classof(const Expr *E) { return E->getKind() == EK_IntLiteral; }
};

class BoolLiteral : public Expr {
public:
  bool Value;
  explicit BoolLiteral(bool V) : Expr(EK_BoolLiteral), Value(V) {}
  static bool classof(const Expr *E) { return E->getKind() == EK_BoolLiteral; }
};

class NullLiteral : public Expr {
public:
  bool IsNullptr;
  explicit NullLiteral(bool NP = false) : Expr(EK_NullLiteral), IsNullptr(NP) {}
  static bool classof(const Expr *E) { return E->getKind() == EK_NullLiteral; }
};

class ArrayLiteral : public Expr {
public:
  SmallVector<std::unique_ptr<Expr>, 8> Elements;
  ArrayLiteral() : Expr(EK_ArrayLiteral) {}
  static bool classof(const Expr *E) { return E->getKind() == EK_ArrayLiteral; }
};

class VarRef : public Expr {
public:
  std::string Name;
  explicit VarRef(StringRef N) : Expr(EK_VarRef), Name(N.str()) {}
  static bool classof(const Expr *E) { return E->getKind() == EK_VarRef; }
};

class FFICallExpr : public Expr {
public:
  std::string Path; // e.g. "DrvAPIInterCon***GpuFlushRenderContext***"
  SmallVector<std::unique_ptr<Expr>, 4> Args;
  explicit FFICallExpr(StringRef P) : Expr(EK_FFICallExpr), Path(P.str()) {}
  static bool classof(const Expr *E) { return E->getKind() == EK_FFICallExpr; }
};

class CallExpr : public Expr {
public:
  std::string FunctionName;
  SmallVector<std::unique_ptr<Expr>, 4> Args;
  explicit CallExpr(StringRef N) : Expr(EK_CallExpr), FunctionName(N.str()) {}
  static bool classof(const Expr *E) { return E->getKind() == EK_CallExpr; }
};

class BinaryExpr : public Expr {
public:
  std::string Op;
  std::unique_ptr<Expr> LHS;
  std::unique_ptr<Expr> RHS;
  BinaryExpr(StringRef O, std::unique_ptr<Expr> L, std::unique_ptr<Expr> R)
      : Expr(EK_BinaryExpr), Op(O.str()),
        LHS(std::move(L)), RHS(std::move(R)) {}
  static bool classof(const Expr *E) { return E->getKind() == EK_BinaryExpr; }
};

// ---------------------------------------------------------------------------
// ASTNode — base statement/declaration node with LLVM RTTI
// ---------------------------------------------------------------------------
class ASTNode {
public:
  enum NodeKind {
    NK_BlockStmt,
    NK_VarDecl,
    NK_IfStmt,
    NK_LoopStmt,
    NK_BreakStmt,
    NK_LogStmt,
    NK_RetStmt,
    NK_AssignStmt,
    NK_ExprStmt,
    NK_FunctionDecl,
    NK_ImportDecl,
    NK_Module,
  };

  NodeKind getKind() const { return Kind; }
  virtual ~ASTNode() = default;

protected:
  explicit ASTNode(NodeKind K) : Kind(K) {}

private:
  NodeKind Kind;
};

// ---------------------------------------------------------------------------
// Block — [ statements ]
// ---------------------------------------------------------------------------
class BlockStmt : public ASTNode {
public:
  SmallVector<std::unique_ptr<ASTNode>, 8> Statements;
  BlockStmt() : ASTNode(NK_BlockStmt) {}
  static bool classof(const ASTNode *N) { return N->getKind() == NK_BlockStmt; }
};

// ---------------------------------------------------------------------------
// Variable declaration — var/let name: <type> = expr;
// ---------------------------------------------------------------------------
class VarDecl : public ASTNode {
public:
  std::string   Name;
  MaraTypeKind  TypeKind;
  std::string   CompoundTypeName;
  bool          IsConst; // true = let, false = var
  std::unique_ptr<Expr> Initializer;

  VarDecl(StringRef N, MaraTypeKind T, bool Const = false)
      : ASTNode(NK_VarDecl), Name(N.str()), TypeKind(T), IsConst(Const) {}
  static bool classof(const ASTNode *N) { return N->getKind() == NK_VarDecl; }
};

// ---------------------------------------------------------------------------
// if (cond) [ then ] else [ else ];
// ---------------------------------------------------------------------------
class IfStmt : public ASTNode {
public:
  std::unique_ptr<Expr>      Cond;
  std::unique_ptr<BlockStmt> Then;
  std::unique_ptr<BlockStmt> Else; // nullptr if no else
  IfStmt() : ASTNode(NK_IfStmt) {}
  static bool classof(const ASTNode *N) { return N->getKind() == NK_IfStmt; }
};

// ---------------------------------------------------------------------------
// loop cond [ body ];
// ---------------------------------------------------------------------------
class LoopStmt : public ASTNode {
public:
  std::unique_ptr<Expr>      Cond; // nullptr = infinite loop
  std::unique_ptr<BlockStmt> Body;
  LoopStmt() : ASTNode(NK_LoopStmt) {}
  static bool classof(const ASTNode *N) { return N->getKind() == NK_LoopStmt; }
};

// ---------------------------------------------------------------------------
// break;
// ---------------------------------------------------------------------------
class BreakStmt : public ASTNode {
public:
  BreakStmt() : ASTNode(NK_BreakStmt) {}
  static bool classof(const ASTNode *N) { return N->getKind() == NK_BreakStmt; }
};

// ---------------------------------------------------------------------------
// log: expr;
// ---------------------------------------------------------------------------
class LogStmt : public ASTNode {
public:
  std::unique_ptr<Expr> Message;
  explicit LogStmt(std::unique_ptr<Expr> M)
      : ASTNode(NK_LogStmt), Message(std::move(M)) {}
  static bool classof(const ASTNode *N) { return N->getKind() == NK_LogStmt; }
};

// ---------------------------------------------------------------------------
// ret expr;  (no colon)
// ---------------------------------------------------------------------------
class RetStmt : public ASTNode {
public:
  std::unique_ptr<Expr> Value; // nullptr = void return
  RetStmt() : ASTNode(NK_RetStmt) {}
  static bool classof(const ASTNode *N) { return N->getKind() == NK_RetStmt; }
};

// ---------------------------------------------------------------------------
// name = expr;
// ---------------------------------------------------------------------------
class AssignStmt : public ASTNode {
public:
  std::string LHS;
  std::unique_ptr<Expr> RHS;
  AssignStmt(StringRef L, std::unique_ptr<Expr> R)
      : ASTNode(NK_AssignStmt), LHS(L.str()), RHS(std::move(R)) {}
  static bool classof(const ASTNode *N) { return N->getKind() == NK_AssignStmt; }
};

// ---------------------------------------------------------------------------
// Bare expression statement (standalone FFI call, function call)
// ---------------------------------------------------------------------------
class ExprStmt : public ASTNode {
public:
  std::unique_ptr<Expr> E;
  explicit ExprStmt(std::unique_ptr<Expr> EE)
      : ASTNode(NK_ExprStmt), E(std::move(EE)) {}
  static bool classof(const ASTNode *N) { return N->getKind() == NK_ExprStmt; }
};

// ---------------------------------------------------------------------------
// Function parameter: name type  (no angle brackets on raw types)
// ---------------------------------------------------------------------------
struct Param {
  std::string  Name;
  MaraTypeKind TypeKind;
  std::string  CompoundTypeName;
};

// ---------------------------------------------------------------------------
// Function declaration — rel op/cl name: [params] [ body ];
// ---------------------------------------------------------------------------
class FunctionDecl : public ASTNode {
public:
  std::string Name;
  Visibility  Vis;
  std::string InheritType; // non-empty → <[string InheritType]>t
  SmallVector<Param, 4> Params;
  std::unique_ptr<BlockStmt> Body;

  FunctionDecl(StringRef N, Visibility V)
      : ASTNode(NK_FunctionDecl), Name(N.str()), Vis(V) {}
  static bool classof(const ASTNode *N) {
    return N->getKind() == NK_FunctionDecl;
  }
};

// ---------------------------------------------------------------------------
// Import — #base <path***[ DepA, DepB ]>;
// ---------------------------------------------------------------------------
class ImportDecl : public ASTNode {
public:
  std::string ModulePath;
  SmallVector<std::string, 4> Deps;
  explicit ImportDecl(StringRef P)
      : ASTNode(NK_ImportDecl), ModulePath(P.str()) {}
  static bool classof(const ASTNode *N) { return N->getKind() == NK_ImportDecl; }
};

// ---------------------------------------------------------------------------
// Top-level module
// ---------------------------------------------------------------------------
class Module : public ASTNode {
public:
  std::string Name;
  SmallVector<std::unique_ptr<ImportDecl>,   4> Imports;
  SmallVector<std::unique_ptr<VarDecl>,      4> Globals;
  SmallVector<std::unique_ptr<FunctionDecl>, 8> Functions;
  Module() : ASTNode(NK_Module) {}
  static bool classof(const ASTNode *N) { return N->getKind() == NK_Module; }
};

} // namespace maratine
} // namespace llvm

#endif // LLVM_MARATINE_AST_H
