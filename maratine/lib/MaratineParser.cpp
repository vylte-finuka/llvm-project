// Vyft Ltd — Mara/Maratine Parser Implementation — Proprietary — 2026

#include "MaratineParser.h"
#include "llvm/Support/raw_ostream.h"
#include <sstream>

using namespace llvm;
using namespace llvm::maratine;

Parser::Parser(const std::vector<Token> &Toks) : Tokens(Toks) {}

// ---------------------------------------------------------------------------
// Token navigation
// ---------------------------------------------------------------------------
const Token &Parser::peek(int Ahead) const {
  size_t I = Cur + (size_t)Ahead;
  if (I >= Tokens.size()) return Tokens.back(); // EOF
  return Tokens[I];
}

const Token &Parser::advance() {
  if (Cur + 1 < Tokens.size()) Cur++;
  else Cur = Tokens.size() - 1;
  return Tokens[Cur - 1];
}

bool Parser::check(TokenKind K) const { return peek().Kind == K; }

bool Parser::match(TokenKind K) {
  if (check(K)) { advance(); return true; }
  return false;
}

bool Parser::expectAndConsume(TokenKind K, StringRef Msg) {
  if (!check(K)) {
    errs() << "[" << peek().Line << ":" << peek().Column << "] "
           << Msg.str() << " (got '" << peek().Value << "')\n";
    return false;
  }
  advance();
  return true;
}

bool Parser::isTypeKw(TokenKind K) const {
  switch (K) {
    case TokenKind::kw_type_string:
    case TokenKind::kw_type_i32:
    case TokenKind::kw_type_i64:
    case TokenKind::kw_type_u64:
    case TokenKind::kw_type_bool:
    case TokenKind::kw_type_ptr:
    case TokenKind::kw_type_array:
      return true;
    default: return false;
  }
}

// ---------------------------------------------------------------------------
// parseModule — top-level dispatcher
// ---------------------------------------------------------------------------
Expected<std::unique_ptr<Module>> Parser::parseModule() {
  auto M = std::make_unique<Module>();

  while (!check(TokenKind::eof)) {
    if (check(TokenKind::hash)) {
      advance(); // consume '#'
      if (!check(TokenKind::kw_base)) {
        errs() << "Expected 'base' after '#'\n";
        return mkErr("expected 'base' after '#'");
      }
      auto Imp = parseImport();
      if (!Imp) return Imp.takeError();
      M->Imports.push_back(std::move(*Imp));
      continue;
    }
    if (check(TokenKind::kw_let) || check(TokenKind::kw_var)) {
      auto VD = parseLetOrVar();
      if (!VD) return VD.takeError();
      M->Globals.push_back(std::move(*VD));
      continue;
    }
    if (check(TokenKind::kw_rel)) {
      auto FD = parseFunction();
      if (!FD) return FD.takeError();
      M->Functions.push_back(std::move(*FD));
      continue;
    }
    errs() << "[" << peek().Line << ":" << peek().Column << "] "
           << "Skipping unexpected token '" << peek().Value << "'\n";
    advance();
  }

  return M;
}

// ---------------------------------------------------------------------------
// parseImport — #base <path***[ DepA, DepB ]>;
// ---------------------------------------------------------------------------
Expected<std::unique_ptr<ImportDecl>> Parser::parseImport() {
  advance(); // consume 'base'

  if (!expectAndConsume(TokenKind::l_angle, "'<' after #base"))
    return mkErr("expected '<' after #base");

  std::string PathBuf;
  while (!check(TokenKind::r_angle) && !check(TokenKind::eof)) {
    if (check(TokenKind::triple_star)) {
      PathBuf += "***";
      advance();
    } else if (check(TokenKind::l_square)) {
      advance(); // '['
      // Dependency list: DepA, DepB
      while (!check(TokenKind::r_square) && !check(TokenKind::eof)) {
        if (check(TokenKind::identifier) || isTypeKw(peek().Kind))
          advance();
        if (check(TokenKind::comma)) advance();
      }
      expectAndConsume(TokenKind::r_square, "']' closing dependency list");
      break;
    } else if (check(TokenKind::identifier) || isTypeKw(peek().Kind)) {
      PathBuf += peek().Value;
      advance();
    } else {
      break;
    }
  }

  if (!expectAndConsume(TokenKind::r_angle, "'>' after module path"))
    return mkErr("expected '>' after module path");
  if (!expectAndConsume(TokenKind::semi, "';' after #base import"))
    return mkErr("expected ';' after #base import");

  return std::make_unique<ImportDecl>(PathBuf);
}

// ---------------------------------------------------------------------------
// parseLetOrVar — let/var name: <type> = expr;
// ---------------------------------------------------------------------------
Expected<std::unique_ptr<VarDecl>> Parser::parseLetOrVar() {
  bool IsConst = check(TokenKind::kw_let);
  advance(); // consume 'let' or 'var'

  if (!check(TokenKind::identifier))
    return mkErr("expected variable name after let/var");

  std::string Name = peek().Value;
  advance();

  if (!expectAndConsume(TokenKind::colon, "':' after variable name"))
    return mkErr("expected ':'");

  MaraTypeKind Kind = MaraTypeKind::Unknown;
  std::string  CompoundName;
  if (!parseTypeAnnotation(Kind, CompoundName))
    return mkErr("expected type annotation");

  auto VD = std::make_unique<VarDecl>(Name, Kind, IsConst);
  VD->CompoundTypeName = CompoundName;

  if (match(TokenKind::equals)) {
    auto Init = parseExpr();
    if (!Init) return Init.takeError();
    VD->Initializer = std::move(*Init);
  }

  if (!expectAndConsume(TokenKind::semi, "';' after declaration"))
    return mkErr("expected ';'");

  return VD;
}

// ---------------------------------------------------------------------------
// parseTypeAnnotation — <string>, <i32>, <[string TypeName]>
// ---------------------------------------------------------------------------
bool Parser::parseTypeAnnotation(MaraTypeKind &Kind, std::string &CompoundName) {
  if (!expectAndConsume(TokenKind::l_angle, "'<' to open type")) return false;

  if (check(TokenKind::l_square)) {
    advance(); // '['
    if (isTypeKw(peek().Kind)) advance(); // skip base type keyword
    if (check(TokenKind::identifier)) {
      CompoundName = peek().Value;
      advance();
    }
    // Optional ***[ deps ]
    if (check(TokenKind::triple_star)) {
      advance();
      if (check(TokenKind::l_square)) {
        advance();
        while (!check(TokenKind::r_square) && !check(TokenKind::eof)) advance();
        advance(); // ']'
      }
    }
    expectAndConsume(TokenKind::r_square, "']' closing compound type");
    Kind = MaraTypeKind::Compound;
  } else {
    switch (peek().Kind) {
      case TokenKind::kw_type_string: Kind = MaraTypeKind::String; break;
      case TokenKind::kw_type_i32:   Kind = MaraTypeKind::I32;    break;
      case TokenKind::kw_type_i64:   Kind = MaraTypeKind::I64;    break;
      case TokenKind::kw_type_u64:   Kind = MaraTypeKind::U64;    break;
      case TokenKind::kw_type_bool:  Kind = MaraTypeKind::Bool;   break;
      case TokenKind::kw_type_ptr:   Kind = MaraTypeKind::Ptr;    break;
      case TokenKind::kw_type_array: Kind = MaraTypeKind::Array;  break;
      default:
        errs() << "Expected Mara type keyword (string i32 i64 u64 bool ptr array)\n";
        return false;
    }
    advance();
  }

  return expectAndConsume(TokenKind::r_angle, "'>' closing type");
}

// ---------------------------------------------------------------------------
// parseInheritanceSuffix — <[string TypeName]>t
// ---------------------------------------------------------------------------
std::string Parser::parseInheritanceSuffix() {
  if (!check(TokenKind::l_angle)) return {};
  advance(); // '<'

  std::string TypeName;
  if (check(TokenKind::l_square)) {
    advance(); // '['
    if (isTypeKw(peek().Kind)) advance(); // base type keyword
    if (check(TokenKind::identifier)) {
      TypeName = peek().Value;
      advance();
    }
    while (!check(TokenKind::r_square) && !check(TokenKind::eof)) advance();
    advance(); // ']'
  }

  if (check(TokenKind::r_angle)) advance(); // '>'

  // The mandatory 't' suffix
  if (check(TokenKind::identifier) && peek().Value == "t") advance();

  return TypeName;
}

// ---------------------------------------------------------------------------
// parseParamList — [name type, name type]
//   Parameter types are raw (no angle brackets): [name string, count i32]
// ---------------------------------------------------------------------------
bool Parser::parseParamList(SmallVectorImpl<Param> &Params) {
  if (!expectAndConsume(TokenKind::l_square, "'[' to open param list")) return false;

  while (!check(TokenKind::r_square) && !check(TokenKind::eof)) {
    if (!check(TokenKind::identifier)) break;
    Param P;
    P.Name = peek().Value;
    advance();

    // Raw type keyword (no < >)
    switch (peek().Kind) {
      case TokenKind::kw_type_string: P.TypeKind = MaraTypeKind::String; break;
      case TokenKind::kw_type_i32:   P.TypeKind = MaraTypeKind::I32;    break;
      case TokenKind::kw_type_i64:   P.TypeKind = MaraTypeKind::I64;    break;
      case TokenKind::kw_type_u64:   P.TypeKind = MaraTypeKind::U64;    break;
      case TokenKind::kw_type_bool:  P.TypeKind = MaraTypeKind::Bool;   break;
      case TokenKind::kw_type_ptr:   P.TypeKind = MaraTypeKind::Ptr;    break;
      case TokenKind::kw_type_array: P.TypeKind = MaraTypeKind::Array;  break;
      case TokenKind::identifier:    // compound type name
        P.TypeKind = MaraTypeKind::Compound;
        P.CompoundTypeName = peek().Value;
        break;
      default: P.TypeKind = MaraTypeKind::Unknown; break;
    }
    if (peek().Kind != TokenKind::r_square && peek().Kind != TokenKind::comma)
      advance(); // consume type keyword

    Params.push_back(std::move(P));
    if (check(TokenKind::comma)) advance();
  }

  return expectAndConsume(TokenKind::r_square, "']' to close param list");
}

// ---------------------------------------------------------------------------
// parseFunction — rel op/cl name: [params] [ body ];
// ---------------------------------------------------------------------------
Expected<std::unique_ptr<FunctionDecl>> Parser::parseFunction() {
  advance(); // consume 'rel'

  bool IsPublic = false;
  if (check(TokenKind::kw_op)) { IsPublic = true;  advance(); }
  else if (check(TokenKind::kw_cl)) { IsPublic = false; advance(); }
  else return mkErr("expected 'op' or 'cl' after 'rel'");

  if (!check(TokenKind::identifier))
    return mkErr("expected function name after rel op/cl");

  std::string Name = peek().Value;
  advance();

  Visibility V = IsPublic ? Visibility::Public : Visibility::Private;
  auto FD = std::make_unique<FunctionDecl>(Name, V);

  if (!expectAndConsume(TokenKind::colon, "':' after function name"))
    return mkErr("expected ':'");

  // Optional inheritance: <[string TypeName]>t
  if (check(TokenKind::l_angle))
    FD->InheritType = parseInheritanceSuffix();

  // Parameter list: [name type, …]
  SmallVector<Param, 4> Params;
  if (check(TokenKind::l_square) &&
      // Distinguish param list [ ] from body [ ]:
      // peek ahead to see if it looks like [identifier typekeyword
      // We just try to parse params and fall back if it's the body.
      // Heuristic: if the content after '[' is `]` or `identifier typekw` it's params.
      true) {
    if (!parseParamList(Params))
      return mkErr("bad parameter list");
    FD->Params = SmallVector<Param, 4>(Params.begin(), Params.end());
  }

  // Body: [ … ]
  auto Body = parseBlock();
  if (!Body) return Body.takeError();
  FD->Body = std::move(*Body);

  if (check(TokenKind::semi)) advance();

  return FD;
}

// ---------------------------------------------------------------------------
// parseBlock — [ statements ]
// ---------------------------------------------------------------------------
Expected<std::unique_ptr<BlockStmt>> Parser::parseBlock() {
  if (!expectAndConsume(TokenKind::l_square, "'[' to open block"))
    return mkErr("expected '['");

  auto Block = std::make_unique<BlockStmt>();
  while (!check(TokenKind::r_square) && !check(TokenKind::eof)) {
    auto S = parseStatement();
    if (!S) return S.takeError();
    Block->Statements.push_back(std::move(*S));
  }

  if (!expectAndConsume(TokenKind::r_square, "']' to close block"))
    return mkErr("expected ']'");

  return Block;
}

// ---------------------------------------------------------------------------
// parseStatement — dispatcher
// ---------------------------------------------------------------------------
Expected<std::unique_ptr<ASTNode>> Parser::parseStatement() {
  if (check(TokenKind::kw_if)) {
    auto S = parseIf(); if (!S) return S.takeError();
    return std::move(*S);
  }
  if (check(TokenKind::kw_loop)) {
    auto S = parseLoop(); if (!S) return S.takeError();
    return std::move(*S);
  }
  if (check(TokenKind::kw_break)) {
    auto S = parseBreak(); if (!S) return S.takeError();
    return std::move(*S);
  }
  if (check(TokenKind::kw_log)) {
    auto S = parseLog(); if (!S) return S.takeError();
    return std::move(*S);
  }
  if (check(TokenKind::kw_ret)) {
    auto S = parseRet(); if (!S) return S.takeError();
    return std::move(*S);
  }
  if (check(TokenKind::kw_let) || check(TokenKind::kw_var)) {
    auto S = parseLetOrVar(); if (!S) return S.takeError();
    return std::move(*S);
  }
  if (check(TokenKind::kw_rel)) {
    auto S = parseFunction(); if (!S) return S.takeError();
    return std::move(*S);
  }

  // FFI call as a standalone statement: <Path***Fn***>(args);
  if (check(TokenKind::l_angle)) {
    auto E = parseFFICall(); if (!E) return E.takeError();
    if (check(TokenKind::semi)) advance();
    return std::make_unique<ExprStmt>(std::move(*E));
  }

  // Assignment: name = expr;
  if (check(TokenKind::identifier) || check(TokenKind::kw_self)) {
    std::string LHS = peek().Value;
    advance();
    if (check(TokenKind::equals)) {
      advance();
      auto RHS = parseExpr(); if (!RHS) return RHS.takeError();
      expectAndConsume(TokenKind::semi, "';' after assignment");
      return std::make_unique<AssignStmt>(LHS, std::move(*RHS));
    }
    // Expression statement: function call
    if (check(TokenKind::l_paren)) {
      auto Call = std::make_unique<CallExpr>(LHS);
      advance(); // '('
      while (!check(TokenKind::r_paren) && !check(TokenKind::eof)) {
        auto A = parseExpr(); if (!A) return A.takeError();
        Call->Args.push_back(std::move(*A));
        if (check(TokenKind::comma)) advance();
      }
      expectAndConsume(TokenKind::r_paren, "')'");
      expectAndConsume(TokenKind::semi, "';'");
      return std::make_unique<ExprStmt>(std::move(Call));
    }
    // Bare expression (e.g. standalone variable usage)
    expectAndConsume(TokenKind::semi, "';'");
    return std::make_unique<ExprStmt>(std::make_unique<VarRef>(LHS));
  }

  errs() << "[" << peek().Line << ":" << peek().Column
         << "] Skipping unknown statement token '" << peek().Value << "'\n";
  advance();
  return std::make_unique<BreakStmt>(); // placeholder
}

// ---------------------------------------------------------------------------
// parseIf — if (cond) [ then ] else [ else ];
// ---------------------------------------------------------------------------
Expected<std::unique_ptr<IfStmt>> Parser::parseIf() {
  advance(); // 'if'
  if (!expectAndConsume(TokenKind::l_paren, "'(' after if"))
    return mkErr("expected '('");

  auto Cond = parseExpr(); if (!Cond) return Cond.takeError();

  if (!expectAndConsume(TokenKind::r_paren, "')' after if condition"))
    return mkErr("expected ')'");

  auto Then = parseBlock(); if (!Then) return Then.takeError();

  auto IS = std::make_unique<IfStmt>();
  IS->Cond = std::move(*Cond);
  IS->Then = std::move(*Then);

  if (check(TokenKind::kw_else)) {
    advance();
    auto Else = parseBlock(); if (!Else) return Else.takeError();
    IS->Else = std::move(*Else);
  }

  if (check(TokenKind::semi)) advance();
  return IS;
}

// ---------------------------------------------------------------------------
// parseLoop — loop cond [ body ];
//   Condition is NOT wrapped in parentheses.
// ---------------------------------------------------------------------------
Expected<std::unique_ptr<LoopStmt>> Parser::parseLoop() {
  advance(); // 'loop'

  auto LS = std::make_unique<LoopStmt>();

  // If the next token is '[' — infinite loop (no condition)
  if (!check(TokenKind::l_square)) {
    auto Cond = parseExpr(); if (!Cond) return Cond.takeError();
    LS->Cond = std::move(*Cond);
  }

  auto Body = parseBlock(); if (!Body) return Body.takeError();
  LS->Body = std::move(*Body);

  if (check(TokenKind::semi)) advance();
  return LS;
}

// ---------------------------------------------------------------------------
// parseBreak — break;
// ---------------------------------------------------------------------------
Expected<std::unique_ptr<BreakStmt>> Parser::parseBreak() {
  advance(); // 'break'
  expectAndConsume(TokenKind::semi, "';' after break");
  return std::make_unique<BreakStmt>();
}

// ---------------------------------------------------------------------------
// parseLog — log: expr;
// ---------------------------------------------------------------------------
Expected<std::unique_ptr<LogStmt>> Parser::parseLog() {
  advance(); // 'log'
  if (!expectAndConsume(TokenKind::colon, "':' after log"))
    return mkErr("expected ':' after log");

  auto E = parseExpr(); if (!E) return E.takeError();
  expectAndConsume(TokenKind::semi, "';' after log expression");
  return std::make_unique<LogStmt>(std::move(*E));
}

// ---------------------------------------------------------------------------
// parseRet — ret expr;   (no colon)
// ---------------------------------------------------------------------------
Expected<std::unique_ptr<RetStmt>> Parser::parseRet() {
  advance(); // 'ret'
  auto RS = std::make_unique<RetStmt>();
  if (!check(TokenKind::semi)) {
    auto E = parseExpr(); if (!E) return E.takeError();
    RS->Value = std::move(*E);
  }
  expectAndConsume(TokenKind::semi, "';' after ret");
  return RS;
}

// ---------------------------------------------------------------------------
// parseFFICall — <Module***Fn***>(args)
// ---------------------------------------------------------------------------
Expected<std::unique_ptr<Expr>> Parser::parseFFICall() {
  advance(); // '<'

  std::string Path;
  while (!check(TokenKind::r_angle) && !check(TokenKind::eof)) {
    if (check(TokenKind::triple_star)) { Path += "***"; advance(); }
    else if (check(TokenKind::identifier) || isTypeKw(peek().Kind))
    { Path += peek().Value; advance(); }
    else if (check(TokenKind::l_paren)) {
      // <Fn***()>  — empty inline call
      advance();
      if (check(TokenKind::r_paren)) advance();
      break;
    } else break;
  }
  expectAndConsume(TokenKind::r_angle, "'>' after FFI path");

  auto Call = std::make_unique<FFICallExpr>(Path);
  if (check(TokenKind::l_paren)) {
    advance();
    while (!check(TokenKind::r_paren) && !check(TokenKind::eof)) {
      auto A = parseExpr(); if (!A) return A.takeError();
      Call->Args.push_back(std::move(*A));
      if (check(TokenKind::comma)) advance();
    }
    expectAndConsume(TokenKind::r_paren, "')' after FFI args");
  }
  return Call;
}

// ---------------------------------------------------------------------------
// parseArrayLiteral — [ elem; elem; … ]
//   Mara rule: separator is ';' not ','
// ---------------------------------------------------------------------------
Expected<std::unique_ptr<Expr>> Parser::parseArrayLiteral() {
  advance(); // '['
  auto A = std::make_unique<ArrayLiteral>();
  while (!check(TokenKind::r_square) && !check(TokenKind::eof)) {
    auto E = parseExpr(); if (!E) return E.takeError();
    A->Elements.push_back(std::move(*E));
    if (check(TokenKind::semi)) advance();
    else if (!check(TokenKind::r_square)) {
      errs() << "Expected ';' between array elements (Mara uses ';' not ',')\n";
      return mkErr("expected ';' between array elements");
    }
  }
  expectAndConsume(TokenKind::r_square, "']' after array literal");
  return A;
}

// ---------------------------------------------------------------------------
// Expression parsing — precedence climbing
// ---------------------------------------------------------------------------
static int binPrec(TokenKind K) {
  switch (K) {
    case TokenKind::pipepipe:      return 1;
    case TokenKind::ampamp:        return 2;
    case TokenKind::pipe:          return 3;
    case TokenKind::caret:         return 4;
    case TokenKind::amp:           return 5;
    case TokenKind::equalequal:
    case TokenKind::exclaimequal:  return 6;
    case TokenKind::l_angle:
    case TokenKind::lessequal:
    case TokenKind::r_angle:
    case TokenKind::greaterequal:  return 7;
    case TokenKind::lessless:
    case TokenKind::greatergreater:return 8;
    case TokenKind::plus:
    case TokenKind::minus:         return 9;
    case TokenKind::star:
    case TokenKind::slash:
    case TokenKind::percent:       return 10;
    default: return -1;
  }
}

Expected<std::unique_ptr<Expr>> Parser::parseExpr() {
  return parseBinaryExpr(0);
}

Expected<std::unique_ptr<Expr>> Parser::parseBinaryExpr(int MinPrec) {
  auto LHS = parseUnaryExpr(); if (!LHS) return LHS.takeError();

  while (true) {
    int Prec = binPrec(peek().Kind);
    if (Prec < MinPrec) break;

    std::string Op = peek().Value;
    advance();

    auto RHS = parseBinaryExpr(Prec + 1); if (!RHS) return RHS.takeError();

    auto B = std::make_unique<BinaryExpr>(Op, std::move(*LHS), std::move(*RHS));
    LHS = Expected<std::unique_ptr<Expr>>(std::move(B));
  }

  return LHS;
}

Expected<std::unique_ptr<Expr>> Parser::parseUnaryExpr() {
  if (check(TokenKind::exclaim) || check(TokenKind::minus)) {
    std::string Op = peek().Value;
    advance();
    auto Sub = parseUnaryExpr(); if (!Sub) return Sub.takeError();
    return std::make_unique<BinaryExpr>(Op,
        std::make_unique<IntLiteral>(0), std::move(*Sub));
  }
  return parsePrimary();
}

Expected<std::unique_ptr<Expr>> Parser::parsePrimary() {
  // FFI call: <Path***Fn***>(args)
  if (check(TokenKind::l_angle)) return parseFFICall();

  // Array literal: [ elem; elem; … ]
  if (check(TokenKind::l_square)) return parseArrayLiteral();

  // String literal
  if (check(TokenKind::string_literal)) {
    std::string V = peek().Value; advance();
    return std::make_unique<StringLiteral>(V);
  }

  // Integer literal
  if (check(TokenKind::integer_literal)) {
    int64_t V = std::stoll(peek().Value); advance();
    return std::make_unique<IntLiteral>(V);
  }

  // Boolean literals
  if (check(TokenKind::kw_true))  { advance(); return std::make_unique<BoolLiteral>(true);  }
  if (check(TokenKind::kw_false)) { advance(); return std::make_unique<BoolLiteral>(false); }

  // Null literals
  if (check(TokenKind::kw_nullptr)) { advance(); return std::make_unique<NullLiteral>(true);  }
  if (check(TokenKind::kw_null))    { advance(); return std::make_unique<NullLiteral>(false); }

  // Identifier: variable reference or function call
  if (check(TokenKind::identifier) || check(TokenKind::kw_self)) {
    std::string Name = peek().Value; advance();
    if (check(TokenKind::l_paren)) {
      advance();
      auto Call = std::make_unique<CallExpr>(Name);
      while (!check(TokenKind::r_paren) && !check(TokenKind::eof)) {
        auto A = parseExpr(); if (!A) return A.takeError();
        Call->Args.push_back(std::move(*A));
        if (check(TokenKind::comma)) advance();
      }
      expectAndConsume(TokenKind::r_paren, "')'");
      return Call;
    }
    return std::make_unique<VarRef>(Name);
  }

  // Parenthesised expression
  if (check(TokenKind::l_paren)) {
    advance();
    auto E = parseExpr(); if (!E) return E.takeError();
    expectAndConsume(TokenKind::r_paren, "')' after expression");
    return E;
  }

  return mkErr("unexpected token in expression: '" + peek().Value + "'");
}

void Parser::printAST(const Module &M) const {
  errs() << "=== Mara AST: " << M.Name << " ===\n";
  errs() << "Imports:   " << M.Imports.size()   << "\n";
  errs() << "Globals:   " << M.Globals.size()   << "\n";
  errs() << "Functions: " << M.Functions.size() << "\n";
  for (const auto &F : M.Functions)
    errs() << "  " << (F->Vis == Visibility::Public ? "op" : "cl")
           << " " << F->Name << "\n";
}
