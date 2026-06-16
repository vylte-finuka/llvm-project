// Vyft Ltd — Mara/Maratine Code Generator Implementation — Proprietary — 2026

#include "MaratineCodeGen.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::maratine;

CodeGenerator::CodeGenerator(StringRef ModuleName)
    : Ctx(std::make_unique<LLVMContext>()),
      Mod(std::make_unique<llvm::Module>(ModuleName, *Ctx)),
      Builder(std::make_unique<IRBuilder<>>(*Ctx)) {
  initRuntime();
}

void CodeGenerator::initRuntime() {
  // Declare printf for log: — takes a char* and returns i32
  std::vector<llvm::Type *> Args = { Builder->getPtrTy() };
  auto *FT = llvm::FunctionType::get(Builder->getInt32Ty(), Args, /*isVarArg=*/true);
  LogFunc = llvm::Function::Create(FT, GlobalValue::ExternalLinkage, "printf", Mod.get());
}

llvm::Type *CodeGenerator::maraTypeToLLVM(MaraTypeKind K) {
  switch (K) {
    case MaraTypeKind::I32:      return Builder->getInt32Ty();
    case MaraTypeKind::I64:      return Builder->getInt64Ty();
    case MaraTypeKind::U64:      return Builder->getInt64Ty();
    case MaraTypeKind::Bool:     return Builder->getInt1Ty();
    case MaraTypeKind::Ptr:      return Builder->getPtrTy();
    case MaraTypeKind::String:   return Builder->getPtrTy();
    case MaraTypeKind::Array:    return Builder->getPtrTy();
    case MaraTypeKind::Compound: return Builder->getPtrTy();
    default:                     return Builder->getInt32Ty();
  }
}

GlobalValue::LinkageTypes CodeGenerator::visToLinkage(Visibility V) {
  return (V == Visibility::Public) ? GlobalValue::ExternalLinkage
                                   : GlobalValue::InternalLinkage;
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------
Expected<llvm::Value *> CodeGenerator::codegenExpr(Expr *E) {
  if (auto *SL = dyn_cast<StringLiteral>(E))  return codegenStringLit(SL);
  if (auto *IL = dyn_cast<IntLiteral>(E))     return codegenIntLit(IL);
  if (auto *BL = dyn_cast<BoolLiteral>(E))    return codegenBoolLit(BL);
  if (auto *NL = dyn_cast<NullLiteral>(E))    return codegenNullLit(NL);
  if (auto *VR = dyn_cast<VarRef>(E))         return codegenVarRef(VR);
  if (auto *CE = dyn_cast<CallExpr>(E))       return codegenCallExpr(CE);
  if (auto *FC = dyn_cast<FFICallExpr>(E))    return codegenFFICall(FC);
  if (auto *BE = dyn_cast<BinaryExpr>(E))     return codegenBinaryExpr(BE);
  if (auto *AL = dyn_cast<ArrayLiteral>(E))   return codegenArrayLiteral(AL);
  return createStringError(inconvertibleErrorCode(), "unknown expression type");
}

Expected<llvm::Value *> CodeGenerator::codegenStringLit(StringLiteral *E) {
  return Builder->CreateGlobalString(E->Value);
}

Expected<llvm::Value *> CodeGenerator::codegenIntLit(IntLiteral *E) {
  if (E->IntTyKind == MaraTypeKind::I64 || E->IntTyKind == MaraTypeKind::U64)
    return ConstantInt::get(Builder->getInt64Ty(), (uint64_t)E->Value);
  return ConstantInt::get(Builder->getInt32Ty(), (uint32_t)E->Value);
}

Expected<llvm::Value *> CodeGenerator::codegenBoolLit(BoolLiteral *E) {
  return ConstantInt::get(Builder->getInt1Ty(), E->Value ? 1 : 0);
}

Expected<llvm::Value *> CodeGenerator::codegenNullLit(NullLiteral *) {
  return ConstantPointerNull::get(Builder->getPtrTy());
}

Expected<llvm::Value *> CodeGenerator::codegenVarRef(VarRef *E) {
  auto It = Syms.find(E->Name);
  if (It == Syms.end())
    return createStringError(inconvertibleErrorCode(),
                             "undefined variable '" + E->Name + "'");
  return Builder->CreateLoad(Builder->getInt32Ty(), It->second, E->Name);
}

Expected<llvm::Value *> CodeGenerator::codegenCallExpr(CallExpr *E) {
  llvm::Function *F = Mod->getFunction(E->FunctionName);
  if (!F)
    return createStringError(inconvertibleErrorCode(),
                             "undefined function '" + E->FunctionName + "'");
  std::vector<llvm::Value *> ArgsV;
  for (auto &A : E->Args) {
    auto V = codegenExpr(A.get()); if (!V) return V.takeError();
    ArgsV.push_back(*V);
  }
  return Builder->CreateCall(F, ArgsV);
}

Expected<llvm::Value *> CodeGenerator::codegenFFICall(FFICallExpr *E) {
  // FFI functions are resolved at link time via DrvAPIInterCon.
  // We declare an external function with the mangled path name.
  std::string MangledName = E->Path;
  // Replace *** with ___ for a valid symbol name
  for (size_t I = 0; (I = MangledName.find("***", I)) != std::string::npos; )
    MangledName.replace(I, 3, "___");

  llvm::Function *F = Mod->getFunction(MangledName);
  if (!F) {
    std::vector<llvm::Type *> ParamTypes(E->Args.size(), Builder->getPtrTy());
    auto *FT = llvm::FunctionType::get(Builder->getInt32Ty(), ParamTypes, false);
    F = llvm::Function::Create(FT, GlobalValue::ExternalLinkage, MangledName, Mod.get());
  }

  std::vector<llvm::Value *> ArgsV;
  for (auto &A : E->Args) {
    auto V = codegenExpr(A.get()); if (!V) return V.takeError();
    ArgsV.push_back(Builder->CreatePointerCast(*V, Builder->getPtrTy()));
  }
  return Builder->CreateCall(F, ArgsV);
}

Expected<llvm::Value *> CodeGenerator::codegenBinaryExpr(BinaryExpr *E) {
  auto L = codegenExpr(E->LHS.get()); if (!L) return L.takeError();
  auto R = codegenExpr(E->RHS.get()); if (!R) return R.takeError();

  StringRef Op = E->Op;
  if (Op == "+")  return Builder->CreateAdd(*L, *R);
  if (Op == "-")  return Builder->CreateSub(*L, *R);
  if (Op == "*")  return Builder->CreateMul(*L, *R);
  if (Op == "/")  return Builder->CreateSDiv(*L, *R);
  if (Op == "%")  return Builder->CreateSRem(*L, *R);
  if (Op == "==") return Builder->CreateICmpEQ(*L, *R);
  if (Op == "!=") return Builder->CreateICmpNE(*L, *R);
  if (Op == "<")  return Builder->CreateICmpSLT(*L, *R);
  if (Op == "<=") return Builder->CreateICmpSLE(*L, *R);
  if (Op == ">")  return Builder->CreateICmpSGT(*L, *R);
  if (Op == ">=") return Builder->CreateICmpSGE(*L, *R);
  if (Op == "&&") return Builder->CreateAnd(*L, *R);
  if (Op == "||") return Builder->CreateOr(*L, *R);
  if (Op == "&")  return Builder->CreateAnd(*L, *R);
  if (Op == "|")  return Builder->CreateOr(*L, *R);
  if (Op == "^")  return Builder->CreateXor(*L, *R);
  if (Op == "<<") return Builder->CreateShl(*L, *R);
  if (Op == ">>") return Builder->CreateAShr(*L, *R);

  return createStringError(inconvertibleErrorCode(), "unknown operator " + Op.str());
}

Expected<llvm::Value *> CodeGenerator::codegenArrayLiteral(ArrayLiteral *E) {
  // For now represent as an i32 count constant (Sema/runtime resolves the pointer)
  return ConstantInt::get(Builder->getInt32Ty(), (uint32_t)E->Elements.size());
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------
Error CodeGenerator::codegenBlock(BlockStmt *B) {
  for (auto &S : B->Statements) {
    if (auto E = codegenStmt(S.get())) return E;
  }
  return Error::success();
}

Error CodeGenerator::codegenStmt(ASTNode *S) {
  if (auto *VD = dyn_cast<VarDecl>(S))   return codegenVarDecl(VD);
  if (auto *IS = dyn_cast<IfStmt>(S))    return codegenIf(IS);
  if (auto *LS = dyn_cast<LoopStmt>(S))  return codegenLoop(LS);
  if (auto *LG = dyn_cast<LogStmt>(S))   return codegenLog(LG);
  if (auto *RS = dyn_cast<RetStmt>(S))   return codegenRet(RS);
  if (auto *AS = dyn_cast<AssignStmt>(S))return codegenAssign(AS);
  if (auto *ES = dyn_cast<ExprStmt>(S)) {
    auto V = codegenExpr(ES->E.get());
    if (!V) return V.takeError();
    return Error::success();
  }
  if (auto *FD = dyn_cast<FunctionDecl>(S)) {
    auto R = codegenFunction(FD);
    if (!R) return R.takeError();
    return Error::success();
  }
  return Error::success(); // BreakStmt — handled by loop exit
}

Error CodeGenerator::codegenVarDecl(VarDecl *D) {
  llvm::Type *Ty = maraTypeToLLVM(D->TypeKind);
  llvm::AllocaInst *Alloca = Builder->CreateAlloca(Ty, nullptr, D->Name);

  if (D->Initializer) {
    auto V = codegenExpr(D->Initializer.get());
    if (!V) return V.takeError();
    Builder->CreateStore(*V, Alloca);
  } else {
    Builder->CreateStore(Constant::getNullValue(Ty), Alloca);
  }

  Syms[D->Name] = Alloca;
  return Error::success();
}

Error CodeGenerator::codegenIf(IfStmt *S) {
  auto CondV = codegenExpr(S->Cond.get());
  if (!CondV) return CondV.takeError();

  llvm::Function *Fn = Builder->GetInsertBlock()->getParent();
  auto *ThenBB  = BasicBlock::Create(*Ctx, "then",  Fn);
  auto *ElseBB  = BasicBlock::Create(*Ctx, "else");
  auto *MergeBB = BasicBlock::Create(*Ctx, "merge");

  Builder->CreateCondBr(*CondV, ThenBB, S->Else ? ElseBB : MergeBB);

  Builder->SetInsertPoint(ThenBB);
  if (auto E = codegenBlock(S->Then.get())) return E;
  Builder->CreateBr(MergeBB);

  if (S->Else) {
    Fn->insert(Fn->end(), ElseBB);
    Builder->SetInsertPoint(ElseBB);
    if (auto E = codegenBlock(S->Else.get())) return E;
    Builder->CreateBr(MergeBB);
  }

  Fn->insert(Fn->end(), MergeBB);
  Builder->SetInsertPoint(MergeBB);
  return Error::success();
}

Error CodeGenerator::codegenLoop(LoopStmt *S) {
  llvm::Function *Fn = Builder->GetInsertBlock()->getParent();
  auto *CondBB = BasicBlock::Create(*Ctx, "loop.cond", Fn);
  auto *BodyBB = BasicBlock::Create(*Ctx, "loop.body");
  auto *ExitBB = BasicBlock::Create(*Ctx, "loop.exit");

  Builder->CreateBr(CondBB);
  Builder->SetInsertPoint(CondBB);

  if (S->Cond) {
    auto CondV = codegenExpr(S->Cond.get());
    if (!CondV) return CondV.takeError();
    Builder->CreateCondBr(*CondV, BodyBB, ExitBB);
  } else {
    Builder->CreateBr(BodyBB); // infinite loop — break exits
  }

  Fn->insert(Fn->end(), BodyBB);
  Builder->SetInsertPoint(BodyBB);
  if (auto E = codegenBlock(S->Body.get())) return E;
  Builder->CreateBr(CondBB);

  Fn->insert(Fn->end(), ExitBB);
  Builder->SetInsertPoint(ExitBB);
  return Error::success();
}

Error CodeGenerator::codegenLog(LogStmt *S) {
  auto Msg = codegenExpr(S->Message.get());
  if (!Msg) return Msg.takeError();

  // Cast to i8* if needed (for printf)
  llvm::Value *Ptr = Builder->CreatePointerCast(*Msg, Builder->getPtrTy());
  Builder->CreateCall(LogFunc, { Ptr });
  return Error::success();
}

Error CodeGenerator::codegenRet(RetStmt *S) {
  if (S->Value) {
    auto V = codegenExpr(S->Value.get());
    if (!V) return V.takeError();
    Builder->CreateRet(*V);
  } else {
    Builder->CreateRetVoid();
  }
  return Error::success();
}

Error CodeGenerator::codegenAssign(AssignStmt *S) {
  auto RHS = codegenExpr(S->RHS.get());
  if (!RHS) return RHS.takeError();

  auto It = Syms.find(S->LHS);
  if (It == Syms.end())
    return createStringError(inconvertibleErrorCode(),
                             "undefined variable '" + S->LHS + "'");

  Builder->CreateStore(*RHS, It->second);
  return Error::success();
}

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------
Expected<llvm::Function *> CodeGenerator::codegenFunction(FunctionDecl *F) {
  std::vector<llvm::Type *> ParamTypes;
  for (const auto &P : F->Params)
    ParamTypes.push_back(maraTypeToLLVM(P.TypeKind));

  auto *FT = llvm::FunctionType::get(Builder->getInt32Ty(), ParamTypes, false);
  auto *Fn = llvm::Function::Create(FT, visToLinkage(F->Vis), F->Name, Mod.get());

  unsigned I = 0;
  for (auto &Arg : Fn->args())
    Arg.setName(F->Params[I++].Name);

  auto *Entry = BasicBlock::Create(*Ctx, "entry", Fn);
  Builder->SetInsertPoint(Entry);
  Syms.clear();
  for (auto &Arg : Fn->args()) {
    auto *A = Builder->CreateAlloca(Arg.getType(), nullptr, Arg.getName());
    Builder->CreateStore(&Arg, A);
    Syms[Arg.getName()] = A;
  }

  if (F->Body) {
    if (auto E = codegenBlock(F->Body.get()))
      return std::move(E);
  }

  // Default return 0 if no explicit ret
  if (Builder->GetInsertBlock() &&
      Builder->GetInsertBlock()->getTerminator() == nullptr) {
    Builder->CreateRet(ConstantInt::get(Builder->getInt32Ty(), 0));
  }

  return Fn;
}

// ---------------------------------------------------------------------------
// Module
// ---------------------------------------------------------------------------
Expected<std::unique_ptr<llvm::Module>> CodeGenerator::codegenModule(Module *M) {
  for (auto &GV : M->Globals) {
    auto *Ty = maraTypeToLLVM(GV->TypeKind);
    Constant *Init = GV->Initializer ? nullptr : Constant::getNullValue(Ty);
    (void)new GlobalVariable(*Mod, Ty, GV->IsConst,
                             GlobalValue::InternalLinkage, Init, GV->Name);
  }

  for (auto &F : M->Functions) {
    auto R = codegenFunction(F.get());
    if (!R) return R.takeError();
  }

  return std::move(Mod);
}

void CodeGenerator::optimize() {
  PassBuilder PB;
  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  PB.registerLoopAnalyses(LAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerModuleAnalyses(MAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(
      OptimizationLevel::O2);
  MPM.run(*Mod, MAM);
}
