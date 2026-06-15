// Vyft Ltd - Maratine Parser
// Parser pour le langage Maratine (*.mart)

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
private:
  std::vector<Token> Tokens;
  size_t Current = 0;

  // Navigation dans les tokens
  const Token &peek() const;
  const Token &advance();
  bool check(TokenKind Kind) const;
  bool match(TokenKind Kind);
  bool match(std::initializer_list<TokenKind> Kinds);

  // Parsing de la structure Maratine
  Expected<std::unique_ptr<ImportStmt>> parseImport();
  Expected<std::unique_ptr<FunctionDecl>> parseFunction();
  Expected<std::unique_ptr<VarDecl>> parseVarDecl();
  Expected<std::unique_ptr<IfStmt>> parseIfStmt();
  Expected<std::unique_ptr<LogStmt>> parseLogStmt();
  Expected<std::unique_ptr<UIComponent>> parseUIComponent();
  Expected<std::unique_ptr<BlockStmt>> parseBlock();

  // Expressions
  Expected<std::unique_ptr<Expr>> parseExpression();
  Expected<std::unique_ptr<Expr>> parsePrimary();
  Expected<std::unique_ptr<Expr>> parseUIComponentExpr();

  // Décorateurs
  Expected<std::unique_ptr<Decorator>> parseDecorator();

  // Utilitaires
  void skipToNextStatement();
  void reportError(StringRef Message);

public:
  explicit Parser(const std::vector<Token> &Toks);

  /// Parse un fichier Maratine complet
  Expected<std::unique_ptr<Module>> parseModule();

  /// Diagnostic
  void printAST(const Module &M) const;
};

} // namespace maratine
} // namespace llvm

#endif // LLVM_MARATINE_PARSER_H
