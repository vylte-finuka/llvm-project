//===--- MaratineToken.h - Token definitions for Mara/Maratine -*- C++ -*-===//
//
// Vyft Ltd — Proprietary — 2026
// Compiler for the Mara language (Maratine runtime), targeting ARM64/Slura OS.
//
//===----------------------------------------------------------------------===//
//
// Canonical token enum for the Mara language.
// Mara spec types: string i32 i64 u64 bool ptr array
// Mara spec keywords: var let rel op cl if else loop log ret break null nullptr
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_MARATINE_MARATINETOKEN_H
#define LLVM_FRONTEND_MARATINE_MARATINETOKEN_H

namespace clang {
namespace maratine {

enum TokenKind : unsigned {
  // -----------------------------------------------------------------------
  // Structural
  // -----------------------------------------------------------------------
  tok_eof,
  tok_identifier,
  tok_number,      // integer literal
  tok_string,      // string literal "…"
  tok_unknown,

  // -----------------------------------------------------------------------
  // Mara keywords
  // -----------------------------------------------------------------------
  kw_var,          // mutable variable declaration
  kw_let,          // immutable constant declaration
  kw_rel,          // function / method declaration introducer
  kw_op,           // public visibility (ExternalLinkage)
  kw_cl,           // private visibility (InternalLinkage)
  kw_if,           // conditional
  kw_else,         // alternative branch
  kw_loop,         // loop condition [ body ]
  kw_break,        // exit loop
  kw_log,          // console output: log: expr;
  kw_ret,          // return value: ret expr;
  kw_null,         // null literal (typed pointers / strings)
  kw_nullptr,      // null pointer literal
  kw_true,         // boolean true
  kw_false,        // boolean false
  kw_self,         // self-reference inside a class body
  kw_hash_base,    // #base import directive

  // -----------------------------------------------------------------------
  // Mara primitive types (only these are valid inside < >)
  // -----------------------------------------------------------------------
  kw_type_string,  // string  — always lowercase
  kw_type_i32,     // i32
  kw_type_i64,     // i64
  kw_type_u64,     // u64
  kw_type_bool,    // bool
  kw_type_ptr,     // ptr  (opaque pointer)
  kw_type_array,   // array

  // -----------------------------------------------------------------------
  // Punctuation
  // -----------------------------------------------------------------------
  tok_l_angle,      // <
  tok_r_angle,      // >
  tok_l_bracket,    // [
  tok_r_bracket,    // ]
  tok_l_paren,      // (
  tok_r_paren,      // )
  tok_dot,          // .
  tok_colon,        // :
  tok_semicolon,    // ;
  tok_equal,        // =
  tok_hash,         // #
  tok_comma,        // ,
  tok_star,         // *
  tok_triple_star,  // *** — module/function path separator
  tok_bang,         // !
  tok_amp,          // &
  tok_pipe,         // |
  tok_caret,        // ^
  tok_tilde,        // ~
  tok_percent,      // %
  tok_slash,        // /
  tok_plus,         // +
  tok_minus,        // -

  // -----------------------------------------------------------------------
  // Compound operators
  // -----------------------------------------------------------------------
  tok_eq_eq,        // ==
  tok_bang_eq,      // !=
  tok_lt_eq,        // <=
  tok_gt_eq,        // >=
  tok_amp_amp,      // &&
  tok_pipe_pipe,    // ||
  tok_lt_lt,        // <<
  tok_gt_gt,        // >>
  tok_arrow,        // ->  (unused in Mara but kept for LLVM IR emission)

  // Inheritance suffix
  kw_inherit_t,     // the literal 't' after <[string Type]> in inheritance
};

} // namespace maratine
} // namespace clang

#endif // LLVM_FRONTEND_MARATINE_MARATINETOKEN_H
