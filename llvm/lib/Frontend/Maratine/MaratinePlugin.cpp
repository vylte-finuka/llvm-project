//===--- MaratinePlugin.cpp - Plugin registration for Maratine frontend --===//
//
// Vyft Ltd — Proprietary — 2026
//
//===----------------------------------------------------------------------===//

#include "MaratineFrontendAction.h"
#include "clang/Frontend/PluginRegistry.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/IR/PassManager.h"

using namespace clang;
using namespace llvm;

// Register the Maratine frontend action with clang's plugin registry.
static FrontendPluginRegistry::Add<maratine::MaratineFrontendAction>
    X("maratine-frontend", "Mara/Maratine language frontend for Slura OS");

// Provide an LLVM pass plugin entry point.
// New pass manager API — no PassManagerBuilder dependency.
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "MaratineFrontend", "1.0",
    [](PassBuilder &) {}
  };
}
