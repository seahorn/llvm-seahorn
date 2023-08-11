//===- LoopExtractor.cpp - Extract each loop into a new function ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A pass wrapper around the ExtractLoop() scalar transformation to extract each
// top-level loop into its own new function. If the loop is the ONLY loop in a
// given function, it is not touched. This is a pass most useful for debugging
// via bugpoint.
//
//===----------------------------------------------------------------------===//

#include "llvm_seahorn/InitializePasses.h"
#include "llvm_seahorn/Transforms/IPO.h"
#include "llvm_seahorn/Transforms/IPO/SeaLoopExtractor.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"
#include <fstream>
#include <set>

using namespace llvm;

#define DEBUG_TYPE "sea-loop-extract"

STATISTIC(NumExtracted, "Number of loops extracted");

DenseMap<const Type *, Function *> m_ndfn;

// TODO: extract into common library for nondet pass and this code
Function &createNewNondetFn(Module &m, Type &type, unsigned num,
                            std::string prefix) {
  std::string name;
  unsigned c = num;

  do
    name = prefix + std::to_string(c++);
  while (m.getNamedValue(name));
  Function *res =
      dyn_cast<Function>(m.getOrInsertFunction(name, &type).getCallee());
  assert(res);
  return *res;
}

// TODO: extract into common library for nondet pass and this code
Function *getNondetFn(Type *type, Module *m) {
  auto it = m_ndfn.find(type);
  if (it != m_ndfn.end()) {
    return it->second;
  }

  Function *res =
      &createNewNondetFn(*m, *type, m_ndfn.size(), "verifier.nondet.");
  m_ndfn[type] = res;
  return res;
}

// Replace the given function body with code that stores ND values
// in output args.
void replaceFnBodyWithND(Function *oldfn, SetVector<Value *> &inputs,
                         SetVector<Value *> &outputs) {
  Function *TheFunction = oldfn;
  auto ret_ty = TheFunction->getReturnType();
  TheFunction->dropAllReferences(); // delete body of function

  BasicBlock *BB =
      BasicBlock::Create(TheFunction->getContext(), "entry", TheFunction);
  IRBuilder<> Builder(TheFunction->getContext());

  Builder.SetInsertPoint(BB);

  // store nd values in output args
  // ASSUME: CodeRegionExtractor creates function formal arg list in the order:
  // fn(IN_0, IN_1, ..., IN_N, OUT_0, OUT_1, ..., OUT_M)
  for (auto i = inputs.size(); i < inputs.size() + outputs.size(); i++) {
    // ASSUME: type is pointer
    // TODO: remove use of deprecated getPointerElementType
    auto nd_val = Builder.CreateCall(
        getNondetFn(TheFunction->getArg(i)->getType()->getPointerElementType(),
                    TheFunction->getParent()));
    Builder.CreateStore(nd_val, TheFunction->getArg(i));
  }

  // set return value to nd
  auto nd_retval =
      Builder.CreateCall(getNondetFn(ret_ty, TheFunction->getParent()));
  Builder.CreateRet(nd_retval);
  verifyFunction(*TheFunction);
}

namespace {
struct SeaLoopExtractorLegacyPass : public ModulePass {
  static char ID; // Pass identification, replacement for typeid

  unsigned NumLoops;

  explicit SeaLoopExtractorLegacyPass(unsigned NumLoops = ~0)
      : ModulePass(ID), NumLoops(NumLoops) {
    initializeSeaLoopExtractorLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnModule(Module &M) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequiredID(BreakCriticalEdgesID);
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addPreserved<LoopInfoWrapperPass>();
    AU.addRequiredID(LoopSimplifyID);
    AU.addUsedIfAvailable<AssumptionCacheTracker>();
  }
};

struct SeaLoopExtractor {
  explicit SeaLoopExtractor(
      unsigned NumLoops,
      function_ref<DominatorTree &(Function &)> LookupDomTree,
      function_ref<LoopInfo &(Function &)> LookupLoopInfo,
      function_ref<AssumptionCache *(Function &)> LookupAssumptionCache)
      : NumLoops(NumLoops), LookupDomTree(LookupDomTree),
        LookupLoopInfo(LookupLoopInfo),
        LookupAssumptionCache(LookupAssumptionCache) {}
  bool runOnModule(Module &M);

private:
  // The number of natural loops to extract from the program into functions.
  unsigned NumLoops;

  function_ref<DominatorTree &(Function &)> LookupDomTree;
  function_ref<LoopInfo &(Function &)> LookupLoopInfo;
  function_ref<AssumptionCache *(Function &)> LookupAssumptionCache;

  bool runOnFunction(Function &F);

  bool extractLoops(Loop::iterator From, Loop::iterator To, LoopInfo &LI,
                    DominatorTree &DT);
  bool extractLoop(Loop *L, LoopInfo &LI, DominatorTree &DT);
};
} // namespace

char SeaLoopExtractorLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(SeaLoopExtractorLegacyPass, "sea-loop-extract",
                      "Extract loops into new functions", false, false)
INITIALIZE_PASS_DEPENDENCY(BreakCriticalEdges)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopSimplify)
INITIALIZE_PASS_END(SeaLoopExtractorLegacyPass, "sea-loop-extract",
                    "Extract loops into new functions", false, false)

namespace {
/// SingleLoopExtractor - For bugpoint.
struct SeaSingleLoopExtractor : public SeaLoopExtractorLegacyPass {
  static char ID; // Pass identification, replacement for typeid
  SeaSingleLoopExtractor() : SeaLoopExtractorLegacyPass(1) {}
};
} // End anonymous namespace

char SeaSingleLoopExtractor::ID = 0;
INITIALIZE_PASS(SeaSingleLoopExtractor, "sea-loop-extract-single",
                "Extract at most one loop into a new function", false, false)

// createLoopExtractorPass - This pass extracts all natural loops from the
// program into a function if it can.
//
ModulePass *llvm_seahorn::createSeaLoopExtractorPass() {
  return new SeaLoopExtractorLegacyPass();
}

bool SeaLoopExtractorLegacyPass::runOnModule(Module &M) {
  if (skipModule(M))
    return false;

  bool Changed = false;
  auto LookupDomTree = [this](Function &F) -> DominatorTree & {
    return this->getAnalysis<DominatorTreeWrapperPass>(F).getDomTree();
  };
  auto LookupLoopInfo = [this, &Changed](Function &F) -> LoopInfo & {
    return this->getAnalysis<LoopInfoWrapperPass>(F, &Changed).getLoopInfo();
  };
  auto LookupACT = [this](Function &F) -> AssumptionCache * {
    if (auto *ACT = this->getAnalysisIfAvailable<AssumptionCacheTracker>())
      return ACT->lookupAssumptionCache(F);
    return nullptr;
  };
  return SeaLoopExtractor(NumLoops, LookupDomTree, LookupLoopInfo, LookupACT)
             .runOnModule(M) ||
         Changed;
}

bool SeaLoopExtractor::runOnModule(Module &M) {
  if (M.empty())
    return false;

  if (!NumLoops)
    return false;

  bool Changed = false;

  // The end of the function list may change (new functions will be added at the
  // end), so we run from the first to the current last.
  auto I = M.begin(), E = --M.end();
  while (true) {
    Function &F = *I;

    Changed |= runOnFunction(F);
    if (!NumLoops)
      break;

    // If this is the last function.
    if (I == E)
      break;

    ++I;
  }
  return Changed;
}

bool SeaLoopExtractor::runOnFunction(Function &F) {
  // Do not modify `optnone` functions.
  if (F.hasOptNone())
    return false;

  if (F.empty())
    return false;

  bool Changed = false;
  LoopInfo &LI = LookupLoopInfo(F);

  // If there are no loops in the function.
  if (LI.empty())
    return Changed;

  DominatorTree &DT = LookupDomTree(F);

  // If there is more than one top-level loop in this function, extract all of
  // the loops.
  if (std::next(LI.begin()) != LI.end())
    return Changed | extractLoops(LI.begin(), LI.end(), LI, DT);

  // Otherwise there is exactly one top-level loop.
  Loop *TLL = *LI.begin();

  // If the loop is in LoopSimplify form, then extract it only if this function
  // is more than a minimal wrapper around the loop.
  if (TLL->isLoopSimplifyForm()) {
    bool ShouldExtractLoop = false;

    // Extract the loop if the entry block doesn't branch to the loop header.
    Instruction *EntryTI = F.getEntryBlock().getTerminator();
    if (!isa<BranchInst>(EntryTI) ||
        !cast<BranchInst>(EntryTI)->isUnconditional() ||
        EntryTI->getSuccessor(0) != TLL->getHeader()) {
      ShouldExtractLoop = true;
    } else {
      // Check to see if any exits from the loop are more than just return
      // blocks.
      SmallVector<BasicBlock *, 8> ExitBlocks;
      TLL->getExitBlocks(ExitBlocks);
      for (auto *ExitBlock : ExitBlocks)
        if (!isa<ReturnInst>(ExitBlock->getTerminator())) {
          ShouldExtractLoop = true;
          break;
        }
    }

    if (ShouldExtractLoop)
      return Changed | extractLoop(TLL, LI, DT);
  }

  // Okay, this function is a minimal container around the specified loop.
  // If we extract the loop, we will continue to just keep extracting it
  // infinitely... so don't extract it. However, if the loop contains any
  // sub-loops, extract them.
  return Changed | extractLoops(TLL->begin(), TLL->end(), LI, DT);
}

bool SeaLoopExtractor::extractLoops(Loop::iterator From, Loop::iterator To,
                                    LoopInfo &LI, DominatorTree &DT) {
  bool Changed = false;
  SmallVector<Loop *, 8> Loops;

  // Save the list of loops, as it may change.
  Loops.assign(From, To);
  for (Loop *L : Loops) {
    // If LoopSimplify form is not available, stay out of trouble.
    if (!L->isLoopSimplifyForm())
      continue;

    Changed |= extractLoop(L, LI, DT);
    if (!NumLoops)
      break;
  }
  return Changed;
}

bool SeaLoopExtractor::extractLoop(Loop *L, LoopInfo &LI, DominatorTree &DT) {
  assert(NumLoops != 0);
  Function &Func = *L->getHeader()->getParent();
  AssumptionCache *AC = LookupAssumptionCache(Func);
  CodeExtractorAnalysisCache CEAC(Func);
  CodeExtractor Extractor(DT, *L, false, nullptr, nullptr, AC);
  SetVector<Value *> inputs, outputs;
  auto *newFunction = Extractor.extractCodeRegion(CEAC, inputs, outputs);
  if (newFunction) {
    LI.erase(L);
    --NumLoops;
    ++NumExtracted;
    replaceFnBodyWithND(newFunction, inputs, outputs);
    return true;
  }
  return false;
}

// createSingleLoopExtractorPass - This pass extracts one natural loop from the
// program into a function if it can.  This is used by bugpoint.
//
ModulePass *llvm_seahorn::createSeaSingleLoopExtractorPass() {
  return new SeaSingleLoopExtractor();
}

PreservedAnalyses SeaLoopExtractorPass::run(Module &M,
                                            ModuleAnalysisManager &AM) {
  auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  auto LookupDomTree = [&FAM](Function &F) -> DominatorTree & {
    return FAM.getResult<DominatorTreeAnalysis>(F);
  };
  auto LookupLoopInfo = [&FAM](Function &F) -> LoopInfo & {
    return FAM.getResult<LoopAnalysis>(F);
  };
  auto LookupAssumptionCache = [&FAM](Function &F) -> AssumptionCache * {
    return FAM.getCachedResult<AssumptionAnalysis>(F);
  };
  if (!SeaLoopExtractor(NumLoops, LookupDomTree, LookupLoopInfo,
                        LookupAssumptionCache)
           .runOnModule(M))
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserve<LoopAnalysis>();
  return PA;
}

void SeaLoopExtractorPass::printPipeline(
    raw_ostream &OS, function_ref<StringRef(StringRef)> MapClassName2PassName) {
  static_cast<PassInfoMixin<SeaLoopExtractorPass> *>(this)->printPipeline(
      OS, MapClassName2PassName);
  OS << "<";
  if (NumLoops == 1)
    OS << "single";
  OS << ">";
}
