//===--- MaratineParser.h - Parser for Maratine language -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_MARATINE_MARATINEPARSER_H
#define LLVM_FRONTEND_MARATINE_MARATINEPARSER_H

#include "clang/Parse/Parser.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Stmt.h"

namespace clang {
namespace maratine {

class MaratineParser : public Parser {
public:
  MaratineParser(Preprocessor &PP, SourceManager &SM,
                 LangOptions &LO, DiagnosticsEngine &Diags);
  ~MaratineParser() override = default;

  /// Entry point – parse a translation unit.
  void ParseTranslationUnit() override;

private:
  // --- Declaration parsing -------------------------------------------------
  bool ParseImportDecl();
  bool ParseLetDecl();
  bool ParseVarDecl();          // mutable variable
  bool ParseConstDecl();        // immutable compile‑time constant
  bool ParseFunctionDecl();

  // --- Statement parsing ---------------------------------------------------
  bool ParseExprStmt(); // handles log: and ret:
  bool ParseLoopStmt(); // handles loop { … } and loop <cond> { … }

  // --- Expression parsing --------------------------------------------------
  Expr *ParseAssignmentOrConditionalExpr(); // thin wrapper
  Expr *ParseArrayLiteralExpression();      // [ expr, … ]

  // --- Helpers -------------------------------------------------------------
  bool ExpectAndConsume(tok::TokenKind K, const char *Msg = nullptr);
  SourceLocation ConsumeToken(tok::TokenKind K);
};

} // namespace maratine
} // namespace clang

#endif // LLVM_FRONTEND_MARATINE_MARATINEPARSER_H