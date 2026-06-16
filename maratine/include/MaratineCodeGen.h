// Vyft Ltd — Mara/Maratine Code Generator — Proprietary — 2026
//
// Generates LLVM IR from the Mara AST.
// Target: ARM64 (Slura OS / Lunée kernel).
// Mara type system: string i32 i64 u64 bool ptr array — no f32/f64/double.

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
public:
  explicit CodeGenerator(StringRef ModuleName);

  Expected<std::unique_ptr<llvm::Module>> codegenModule(Module *M);
  Expected<llvm::Function *>              codegenFunction(FunctionDecl *F);
  void optimize();
  std::unique_ptr<llvm::Module> getModule() { return std::move(Mod); }

private:
  std::unique_ptr<LLVMContext>   Ctx;
  std::unique_ptr<llvm::Module>  Mod;
  std::unique_ptr<IRBuilder<>>   Builder;

  // Local symbol table: variable name → alloca
  DenseMap<StringRef, llvm::Value *> Syms;

  // Runtime
  llvm::Function *LogFunc = nullptr;

  void initRuntime();

  // Type mapping
  llvm::Type *maraTypeToLLVM(MaraTypeKind K);
  GlobalValue::LinkageTypes visToLinkage(Visibility V);

  // Codegen helpers
  Expected<llvm::Value *> codegenExpr(Expr *E);
  Expected<llvm::Value *> codegenStringLit(StringLiteral *E);
  Expected<llvm::Value *> codegenIntLit(IntLiteral *E);
  Expected<llvm::Value *> codegenBoolLit(BoolLiteral *E);
  Expected<llvm::Value *> codegenNullLit(NullLiteral *E);
  Expected<llvm::Value *> codegenVarRef(VarRef *E);
  Expected<llvm::Value *> codegenCallExpr(CallExpr *E);
  Expected<llvm::Value *> codegenFFICall(FFICallExpr *E);
  Expected<llvm::Value *> codegenBinaryExpr(BinaryExpr *E);
  Expected<llvm::Value *> codegenArrayLiteral(ArrayLiteral *E);

  Error codegenStmt(ASTNode *S);
  Error codegenBlock(BlockStmt *B);
  Error codegenVarDecl(VarDecl *D);
  Error codegenIf(IfStmt *S);
  Error codegenLoop(LoopStmt *S);
  Error codegenLog(LogStmt *S);
  Error codegenRet(RetStmt *S);
  Error codegenAssign(AssignStmt *S);
};

} // namespace maratine
} // namespace llvm

#endif // LLVM_MARATINE_CODEGEN_H
