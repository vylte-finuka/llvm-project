//===--- MaratineToken.h - Token definitions for Maratine -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_MARATINE_MARATINETOKEN_H
#define LLVM_FRONTEND_MARATINE_MARATINETOKEN_H

namespace clang {
namespace maratine {

enum TokenKind {
  // End of file
  tok_eof,
  // Identifiers
  tok_identifier,
  // Literals
  tok_number,   // integer literal
  tok_string,   // string literal
  // Keywords
  kw_base,
  kw_let,
  kw_rel,
  kw_cl,
  kw_log,
  kw_ret,
  // Punctuation
  l_angle,      // <
  r_angle,      // >
  l_bracket,    // [
  r_bracket,    // ]
  l_paren,      // (
  r_paren,      // )
  l_brace,      // {
  r_brace,      // }
  dot,          // .
  colon,        // :
  semicolon,    // ;
  equal,        // =
  hash,         // #
  comma,        // ,
  // Unknown
  tok_unknown
};

} // namespace maratine
} // namespace clang

#endif // LLVM_FRONTEND_MARATINE_MARATINETOKEN_H