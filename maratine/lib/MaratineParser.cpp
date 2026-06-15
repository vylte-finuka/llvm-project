// Vyft Ltd - Maratine Parser Implementation

#include "MaratineParser.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::maratine;

Parser::Parser(const std::vector<Token> &Toks) : Tokens(Toks) {}

const Token &Parser::peek() const {
  if (Current >= Tokens.size())
    return Tokens.back(); // Return EOF
  return Tokens[Current];
}

const Token &Parser::advance() {
  if (Current < Tokens.size() - 1)
    Current++;
  return peek();
}

bool Parser::check(TokenKind Kind) const {
  return peek().Kind == Kind;
}

bool Parser::match(TokenKind Kind) {
  if (check(Kind)) {
    advance();
    return true;
  }
  return false;
}

bool Parser::match(std::initializer_list<TokenKind> Kinds) {
  for (TokenKind K : Kinds) {
    if (check(K)) {
      advance();
      return true;
    }
  }
  return false;
}

Expected<std::unique_ptr<ImportStmt>> Parser::parseImport() {
  // std***module***imports
  if (!match(TokenKind::kw_std))
    return createStringError(inconvertibleErrorCode(),
                            "Expected 'std' for import");

  // Skip *** markers
  while (match(TokenKind::star)) {
  }

  if (!check(TokenKind::identifier))
    return createStringError(inconvertibleErrorCode(),
                            "Expected module name");

  std::string ModuleName = peek().Value;
  advance();

  auto Import = std::make_unique<ImportStmt>(ModuleName);

  // Optionnel: {item1, item2}
  if (match(TokenKind::l_brace)) {
    while (!check(TokenKind::r_brace) && !check(TokenKind::eof)) {
      if (check(TokenKind::identifier)) {
        Import->Items.push_back(peek().Value);
        advance();
      }

      if (!match(TokenKind::comma))
        break;
    }

    if (!match(TokenKind::r_brace))
      return createStringError(inconvertibleErrorCode(),
                              "Expected '}' after import items");
  }

  match(TokenKind::semicolon); // Optional semicolon

  return Import;
}

Expected<std::unique_ptr<VarDecl>> Parser::parseVarDecl() {
  // let VarName: Type = value
  if (!match(TokenKind::kw_let))
    return createStringError(inconvertibleErrorCode(), "Expected 'let'");

  if (!check(TokenKind::identifier))
    return createStringError(inconvertibleErrorCode(),
                            "Expected variable name");

  std::string VarName = peek().Value;
  advance();

  std::string TypeName = "auto";

  if (match(TokenKind::colon)) {
    if (check(TokenKind::identifier)) {
      TypeName = peek().Value;
      advance();
    }
  }

  auto VarD = std::make_unique<VarDecl>(VarName, TypeName);

  // Initializer
  if (match(TokenKind::equals)) {
    auto InitExpr = parseExpression();
    if (!InitExpr)
      return InitExpr.takeError();
    VarD->Initializer = std::move(*InitExpr);
  }

  return VarD;
}

Expected<std::unique_ptr<LogStmt>> Parser::parseLogStmt() {
  // log: "message"
  if (!check(TokenKind::kw_log))
    return createStringError(inconvertibleErrorCode(), "Expected 'log'");

  advance();

  if (!match(TokenKind::colon))
    return createStringError(inconvertibleErrorCode(),
                            "Expected ':' after 'log'");

  auto MsgExpr = parseExpression();
  if (!MsgExpr)
    return MsgExpr.takeError();

  return std::make_unique<LogStmt>(std::move(*MsgExpr));
}

Expected<std::unique_ptr<IfStmt>> Parser::parseIfStmt() {
  // if condition [ ... ]
  if (!match(TokenKind::kw_if))
    return createStringError(inconvertibleErrorCode(), "Expected 'if'");

  if (!check(TokenKind::identifier))
    return createStringError(inconvertibleErrorCode(),
                            "Expected condition identifier");

  std::string Condition = peek().Value;
  advance();

  auto IfS = std::make_unique<IfStmt>();
  IfS->Condition = Condition;

  if (!match(TokenKind::l_square))
    return createStringError(inconvertibleErrorCode(),
                            "Expected '[' after if condition");

  auto ThenBlock = parseBlock();
  if (!ThenBlock)
    return ThenBlock.takeError();

  IfS->ThenBlock = std::move(*ThenBlock);

  return IfS;
}

Expected<std::unique_ptr<BlockStmt>> Parser::parseBlock() {
  auto Block = std::make_unique<BlockStmt>();

  while (!check(TokenKind::r_square) && !check(TokenKind::eof)) {
    if (check(TokenKind::kw_let)) {
      auto VarD = parseVarDecl();
      if (!VarD)
        return VarD.takeError();
      Block->Statements.push_back(std::move(*VarD));
    } else if (check(TokenKind::kw_if)) {
      auto IfS = parseIfStmt();
      if (!IfS)
        return IfS.takeError();
      Block->Statements.push_back(std::move(*IfS));
    } else if (check(TokenKind::kw_log)) {
      auto LogS = parseLogStmt();
      if (!LogS)
        return LogS.takeError();
      Block->Statements.push_back(std::move(*LogS));
    } else {
      advance(); // Skip unknown token
    }
  }

  if (!match(TokenKind::r_square))
    return createStringError(inconvertibleErrorCode(),
                            "Expected ']' to close block");

  return Block;
}

Expected<std::unique_ptr<Expr>> Parser::parsePrimary() {
  // Littéral string
  if (check(TokenKind::string_literal)) {
    std::string Value = peek().Value;
    advance();
    return std::make_unique<StringLiteral>(Value);
  }

  // Nombre
  if (check(TokenKind::number)) {
    double Value = std::stod(peek().Value);
    advance();
    return std::make_unique<NumberLiteral>(Value);
  }

  // Identificateur (variable ou appel fonction)
  if (check(TokenKind::identifier)) {
    std::string Name = peek().Value;
    advance();

    if (match(TokenKind::l_paren)) {
      // Appel fonction
      auto Call = std::make_unique<CallExpr>(Name);

      while (!check(TokenKind::r_paren) && !check(TokenKind::eof)) {
        auto Arg = parseExpression();
        if (!Arg)
          return Arg.takeError();
        Call->Arguments.push_back(std::move(*Arg));

        if (!match(TokenKind::comma))
          break;
      }

      if (!match(TokenKind::r_paren))
        return createStringError(inconvertibleErrorCode(),
                                "Expected ')' after function arguments");

      return Call;
    }

    // Référence variable
    return std::make_unique<VarRef>(Name);
  }

  // Composant UI
  if (check(TokenKind::l_angle)) {
    return parseUIComponentExpr();
  }

  return createStringError(inconvertibleErrorCode(),
                          "Unexpected token in expression");
}

Expected<std::unique_ptr<Expr>> Parser::parseUIComponentExpr() {
  if (!match(TokenKind::l_angle))
    return createStringError(inconvertibleErrorCode(),
                            "Expected '<' for UI component");

  if (!check(TokenKind::identifier))
    return createStringError(inconvertibleErrorCode(),
                            "Expected component name");

  std::string CompName = peek().Value;
  advance();

  auto UIComp = std::make_unique<UIComponent>(CompName);

  if (!match(TokenKind::r_angle))
    return createStringError(inconvertibleErrorCode(),
                            "Expected '>' after component name");

  // Contenu du composant
  while (!check(TokenKind::l_angle) || peek(1).Value != "/" + CompName) {
    if (check(TokenKind::string_literal)) {
      std::string Value = peek().Value;
      UIComp->Children.push_back(
          std::make_unique<StringLiteral>(Value));
      advance();
    } else if (check(TokenKind::l_angle)) {
      auto Child = parseUIComponentExpr();
      if (!Child)
        return Child.takeError();
      UIComp->Children.push_back(std::move(*Child));
    } else {
      advance();
    }
  }

  // Closing tag
  if (match(TokenKind::l_angle) && match(TokenKind::star)) {
    advance(); // Skip component name
    if (!match(TokenKind::r_angle))
      return createStringError(inconvertibleErrorCode(),
                              "Expected '>' for closing tag");
  }

  return UIComp;
}

Expected<std::unique_ptr<Expr>> Parser::parseExpression() {
  return parsePrimary();
}

Expected<std::unique_ptr<FunctionDecl>> Parser::parseFunction() {
  // rel op FunctionName[ ... ]
  Visibility Vis = Visibility::Private;

  if (match(TokenKind::kw_rel)) {
    if (match(TokenKind::kw_op)) {
      Vis = Visibility::Public;
    } else if (match(TokenKind::kw_cl)) {
      Vis = Visibility::Private;
    }
  }

  if (!check(TokenKind::identifier))
    return createStringError(inconvertibleErrorCode(),
                            "Expected function name");

  std::string FuncName = peek().Value;
  advance();

  auto Func = std::make_unique<FunctionDecl>(FuncName, Vis);

  // Parameters
  if (match(TokenKind::l_paren)) {
    while (!check(TokenKind::r_paren) && !check(TokenKind::eof)) {
      if (check(TokenKind::identifier)) {
        std::string ParamName = peek().Value;
        advance();

        std::string ParamType = "auto";
        if (match(TokenKind::colon)) {
          if (check(TokenKind::identifier)) {
            ParamType = peek().Value;
            advance();
          }
        }

        Func->Parameters.push_back({ParamName, ParamType});
      }

      if (!match(TokenKind::comma))
        break;
    }

    if (!match(TokenKind::r_paren))
      return createStringError(inconvertibleErrorCode(),
                              "Expected ')' after parameters");
  }

  // Return type
  if (match(TokenKind::arrow)) {
    if (check(TokenKind::identifier)) {
      Func->ReturnType = peek().Value;
      advance();
    }
  }

  // Body
  if (!match(TokenKind::l_square))
    return createStringError(inconvertibleErrorCode(),
                            "Expected '[' for function body");

  auto Body = parseBlock();
  if (!Body)
    return Body.takeError();

  Func->Body = std::move(*Body);

  return Func;
}

Expected<std::unique_ptr<Decorator>> Parser::parseDecorator() {
  // #[DecoratorName]
  if (!match(TokenKind::hash))
    return createStringError(inconvertibleErrorCode(),
                            "Expected '#' for decorator");

  if (!match(TokenKind::l_square))
    return createStringError(inconvertibleErrorCode(),
                            "Expected '[' after '#'");

  if (!check(TokenKind::identifier))
    return createStringError(inconvertibleErrorCode(),
                            "Expected decorator name");

  std::string DecName = peek().Value;
  advance();

  if (!match(TokenKind::r_square))
    return createStringError(inconvertibleErrorCode(),
                            "Expected ']' after decorator name");

  return std::make_unique<Decorator>(DecName);
}

Expected<std::unique_ptr<Module>> Parser::parseModule() {
  auto M = std::make_unique<Module>();

  while (!check(TokenKind::eof)) {
    // Imports
    if (check(TokenKind::kw_std)) {
      auto Imp = parseImport();
      if (!Imp)
        return Imp.takeError();
      M->Imports.push_back(std::move(*Imp));
    }
    // Decorateurs
    else if (check(TokenKind::hash)) {
      auto Dec = parseDecorator();
      if (!Dec)
        return Dec.takeError();
    }
    // Fonctions
    else if (check(TokenKind::kw_rel) || check(TokenKind::kw_op)) {
      auto Func = parseFunction();
      if (!Func)
        return Func.takeError();
      M->Functions.push_back(std::move(*Func));
    }
    // Composants UI
    else if (check(TokenKind::l_angle)) {
      auto UI = parseUIComponentExpr();
      if (!UI)
        return UI.takeError();
      // Stocker comme composant UI top-level si possible
    } else {
      advance();
    }
  }

  return M;
}

void Parser::printAST(const Module &M) const {
  errs() << "=== Maratine AST ===\n";
  errs() << "Module with " << M.Imports.size() << " imports and "
         << M.Functions.size() << " functions\n";

  for (const auto &Func : M.Functions) {
    errs() << "Function: " << Func->Name << " (vis="
           << (int)Func->Vis << ")\n";
  }
}
