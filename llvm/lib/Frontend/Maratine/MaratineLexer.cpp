//===--- MaratineLexer.cpp - Lexer for Maratine language -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MaratineLexer.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Basic/CharInfo.h"
#include "llvm/ADT/StringSwitch.h"

using namespace clang;
using namespace clang::maratine;

MaratineLexer::MaratineLexer(const LangOptions &LO, SourceManager &SM,
                             const FileID &FID, const SrcMgr::CharacteristicKind CharWidth,
                             bool DisableTrigraphs)
  : TokenLexer(LO, SM, FID, CharWidth, DisableTrigraphs) {}

bool MaratineLexer::LexToken(Token &Result) {
  // Let the base lexer do the heavy lifting (whitespace, comments, etc.)
  if (TokenLexer::LexToken(Result))
    return true;

  // If we got an identifier, see if it's a Maratine keyword.
  if (Result.is(tok::identifier)) {
    StringRef Text = getSourceManager().getCharacterData(
                         Result.getLocation())
                         .substr(0, Result.getLength());
    TokenKind K = getKeywordKind(Text);
    if (K != tok::unknown) {
      Result.setKind(K);
      return true;
    }
  }

  // Handle multi‑character operators that are not in the base lexer.
  if (Result.is(tok::unknown)) {
    const char *Ptr = getSourceManager().getCharacterData(Result.getLocation());
    // Two‑character operators
    if (Ptr[0] == '+' && Ptr[1] == '>') {
      Result.setKind(tok::maratine_greater_assign);
      Result.setLength(2);
      return true;
    }
    if (Ptr[0] == '+' && Ptr[1] == '<') {
      Result.setKind(tok::maratine_less_assign);
      Result.setLength(2);
      return true;
    }
    if (Ptr[0] == '-' && Ptr[1] == '<') {
      Result.setKind(tok::maratine_minus_less);
      Result.setLength(2);
      return true;
    }
    if (Ptr[0] == '=' && Ptr[1] == '+') {
      Result.setKind(tok::maratine_eq_plus);
      Result.setLength(2);
      return true;
    }
    if (Ptr[0] == '-' && Ptr[1] == '=') {
      Result.setKind(tok::maratine_minus_eq);
      Result.setLength(2);
      return true;
    }
    if (Ptr[0] == '*' && Ptr[1] == '>') {
      Result.setKind(tok::maratine_star_greater);
      Result.setLength(2);
      return true;
    }
    if (Ptr[0] == '*' && Ptr[1] == '<') {
      Result.setKind(tok::maratine_star_less);
      Result.setLength(2);
      return true;
    }
    if (Ptr[0] == '/' && Ptr[1] == '>') {
      Result.setKind(tok::maratine_slash_greater);
      Result.setLength(2);
      return true;
    }
    if (Ptr[0] == '/' && Ptr[1] == '<') {
      Result.setKind(tok::maratine_slash_less);
      Result.setLength(2);
      return true;
    }
    // Arrow operator "->"
    if (Ptr[0] == '-' && Ptr[1] == '>') {
      Result.setKind(tok::maratine_arrow);
      Result.setLength(2);
      return true;
    }
  }

  return false;
}

TokenKind MaratineLexer::getKeywordKind(StringRef Text) {
  return StringSwitch<TokenKind>(Text)
    // Maratine‑specific keywords
    .Case("#base",      tok::maratine_hash_base)
    .Case("let",        tok::maratine_let)
    .Case("rel",        tok::maratine_rel)
    .Case("cl",         tok::maratine_cl)
    .Case("log",        tok::maratine_log)
    .Case("ret",        tok::maratine_ret)
    .Case("if",         tok::maratine_if)
    .Case("else",       tok::maratine_else)
    .Case("then",       tok::maratine_then)
    .Case("loop",       tok::maratine_loop)
    .Case("var",        tok::maratine_var)
    .Case("const",      tok::maratine_const)
    .Case("print",      tok::maratine_print)
    // Integer types
    .Case("i8",         tok::maratine_type_i8)
    .Case("i16",        tok::maratine_type_i16)
    .Case("i32",        tok::maratine_type_i32)
    .Case("i64",        tok::maratine_type_i64)
    .Case("i128",       tok::maratine_type_i128)
    .Case("i256",       tok::maratine_type_i256)
    .Case("i512",       tok::maratine_type_i512)
    .Case("i1024",      tok::maratine_type_i1024)
    .Case("i2048",      tok::maratine_type_i2048)
    // Unsigned integer types
    .Case("u8",         tok::maratine_type_u8)
    .Case("u16",        tok::maratine_type_u16)
    .Case("u32",        tok::maratine_type_u32)
    .Case("u64",        tok::maratine_type_u64)
    .Case("u128",       tok::maratine_type_u128)
    .Case("u256",       tok::maratine_type_u256)
    .Case("u512",       tok::maratine_type_u512)
    .Case("u1024",      tok::maratine_type_u1024)
    .Case("u2048",      tok::maratine_type_u2048)
    // Floating‑point types
    .Case("f8",         tok::maratine_type_f8)
    .Case("f16",        tok::maratine_type_f16)
    .Case("f32",        tok::maratine_type_f32)
    .Case("f64",        tok::maratine_type_f64)
    .Case("f128",       tok::maratine_type_f128)
    .Case("f256",       tok::maratine_type_f256)
    .Case("f512",       tok::maratine_type_f512)
    .Case("f1024",      tok::maratine_type_f1024)
    .Case("f2048",      tok::maratine_type_f2048)
    // String type
    .Case("String",     tok::maratine_type_string)
    // Array type token
    .Case("array",      tok::maratine_type_array)
    .Default(tok::unknown);
}
