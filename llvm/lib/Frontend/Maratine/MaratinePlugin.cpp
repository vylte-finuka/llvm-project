//===--- MaratinePlugin.cpp - Plugin registration for Maratine frontend --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MaratineFrontendAction.h"
#include "clang/Frontend/PluginRegistry.h"
#include "clang/Frontend/FrontendRegistry.h"

using namespace clang;

static FrontendPluginRegistry::Add<MaratineFrontendAction>
X("maratine-frontend", "Maratine language frontend");

static bool
RegisterMaratinePass(const PassName &, const StringRef &,
                     const PassRegistrationListenerType &) {
  return true;
}
static llvm::RegisterStandardPasses
Y(PassManagerBuilder::EP_EarlyAsPossible,
  RegisterMaratinePass);