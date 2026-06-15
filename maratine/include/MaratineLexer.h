// Vyft Ltd - Maratine Lexer
// Lexer pour le langage Maratine (*.mart)

#ifndef LLVM_MARATINE_LEXER_H
#define LLVM_MARATINE_LEXER_H

#include "llvm/Support/SMLoc.h"
#include "llvm/ADT/StringRef.h"
#include <string>
#include <vector>

namespace llvm {
namespace maratine {

enum class TokenKind {
  // Keywords
  kw_std,      // std***module***imports
  kw_rel,      // rel (relation/fonction)
  kw_op,       // op (opération/public)
  kw_cl,       // cl (privé/class)
  kw_let,      // let (variable)
  kw_if,       // if (condition)
  kw_log,      // log (affichage)
  
  // UI Components
  kw_View,     // <View>
  kw_Text,     // <Text>
  
  // Literals
  string_literal,
  identifier,
  number,
  
  // Punctuation
  l_paren,     // (
  r_paren,     // )
  l_square,    // [
  r_square,    // ]
  l_angle,     // <
  r_angle,     // >
  l_brace,     // {
  r_brace,     // }
  
  // Operators
  colon,       // :
  semicolon,   // ;
  comma,       // ,
  star,        // *
  equals,      // =
  arrow,       // ->
  hash,        // #
  
  // Special
  eof,
  unknown
};

struct Token {
  TokenKind Kind;
  StringRef Lexeme;
  int Line;
  int Column;
  std::string Value;
};

class Lexer {
private:
  StringRef Source;
  size_t Position = 0;
  int Line = 1;
  int Column = 1;
  std::vector<Token> Tokens;

  char peek(int offset = 0) const;
  char advance();
  void skipWhitespace();
  void skipComment();
  Token makeToken(TokenKind Kind, StringRef Lexeme);
  Token makeStringLiteral();
  Token makeIdentifier();
  Token makeNumber();
  TokenKind getKeywordKind(StringRef Text);

public:
  explicit Lexer(StringRef Src);
  
  std::vector<Token> tokenize();
  Token nextToken();
  
  // Diagnostic helpers
  void printTokens() const;
};

} // namespace maratine
} // namespace llvm

#endif // LLVM_MARATINE_LEXER_H
