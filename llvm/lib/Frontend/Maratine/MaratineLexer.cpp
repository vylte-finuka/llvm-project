//===--- MaratineLexer.cpp - Lexer for Mara/Maratine language -----------===//
//
// Vyft Ltd — Proprietary — 2026
//
//===----------------------------------------------------------------------===//
//
// Lexer for the Mara language. Extends the base clang TokenLexer to:
//   - Recognize Mara keywords (var, let, rel, op, cl, if, else, loop, …)
//   - Lex the *** triple-star path separator as a single token
//   - Map only the 7 Mara primitive types (string i32 i64 u64 bool ptr array)
//   - Reject forbidden words (then, const, String, print, f32, u32, …)
//     by letting them fall through as plain identifiers so the parser can
//     emit a targeted diagnostic.
//
//===----------------------------------------------------------------------===//

#include "MaratineLexer.h"
#include "clang/Basic/CharInfo.h"
#include "llvm/ADT/StringSwitch.h"

using namespace clang;
using namespace clang::maratine;

MaratineLexer::MaratineLexer(const LangOptions &LO, SourceManager &SM,
                             const FileID &FID,
                             const SrcMgr::CharacteristicKind CharWidth,
                             bool DisableTrigraphs)
  : TokenLexer(LO, SM, FID, CharWidth, DisableTrigraphs) {}

bool MaratineLexer::LexToken(Token &Result) {
  if (TokenLexer::LexToken(Result))
    return true;

  // -----------------------------------------------------------------------
  // 1. Keyword recognition
  // -----------------------------------------------------------------------
  if (Result.is(tok::identifier)) {
    StringRef Text = getSourceManager()
                       .getCharacterData(Result.getLocation())
                       .substr(0, Result.getLength());
    tok::TokenKind K = getKeywordKind(Text);
    if (K != tok::unknown) {
      Result.setKind(K);
      return true;
    }
    // Plain identifier — leave as-is.
    return true;
  }

  // -----------------------------------------------------------------------
  // 2. *** triple-star path separator
  //    Lex three consecutive '*' characters as a single tok::maratine_triple_star.
  // -----------------------------------------------------------------------
  if (Result.is(tok::star)) {
    const char *Ptr =
        getSourceManager().getCharacterData(Result.getLocation());
    if (Ptr[1] == '*' && Ptr[2] == '*') {
      Result.setKind(tok::maratine_triple_star);
      Result.setLength(3);
      return true;
    }
    // Single '*' — leave as tok::star.
    return true;
  }

  // -----------------------------------------------------------------------
  // 3. Compound operators
  // -----------------------------------------------------------------------
  if (Result.is(tok::unknown)) {
    const char *Ptr =
        getSourceManager().getCharacterData(Result.getLocation());

    auto tryTwo = [&](char a, char b, tok::TokenKind K) -> bool {
      if (Ptr[0] == a && Ptr[1] == b) {
        Result.setKind(K);
        Result.setLength(2);
        return true;
      }
      return false;
    };

    if (tryTwo('=', '=', tok::equalequal))    return true;
    if (tryTwo('!', '=', tok::exclaimequal))  return true;
    if (tryTwo('<', '=', tok::lessequal))      return true;
    if (tryTwo('>', '=', tok::greaterequal))   return true;
    if (tryTwo('&', '&', tok::ampamp))         return true;
    if (tryTwo('|', '|', tok::pipepipe))       return true;
    if (tryTwo('<', '<', tok::lessless))       return true;
    if (tryTwo('>', '>', tok::greatergreater)) return true;
    if (tryTwo('-', '>', tok::arrow))          return true;
  }

  return false;
}

// -------------------------------------------------------------------------
// Keyword table — strictly maps the Mara spec.
// Words NOT in this table are plain identifiers (the parser handles errors).
// -------------------------------------------------------------------------
tok::TokenKind MaratineLexer::getKeywordKind(StringRef Text) {
  return llvm::StringSwitch<tok::TokenKind>(Text)
    // --- Import directive (handled as a keyword prefix) ---
    // Note: "#base" is lexed as '#' + "base"; the parser combines them.
    // We handle the bare word "base" here for robustness.
    .Case("base",     tok::maratine_base)

    // --- Variable / constant declaration ---
    .Case("var",      tok::maratine_var)   // mutable
    .Case("let",      tok::maratine_let)   // immutable

    // --- Function declaration ---
    .Case("rel",      tok::maratine_rel)
    .Case("op",       tok::maratine_op)    // public
    .Case("cl",       tok::maratine_cl)    // private

    // --- Control flow ---
    .Case("if",       tok::maratine_if)
    .Case("else",     tok::maratine_else)
    .Case("loop",     tok::maratine_loop)
    .Case("break",    tok::maratine_break)

    // --- Statements ---
    .Case("log",      tok::maratine_log)
    .Case("ret",      tok::maratine_ret)

    // --- Literals ---
    .Case("null",     tok::maratine_null)
    .Case("nullptr",  tok::maratine_nullptr)
    .Case("true",     tok::maratine_true)
    .Case("false",    tok::maratine_false)
    .Case("self",     tok::maratine_self)

    // --- Mara primitive types (only valid inside < >) ---
    .Case("string",   tok::maratine_type_string)  // always lowercase
    .Case("i32",      tok::maratine_type_i32)
    .Case("i64",      tok::maratine_type_i64)
    .Case("u64",      tok::maratine_type_u64)
    .Case("bool",     tok::maratine_type_bool)
    .Case("ptr",      tok::maratine_type_ptr)
    .Case("array",    tok::maratine_type_array)

    // Everything else is a plain identifier.
    .Default(tok::unknown);
}
