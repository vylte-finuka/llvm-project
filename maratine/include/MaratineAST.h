// Vyft Ltd - Maratine Abstract Syntax Tree
// Représentation AST pour le langage Maratine

#ifndef LLVM_MARATINE_AST_H
#define LLVM_MARATINE_AST_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/SmallVector.h"
#include <memory>
#include <vector>

namespace llvm {
namespace maratine {

// Visibilité des déclarations
enum class Visibility {
  Public,   // op - visible partout
  Private,  // cl - privé/interne
  Internal  // Interne au module
};

// Nœud AST de base
class ASTNode {
public:
  virtual ~ASTNode() = default;
};

// Déclaration de variable
class VarDecl : public ASTNode {
public:
  std::string Name;
  std::string TypeName;
  std::unique_ptr<class Expr> Initializer;
  
  VarDecl(StringRef N, StringRef T)
    : Name(N), TypeName(T) {}
};

// Expression
class Expr : public ASTNode {
public:
  virtual ~Expr() = default;
};

// Littéral string
class StringLiteral : public Expr {
public:
  std::string Value;
  explicit StringLiteral(StringRef V) : Value(V) {}
};

// Littéral numérique
class NumberLiteral : public Expr {
public:
  double Value;
  explicit NumberLiteral(double V) : Value(V) {}
};

// Variable référence
class VarRef : public Expr {
public:
  std::string Name;
  explicit VarRef(StringRef N) : Name(N) {}
};

// Call de fonction
class CallExpr : public Expr {
public:
  std::string FunctionName;
  SmallVector<std::unique_ptr<Expr>, 4> Arguments;
  
  explicit CallExpr(StringRef Name) : FunctionName(Name) {}
};

// Bloc d'instruction
class BlockStmt : public ASTNode {
public:
  SmallVector<std::unique_ptr<ASTNode>, 8> Statements;
};

// Déclaration If
class IfStmt : public ASTNode {
public:
  std::string Condition;  // "sendit"
  std::unique_ptr<BlockStmt> ThenBlock;
  std::unique_ptr<BlockStmt> ElseBlock;
};

// Statement Log (affichage)
class LogStmt : public ASTNode {
public:
  std::unique_ptr<Expr> Message;
  explicit LogStmt(std::unique_ptr<Expr> Msg)
    : Message(std::move(Msg)) {}
};

// Déclaration de fonction
class FunctionDecl : public ASTNode {
public:
  std::string Name;
  Visibility Vis;
  SmallVector<std::pair<std::string, std::string>, 4> Parameters;
  std::string ReturnType;
  std::unique_ptr<BlockStmt> Body;
  
  FunctionDecl(StringRef N, Visibility V)
    : Name(N), Vis(V), ReturnType("void") {}
};

// Import de module
class ImportStmt : public ASTNode {
public:
  std::string ModuleName;
  SmallVector<std::string, 4> Items;
  
  explicit ImportStmt(StringRef M) : ModuleName(M) {}
};

// Décorateur (attribut)
class Decorator : public ASTNode {
public:
  std::string Name;
  explicit Decorator(StringRef N) : Name(N) {}
};

// Composant UI (View, Text, etc.)
class UIComponent : public Expr {
public:
  std::string ComponentName;
  SmallVector<std::unique_ptr<Expr>, 4> Children;
  SmallVector<std::pair<std::string, std::string>, 4> Properties;
  
  explicit UIComponent(StringRef Name) : ComponentName(Name) {}
};

// Module Maratine
class Module : public ASTNode {
public:
  std::string Name;
  SmallVector<std::unique_ptr<ImportStmt>, 4> Imports;
  SmallVector<std::unique_ptr<FunctionDecl>, 8> Functions;
  SmallVector<std::unique_ptr<UIComponent>, 4> UIElements;
};

} // namespace maratine
} // namespace llvm

#endif // LLVM_MARATINE_AST_H
