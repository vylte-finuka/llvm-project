//===--- MaratineParser.h - Parser for Mara/Maratine language -----------===//
//
// Vyft Ltd — Proprietary — 2026
//
//===----------------------------------------------------------------------===//
//
// Recursive-descent parser for the Mara language.
//
// Key Mara syntax rules enforced here:
//   - Function bodies use [ ] — never { }
//   - Array literals: [ elem; elem; … ] — separator is ; not ,
//   - if (cond) [ … ] else [ … ]  — no "then" keyword
//   - loop cond [ … ]             — condition is not parenthesised
//   - ret expr;                   — no colon after ret
//   - log: expr;                  — colon IS part of log syntax
//   - rel op / rel cl             — both visibilities parsed
//   - Types inside < >: string i32 i64 u64 bool ptr array (only)
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_MARATINE_MARATINEPARSER_H
#define LLVM_FRONTEND_MARATINE_MARATINEPARSER_H

#include "MaratineAST.h"
#include "clang/Parse/Parser.h"

namespace clang {
namespace maratine {

class MaratineParser : public Parser {
public:
  MaratineParser(Preprocessor &PP, SourceManager &SM,
                 LangOptions &LO, DiagnosticsEngine &Diags);
  ~MaratineParser() override = default;

  /// Entry point — parse a full translation unit.
  void ParseTranslationUnit() override;

private:
  // -------------------------------------------------------------------------
  // Declaration parsing
  // -------------------------------------------------------------------------
  bool ParseImportDecl();       // #base <path***[ deps ]>;
  bool ParseLetDecl();          // let name: <type> = expr;
  bool ParseVarDecl();          // var name: <type> = expr;
  bool ParseFunctionDecl();     // rel op/cl name: [params] [ body ];

  // -------------------------------------------------------------------------
  // Statement parsing (used inside [ body ] blocks)
  // -------------------------------------------------------------------------
  Stmt *ParseStatement();       // dispatcher
  bool  ParseIfStmt();          // if (cond) [ … ] else [ … ];
  bool  ParseLoopStmt();        // loop cond [ … ];
  bool  ParseBreakStmt();       // break;
  bool  ParseLogStmt();         // log: expr;
  bool  ParseRetStmt();         // ret expr;
  bool  ParseAssignOrCallStmt();// expr = expr;  /  call expr;

  // -------------------------------------------------------------------------
  // Block parsing
  // -------------------------------------------------------------------------
  Stmt *ParseBlock();           // consumes '[' stmts ']' and builds a CompoundStmt

  // -------------------------------------------------------------------------
  // Type parsing
  // -------------------------------------------------------------------------
  /// Parse a Mara type annotation: <string>, <i32>, <[string TypeName]>, …
  /// Returns the parsed type as a Clang QualType.
  bool ParseTypeAnnotation(QualType &Out);

  /// Parse the inheritance suffix: <[string TypeName]>t
  /// Returns the base type name.
  StringRef ParseInheritanceSuffix();

  // -------------------------------------------------------------------------
  // Expression parsing
  // -------------------------------------------------------------------------
  Expr *ParseExpression();
  Expr *ParseBinaryExpr(int MinPrec = 0);
  Expr *ParseUnaryExpr();
  Expr *ParsePrimaryExpr();
  Expr *ParseArrayLiteralExpr();   // [ elem; elem; … ]  (separator = ';')
  Expr *ParseFFICallExpr();        // <Module***Fn***>(args)
  Expr *ParseCallExpr(Expr *Callee);

  // -------------------------------------------------------------------------
  // Parameter list parsing: [name type, name type]
  // -------------------------------------------------------------------------
  bool ParseParamList(SmallVectorImpl<ParmVarDecl *> &Params);

  // -------------------------------------------------------------------------
  // Helpers
  // -------------------------------------------------------------------------
  bool           Expect(tok::TokenKind K, const char *Msg = nullptr);
  bool           ExpectAndConsumeTok(tok::TokenKind K, const char *Msg = nullptr);
  SourceLocation ConsumeTok();
  bool           IsMараType(tok::TokenKind K) const;

  // Token predicates
  bool isKw(tok::TokenKind K) const { return Tok.is(K); }
};

} // namespace maratine
} // namespace clang

#endif // LLVM_FRONTEND_MARATINE_MARATINEPARSER_H
