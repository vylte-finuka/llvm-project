//===- AArch64CodeGenPrepare.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// AArch64-specific IR-level pre-codegen transformations.
//
// widenCrossBlockBoolVectorChains
// -------------------------------
// SelectionDAG type-legalization promotes <N x i1> to a single MVT for the
// entire function, so values that cross a basic-block boundary cannot adapt
// per-producer to the natural compare-result width: the width is committed
// globally and any producer that doesn't match incurs a narrowing/widening
// pair (XTN+SHLL, USHLL+SHL, etc.).
//
// This pass identifies connected chains of <N x i1> operations -- compares
// (producers), and/or/xor, shufflevector, phi -- where at least one chain
// value has a use in a different basic block, and rewrites the entire chain
// on <N x iSrcWidth>, where iSrcWidth is the widest compare-source element
// width seen in the chain. Because that target type is itself a legal NEON
// vector, the type legalizer leaves the chain alone and no XTN/SHLL is
// emitted along the chain. Re-narrowing happens only at the boundary where a
// non-chain user still expects <N x i1> (and/or/xor on i1, select-mask, etc).
//
//===----------------------------------------------------------------------===//

#include "AArch64.h"
#include "AArch64SMEAttributes.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "aarch64-codegenprepare"
#define PASS_NAME "AArch64 CodeGenPrepare"

STATISTIC(NumChainsWidened, "Number of cross-block <N x i1> chains widened");
STATISTIC(NumChainsSkippedTooWide,
          "Number of <N x i1> chains skipped due to widened-size cap");
STATISTIC(NumChainsSkippedNoCmpWidth,
          "Number of <N x i1> chains skipped: no producer cmp source width");
STATISTIC(NumValuesWidened, "Number of <N x i1> values rewritten on a wider type");

static cl::opt<bool> EnableWidenCrossBlockBoolVec(
    "aarch64-widen-cross-block-bool-vec",
    cl::desc("Pre-widen <N x i1> chains that span block boundaries to their "
             "natural producer width before SelectionDAG."),
    cl::init(true), cl::Hidden);

/// Cap on the size in bits of a widened chain vector. NEON registers are
/// 128 bits; widening past that produces multi-register vectors that the
/// type legalizer will split, which generally wipes out the win.
static constexpr unsigned MaxWidenedVectorSizeBits = 128;

namespace {

static bool isBoolVectorTy(Type *Ty) {
  auto *VT = dyn_cast<FixedVectorType>(Ty);
  return VT && VT->getElementType()->isIntegerTy(1);
}

/// True if `V` is a candidate member of an <NumElts x i1> chain: it produces
/// the right type and is one of the opcodes the rewrite handles.
static bool isChainMember(Value *V, unsigned NumElts) {
  if (!isBoolVectorTy(V->getType()))
    return false;
  if (cast<FixedVectorType>(V->getType())->getNumElements() != NumElts)
    return false;
  if (isa<CmpInst>(V))
    return true;
  auto *I = dyn_cast<Instruction>(V);
  if (!I)
    return false;
  if (isa<PHINode>(I) || isa<ShuffleVectorInst>(I))
    return true;
  return I->isBitwiseLogicOp();
}

/// Element bit-width of a producer compare's source operand. Pointer types
/// are sized via the DataLayout. Returns 0 for non-vector or unsized sources.
static unsigned getCmpSourceWidth(CmpInst *C) {
  auto *VT = dyn_cast<FixedVectorType>(C->getOperand(0)->getType());
  if (!VT)
    return 0;
  Type *EltTy = VT->getElementType();
  if (EltTy->isPointerTy())
    return C->getDataLayout().getPointerSizeInBits(EltTy->getPointerAddressSpace());
  return EltTy->getPrimitiveSizeInBits();
}

/// True if any user of `I` is in a different basic block. PHIs naturally
/// satisfy this through their incoming-value relationship: an incoming
/// value's user (the PHI) lives in a different block, so the incoming value
/// is flagged here without needing an explicit isa<PHINode> check.
static bool crossesBlock(Instruction *I) {
  BasicBlock *DefBB = I->getParent();
  for (User *U : I->users())
    if (auto *UI = dyn_cast<Instruction>(U))
      if (UI->getParent() != DefBB)
        return true;
  return false;
}

class AArch64CodeGenPrepare {
  Function &F;

public:
  AArch64CodeGenPrepare(Function &F) : F(F) {}

  bool run() {
    if (!EnableWidenCrossBlockBoolVec)
      return false;
    // SVE/SME streaming-mode functions go through a different lowering path
    // for predicates; this pass targets pure-NEON fixed-vector codegen.
    SMEAttrs FnAttrs(F);
    if (FnAttrs.hasStreamingInterfaceOrBody() ||
        FnAttrs.hasStreamingCompatibleInterface())
      return false;
    return widenCrossBlockBoolVectorChains();
  }

private:
  bool widenCrossBlockBoolVectorChains();

  /// Build the connected chain reachable from `Seed` by walking def-use
  /// edges through chain members of the same NumElts. Members are returned
  /// in BFS-discovery order to keep iteration deterministic.
  void collectChain(Value *Seed, unsigned NumElts,
                    SetVector<Value *> &Members);

  /// Rewrite the chain to operate on <NumElts x WideEltTy>.
  void applyWidening(const SetVector<Value *> &Members, unsigned NumElts,
                     Type *WideEltTy);
};

} // namespace

void AArch64CodeGenPrepare::collectChain(Value *Seed, unsigned NumElts,
                                         SetVector<Value *> &Members) {
  SmallVector<Value *, 32> Worklist;
  Worklist.push_back(Seed);
  while (!Worklist.empty()) {
    Value *V = Worklist.pop_back_val();
    if (!isChainMember(V, NumElts))
      continue;
    if (!Members.insert(V))
      continue;
    for (User *U : V->users())
      if (isChainMember(U, NumElts))
        Worklist.push_back(U);
    if (auto *I = dyn_cast<Instruction>(V))
      for (Value *Op : I->operands())
        if (isChainMember(Op, NumElts))
          Worklist.push_back(Op);
  }
}

bool AArch64CodeGenPrepare::widenCrossBlockBoolVectorChains() {
  // Snapshot all <N x i1>-typed instructions; iterating live IR would risk
  // iterator invalidation when applyWidening inserts new instructions.
  SmallVector<Instruction *, 32> Seeds;
  for (BasicBlock &BB : F)
    for (Instruction &I : BB)
      if (isBoolVectorTy(I.getType()))
        Seeds.push_back(&I);
  if (Seeds.empty())
    return false;

  SmallPtrSet<Value *, 32> AllProcessed;
  bool Changed = false;

  for (Instruction *Seed : Seeds) {
    if (AllProcessed.contains(Seed))
      continue;
    auto *VT = dyn_cast<FixedVectorType>(Seed->getType());
    if (!VT)
      continue;
    unsigned NumElts = VT->getNumElements();
    if (!isChainMember(Seed, NumElts))
      continue;

    SetVector<Value *> Members;
    collectChain(Seed, NumElts, Members);
    if (Members.empty())
      continue;
    AllProcessed.insert_range(Members);

    // Trigger condition: at least one chain value crosses a block.
    if (!any_of(Members, [](Value *V) {
          return crossesBlock(cast<Instruction>(V));
        }))
      continue;

    // A bare-cmp chain (no joining operation) gains nothing from widening:
    // re-narrowing at the consumers offsets the saved truncation. Require at
    // least one non-cmp instruction (and/or/xor/shuffle/phi) in the chain.
    if (!any_of(Members, [](Value *V) { return !isa<CmpInst>(V); }))
      continue;

    // Pick the widest compare source width as the chain's natural type.
    unsigned MaxSrcWidth = 0;
    for (Value *V : Members)
      if (auto *C = dyn_cast<CmpInst>(V))
        MaxSrcWidth = std::max(MaxSrcWidth, getCmpSourceWidth(C));
    if (MaxSrcWidth <= 1) {
      ++NumChainsSkippedNoCmpWidth;
      continue;
    }

    // Profitability cap: the widened vector must fit in a single NEON
    // register; otherwise type-legalization will split it and any savings
    // are lost.
    if (NumElts * MaxSrcWidth > MaxWidenedVectorSizeBits) {
      ++NumChainsSkippedTooWide;
      continue;
    }

    Type *WideEltTy = IntegerType::get(F.getContext(), MaxSrcWidth);
    LLVM_DEBUG(dbgs() << "AArch64CodeGenPrepare: widening cross-block <"
                      << NumElts << " x i1> chain (" << Members.size()
                      << " members) in @" << F.getName() << " to <" << NumElts
                      << " x i" << MaxSrcWidth << ">\n");
    applyWidening(Members, NumElts, WideEltTy);
    ++NumChainsWidened;
    NumValuesWidened += Members.size();
    Changed = true;
  }

  return Changed;
}

void AArch64CodeGenPrepare::applyWidening(const SetVector<Value *> &Members,
                                          unsigned NumElts, Type *WideEltTy) {
  Type *WideTy = FixedVectorType::get(WideEltTy, NumElts);

  // Map original chain values to their wide replacements (lookup only;
  // iteration uses `Members`'s deterministic order).
  DenseMap<Value *, Value *> Wide;
  // Instructions we created (membership only; never iterated).
  SmallPtrSet<Instruction *, 16> Created;

  // Phase 1: create wide PHI placeholders. PHIs need to exist before any
  // dependent op so back-edge cycles can resolve.
  SmallVector<std::pair<PHINode *, PHINode *>, 8> PhiPairs;
  for (Value *V : Members) {
    if (auto *PN = dyn_cast<PHINode>(V)) {
      PHINode *WidePN = PHINode::Create(WideTy, PN->getNumIncomingValues(),
                                        PN->getName() + ".wide");
      WidePN->insertBefore(PN->getIterator());
      Wide[PN] = WidePN;
      Created.insert(WidePN);
      PhiPairs.push_back({PN, WidePN});
    }
  }

  // Phase 1 cont.: widen producer compares via sext to WideTy. Insert at
  // the def's natural insertion point, mirroring the non-chain-operand path
  // in `getWide` for consistency.
  for (Value *V : Members) {
    if (auto *C = dyn_cast<CmpInst>(V)) {
      auto IP = C->getInsertionPointAfterDef();
      assert(IP && "compare has no insertion point");
      IRBuilder<> B((*IP)->getParent(), *IP);
      Value *Ext = B.CreateSExt(C, WideTy, C->getName() + ".wide");
      Wide[C] = Ext;
      Created.insert(cast<Instruction>(Ext));
    }
  }

  // Helper: get the wide form of an operand. For chain-member operands the
  // caller must have ensured the wide form is in `Wide` (handled by the
  // dependency-ordering loop in Phase 2). For non-chain operands we insert
  // a memoized sext at the operand's def site so the result dominates every
  // chain consumer regardless of which block they live in. Constant
  // operands are folded inline by IRBuilder and don't materialize an
  // instruction.
  auto getWide = [&](Value *Op) -> Value * {
    if (auto It = Wide.find(Op); It != Wide.end())
      return It->second;
    Value *E;
    BasicBlock &Entry = F.getEntryBlock();
    if (auto *I = dyn_cast<Instruction>(Op)) {
      auto IP = I->getInsertionPointAfterDef();
      assert(IP && "non-chain instruction operand has no insertion point");
      // The IP iterator may live in a different block than `I` -- e.g. an
      // invoke's normal-dest, or past PHI/EH-pad nodes -- so derive BB from
      // the iterator itself.
      BasicBlock *IPBB = (*IP)->getParent();
      IRBuilder<> B(IPBB, *IP);
      E = B.CreateSExt(Op, WideTy);
    } else {
      // Argument / constant / poison / undef: insert at function entry so
      // the result dominates every consumer regardless of which block.
      // IRBuilder folds constant operands inline.
      IRBuilder<> B(&Entry, Entry.getFirstInsertionPt());
      E = B.CreateSExt(Op, WideTy);
    }
    Wide[Op] = E;
    if (auto *EI = dyn_cast<Instruction>(E))
      Created.insert(EI);
    return E;
  };

  // Phase 2: widen non-PHI chain ops (and/or/xor, shufflevector). The
  // dependency graph among non-PHI chain ops is acyclic in valid SSA (any
  // cycle must pass through a PHI, which is already in `Wide`), so a fixed-
  // point pass converges in at most `Pending.size()` iterations.
  SmallVector<Value *, 32> Pending;
  for (Value *V : Members) {
    if (Wide.contains(V))
      continue; // PHI / cmp already handled above.
    auto *I = cast<Instruction>(V);
    assert((isa<ShuffleVectorInst>(I) || I->isBitwiseLogicOp()) &&
           "unexpected chain member kind");
    Pending.push_back(I);
  }

  while (!Pending.empty()) {
    bool Progress = false;
    SmallVector<Value *, 32> Next;
    for (Value *V : Pending) {
      auto *I = cast<Instruction>(V);
      if (any_of(I->operands(), [&](Value *Op) {
            return Members.contains(Op) && !Wide.contains(Op);
          })) {
        Next.push_back(V);
        continue;
      }
      Progress = true;

      if (auto *SVI = dyn_cast<ShuffleVectorInst>(I)) {
        IRBuilder<> B(SVI->getNextNode());
        Value *W0 = getWide(SVI->getOperand(0));
        Value *Op1 = SVI->getOperand(1);
        Value *W1 = (isa<PoisonValue>(Op1) || isa<UndefValue>(Op1))
                        ? PoisonValue::get(WideTy)
                        : getWide(Op1);
        Value *Shuf = B.CreateShuffleVector(W0, W1, SVI->getShuffleMask(),
                                            SVI->getName() + ".wide");
        Wide[SVI] = Shuf;
        if (auto *WI = dyn_cast<Instruction>(Shuf))
          Created.insert(WI);
        continue;
      }

      IRBuilder<> B(I->getNextNode());
      Value *W0 = getWide(I->getOperand(0));
      Value *W1 = getWide(I->getOperand(1));
      Value *WideRes = B.CreateBinOp((Instruction::BinaryOps)I->getOpcode(),
                                     W0, W1, I->getName() + ".wide");
      Wide[I] = WideRes;
      if (auto *WI = dyn_cast<Instruction>(WideRes))
        Created.insert(WI);
    }
    Pending = std::move(Next);
    assert((Progress || Pending.empty()) &&
           "fixed-point loop made no progress; chain has unresolvable cycle");
  }

  // Phase 3: fill in PHI incoming values now that all wide forms exist.
  // Non-chain incomings get a memoized sext at the predecessor terminator.
  DenseMap<std::pair<Value *, BasicBlock *>, Value *> SExtCache;
  for (auto &[OrigPN, WidePN] : PhiPairs) {
    for (unsigned i = 0, e = OrigPN->getNumIncomingValues(); i != e; ++i) {
      Value *Inc = OrigPN->getIncomingValue(i);
      BasicBlock *PredBB = OrigPN->getIncomingBlock(i);
      Value *WInc;
      if (auto It = Wide.find(Inc); It != Wide.end()) {
        WInc = It->second;
      } else {
        auto Key = std::make_pair(Inc, PredBB);
        auto SIt = SExtCache.find(Key);
        if (SIt != SExtCache.end()) {
          WInc = SIt->second;
        } else {
          IRBuilder<> B(PredBB->getTerminator());
          WInc = B.CreateSExt(Inc, WideTy);
          SExtCache[Key] = WInc;
          if (auto *I = dyn_cast<Instruction>(WInc))
            Created.insert(I);
        }
      }
      WidePN->addIncoming(WInc, PredBB);
    }
  }

  // Phase 4: re-narrow external uses (icmp slt 0). Iterate `Members`
  // (deterministic order) rather than `Wide`. Each chain value has a
  // unique wide form, so a single Narrow per Orig suffices; no cache.
  for (Value *Orig : Members) {
    auto It = Wide.find(Orig);
    if (It == Wide.end())
      continue;
    Value *W = It->second;

    SmallVector<Use *, 8> ExtUses;
    for (Use &U : Orig->uses()) {
      auto *User = dyn_cast<Instruction>(U.getUser());
      if (!User)
        continue;
      if (Created.contains(User))
        continue;
      if (Wide.contains(User))
        continue;
      ExtUses.push_back(&U);
    }
    if (ExtUses.empty())
      continue;

    // Wide values are always Instructions (PHI placeholders or instructions
    // we created). For PHIs insert after the prologue; for any other
    // instruction insert immediately after it. Either point dominates the
    // original's users because they were already dominated by the
    // original's def.
    auto *WI = cast<Instruction>(W);
    BasicBlock::iterator InsertIt = isa<PHINode>(WI)
                                        ? WI->getParent()->getFirstNonPHIIt()
                                        : std::next(WI->getIterator());
    IRBuilder<> B(InsertIt->getParent(), InsertIt);
    Value *Narrow = B.CreateICmpSLT(W, Constant::getNullValue(WideTy),
                                    Orig->getName() + ".narrow");
    if (auto *I = dyn_cast<Instruction>(Narrow))
      Created.insert(I);
    for (Use *U : ExtUses)
      U->set(Narrow);
  }

  // Phase 5: erase dead originals. Producer compares stay alive: their
  // Wide form is sext(cmp), which still references the original. The
  // remaining chain members (and/or/xor, shufflevector, phi) are dead in
  // the visible program after Phase 4 -- their only remaining users are
  // other chain members of the same kind, forming an internal cycle.
  // Drop those references via RAUW(poison) and erase.
  for (Value *Orig : Members) {
    if (isa<CmpInst>(Orig))
      continue;
    auto *OI = dyn_cast<Instruction>(Orig);
    if (!OI || Created.contains(OI))
      continue;
    OI->replaceAllUsesWith(PoisonValue::get(OI->getType()));
  }
  for (Value *Orig : Members) {
    if (isa<CmpInst>(Orig))
      continue;
    auto *OI = dyn_cast<Instruction>(Orig);
    if (OI && !Created.contains(OI))
      OI->eraseFromParent();
  }
}

namespace {
class AArch64CodeGenPrepareLegacyPass : public FunctionPass {
public:
  static char ID;
  AArch64CodeGenPrepareLegacyPass() : FunctionPass(ID) {}
  bool runOnFunction(Function &F) override {
    if (skipFunction(F))
      return false;
    return AArch64CodeGenPrepare(F).run();
  }
  StringRef getPassName() const override { return PASS_NAME; }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
  }
};
} // namespace

INITIALIZE_PASS(AArch64CodeGenPrepareLegacyPass, DEBUG_TYPE, PASS_NAME, false,
                false)

char AArch64CodeGenPrepareLegacyPass::ID = 0;

FunctionPass *llvm::createAArch64CodeGenPrepareLegacyPass() {
  return new AArch64CodeGenPrepareLegacyPass();
}

PreservedAnalyses AArch64CodeGenPreparePass::run(Function &F,
                                                 FunctionAnalysisManager &) {
  if (F.hasOptNone())
    return PreservedAnalyses::all();
  bool Changed = AArch64CodeGenPrepare(F).run();
  if (!Changed)
    return PreservedAnalyses::all();
  PreservedAnalyses PA = PreservedAnalyses::none();
  PA.preserveSet<CFGAnalyses>();
  return PA;
}
