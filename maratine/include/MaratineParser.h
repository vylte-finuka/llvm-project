// Vyft Ltd — Mara/Maratine Parser — Proprietary — 2026
//
// Recursive-descent parser for the Mara language.
//
// Mara syntax contract:
//   Blocks         [ … ]  — square brackets, NEVER { }
//   Arrays         [ a; b; c ]  — semicolon separator
//   if (cond) [ then ] else [ else ];   — no "then" keyword
//   loop cond [ body ];                 — condition NOT parenthesised
//   ret expr;     — no colon
//   log: expr;    — colon IS required
//   rel op/cl name: [params] [ body ];
//   Types          <string> <i32> <i64> <u64> <bool> <ptr> <array>
//   FFI            <Path***Fn***>(args)

#ifndef LLVM_MARATINE_PARSER_H
#define LLVM_MARATINE_PARSER_H

#include "MaratineLexer.h"
#include "MaratineAST.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <vector>

namespace llvm {
namespace maratine {

class Parser {
public:
  explicit Parser(const std::vector<Token> &Toks);

  Expected<std::unique_ptr<Module>> parseModule();
  void printAST(const Module &M) const;

private:
  // -------------------------------------------------------------------------
  // Token navigation
  // -------------------------------------------------------------------------
  const Token &peek(int Ahead = 0) const;
  const Token &advance();
  bool         check(TokenKind K) const;
  bool         match(TokenKind K);
  bool         expectAndConsume(TokenKind K, StringRef Msg);
  bool         isTypeKw(TokenKind K) const;

  // -------------------------------------------------------------------------
  // Top-level declarations
  // -------------------------------------------------------------------------
  Expected<std::unique_ptr<ImportDecl>>   parseImport();
  Expected<std::unique_ptr<VarDecl>>      parseLetOrVar();
  Expected<std::unique_ptr<FunctionDecl>> parseFunction();

  // -------------------------------------------------------------------------
  // Type parsing
  // -------------------------------------------------------------------------
  // Parses <type> or <[string TypeName]> — returns the kind and compound name
  bool parseTypeAnnotation(MaraTypeKind &Kind, std::string &CompoundName);
  // Parses <[string TypeName]>t and returns the base type name
  std::string parseInheritanceSuffix();
  // Parses [name type, …] parameter list
  bool parseParamList(SmallVectorImpl<Param> &Params);

  // -------------------------------------------------------------------------
  // Statements (inside [ block ] bodies)
  // -------------------------------------------------------------------------
  Expected<std::unique_ptr<ASTNode>>      parseStatement();
  Expected<std::unique_ptr<BlockStmt>>    parseBlock();
  Expected<std::unique_ptr<IfStmt>>       parseIf();
  Expected<std::unique_ptr<LoopStmt>>     parseLoop();
  Expected<std::unique_ptr<BreakStmt>>    parseBreak();
  Expected<std::unique_ptr<LogStmt>>      parseLog();
  Expected<std::unique_ptr<RetStmt>>      parseRet();

  // -------------------------------------------------------------------------
  // Expressions
  // -------------------------------------------------------------------------
  Expected<std::unique_ptr<Expr>>         parseExpr();
  Expected<std::unique_ptr<Expr>>         parseBinaryExpr(int MinPrec = 0);
  Expected<std::unique_ptr<Expr>>         parseUnaryExpr();
  Expected<std::unique_ptr<Expr>>         parsePrimary();
  Expected<std::unique_ptr<Expr>>         parseFFICall();
  Expected<std::unique_ptr<Expr>>         parseArrayLiteral();

  // -------------------------------------------------------------------------
  // Error helper
  // -------------------------------------------------------------------------
  template <typename T = ASTNode>
  Error mkErr(StringRef Msg) {
    return createStringError(inconvertibleErrorCode(),
                             "[" + std::to_string(peek().Line) + ":" +
                             std::to_string(peek().Column) + "] " + Msg.str());
  }

  // -------------------------------------------------------------------------
  // State
  // -------------------------------------------------------------------------
  std::vector<Token> Tokens;
  size_t             Cur = 0;
};

} // namespace maratine
} // namespace llvm

#endif // LLVM_MARATINE_PARSER_H
