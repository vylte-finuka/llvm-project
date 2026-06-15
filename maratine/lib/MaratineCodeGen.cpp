// Vyft Ltd - Maratine Code Generator Implementation

#include "MaratineCodeGen.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::maratine;

CodeGenerator::CodeGenerator(StringRef ModuleName)
    : Context(std::make_unique<LLVMContext>()),
      Module(std::make_unique<llvm::Module>(ModuleName, *Context)),
      Builder(std::make_unique<IRBuilder<>>(*Context)) {
  initializeRuntimeLibrary();
}

void CodeGenerator::initializeRuntimeLibrary() {
  // Déclarer printf-like function pour log
  std::vector<llvm::Type *> PrintfArgs = {
      Builder->getInt8PtrTy() // format string
  };

  LogFuncType = llvm::FunctionType::get(Builder->getInt32Ty(), PrintfArgs, true);
  LogFunc = llvm::Function::Create(
      LogFuncType, 
      llvm::Function::ExternalLinkage, 
      "printf", 
      Module.get()
  );
}

llvm::FunctionType *CodeGenerator::getLLVMType(StringRef MartType) {
  if (MartType == "void")
    return llvm::FunctionType::get(Builder->getVoidTy(), false);
  if (MartType == "i32")
    return llvm::FunctionType::get(Builder->getInt32Ty(), false);
  if (MartType == "i64")
    return llvm::FunctionType::get(Builder->getInt64Ty(), false);
  if (MartType == "f32")
    return llvm::FunctionType::get(Builder->getFloatTy(), false);
  if (MartType == "f64")
    return llvm::FunctionType::get(Builder->getDoubleTy(), false);

  // Default: void
  return llvm::FunctionType::get(Builder->getVoidTy(), false);
}

GlobalValue::LinkageTypes CodeGenerator::getVisibilityLinkage(Visibility V) {
  switch (V) {
  case Visibility::Public:
    return GlobalValue::ExternalLinkage;
  case Visibility::Private:
    return GlobalValue::InternalLinkage;
  case Visibility::Internal:
    return GlobalValue::PrivateLinkage;
  }
  return GlobalValue::InternalLinkage;
}

Expected<llvm::Value *> CodeGenerator::codegenStringLiteral(StringLiteral *E) {
  return Builder->CreateGlobalStringPtr(E->Value);
}

Expected<llvm::Value *> CodeGenerator::codegenVarRef(VarRef *E) {
  auto It = NamedValues.find(E->Name);
  if (It == NamedValues.end())
    return createStringError(inconvertibleErrorCode(),
                            "Unknown variable '" + E->Name + "'");
  return It->second;
}

Expected<llvm::Value *> CodeGenerator::codegenCallExpr(CallExpr *E) {
  // Chercher la fonction
  llvm::Function *CalleeF = Module->getFunction(E->FunctionName);
  if (!CalleeF)
    return createStringError(inconvertibleErrorCode(),
                            "Unknown function '" + E->FunctionName + "'");

  std::vector<llvm::Value *> ArgsV;
  for (const auto &Arg : E->Arguments) {
    auto Val = codegenExpr(Arg.get());
    if (!Val)
      return Val.takeError();
    ArgsV.push_back(*Val);
  }

  if (ArgsV.size() != CalleeF->arg_size())
    return createStringError(inconvertibleErrorCode(),
                            "Argument count mismatch");

  return Builder->CreateCall(CalleeF, ArgsV);
}

Expected<llvm::Value *> CodeGenerator::codegenExpr(Expr *E) {
  if (auto *SE = dyn_cast<StringLiteral>(E))
    return codegenStringLiteral(SE);
  
  if (auto *VR = dyn_cast<VarRef>(E))
    return codegenVarRef(VR);
  
  if (auto *CE = dyn_cast<CallExpr>(E))
    return codegenCallExpr(CE);

  if (auto *UI = dyn_cast<UIComponent>(E))
    return codegenUIComponent(UI);

  return createStringError(inconvertibleErrorCode(),
                          "Unknown expression type");
}

Expected<void> CodeGenerator::codegenLogStmt(LogStmt *S) {
  auto Msg = codegenExpr(S->Message.get());
  if (!Msg)
    return Msg.takeError();

  std::vector<llvm::Value *> Args = {*Msg};
  Builder->CreateCall(LogFunc, Args);

  return Error::success();
}

Expected<void> CodeGenerator::codegenIfStmt(IfStmt *S) {
  // Condition simple (vérifier si variable existe)
  auto CondVar = NamedValues.find(S->Condition);
  llvm::Value *CondVal = CondVar != NamedValues.end() 
    ? CondVar->second 
    : Builder->getInt1(1); // Default: true

  llvm::Function *TheFunction = Builder->GetInsertBlock()->getParent();

  // Créer les blocs
  llvm::BasicBlock *ThenBB =
      llvm::BasicBlock::Create(*Context, "then", TheFunction);
  llvm::BasicBlock *MergeBB =
      llvm::BasicBlock::Create(*Context, "ifcont");

  Builder->CreateCondBr(CondVal, ThenBB, MergeBB);

  // Générer le bloc then
  Builder->SetInsertPoint(ThenBB);

  if (S->ThenBlock) {
    for (const auto &Stmt : S->ThenBlock->Statements) {
      if (auto *LogS = dyn_cast<LogStmt>(Stmt.get())) {
        auto Res = codegenLogStmt(LogS);
        if (!Res)
          return Res.takeError();
      }
    }
  }

  Builder->CreateBr(MergeBB);

  // Finaliser
  TheFunction->insert(TheFunction->end(), MergeBB);
  Builder->SetInsertPoint(MergeBB);

  return Error::success();
}

Expected<void> CodeGenerator::codegenVarDecl(VarDecl *D) {
  llvm::Value *InitVal = nullptr;

  if (D->Initializer) {
    auto Val = codegenExpr(D->Initializer.get());
    if (!Val)
      return Val.takeError();
    InitVal = *Val;
  } else {
    InitVal = llvm::ConstantInt::get(Builder->getInt32Ty(), 0);
  }

  // Allouer et stocker
  llvm::AllocaInst *Alloca = Builder->CreateAlloca(InitVal->getType(), nullptr,
                                                   D->Name);
  Builder->CreateStore(InitVal, Alloca);

  NamedValues[D->Name] = Alloca;

  return Error::success();
}

Expected<llvm::Value *> CodeGenerator::codegenUIComponent(UIComponent *UI) {
  // Placeholder: Pour maintenant, retourner une valeur nulle
  // Dans une implémentation réelle, cela générerait du code pour construire
  // l'arborescence UI
  return llvm::ConstantInt::get(Builder->getInt32Ty(), 0);
}

Expected<llvm::Function *> CodeGenerator::codegenFunction(FunctionDecl *F) {
  // Créer le prototype de fonction
  std::vector<llvm::Type *> ParamTypes;
  for (const auto &Param : F->Parameters) {
    ParamTypes.push_back(Builder->getInt32Ty()); // Default: i32
  }

  llvm::FunctionType *FT = llvm::FunctionType::get(
      Builder->getInt32Ty(), // Return type
      ParamTypes,
      false // Not variadic
  );

  llvm::Function *Func = llvm::Function::Create(
      FT,
      getVisibilityLinkage(F->Vis),
      F->Name,
      Module.get());

  // Nommer les paramètres
  unsigned Idx = 0;
  for (auto &Arg : Func->args()) {
    Arg.setName(F->Parameters[Idx++].first);
  }

  // Créer le corps
  llvm::BasicBlock *BB = llvm::BasicBlock::Create(*Context, "entry", Func);
  Builder->SetInsertPoint(BB);

  // Ajouter les paramètres à la table de symboles
  NamedValues.clear();
  for (auto &Arg : Func->args()) {
    NamedValues[Arg.getName()] = &Arg;
  }

  // Générer le corps
  if (F->Body) {
    for (const auto &Stmt : F->Body->Statements) {
      if (auto *VarD = dyn_cast<VarDecl>(Stmt.get())) {
        auto Res = codegenVarDecl(VarD);
        if (!Res)
          return Res.takeError();
      } else if (auto *IfS = dyn_cast<IfStmt>(Stmt.get())) {
        auto Res = codegenIfStmt(IfS);
        if (!Res)
          return Res.takeError();
      } else if (auto *LogS = dyn_cast<LogStmt>(Stmt.get())) {
        auto Res = codegenLogStmt(LogS);
        if (!Res)
          return Res.takeError();
      }
    }
  }

  // Retour par défaut
  Builder->CreateRet(llvm::ConstantInt::get(Builder->getInt32Ty(), 0));

  return Func;
}

Expected<std::unique_ptr<llvm::Module>> CodeGenerator::codegenModule(Module *M) {
  // Générer les fonctions
  for (const auto &Func : M->Functions) {
    auto GenFunc = codegenFunction(Func.get());
    if (!GenFunc)
      return GenFunc.takeError();
  }

  return std::move(Module);
}

void CodeGenerator::optimize() {
  // Utiliser le nouveau pass manager
  PassBuilder PB;
  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  PB.registerLoopAnalyses(LAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerModuleAnalyses(MAM);

  FunctionPassManager FPM = PB.buildFunctionSimplificationPipeline(
      PassBuilder::OptimizationLevel::O2,
      ThinLTOPhase::None);

  ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(
      PassBuilder::OptimizationLevel::O2);

  MPM.run(*Module, MAM);
}
