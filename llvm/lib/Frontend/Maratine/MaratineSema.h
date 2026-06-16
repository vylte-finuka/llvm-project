//===--- MaratineSema.h - Semantic analysis for Mara/Maratine language --===//
//
// Vyft Ltd — Proprietary — 2026
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_MARATINE_MARATINESEMA_H
#define LLVM_FRONTEND_MARATINE_MARATINESEMA_H

#include "MaratineAST.h"
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

  bool ActOnImportDecl(MaratineImportDecl *D);
  bool ActOnLetDecl(MaratineLetDecl *D);
  bool ActOnFunctionDecl(MaratineFunctionDecl *D);
  bool ActOnIfStmt(MaratineIfStmt *S);
  bool ActOnLoopStmt(MaratineLoopStmt *S);
  bool ActOnExprStmt(MaratineExprStmt *S);
};

} // namespace maratine
} // namespace clang

#endif // LLVM_FRONTEND_MARATINE_MARATINESEMA_H
