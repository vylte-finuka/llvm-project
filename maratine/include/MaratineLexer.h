// Vyft Ltd — Mara/Maratine Lexer — Proprietary — 2026
//
// Lexer for the Mara language (*.mara source files).
//
// Token rules:
//   *** is lexed as a single triple_star token (module path separator)
//   Types (string i32 i64 u64 bool ptr array) are keywords
//   { } braces are NOT valid Mara punctuation — bodies use [ ]
//   Array literals use ';' as separator (not ',')

#ifndef LLVM_MARATINE_LEXER_H
#define LLVM_MARATINE_LEXER_H

#include "llvm/ADT/StringRef.h"
#include <string>
#include <vector>

namespace llvm {
namespace maratine {

enum class TokenKind {
  // Directives
  kw_base,        // base  (after #, forms #base)
  hash,           // #

  // Variable declarations
  kw_var,         // var — mutable
  kw_let,         // let — immutable

  // Function declaration
  kw_rel,         // rel
  kw_op,          // op  (public)
  kw_cl,          // cl  (private)

  // Control flow
  kw_if,          // if
  kw_else,        // else
  kw_loop,        // loop
  kw_break,       // break

  // Statements
  kw_log,         // log
  kw_ret,         // ret

  // Self reference
  kw_self,        // self

  // Literals
  kw_null,        // null
  kw_nullptr,     // nullptr
  kw_true,        // true
  kw_false,       // false

  // Primitive types (only valid inside < >)
  kw_type_string, // string
  kw_type_i32,    // i32
  kw_type_i64,    // i64
  kw_type_u64,    // u64
  kw_type_bool,   // bool
  kw_type_ptr,    // ptr
  kw_type_array,  // array

  // Tokens
  identifier,
  string_literal,
  integer_literal,

  // Punctuation — Mara uses [ ] for ALL bodies and arrays
  l_paren,        // (
  r_paren,        // )
  l_square,       // [
  r_square,       // ]
  l_angle,        // <
  r_angle,        // >

  // Separators
  colon,          // :
  semi,           // ;  (also array element separator in Mara)
  comma,          // ,  (used in parameter lists and dependency lists)
  equals,         // =
  dot,            // .

  // Operators
  plus,           // +
  minus,          // -
  star,           // *
  slash,          // /
  percent,        // %
  amp,            // &
  pipe,           // |
  caret,          // ^
  tilde,          // ~
  exclaim,        // !
  ampamp,         // &&
  pipepipe,       // ||
  equalequal,     // ==
  exclaimequal,   // !=
  lessequal,      // <=
  greaterequal,   // >=
  lessless,       // <<
  greatergreater, // >>
  arrow,          // ->

  // Mara-specific
  triple_star,    // ***  (module path separator)
  inherit_t,      // t  (suffix in <[string T]>t — lexed as kw_inherit_t
                  //      only when appearing after > in that context)

  // Special
  eof,
  unknown
};

struct Token {
  TokenKind   Kind;
  std::string Value;
  int         Line;
  int         Column;
};

class Lexer {
public:
  explicit Lexer(StringRef Src);

  std::vector<Token> tokenize();
  Token              nextToken();
  void               printTokens() const;

private:
  StringRef Source;
  size_t    Pos   = 0;
  int       Line  = 1;
  int       Col   = 1;
  std::vector<Token> Tokens;

  char       peek(int Offset = 0) const;
  char       advance();
  void       skipWhitespace();
  void       skipLineComment();
  Token      makeToken(TokenKind K, std::string V = {});
  Token      lexStringLiteral();
  Token      lexIdentifier();
  Token      lexInteger();
  TokenKind  keywordKind(StringRef Text) const;
};

} // namespace maratine
} // namespace llvm

#endif // LLVM_MARATINE_LEXER_H
