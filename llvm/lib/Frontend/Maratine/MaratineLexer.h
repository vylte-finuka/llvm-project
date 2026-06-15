//===--- MaratineLexer.h - Lexer for Maratine language -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_MARATINE_MARATINELEXER_H
#define LLVM_FRONTEND_MARATINE_MARATINELEXER_H

#include "clang/Lex/TokenLexer.h"
#include "clang/Lex/Token.h"
#include "llvm/ADT/StringRef.h"

namespace clang {
namespace maratine {

class MaratineLexer : public TokenLexer {
public:
  MaratineLexer(const LangOptions &LO, SourceManager &SM,
                const FileID &FID, const SrcMgr::CharacteristicKind CharWidth,
                bool DisableTrigraphs = false);

  /// Override to recognise Maratine‑specific keywords and operators.
  bool LexToken(Token &Result) override;

private:
  /// Map a string to a token kind (keywords, types).
  TokenKind getKeywordKind(StringRef Text);
};

} // namespace maratine
} // namespace clang

#endif // LLVM_FRONTEND_MARATINE_MARATINELEXER_H