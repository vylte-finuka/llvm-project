//===--- MaratineParser.cpp - Parser for Mara/Maratine language ---------===//
//
// Vyft Ltd — Proprietary — 2026
//
//===----------------------------------------------------------------------===//
//
// Recursive-descent parser for the Mara language.
//
// Mara syntax contract enforced by this file:
//   BODIES        [ … ]   — square brackets, NEVER { }
//   ARRAYS        [ a; b; c ]  — semicolon separator (not comma)
//   IF            if (cond) [ then ] else [ else ];  — no "then" keyword
//   LOOP          loop cond [ body ];
//   RET           ret expr;     — no colon
//   LOG           log: expr;    — colon IS required
//   FUNCTION      rel op/cl name: [params] [ body ];
//   TYPES         <string> <i32> <i64> <u64> <bool> <ptr> <array>
//   PATH SEP      ***  (triple-star, single token)
//
//===----------------------------------------------------------------------===//

#include "MaratineParser.h"
#include "MaratineAST.h"
#include "MaratineLexer.h"
#include "clang/AST/ASTContext.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::maratine;
using llvm::errs;

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
MaratineParser::MaratineParser(Preprocessor &PP, SourceManager &SM,
                               LangOptions &LO, DiagnosticsEngine &Diags)
  : Parser(PP, SM, LO, Diags,
           /*SkipFunctionBodies=*/false,
           /*ParseComments=*/true) {
  setLexer(std::make_unique<MaratineLexer>(
      LO, SM, SM.getMainFileID(), SrcMgr::CK_Char));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
SourceLocation MaratineParser::ConsumeTok() {
  SourceLocation L = Tok.getLocation();
  ConsumeToken();
  return L;
}

bool MaratineParser::Expect(tok::TokenKind K, const char *Msg) {
  if (!Tok.is(K)) {
    Diags.Report(Tok.getLocation(), diag::err_expected)
        << (Msg ? Msg : tok::getTokenName(K));
    return false;
  }
  return true;
}

bool MaratineParser::ExpectAndConsumeTok(tok::TokenKind K, const char *Msg) {
  if (!Expect(K, Msg)) return false;
  ConsumeTok();
  return true;
}

bool MaratineParser::IsMараType(tok::TokenKind K) const {
  switch (K) {
    case tok::maratine_type_string:
    case tok::maratine_type_i32:
    case tok::maratine_type_i64:
    case tok::maratine_type_u64:
    case tok::maratine_type_bool:
    case tok::maratine_type_ptr:
    case tok::maratine_type_array:
      return true;
    default:
      return false;
  }
}

// ---------------------------------------------------------------------------
// ParseTranslationUnit — top-level dispatcher
// ---------------------------------------------------------------------------
void MaratineParser::ParseTranslationUnit() {
  while (!Tok.is(tok::eof)) {
    // #base import
    if (Tok.is(tok::hash)) {
      if (!ParseImportDecl()) return;
      continue;
    }
    // #include passthrough (forwarded to base clang lexer)
    if (Tok.is(tok::hash)) {
      // handled by base class
      ParseDeclarationOrFunctionDefinition();
      continue;
    }
    // let / var declarations at module scope
    if (Tok.is(tok::maratine_let)) {
      if (!ParseLetDecl()) return;
      continue;
    }
    if (Tok.is(tok::maratine_var)) {
      if (!ParseVarDecl()) return;
      continue;
    }
    // rel op / rel cl function / class declaration
    if (Tok.is(tok::maratine_rel)) {
      if (!ParseFunctionDecl()) return;
      continue;
    }
    // Unknown token — skip with diagnostic
    Diags.Report(Tok.getLocation(), diag::err_expected)
        << "declaration (let, var, rel, #base, #include)";
    ConsumeTok();
  }
}

// ---------------------------------------------------------------------------
// ParseImportDecl — #base <path***[ DepA, DepB ]>;
//                   #base <path>;
// ---------------------------------------------------------------------------
bool MaratineParser::ParseImportDecl() {
  assert(Tok.is(tok::hash));
  SourceLocation HashLoc = ConsumeTok(); // consume '#'

  // Expect the word "base" immediately after '#'
  if (!Tok.is(tok::maratine_base) && !(Tok.is(tok::identifier) &&
      Tok.getIdentifierInfo()->getName() == "base")) {
    // Could be a regular #include — delegate to base parser.
    ParseDeclarationOrFunctionDefinition();
    return true;
  }
  ConsumeTok(); // consume "base"

  // Expect '<'
  if (!ExpectAndConsumeTok(tok::less, "'<' after #base")) return false;
  SourceLocation LAngle = Tok.getLocation();

  // Collect the module path: identifiers separated by ***
  SmallString<256> PathBuf;
  SmallVector<StringRef, 4> Deps;

  while (!Tok.is(tok::greater) && !Tok.is(tok::eof)) {
    if (Tok.is(tok::maratine_triple_star)) {
      PathBuf.append("***");
      ConsumeTok();
      continue;
    }
    if (Tok.is(tok::l_square)) {
      // Dependency list: [ DepA, DepB ]
      ConsumeTok(); // consume '['
      while (!Tok.is(tok::r_square) && !Tok.is(tok::eof)) {
        if (Tok.is(tok::identifier)) {
          Deps.push_back(Tok.getIdentifierInfo()->getName());
          ConsumeTok();
          if (Tok.is(tok::comma)) ConsumeTok();
        } else {
          Diags.Report(Tok.getLocation(), diag::err_expected_identifier)
              << "dependency name in #base import";
          return false;
        }
      }
      if (!ExpectAndConsumeTok(tok::r_square, "']' after dependency list"))
        return false;
      break;
    }
    if (Tok.is(tok::identifier)) {
      PathBuf.append(Tok.getIdentifierInfo()->getName());
      ConsumeTok();
      continue;
    }
    // Unexpected character — stop
    break;
  }

  SourceLocation RAngle = Tok.getLocation();
  if (!ExpectAndConsumeTok(tok::greater, "'>' after module path")) return false;
  if (!ExpectAndConsumeTok(tok::semi,    "';' after #base import")) return false;

  StringRef ModulePath = PathBuf.str();
  auto *D = MaratineImportDecl::Create(Context, HashLoc, LAngle, RAngle,
                                       ModulePath, Deps);
  Actions.AddDecl(D);
  return true;
}

// ---------------------------------------------------------------------------
// ParseLetDecl — let name: <type> = expr;
// ---------------------------------------------------------------------------
bool MaratineParser::ParseLetDecl() {
  assert(Tok.is(tok::maratine_let));
  SourceLocation KwLoc = ConsumeTok();

  if (!Tok.is(tok::identifier)) {
    Diags.Report(Tok.getLocation(), diag::err_expected_identifier)
        << "constant name after 'let'";
    return false;
  }
  IdentifierInfo *II = Tok.getIdentifierInfo();
  SourceLocation  IdLoc = ConsumeTok();

  if (!ExpectAndConsumeTok(tok::colon, "':' after constant name")) return false;

  QualType Ty;
  if (!ParseTypeAnnotation(Ty)) return false;

  Expr *Init = nullptr;
  if (Tok.is(tok::equal)) {
    ConsumeTok(); // '='
    Init = ParseExpression();
    if (!Init) return false;
  }

  if (!ExpectAndConsumeTok(tok::semi, "';' after let declaration")) return false;

  VarDecl *VD = VarDecl::Create(Context, Actions.CurContext,
                                 IdLoc, IdLoc, II, Ty,
                                 Context.getTrivialTypeSourceInfo(Ty),
                                 SC_None);
  VD->setConstexpr(true);

  auto *LD = MaratineLetDecl::Create(Context, KwLoc, /*IsConst=*/true, VD, Init);
  Actions.AddDecl(LD);
  return true;
}

// ---------------------------------------------------------------------------
// ParseVarDecl — var name: <type> = expr;
// ---------------------------------------------------------------------------
bool MaratineParser::ParseVarDecl() {
  assert(Tok.is(tok::maratine_var));
  SourceLocation KwLoc = ConsumeTok();

  if (!Tok.is(tok::identifier)) {
    Diags.Report(Tok.getLocation(), diag::err_expected_identifier)
        << "variable name after 'var'";
    return false;
  }
  IdentifierInfo *II = Tok.getIdentifierInfo();
  SourceLocation  IdLoc = ConsumeTok();

  if (!ExpectAndConsumeTok(tok::colon, "':' after variable name")) return false;

  QualType Ty;
  if (!ParseTypeAnnotation(Ty)) return false;

  Expr *Init = nullptr;
  if (Tok.is(tok::equal)) {
    ConsumeTok(); // '='
    Init = ParseExpression();
    if (!Init) return false;
  }

  if (!ExpectAndConsumeTok(tok::semi, "';' after var declaration")) return false;

  VarDecl *VD = VarDecl::Create(Context, Actions.CurContext,
                                 IdLoc, IdLoc, II, Ty,
                                 Context.getTrivialTypeSourceInfo(Ty),
                                 SC_None);

  auto *LD = MaratineLetDecl::Create(Context, KwLoc, /*IsConst=*/false, VD, Init);
  Actions.AddDecl(LD);
  return true;
}

// ---------------------------------------------------------------------------
// ParseFunctionDecl — rel op/cl name: [params] [ body ];
//                     rel op Name: <[string Base]>t [ body ];
// ---------------------------------------------------------------------------
bool MaratineParser::ParseFunctionDecl() {
  assert(Tok.is(tok::maratine_rel));
  SourceLocation RelLoc = ConsumeTok();

  // Visibility: op (public) or cl (private)
  bool IsPublic = false;
  if (Tok.is(tok::maratine_op)) {
    IsPublic = true;
    ConsumeTok();
  } else if (Tok.is(tok::maratine_cl)) {
    IsPublic = false;
    ConsumeTok();
  } else {
    Diags.Report(Tok.getLocation(), diag::err_expected)
        << "'op' or 'cl' after 'rel'";
    return false;
  }

  // Function name
  if (!Tok.is(tok::identifier)) {
    Diags.Report(Tok.getLocation(), diag::err_expected_identifier)
        << "function name after rel op/cl";
    return false;
  }
  IdentifierInfo *FnII  = Tok.getIdentifierInfo();
  SourceLocation  FnLoc = ConsumeTok();

  if (!ExpectAndConsumeTok(tok::colon, "':' after function name")) return false;

  // Optional inheritance: <[string TypeName]>t
  StringRef InheritType;
  if (Tok.is(tok::less)) {
    InheritType = ParseInheritanceSuffix();
    // After parsing <[string Type]>t we expect the body '[' next
  }

  // Parameter list: [name type, name type]  — may be empty []
  SmallVector<ParmVarDecl *, 8> Params;
  if (Tok.is(tok::l_square)) {
    if (!ParseParamList(Params)) return false;
  }

  // Body: [ statements ]
  SourceLocation LB = Tok.getLocation();
  Stmt *Body = ParseBlock();
  if (!Body) return false;
  SourceLocation RB = Body->getEndLoc();

  // Optional trailing ';'
  if (Tok.is(tok::semi)) ConsumeTok();

  // Build a Clang FunctionDecl skeleton
  FunctionDecl *FnDecl = FunctionDecl::Create(
      Context, Actions.CurContext, FnLoc, FnLoc,
      DeclarationName(FnII), Context.VoidTy,
      Context.getTrivialTypeSourceInfo(Context.VoidTy),
      IsPublic ? SC_Extern : SC_Static,
      /*UsesFPIntrin=*/false,
      /*isInlineSpecified=*/false);

  auto *MD = MaratineFunctionDecl::Create(Context, RelLoc, IsPublic,
                                          InheritType, LB, RB, FnDecl, Body);
  Actions.AddDecl(MD);
  return true;
}

// ---------------------------------------------------------------------------
// ParseParamList — [name type, name type, …]
//   Note: parameter types are RAW (no angle brackets) per Mara spec:
//         rel op Foo: [name string, count i32] [ … ]
// ---------------------------------------------------------------------------
bool MaratineParser::ParseParamList(SmallVectorImpl<ParmVarDecl *> &Params) {
  if (!ExpectAndConsumeTok(tok::l_square, "'[' to open parameter list"))
    return false;

  while (!Tok.is(tok::r_square) && !Tok.is(tok::eof)) {
    // Parameter name
    if (!Tok.is(tok::identifier)) {
      Diags.Report(Tok.getLocation(), diag::err_expected_identifier)
          << "parameter name";
      return false;
    }
    IdentifierInfo *PII  = Tok.getIdentifierInfo();
    SourceLocation  PLoc = ConsumeTok();

    // Parameter type (raw keyword or identifier — no angle brackets)
    QualType PTy = Context.VoidTy;
    if (IsMараType(Tok.getKind())) {
      // map Mara type keyword to a Clang built-in type
      switch (Tok.getKind()) {
        case tok::maratine_type_i32:    PTy = Context.IntTy;     break;
        case tok::maratine_type_i64:    PTy = Context.LongLongTy; break;
        case tok::maratine_type_u64:    PTy = Context.UnsignedLongLongTy; break;
        case tok::maratine_type_bool:   PTy = Context.BoolTy;    break;
        case tok::maratine_type_ptr:    PTy = Context.VoidPtrTy; break;
        case tok::maratine_type_string:
        case tok::maratine_type_array:  PTy = Context.VoidPtrTy; break;
        default:                        PTy = Context.VoidTy;    break;
      }
      ConsumeTok();
    } else if (Tok.is(tok::identifier)) {
      // compound type name → treat as void* opaque for now
      PTy = Context.VoidPtrTy;
      ConsumeTok();
    }

    auto *PVD = ParmVarDecl::Create(Context, Actions.CurContext,
                                    PLoc, PLoc, PII, PTy,
                                    Context.getTrivialTypeSourceInfo(PTy),
                                    SC_None, nullptr);
    Params.push_back(PVD);

    if (Tok.is(tok::comma)) ConsumeTok();
  }

  return ExpectAndConsumeTok(tok::r_square, "']' to close parameter list");
}

// ---------------------------------------------------------------------------
// ParseTypeAnnotation — <string>, <i32>, <[string TypeName]>, <ptr>, …
// ---------------------------------------------------------------------------
bool MaratineParser::ParseTypeAnnotation(QualType &Out) {
  if (!ExpectAndConsumeTok(tok::less, "'<' to open type annotation"))
    return false;

  // Compound type: <[string TypeName]> or <[string TypeName***[ deps ]]>
  if (Tok.is(tok::l_square)) {
    ConsumeTok(); // '['
    // Expect a primitive keyword (usually 'string')
    if (!IsMараType(Tok.getKind()) && !Tok.is(tok::identifier)) {
      Diags.Report(Tok.getLocation(), diag::err_expected)
          << "type keyword inside <[…]>";
      return false;
    }
    ConsumeTok(); // the base type keyword
    // Then the compound type name
    if (Tok.is(tok::identifier)) ConsumeTok();
    // Optionally followed by ***[ deps ]
    if (Tok.is(tok::maratine_triple_star)) {
      ConsumeTok();
      if (Tok.is(tok::l_square)) {
        ConsumeTok();
        while (!Tok.is(tok::r_square) && !Tok.is(tok::eof)) ConsumeTok();
        if (!ExpectAndConsumeTok(tok::r_square, "']' closing dependency list"))
          return false;
      }
    }
    if (!ExpectAndConsumeTok(tok::r_square, "']' closing compound type"))
      return false;
    Out = Context.VoidPtrTy; // opaque for now; Sema will resolve
  } else {
    // Primitive type
    if (!IsMараType(Tok.getKind())) {
      Diags.Report(Tok.getLocation(), diag::err_expected)
          << "Mara type (string, i32, i64, u64, bool, ptr, array)";
      return false;
    }
    switch (Tok.getKind()) {
      case tok::maratine_type_i32:  Out = Context.IntTy;              break;
      case tok::maratine_type_i64:  Out = Context.LongLongTy;         break;
      case tok::maratine_type_u64:  Out = Context.UnsignedLongLongTy; break;
      case tok::maratine_type_bool: Out = Context.BoolTy;             break;
      case tok::maratine_type_ptr:
      case tok::maratine_type_string:
      case tok::maratine_type_array:
        Out = Context.VoidPtrTy; break;
      default: Out = Context.VoidTy; break;
    }
    ConsumeTok();
  }

  return ExpectAndConsumeTok(tok::greater, "'>' to close type annotation");
}

// ---------------------------------------------------------------------------
// ParseInheritanceSuffix — <[string TypeName]>t
//   Called after the ':' in:  rel op Name: <[string Base]>t [ body ]
// ---------------------------------------------------------------------------
StringRef MaratineParser::ParseInheritanceSuffix() {
  // We are at '<'
  if (!Tok.is(tok::less)) return StringRef();
  ConsumeTok(); // '<'

  SmallString<64> TypeName;
  if (Tok.is(tok::l_square)) {
    ConsumeTok(); // '['
    // skip the primitive keyword (usually 'string')
    if (IsMараType(Tok.getKind()) || Tok.is(tok::identifier)) ConsumeTok();
    // collect the type name (may have *** paths)
    while (!Tok.is(tok::r_square) && !Tok.is(tok::eof)) {
      if (Tok.is(tok::identifier))
        TypeName.append(Tok.getIdentifierInfo()->getName());
      else if (Tok.is(tok::maratine_triple_star))
        TypeName.append("***");
      ConsumeTok();
    }
    if (Tok.is(tok::r_square)) ConsumeTok(); // ']'
  }

  if (Tok.is(tok::greater)) ConsumeTok(); // '>'

  // Consume the mandatory 't' suffix for inheritance
  if (Tok.is(tok::identifier) &&
      Tok.getIdentifierInfo()->getName() == "t") {
    ConsumeTok();
  }

  return TypeName.str();
}

// ---------------------------------------------------------------------------
// ParseBlock — [ statements … ]
//   Returns a CompoundStmt. Mara uses [ ] — never { }.
// ---------------------------------------------------------------------------
Stmt *MaratineParser::ParseBlock() {
  if (!Expect(tok::l_square, "'[' to open block")) return nullptr;
  SourceLocation LLoc = ConsumeTok();

  SmallVector<Stmt *, 16> Stmts;
  while (!Tok.is(tok::r_square) && !Tok.is(tok::eof)) {
    Stmt *S = ParseStatement();
    if (!S) return nullptr;
    Stmts.push_back(S);
  }

  if (!Expect(tok::r_square, "']' to close block")) return nullptr;
  SourceLocation RLoc = ConsumeTok();

  return CompoundStmt::Create(Context, Stmts, FPOptionsOverride(), LLoc, RLoc);
}

// ---------------------------------------------------------------------------
// ParseStatement — dispatcher
// ---------------------------------------------------------------------------
Stmt *MaratineParser::ParseStatement() {
  if (Tok.is(tok::maratine_if))    { ParseIfStmt();   return nullptr; /* TODO: return node */ }
  if (Tok.is(tok::maratine_loop))  { ParseLoopStmt(); return nullptr; }
  if (Tok.is(tok::maratine_break)) { ParseBreakStmt(); return nullptr; }
  if (Tok.is(tok::maratine_log))   { ParseLogStmt();  return nullptr; }
  if (Tok.is(tok::maratine_ret))   { ParseRetStmt();  return nullptr; }
  if (Tok.is(tok::maratine_let))   { ParseLetDecl();  return nullptr; }
  if (Tok.is(tok::maratine_var))   { ParseVarDecl();  return nullptr; }
  // Expression statement (assignment, call, …)
  ParseAssignOrCallStmt();
  return nullptr;
}

// ---------------------------------------------------------------------------
// ParseIfStmt — if (cond) [ then ] else [ else ];
//   Mara rule: NO "then" keyword; body is ALWAYS [ … ]
// ---------------------------------------------------------------------------
bool MaratineParser::ParseIfStmt() {
  assert(Tok.is(tok::maratine_if));
  SourceLocation IfLoc = ConsumeTok();

  // Condition in parentheses
  if (!ExpectAndConsumeTok(tok::l_paren, "'(' before if condition")) return false;
  Expr *Cond = ParseExpression();
  if (!Cond) return false;
  if (!ExpectAndConsumeTok(tok::r_paren, "')' after if condition")) return false;

  // Then-block: [ … ]
  Stmt *Then = ParseBlock();
  if (!Then) return false;

  // Optional else branch
  Stmt *Else = nullptr;
  if (Tok.is(tok::maratine_else)) {
    ConsumeTok(); // 'else'
    Else = ParseBlock();
    if (!Else) return false;
  }

  // Trailing ';'
  if (Tok.is(tok::semi)) ConsumeTok();

  auto *IS = MaratineIfStmt::Create(Context, IfLoc, Cond, Then, Else);
  Actions.AddStmt(IS);
  return true;
}

// ---------------------------------------------------------------------------
// ParseLoopStmt — loop cond [ body ];
//   The condition is NOT parenthesised (unlike C while).
//   Mara rule: body is ALWAYS [ … ], never { }.
// ---------------------------------------------------------------------------
bool MaratineParser::ParseLoopStmt() {
  assert(Tok.is(tok::maratine_loop));
  SourceLocation LoopLoc = ConsumeTok();

  // Condition expression (everything up to '[')
  Expr *Cond = nullptr;
  if (!Tok.is(tok::l_square)) {
    Cond = ParseExpression();
    if (!Cond) return false;
  }

  // Body: [ … ]
  Stmt *Body = ParseBlock();
  if (!Body) return false;

  if (Tok.is(tok::semi)) ConsumeTok();

  auto *LS = MaratineLoopStmt::Create(Context, LoopLoc, Cond, Body);
  Actions.AddStmt(LS);
  return true;
}

// ---------------------------------------------------------------------------
// ParseBreakStmt — break;
// ---------------------------------------------------------------------------
bool MaratineParser::ParseBreakStmt() {
  SourceLocation Loc = ConsumeTok(); // 'break'
  if (!ExpectAndConsumeTok(tok::semi, "';' after break")) return false;
  auto *BS = Actions.ActOnBreakStmt(Loc, Actions.getCurScope());
  Actions.AddStmt(BS.get());
  return true;
}

// ---------------------------------------------------------------------------
// ParseLogStmt — log: expr;
// ---------------------------------------------------------------------------
bool MaratineParser::ParseLogStmt() {
  SourceLocation KwLoc = ConsumeTok(); // 'log'
  if (!ExpectAndConsumeTok(tok::colon, "':' after log")) return false;

  Expr *E = ParseExpression();
  if (!E) return false;
  if (!ExpectAndConsumeTok(tok::semi, "';' after log expression")) return false;

  auto *ES = MaratineExprStmt::Create(Context, KwLoc, /*IsLog=*/true, E);
  Actions.AddStmt(ES);
  return true;
}

// ---------------------------------------------------------------------------
// ParseRetStmt — ret expr;
//   Mara rule: NO colon after ret (unlike log:).
// ---------------------------------------------------------------------------
bool MaratineParser::ParseRetStmt() {
  SourceLocation KwLoc = ConsumeTok(); // 'ret'

  // ret with no value (void functions): ret;
  if (Tok.is(tok::semi)) {
    ConsumeTok();
    auto *RS = Actions.ActOnReturnStmt(KwLoc, nullptr, Actions.getCurScope());
    Actions.AddStmt(RS.get());
    return true;
  }

  Expr *E = ParseExpression();
  if (!E) return false;
  if (!ExpectAndConsumeTok(tok::semi, "';' after ret expression")) return false;

  auto *ES = MaratineExprStmt::Create(Context, KwLoc, /*IsLog=*/false, E);
  Actions.AddStmt(ES);
  return true;
}

// ---------------------------------------------------------------------------
// ParseAssignOrCallStmt — identifier = expr;  /  <FFI***Call***>(args);
// ---------------------------------------------------------------------------
bool MaratineParser::ParseAssignOrCallStmt() {
  Expr *LHS = ParseExpression();
  if (!LHS) return false;

  if (Tok.is(tok::equal)) {
    ConsumeTok();
    Expr *RHS = ParseExpression();
    if (!RHS) return false;
    ExprResult Assign = Actions.ActOnBinOp(
        Actions.getCurScope(), Tok.getLocation(), tok::equal, LHS, RHS);
    if (Assign.isInvalid()) return false;
    Actions.AddStmt(Assign.get());
  } else {
    Actions.AddStmt(LHS);
  }

  return ExpectAndConsumeTok(tok::semi, "';' after statement");
}

// ---------------------------------------------------------------------------
// ParseExpression / ParseBinaryExpr / ParseUnaryExpr / ParsePrimaryExpr
// ---------------------------------------------------------------------------
Expr *MaratineParser::ParseExpression() {
  return ParseBinaryExpr(0);
}

static int getBinOpPrec(tok::TokenKind K) {
  switch (K) {
    case tok::pipepipe:     return 1;
    case tok::ampamp:       return 2;
    case tok::pipe:         return 3;
    case tok::caret:        return 4;
    case tok::amp:          return 5;
    case tok::equalequal:
    case tok::exclaimequal: return 6;
    case tok::less:
    case tok::lessequal:
    case tok::greater:
    case tok::greaterequal: return 7;
    case tok::lessless:
    case tok::greatergreater: return 8;
    case tok::plus:
    case tok::minus:        return 9;
    case tok::star:
    case tok::slash:
    case tok::percent:      return 10;
    default: return -1;
  }
}

Expr *MaratineParser::ParseBinaryExpr(int MinPrec) {
  Expr *LHS = ParseUnaryExpr();
  if (!LHS) return nullptr;

  while (true) {
    int Prec = getBinOpPrec(Tok.getKind());
    if (Prec < MinPrec) break;

    tok::TokenKind Op = Tok.getKind();
    SourceLocation OpLoc = ConsumeTok();

    Expr *RHS = ParseBinaryExpr(Prec + 1);
    if (!RHS) return nullptr;

    ExprResult Res = Actions.ActOnBinOp(
        Actions.getCurScope(), OpLoc, Op, LHS, RHS);
    if (Res.isInvalid()) return nullptr;
    LHS = Res.get();
  }
  return LHS;
}

Expr *MaratineParser::ParseUnaryExpr() {
  if (Tok.is(tok::exclaim) || Tok.is(tok::minus) || Tok.is(tok::tilde)) {
    tok::TokenKind Op = Tok.getKind();
    SourceLocation Loc = ConsumeTok();
    Expr *Sub = ParseUnaryExpr();
    if (!Sub) return nullptr;
    ExprResult R = Actions.ActOnUnaryOp(Actions.getCurScope(), Loc, Op, Sub);
    return R.isInvalid() ? nullptr : R.get();
  }
  return ParsePrimaryExpr();
}

Expr *MaratineParser::ParsePrimaryExpr() {
  // FFI call: <Module***Fn***>(args)
  if (Tok.is(tok::less)) {
    return ParseFFICallExpr();
  }

  // Array literal: [ elem; elem; … ]
  if (Tok.is(tok::l_square)) {
    return ParseArrayLiteralExpr();
  }

  // String literal
  if (Tok.is(tok::string_literal)) {
    ExprResult R = ParseStringLiteralExpression();
    return R.isInvalid() ? nullptr : R.get();
  }

  // Integer literal
  if (Tok.is(tok::numeric_constant)) {
    ExprResult R = Actions.ActOnNumericConstant(Tok);
    ConsumeTok();
    return R.isInvalid() ? nullptr : R.get();
  }

  // Boolean / null literals
  if (Tok.is(tok::maratine_true) || Tok.is(tok::maratine_false)) {
    bool Val = Tok.is(tok::maratine_true);
    SourceLocation Loc = ConsumeTok();
    return Actions.ActOnCXXBoolLiteral(Loc, Val ? tok::kw_true : tok::kw_false).get();
  }
  if (Tok.is(tok::maratine_nullptr) || Tok.is(tok::maratine_null)) {
    SourceLocation Loc = ConsumeTok();
    return Actions.ActOnCXXNullPtrLiteral(Loc).get();
  }

  // Identifier (variable ref, function call)
  if (Tok.is(tok::identifier)) {
    IdentifierInfo *II  = Tok.getIdentifierInfo();
    SourceLocation  Loc = ConsumeTok();
    ExprResult R = Actions.ActOnIdExpression(Actions.getCurScope(),
                                             CXXScopeSpec(), SourceLocation(),
                                             DeclarationNameInfo(
                                                 DeclarationName(II), Loc),
                                             /*HasTrailingLParen=*/Tok.is(tok::l_paren),
                                             /*IsAddressOfOperand=*/false);
    Expr *E = R.isInvalid() ? nullptr : R.get();
    if (E && Tok.is(tok::l_paren))
      E = ParseCallExpr(E);
    return E;
  }

  // Parenthesised expression
  if (Tok.is(tok::l_paren)) {
    ConsumeTok();
    Expr *Inner = ParseExpression();
    if (!Inner) return nullptr;
    if (!ExpectAndConsumeTok(tok::r_paren, "')' after expression")) return nullptr;
    return Inner;
  }

  Diags.Report(Tok.getLocation(), diag::err_expected_expression);
  return nullptr;
}

// ---------------------------------------------------------------------------
// ParseFFICallExpr — <Module***Function***>(args)
//   Mara FFI syntax: angle-brackets wrap the function path.
// ---------------------------------------------------------------------------
Expr *MaratineParser::ParseFFICallExpr() {
  assert(Tok.is(tok::less));
  SourceLocation LAngle = ConsumeTok();

  // Collect the path: identifiers and *** separators
  SmallString<256> PathBuf;
  while (!Tok.is(tok::greater) && !Tok.is(tok::eof)) {
    if (Tok.is(tok::maratine_triple_star)) {
      PathBuf.append("***");
      ConsumeTok();
    } else if (Tok.is(tok::identifier)) {
      PathBuf.append(Tok.getIdentifierInfo()->getName());
      ConsumeTok();
    } else if (Tok.is(tok::l_paren)) {
      // <Fn***()> — empty call inside angle brackets
      ConsumeTok();
      if (Tok.is(tok::r_paren)) ConsumeTok();
      break;
    } else {
      break;
    }
  }
  if (!ExpectAndConsumeTok(tok::greater, "'>' after FFI path")) return nullptr;

  // Argument list
  SmallVector<Expr *, 8> Args;
  if (Tok.is(tok::l_paren)) {
    ConsumeTok();
    while (!Tok.is(tok::r_paren) && !Tok.is(tok::eof)) {
      Expr *A = ParseExpression();
      if (!A) return nullptr;
      Args.push_back(A);
      if (Tok.is(tok::comma)) ConsumeTok();
    }
    if (!ExpectAndConsumeTok(tok::r_paren, "')' after FFI arguments"))
      return nullptr;
  }

  // Build an opaque call expression (Sema resolves the FFI symbol later)
  IdentifierInfo *FFI_II = &Context.Idents.get(PathBuf.str());
  ExprResult FnRef = Actions.ActOnIdExpression(
      Actions.getCurScope(), CXXScopeSpec(), SourceLocation(),
      DeclarationNameInfo(DeclarationName(FFI_II), LAngle),
      /*HasTrailingLParen=*/true, /*IsAddressOfOperand=*/false);
  if (FnRef.isInvalid()) return nullptr;

  ExprResult Call = Actions.ActOnCallExpr(
      Actions.getCurScope(), FnRef.get(), LAngle, Args, Tok.getLocation());
  return Call.isInvalid() ? nullptr : Call.get();
}

// ---------------------------------------------------------------------------
// ParseCallExpr — name(args)
// ---------------------------------------------------------------------------
Expr *MaratineParser::ParseCallExpr(Expr *Callee) {
  SourceLocation LParenLoc = ConsumeTok(); // '('
  SmallVector<Expr *, 8> Args;
  while (!Tok.is(tok::r_paren) && !Tok.is(tok::eof)) {
    Expr *A = ParseExpression();
    if (!A) return nullptr;
    Args.push_back(A);
    if (Tok.is(tok::comma)) ConsumeTok();
  }
  SourceLocation RParenLoc = Tok.getLocation();
  if (!ExpectAndConsumeTok(tok::r_paren, "')' after call arguments"))
    return nullptr;

  ExprResult R = Actions.ActOnCallExpr(
      Actions.getCurScope(), Callee, LParenLoc, Args, RParenLoc);
  return R.isInvalid() ? nullptr : R.get();
}

// ---------------------------------------------------------------------------
// ParseArrayLiteralExpr — [ elem; elem; … ]
//   Mara rule: separator is ';' (semicolon), NOT comma.
// ---------------------------------------------------------------------------
Expr *MaratineParser::ParseArrayLiteralExpr() {
  assert(Tok.is(tok::l_square));
  SourceLocation LLoc = ConsumeTok();

  SmallVector<Expr *, 8> Elements;
  while (!Tok.is(tok::r_square) && !Tok.is(tok::eof)) {
    Expr *E = ParseExpression();
    if (!E) return nullptr;
    Elements.push_back(E);

    if (Tok.is(tok::semi)) {
      ConsumeTok(); // consume ';' — the Mara array separator
    } else if (Tok.is(tok::r_square)) {
      break;
    } else {
      Diags.Report(Tok.getLocation(), diag::err_expected)
          << "';' between array elements (Mara arrays use ';' not ',')";
      return nullptr;
    }
  }

  SourceLocation RLoc = Tok.getLocation();
  if (!ExpectAndConsumeTok(tok::r_square, "']' after array literal"))
    return nullptr;

  ExprResult R = Actions.ActOnInitListExpr(LLoc, Elements, RLoc);
  return R.isInvalid() ? nullptr : R.get();
}
