// Vyft Ltd - Maratine Lexer Implementation

#include "MaratineLexer.h"
#include "llvm/Support/raw_ostream.h"
#include <cctype>
#include <map>

using namespace llvm;
using namespace llvm::maratine;

Lexer::Lexer(StringRef Src) : Source(Src) {}

char Lexer::peek(int offset) const {
  if (Position + offset >= Source.size())
    return '\0';
  return Source[Position + offset];
}

char Lexer::advance() {
  if (Position >= Source.size())
    return '\0';

  char Ch = Source[Position++];
  if (Ch == '\n') {
    Line++;
    Column = 1;
  } else {
    Column++;
  }
  return Ch;
}

void Lexer::skipWhitespace() {
  while (std::isspace(peek()))
    advance();
}

void Lexer::skipComment() {
  if (peek() == '/' && peek(1) == '/') {
    while (peek() != '\n' && peek() != '\0')
      advance();
  }
}

Token Lexer::makeToken(TokenKind Kind, StringRef Lexeme) {
  return Token{Kind, Lexeme, Line, Column};
}

Token Lexer::makeStringLiteral() {
  advance(); // Skip opening quote
  size_t Start = Position;

  while (peek() != '"' && peek() != '\0')
    advance();

  std::string Value = Source.substr(Start, Position - Start).str();
  advance(); // Skip closing quote

  Token T = makeToken(TokenKind::string_literal, "\"");
  T.Value = Value;
  return T;
}

Token Lexer::makeIdentifier() {
  size_t Start = Position;

  while (std::isalnum(peek()) || peek() == '_')
    advance();

  StringRef Lexeme = Source.substr(Start, Position - Start);
  TokenKind Kind = getKeywordKind(Lexeme);

  Token T = makeToken(Kind, Lexeme);
  T.Value = Lexeme.str();
  return T;
}

Token Lexer::makeNumber() {
  size_t Start = Position;

  while (std::isdigit(peek()))
    advance();

  if (peek() == '.' && std::isdigit(peek(1))) {
    advance();
    while (std::isdigit(peek()))
      advance();
  }

  StringRef Lexeme = Source.substr(Start, Position - Start);
  Token T = makeToken(TokenKind::number, Lexeme);
  T.Value = Lexeme.str();
  return T;
}

TokenKind Lexer::getKeywordKind(StringRef Text) {
  static const std::map<std::string, TokenKind> Keywords = {
    {"std", TokenKind::kw_std},
    {"rel", TokenKind::kw_rel},
    {"op", TokenKind::kw_op},
    {"cl", TokenKind::kw_cl},
    {"let", TokenKind::kw_let},
    {"if", TokenKind::kw_if},
    {"log", TokenKind::kw_log},
    {"View", TokenKind::kw_View},
    {"Text", TokenKind::kw_Text},
  };

  auto It = Keywords.find(Text.str());
  if (It != Keywords.end())
    return It->second;

  return TokenKind::identifier;
}

Token Lexer::nextToken() {
  skipWhitespace();

  if (Position >= Source.size())
    return makeToken(TokenKind::eof, "");

  // Traiter les commentaires
  while (peek() == '/' && peek(1) == '/') {
    skipComment();
    skipWhitespace();
  }

  char Ch = peek();

  // Littéraux strings
  if (Ch == '"')
    return makeStringLiteral();

  // Identifiants et keywords
  if (std::isalpha(Ch) || Ch == '_')
    return makeIdentifier();

  // Nombres
  if (std::isdigit(Ch))
    return makeNumber();

  // Caractères spéciaux
  advance();

  switch (Ch) {
  case '(':
    return makeToken(TokenKind::l_paren, "(");
  case ')':
    return makeToken(TokenKind::r_paren, ")");
  case '[':
    return makeToken(TokenKind::l_square, "[");
  case ']':
    return makeToken(TokenKind::r_square, "]");
  case '<':
    return makeToken(TokenKind::l_angle, "<");
  case '>':
    if (peek() == '=') {
      advance();
      return makeToken(TokenKind::arrow, "->");
    }
    return makeToken(TokenKind::r_angle, ">");
  case '{':
    return makeToken(TokenKind::l_brace, "{");
  case '}':
    return makeToken(TokenKind::r_brace, "}");
  case ':':
    return makeToken(TokenKind::colon, ":");
  case ';':
    return makeToken(TokenKind::semicolon, ";");
  case ',':
    return makeToken(TokenKind::comma, ",");
  case '*':
    return makeToken(TokenKind::star, "*");
  case '=':
    return makeToken(TokenKind::equals, "=");
  case '#':
    return makeToken(TokenKind::hash, "#");
  case '-':
    if (peek() == '>')
      advance();
    return makeToken(TokenKind::arrow, "->");
  default:
    return makeToken(TokenKind::unknown, StringRef(&Ch, 1));
  }
}

std::vector<Token> Lexer::tokenize() {
  Token T = nextToken();

  while (T.Kind != TokenKind::eof) {
    Tokens.push_back(T);
    T = nextToken();
  }

  Tokens.push_back(T); // EOF token
  return Tokens;
}

void Lexer::printTokens() const {
  for (const auto &T : Tokens) {
    errs() << "Token: " << (int)T.Kind << " Value: " << T.Value << "\n";
  }
}
