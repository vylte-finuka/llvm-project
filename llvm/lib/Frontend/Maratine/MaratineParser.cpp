//===--- MaratineParser.cpp - Parser for Maratine language -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MaratineParser.h"
#include "MaratineAST.h"
#include "MaratineLexer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/Token.h"
#include "clang/Parse/ParseAST.h"
#include "clang/Parse/ParseExpr.h"
#include "clang/Parse/ParseTentative.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/StringRef.h"

using namespace clang;
using namespace clang::maratine;

MaratineParser::MaratineParser(Preprocessor &PP, SourceManager &SM,
                               LangOptions &LO, DiagnosticsEngine &Diags)
  : Parser(PP, SM, LO, Diags,
           /*SkipFunctionBodies=*/false,
           /*ParseComments=*/true) {
  // Replace the base lexer with our Maratine‑aware lexer.
  setLexer(std::make_unique<MaratineLexer>(LO, SM,
                                           PP.getSourceManager().getMainFileID(),
                                           SrcMgr::CK_Char));
}

void MaratineParser::ParseTranslationUnit() {
  while (!Tok.is(tok::eof)) {
    if (Tok.is(tok::maratine_hash_base)) {
      if (ParseImportDecl()) return;
    } else if (Tok.is(tok::maratine_let)) {
      if (ParseLetDecl()) return;
    } else if (Tok.is(tok::maratine_var)) {
      if (ParseVarDecl()) return;
    } else if (Tok.is(tok::maratine_const)) {
      if (ParseConstDecl()) return;
    } else if (Tok.is(tok::maratine_rel)) {
      if (ParseFunctionDecl()) return;
    } else if (Tok.is(tok::maratine_log) || Tok.is(tok::maratine_ret)) {
      if (ParseExprStmt()) return;
    } else if (Tok.is(tok::maratine_loop)) {
      if (ParseLoopStmt()) return;
    } else {
      // Fallback to Clang's generic parser (allows mixing C code if needed)
      ParseDeclarationOrFunctionDefinition();
    }
  }
}

/* ---------- Import ---------- */
bool MaratineParser::ParseImportDecl() {
  assert(Tok.is(tok::maratine_hash_base) && "Not a #base token");
  SourceLocation HashLoc = ConsumeToken(tok::maratine_hash_base);
  if (!ExpectAndConsume(tok::less, "expected '<' after #base"))
    return true;

  // Collect the module path until we hit '[' or '>'
  SmallString<128> PathBuf;
  while (!Tok.is(tok::equal) && !Tok.is(tok::greater) && !Tok.is(tok::eof)) {
    if (Tok.is(tok::star) && Tok.getLocation().getRawEncoding() + 2 <
                               Tok.getEndLocation().getRawEncoding() &&
        Tok.getLiteralData()[0] == '*' && Tok.getLiteralData()[1] == '*' &&
        Tok.getLiteralData()[2] == '*') {
      PathBuf.append("***");
      ConsumeToken(tok::star); ConsumeToken(tok::star); ConsumeToken(tok::star);
    } else {
      PathBuf.push_back(static_cast<char>(Tok.getKind()));
      ConsumeToken(Tok.getKind());
    }
  }
  StringRef ModulePath = PathBuf.str();

  // Optional dependency list: [ dep1, dep2 ]
  SmallVector<StringRef, 4> Deps;
  if (Tok.is(tok::equal)) {
    ConsumeToken(tok::equal); // '['
    if (!ExpectAndConsume(tok::less, "expected '[' after '='"))
      return true;
    while (!Tok.is(tok::greater)) {
      if (Tok.is(tok::identifier)) {
        Deps.push_back(Tok.getIdentifierInfo()->getName());
        ConsumeToken(tok::identifier);
        if (Tok.is(tok::comma)) ConsumeToken(tok::comma);
      } else {
        Diags.Report(Tok.getLocation(), diag::err_expected_identifier)
          << "dependency name";
        return true;
      }
    }
    if (!ExpectAndConsume(tok::greater, "expected ']' after dependency list"))
      return true;
  }

  if (!ExpectAndConsume(tok::greater, "expected '>' after module path"))
    return true;
  if (!ExpectAndConsume(tok::semi, "expected ';' after import"))
    return true;

  MaratineImportDecl *D =
      MaratineImportDecl::Create(Context, HashLoc, HashLoc, HashLoc,
                                 ModulePath, Deps);
  Actions.AddDecl(D);
  return false;
}

/* ---------- Let (immutable) ---------- */
bool MaratineParser::ParseLetDecl() {
  assert(Tok.is(tok::maratine_let) && "Not a let token");
  SourceLocation LetLoc = ConsumeToken(tok::maratine_let);
  if (Tok.isNot(tok::identifier)) {
    Diags.Report(Tok.getLocation(), diag::err_expected_identifier)
      << "variable name";
    return true;
  }
  SourceLocation IdLoc = Tok.getLocation();
  IdentifierInfo *II = Tok.getIdentifierInfo();
  ConsumeToken(tok::identifier);

  if (!ExpectAndConsume(tok::colon, "expected ':' after variable name"))
    return true;

  // Parse type – we reuse Clang's type parser via a temporary DeclSpec
  DeclSpec TypeSpec;
  if (ParseDeclSpec(TypeSpec, /*isInline=*/false, /*isFriend=*/false))
    return true;

  Expr *Init = nullptr;
  if (Tok.is(tok::equal)) {
    ConsumeToken(tok::equal);
    Init = ParseExpression();
    if (!Init) return true;
  }

  if (!ExpectAndConsume(tok::semi, "expected ';' after let declaration"))
    return true;

  // Create a VarDecl (const‑qualified for let/const)
  VarDecl *VD = VarDecl::Create(Context, TranslationUnitDecl::get(Context),
                                IdLoc, IdLoc, II, TypeSpec.getTypeRepAsWritten(),
                                /*InitStyle=*/VarDecl::InitNone, nullptr);
  // Mark as const (let/const are immutable)
  VD->setConstQualifier(true);

  MaratineLetDecl *LD =
      MaratineLetDecl::Create(Context, LetLoc, LetLoc, IdLoc, VD, Init);
  Actions.AddDecl(LD);
  return false;
}

/* ---------- Var (mutable) ---------- */
bool MaratineParser::ParseVarDecl() {
  assert(Tok.is(tok::maratine_var) && "Not a var token");
  SourceLocation VarLoc = ConsumeToken(tok::maratine_var);
  if (Tok.isNot(tok::identifier)) {
    Diags.Report(Tok.getLocation(), diag::err_expected_identifier)
      << "variable name";
    return true;
  }
  SourceLocation IdLoc = Tok.getLocation();
  IdentifierInfo *II = Tok.getIdentifierInfo();
  ConsumeToken(tok::identifier);

  if (!ExpectAndConsume(tok::colon, "expected ':' after variable name"))
    return true;

  DeclSpec TypeSpec;
  if (ParseDeclSpec(TypeSpec, /*isInline=*/false, /*isFriend=*/false))
    return true;

  Expr *Init = nullptr;
  if (Tok.is(tok::equal)) {
    ConsumeToken(tok::equal);
    Init = ParseExpression();
    if (!Init) return true;
  }

  if (!ExpectAndConsume(tok::semi, "expected ';' after var declaration"))
    return true;

  // Create a VarDecl (non‑const)
  VarDecl *VD = VarDecl::Create(Context, TranslationUnitDecl::get(Context),
                                IdLoc, IdLoc, II, TypeSpec.getTypeRepAsWritten(),
                                /*InitStyle=*/VarDecl::InitNone, nullptr);
  // mutable → no const qualifier
  VD->setConstQualifier(false);

  MaratineLetDecl *LD =
      MaratineLetDecl::Create(Context, VarLoc, VarLoc, IdLoc, VD, Init);
  // Re‑use the same AST node; we will interpret the const‑flag later in Sema.
  Actions.AddDecl(LD);
  return false;
}

/* ---------- Const (compile‑time constant) ---------- */
bool MaratineParser::ParseConstDecl() {
  assert(Tok.is(tok::maratine_const) && "Not a const token");
  SourceLocation ConstLoc = ConsumeToken(tok::maratine_const);
  if (Tok.isNot(tok::identifier)) {
    Diags.Report(Tok.getLocation(), diag::err_expected_identifier)
      << "constant name";
    return true;
  }
  SourceLocation IdLoc = Tok.getLocation();
  IdentifierInfo *II = Tok.getIdentifierInfo();
  ConsumeToken(tok::identifier);

  if (!ExpectAndConsume(tok::colon, "expected ':' after constant name"))
    return true;

  DeclSpec TypeSpec;
  if (ParseDeclSpec(TypeSpec, /*isInline=*/false, /*isFriend=*/false))
    return true;

  // A const must have an initializer
  if (!Tok.is(tok::equal)) {
    Diags.Report(Tok.getLocation(), diag::err_expected) << "=' after const";
    return true;
  }
  ConsumeToken(tok::equal);
  Expr *Init = ParseExpression();
  if (!Init) return true;

  if (!ExpectAndConsume(tok::semi, "expected ';' after const declaration"))
    return true;

  // Create a VarDecl marked const
  VarDecl *VD = VarDecl::Create(Context, TranslationUnitDecl::get(Context),
                                IdLoc, IdLoc, II, TypeSpec.getTypeRepAsWritten(),
                                /*InitStyle=*/VarDecl::InitNone, nullptr);
  VD->setConstQualifier(true);

  MaratineLetDecl *LD =
      MaratineLetDecl::Create(Context, ConstLoc, ConstLoc, IdLoc, VD, Init);
  Actions.AddDecl(LD);
  return false;
}

/* ---------- Function ---------- */
bool MaratineParser::ParseFunctionDecl() {
  assert(Tok.is(tok::maratine_rel) && "Not a rel token");
  SourceLocation RelLoc = ConsumeToken(tok::maratine_rel);
  if (!ExpectAndConsume(tok::maratine_cl, "expected 'cl' after 'rel'"))
    return true;
  SourceLocation CLoc = Tok.getLocation();
  ConsumeToken(tok::maratine_cl);

  if (Tok.isNot(tok::identifier)) {
    Diags.Report(Tok.getLocation(), diag::err_expected_identifier)
      << "function name";
    return true;
  }
  SourceLocation FnIdLoc = Tok.getLocation();
  IdentifierInfo *FnII = Tok.getIdentifierInfo();
  ConsumeToken(tok::identifier);

  if (!ExpectAndConsume(tok::colon, "expected ':' after function name"))
    return true;

  // Parameter list – we reuse Clang's function declarator parsing
  ParsedDeclAttributes attrs;
  FunctionDecl *Fn = ActOnDeclarator(
      /*Scope=*/nullptr,
      /*DeclSpec=*/DeclSpec(),
      /*Declarator=*/ParseDeclarator(attrs, /*IsTypedefName=*/false,
                                     /*Context=*/AK_function),
      /*Attrs=*/attrs);
  if (!Fn) return true;

  if (!ExpectAndConsume(tok::less, "expected '[' after function signature"))
    return true;
  if (!ExpectAndConsume(tok::greater, "expected ']' after function signature"))
    return true;

  // Parse body as a compound statement
  Stmt *Body = nullptr;
  if (Tok.is(tok::l_brace)) {
    Body = ParseCompoundStatement(true);
    if (!Body) return true;
  } else {
    Diags.Report(Tok.getLocation(), diag::err_expected_lbrace)
      << "function body";
    return true;
  }

  MaratineFunctionDecl *FD =
      MaratineFunctionDecl::Create(Context, RelLoc, CLoc, FnIdLoc,
                                   Body->getBeginLoc(), Body->getEndLoc(),
                                   Fn, Body);
  Actions.AddDecl(FD);
  return false;
}

/* ---------- ExprStmt (log: / ret:) ---------- */
bool MaratineParser::ParseExprStmt() {
  bool IsLog = Tok.is(tok::maratine_log);
  SourceLocation ColonLoc = ConsumeToken(Tok.getKind()); // log: or ret:
  Expr *E = ParseExpression();
  if (!E) return true;
  if (!ExpectAndConsume(tok::semi, "expected ';' after expression"))
    return true;
  MaratineExprStmt *ES =
      MaratineExprStmt::Create(Context, ColonLoc, E, IsLog);
  Actions.AddStmt(ES);
  return false;
}

/* ---------- Loop statement ---------- */
bool MaratineParser::ParseLoopStmt() {
  assert(Tok.is(tok::maratine_loop) && "Not a loop token");
  SourceLocation LoopLoc = ConsumeToken(tok::maratine_loop);

  // Optional condition: loop <expr> { … }
  Expr *Cond = nullptr;
  if (!Tok.is(tok::l_brace)) { // if not a direct '{', we treat it as a condition
    Cond = ParseExpression();
    if (!Cond) return true;
  }

  if (!ExpectAndConsume(tok::l_brace, "expected '{' after loop"))
    return true;

  Stmt *Body = ParseCompoundStatement(true);
  if (!Body) return true;

  if (!ExpectAndConsume(tok::r_brace, "expected '}' after loop body"))
    return true;

  // If we have a condition, build a WhileStmt; otherwise a NullStmt with a back‑edge.
  Stmt *LoopStmt = nullptr;
  if (Cond) {
    // while (cond) { body }
    LoopStmt = Actions.BuildWhileStmt(Cond, Body, /*HasElse=*/false);
  } else {
    // infinite loop: for (;;) { body }
    LoopStmt = Actions.BuildForStmt(/*Init=*/nullptr,
                                    /*Cond=*/nullptr,
                                    /*Inc=*/nullptr,
                                    Body);
  }
  Actions.AddStmt(LoopStmt);
  return false;
}

/* ---------- Array literal expression ---------- */
Expr *MaratineParser::ParseArrayLiteralExpression() {
  if (!ExpectAndConsume(tok::l_bracket, "expected '['"))
    return nullptr;

  SmallVector<Expr*, 8> Elements;
  if (!Tok.is(tok::r_bracket)) {
    while (true) {
      Expr *E = ParseExpression();
      if (!E) return nullptr;
      Elements.push_back(E);

      if (Tok.is(tok::comma)) {
        ConsumeToken(tok::comma);
        if (Tok.is(tok::r_bracket)) break; // allow trailing comma
        continue;
      }
      if (Tok.is(tok::r_bracket)) break;
      Diags.Report(Tok.getLocation(), diag::err_expected) << "',' or ']'";
      return nullptr;
    }
  }

  if (!ExpectAndConsume(tok::r_bracket, "expected ']'"))
    return nullptr;

  // Build an InitListExpr from the element list.
  // We rely on Sema to turn this into a proper array type later.
  ExprResult InitList = Actions.ActOnInitListExpr(
      /*Loc=*/SourceLocation(), /*L=*/SourceLocation(),
      /*Exprs=*/Elements);
  if (InitList.isInvalid()) return nullptr;
  return InitList.get();
}

/* ---------- Helpers ---------- */
bool MaratineParser::ExpectAndConsume(tok::TokenKind K, const char *Msg) {
  if (!Tok.is(K)) {
    if (Msg) Diags.Report(Tok.getLocation(), diag::err_expected) << Msg;
    else Diags.Report(Tok.getLocation(), diag::err_expected_token)
      << Tok.getName() << " expected " << Tok.getTokenName(K);
    return false;
  }
  ConsumeToken(K);
  return true;
}

SourceLocation MaratineParser::ConsumeToken(tok::TokenKind K) {
  SourceLocation L = Tok.getLocation();
  Lex(); // advance
  return L;
}