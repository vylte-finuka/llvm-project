// Vyft Ltd — Mara/Maratine Lexer Implementation — Proprietary — 2026

#include "MaratineLexer.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cctype>
#include <map>

using namespace llvm;
using namespace llvm::maratine;

Lexer::Lexer(StringRef Src) : Source(Src) {}

char Lexer::peek(int Offset) const {
  assert(Offset >= 0 && "peek() does not support negative offsets");
  size_t I = Pos + (size_t)Offset;
  if (I >= Source.size()) return '\0';
  return Source[I];
}

char Lexer::advance() {
  if (Pos >= Source.size()) return '\0';
  char Ch = Source[Pos++];
  if (Ch == '\n') { Line++; Col = 1; } else { Col++; }
  return Ch;
}

void Lexer::skipWhitespace() {
  while (Pos < Source.size() && std::isspace(peek()))
    advance();
}

void Lexer::skipLineComment() {
  while (peek() != '\n' && peek() != '\0')
    advance();
}

Token Lexer::makeToken(TokenKind K, std::string V) {
  return Token{K, std::move(V), Line, Col};
}

Token Lexer::lexStringLiteral() {
  advance(); // skip opening '"'
  size_t Start = Pos;
  while (peek() != '"' && peek() != '\0') advance();
  std::string Val = Source.substr(Start, Pos - Start).str();
  advance(); // skip closing '"'
  return makeToken(TokenKind::string_literal, Val);
}

Token Lexer::lexInteger() {
  size_t Start = Pos;
  // Hex literal: 0x / 0X — require at least one hex digit after the prefix.
  if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X') && std::isxdigit(peek(2))) {
    advance(); advance(); // consume '0' and 'x'/'X'
    while (std::isxdigit(peek())) advance();
  } else {
    while (std::isdigit(peek())) advance();
  }
  return makeToken(TokenKind::integer_literal,
                   Source.substr(Start, Pos - Start).str());
}

Token Lexer::lexIdentifier() {
  size_t Start = Pos;
  while (std::isalnum(peek()) || peek() == '_') advance();
  StringRef Text = Source.substr(Start, Pos - Start);
  TokenKind K = keywordKind(Text);
  return makeToken(K, Text.str());
}

TokenKind Lexer::keywordKind(StringRef T) const {
  static const std::map<std::string, TokenKind> KW = {
    { "base",    TokenKind::kw_base   },
    { "var",     TokenKind::kw_var    },
    { "let",     TokenKind::kw_let    },
    { "rel",     TokenKind::kw_rel    },
    { "op",      TokenKind::kw_op     },
    { "cl",      TokenKind::kw_cl     },
    { "if",      TokenKind::kw_if     },
    { "else",    TokenKind::kw_else   },
    { "loop",    TokenKind::kw_loop   },
    { "break",   TokenKind::kw_break  },
    { "log",     TokenKind::kw_log    },
    { "ret",     TokenKind::kw_ret    },
    { "self",    TokenKind::kw_self   },
    { "null",    TokenKind::kw_null   },
    { "nullptr", TokenKind::kw_nullptr},
    { "true",    TokenKind::kw_true   },
    { "false",   TokenKind::kw_false  },
    // Mara primitive types (7 total)
    { "string",  TokenKind::kw_type_string },
    { "i32",     TokenKind::kw_type_i32   },
    { "i64",     TokenKind::kw_type_i64   },
    { "u64",     TokenKind::kw_type_u64   },
    { "bool",    TokenKind::kw_type_bool  },
    { "ptr",     TokenKind::kw_type_ptr   },
    { "array",   TokenKind::kw_type_array },
  };
  auto It = KW.find(T.str());
  return (It != KW.end()) ? It->second : TokenKind::identifier;
}

Token Lexer::nextToken() {
  skipWhitespace();

  if (Pos >= Source.size())
    return makeToken(TokenKind::eof);

  // Line comments: //
  if (peek() == '/' && peek(1) == '/') {
    skipLineComment();
    return nextToken();
  }

  char Ch = peek();

  if (Ch == '"')  return lexStringLiteral();
  if (std::isdigit(Ch)) return lexInteger();
  if (std::isalpha(Ch) || Ch == '_') return lexIdentifier();

  advance(); // consume the character

  switch (Ch) {
    // *** triple-star (module path separator)
    case '*':
      if (peek() == '*' && peek(1) == '*') {
        advance(); advance();
        return makeToken(TokenKind::triple_star, "***");
      }
      return makeToken(TokenKind::star, "*");

    case '(': return makeToken(TokenKind::l_paren,  "(");
    case ')': return makeToken(TokenKind::r_paren,  ")");
    case '[': return makeToken(TokenKind::l_square, "[");
    case ']': return makeToken(TokenKind::r_square, "]");
    case '<':
      if (peek() == '<') { advance(); return makeToken(TokenKind::lessless, "<<"); }
      if (peek() == '=') { advance(); return makeToken(TokenKind::lessequal, "<="); }
      return makeToken(TokenKind::l_angle, "<");
    case '>':
      if (peek() == '>') { advance(); return makeToken(TokenKind::greatergreater, ">>"); }
      if (peek() == '=') { advance(); return makeToken(TokenKind::greaterequal, ">="); }
      return makeToken(TokenKind::r_angle, ">");
    case ':': return makeToken(TokenKind::colon,  ":");
    case ';': return makeToken(TokenKind::semi,   ";");
    case ',': return makeToken(TokenKind::comma,  ",");
    case '=':
      if (peek() == '=') { advance(); return makeToken(TokenKind::equalequal, "=="); }
      return makeToken(TokenKind::equals, "=");
    case '.': return makeToken(TokenKind::dot, ".");
    case '+': return makeToken(TokenKind::plus,  "+");
    case '-':
      if (peek() == '>') { advance(); return makeToken(TokenKind::arrow, "->"); }
      return makeToken(TokenKind::minus, "-");
    case '/': return makeToken(TokenKind::slash, "/");
    case '%': return makeToken(TokenKind::percent, "%");
    case '&':
      if (peek() == '&') { advance(); return makeToken(TokenKind::ampamp, "&&"); }
      return makeToken(TokenKind::amp, "&");
    case '|':
      if (peek() == '|') { advance(); return makeToken(TokenKind::pipepipe, "||"); }
      return makeToken(TokenKind::pipe, "|");
    case '^': return makeToken(TokenKind::caret,  "^");
    case '~': return makeToken(TokenKind::tilde,  "~");
    case '!':
      if (peek() == '=') { advance(); return makeToken(TokenKind::exclaimequal, "!="); }
      return makeToken(TokenKind::exclaim, "!");
    case '#': return makeToken(TokenKind::hash, "#");
    default:  return makeToken(TokenKind::unknown, std::string(1, Ch));
  }
}

std::vector<Token> Lexer::tokenize() {
  Tokens.clear();
  Token T = nextToken();
  while (T.Kind != TokenKind::eof) {
    Tokens.push_back(T);
    T = nextToken();
  }
  Tokens.push_back(T);
  return Tokens;
}

void Lexer::printTokens() const {
  for (const auto &T : Tokens)
    errs() << "Tok[" << T.Line << ":" << T.Column << "] "
           << (int)T.Kind << " = " << T.Value << "\n";
}
