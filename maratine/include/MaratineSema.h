// Vyft Ltd — Mara/Maratine Semantic Analysis — Proprietary — 2026
//
// Sema validates a parsed Module before code generation:
//   - scope-aware symbol table (variables, functions, parameters)
//   - type compatibility checking for assignments and binary operations
//   - arity checking for Mara-level function calls
//   - ret statement presence and type matching
//   - use-before-declare detection

#ifndef LLVM_MARATINE_SEMA_H
#define LLVM_MARATINE_SEMA_H

#include "MaratineAST.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include <map>
#include <string>
#include <vector>

namespace llvm {
namespace maratine {

// ---------------------------------------------------------------------------
// Symbol table entry
// ---------------------------------------------------------------------------
struct SymbolEntry {
  std::string  Name;
  MaraTypeKind TypeKind       = MaraTypeKind::Unknown;
  std::string  CompoundName;         // for <[string T]>
  bool         IsConst        = false;
  bool         IsFunction     = false;
  unsigned     ParamCount     = 0;   // functions only
};

// ---------------------------------------------------------------------------
// Lexical scope
// ---------------------------------------------------------------------------
class Scope {
public:
  bool define(const SymbolEntry &S) {
    return Syms.emplace(S.Name, S).second; // false if duplicate
  }
  const SymbolEntry *lookup(const std::string &N) const {
    auto It = Syms.find(N);
    return It != Syms.end() ? &It->second : nullptr;
  }

private:
  std::map<std::string, SymbolEntry> Syms;
};

// ---------------------------------------------------------------------------
// Scope chain — push/pop during function/block entry/exit
// ---------------------------------------------------------------------------
class ScopeChain {
public:
  void push() { Stack.emplace_back(); }
  void pop()  { if (!Stack.empty()) Stack.pop_back(); }

  // Define in innermost scope — returns false on redefinition in same scope
  bool define(const SymbolEntry &S) {
    if (Stack.empty()) return false;
    return Stack.back().define(S);
  }

  // Walk scopes outward — returns nullptr if not found
  const SymbolEntry *lookup(const std::string &N) const {
    for (auto It = Stack.rbegin(); It != Stack.rend(); ++It) {
      if (auto *E = It->lookup(N)) return E;
    }
    return nullptr;
  }

private:
  std::vector<Scope> Stack;
};

// ---------------------------------------------------------------------------
// Diagnostic message
// ---------------------------------------------------------------------------
struct SemaDiag {
  unsigned     Line   = 0;
  unsigned     Col    = 0;
  bool         IsErr  = true;  // false = warning
  std::string  Msg;
};

// ---------------------------------------------------------------------------
// Sema — semantic analysis pass
// ---------------------------------------------------------------------------
class Sema {
public:
  Sema() = default;

  // Run full analysis. Returns success or an aggregated error message.
  Error analyse(Module &M);

  // Diagnostics accumulated during analysis
  const std::vector<SemaDiag> &diagnostics() const { return Diags; }
  bool hasErrors() const;
  void printDiagnostics(raw_ostream &OS) const;

private:
  ScopeChain            Ctx;
  std::vector<SemaDiag> Diags;
  MaraTypeKind          ReturnType  = MaraTypeKind::Unknown;
  bool                  InFunction  = false;
  bool                  SeenRet     = false;

  // --- helpers ---
  void diag(unsigned L, unsigned C, bool Err, StringRef Msg);
  void err(unsigned L, unsigned C, StringRef Msg)  { diag(L, C, true,  Msg); }
  void warn(unsigned L, unsigned C, StringRef Msg) { diag(L, C, false, Msg); }

  bool typesCompatible(MaraTypeKind Dst, MaraTypeKind Src) const;
  std::string typeStr(MaraTypeKind K) const;

  // --- analysis methods ---
  void analyseGlobals(Module &M);
  void registerFunctionSignatures(Module &M);
  void analyseFunction(FunctionDecl &F);
  void analyseBlock(BlockStmt &B);
  void analyseStmt(ASTNode &S);
  void analyseVarDecl(VarDecl &D);
  void analyseIfStmt(IfStmt &S);
  void analyseLoopStmt(LoopStmt &S);
  void analyseRetStmt(RetStmt &S);
  void analyseAssignStmt(AssignStmt &S);
  void analyseLogStmt(LogStmt &S);
  void analyseExprStmt(ExprStmt &S);

  MaraTypeKind analyseExpr(Expr &E);
  MaraTypeKind analyseFFICall(FFICallExpr &E);
  MaraTypeKind analyseCallExpr(CallExpr &E);
  MaraTypeKind analyseBinaryExpr(BinaryExpr &E);
  MaraTypeKind analyseVarRef(VarRef &E);
};

} // namespace maratine
} // namespace llvm

#endif // LLVM_MARATINE_SEMA_H
