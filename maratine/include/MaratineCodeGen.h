// Vyft Ltd - Maratine Code Generator
// Générateur LLVM IR pour le langage Maratine

#ifndef LLVM_MARATINE_CODEGEN_H
#define LLVM_MARATINE_CODEGEN_H

#include "MaratineAST.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/ADT/DenseMap.h"
#include <memory>
#include <unordered_map>

namespace llvm {
namespace maratine {

class CodeGenerator {
private:
  std::unique_ptr<LLVMContext> Context;
  std::unique_ptr<llvm::Module> Module;
  std::unique_ptr<IRBuilder<>> Builder;
  
  // Symboles locaux
  DenseMap<StringRef, llvm::Value *> NamedValues;
  
  // Types du runtime Maratine
  llvm::FunctionType *LogFuncType;
  llvm::Function *LogFunc;

  // Génération d'expressions
  Expected<llvm::Value *> codegenExpr(Expr *E);
  Expected<llvm::Value *> codegenStringLiteral(StringLiteral *E);
  Expected<llvm::Value *> codegenVarRef(VarRef *E);
  Expected<llvm::Value *> codegenCallExpr(CallExpr *E);

  // Génération de statements
  Expected<void> codegenLogStmt(LogStmt *S);
  Expected<void> codegenIfStmt(IfStmt *S);
  Expected<void> codegenVarDecl(VarDecl *D);

  // Génération de composants UI
  Expected<llvm::Value *> codegenUIComponent(UIComponent *UI);

  // Utilitaires
  void initializeRuntimeLibrary();
  llvm::FunctionType *getLLVMType(StringRef MartType);
  GlobalValue::LinkageTypes getVisibilityLinkage(Visibility V);

public:
  CodeGenerator(StringRef ModuleName);

  /// Génère LLVM IR depuis l'AST
  Expected<std::unique_ptr<llvm::Module>> codegenModule(Module *M);

  /// Génère code pour une fonction
  Expected<llvm::Function *> codegenFunction(FunctionDecl *F);

  /// Optimise le code généré
  void optimize();

  /// Produit le résultat
  std::unique_ptr<llvm::Module> getModule() { return std::move(Module); }
};

} // namespace maratine
} // namespace llvm

#endif // LLVM_MARATINE_CODEGEN_H
