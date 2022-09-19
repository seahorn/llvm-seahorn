//===- LoopRotation.cpp - Loop Rotation Pass ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements Loop Rotation Pass.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LoopRotation.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm_seahorn/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Utils/LoopRotationUtils.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm_seahorn/Transforms/Scalar.h"
#include <limits>
using namespace llvm;

#define DEBUG_TYPE "sea-loop-rotate"

static cl::opt<unsigned> DefaultRotationThreshold(
    "sea-rotation-max-header-size",
    cl::init(std::numeric_limits<unsigned>::max()), cl::Hidden,
    cl::desc("The default maximum header size for automatic loop rotation"));

static cl::opt<bool> PrepareForLTOOption(
    "sea-rotation-prepare-for-lto", cl::init(false), cl::Hidden,
    cl::desc("Run loop-rotation in the prepare-for-lto stage. This option "
             "should be used for testing only."));

#if 0 /* SEAHORN DISABLE */
LoopRotatePass::LoopRotatePass(bool EnableHeaderDuplication, bool PrepareForLTO)
    : EnableHeaderDuplication(EnableHeaderDuplication),
      PrepareForLTO(PrepareForLTO) {}

PreservedAnalyses LoopRotatePass::run(Loop &L, LoopAnalysisManager &AM,
                                      LoopStandardAnalysisResults &AR,
                                      LPMUpdater &) {
  // Vectorization requires loop-rotation. Use default threshold for loops the
  // user explicitly marked for vectorization, even when header duplication is
  // disabled.
  int Threshold = EnableHeaderDuplication ||
                          hasVectorizeTransformation(&L) == TM_ForcedByUser
                      ? DefaultRotationThreshold
                      : 0;
  const DataLayout &DL = L.getHeader()->getModule()->getDataLayout();
  const SimplifyQuery SQ = getBestSimplifyQuery(AR, DL);

  Optional<MemorySSAUpdater> MSSAU;
  if (AR.MSSA)
    MSSAU = MemorySSAUpdater(AR.MSSA);
  bool Changed =
      LoopRotation(&L, &AR.LI, &AR.TTI, &AR.AC, &AR.DT, &AR.SE,
                   MSSAU.hasValue() ? MSSAU.getPointer() : nullptr, SQ, false,
                   Threshold, false, PrepareForLTO || PrepareForLTOOption);

  if (!Changed)
    return PreservedAnalyses::all();

  if (AR.MSSA && VerifyMemorySSA)
    AR.MSSA->verifyMemorySSA();

  auto PA = getLoopPassPreservedAnalyses();
  if (AR.MSSA)
    PA.preserve<MemorySSAAnalysis>();
  return PA;
}

#endif

namespace {

class SeaLoopRotateLegacyPass : public LoopPass {
  unsigned MaxHeaderSize;
  bool PrepareForLTO;

public:
  static char ID; // Pass ID, replacement for typeid
  SeaLoopRotateLegacyPass(int SpecifiedMaxHeaderSize = -1,
                       bool PrepareForLTO = false)
      : LoopPass(ID), PrepareForLTO(PrepareForLTO) {
    initializeSeaLoopRotateLegacyPassPass(*PassRegistry::getPassRegistry());
    if (SpecifiedMaxHeaderSize == -1)
      MaxHeaderSize = DefaultRotationThreshold;
    else
      MaxHeaderSize = unsigned(SpecifiedMaxHeaderSize);
  }

  // LCSSA form makes instruction renaming easier.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
    if (EnableMSSALoopDependency)
      AU.addPreserved<MemorySSAWrapperPass>();
    getLoopAnalysisUsage(AU);
  }

  bool runOnLoop(Loop *L, LPPassManager &LPM) override {
    if (skipLoop(L))
      return false;
    Function &F = *L->getHeader()->getParent();

    auto *LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    const auto *TTI = &getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
    auto *AC = &getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);
    auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    auto &SE = getAnalysis<ScalarEvolutionWrapperPass>().getSE();
    const SimplifyQuery SQ = getBestSimplifyQuery(*this, F);
    Optional<MemorySSAUpdater> MSSAU;
    if (EnableMSSALoopDependency) {
      // Not requiring MemorySSA and getting it only if available will split
      // the loop pass pipeline when LoopRotate is being run first.
      auto *MSSAA = getAnalysisIfAvailable<MemorySSAWrapperPass>();
      if (MSSAA)
        MSSAU = MemorySSAUpdater(&MSSAA->getMSSA());
    }
    // Vectorization requires loop-rotation. Use default threshold for loops the
    // user explicitly marked for vectorization, even when header duplication is
    // disabled.
    int Threshold = hasVectorizeTransformation(L) == TM_ForcedByUser
                        ? DefaultRotationThreshold
                        : MaxHeaderSize;

    return LoopRotation(L, LI, TTI, AC, &DT, &SE,
                        MSSAU.hasValue() ? MSSAU.getPointer() : nullptr, SQ,
                        false, Threshold, false,
                        PrepareForLTO || PrepareForLTOOption);
  }
};
} // namespace

char SeaLoopRotateLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(SeaLoopRotateLegacyPass, "sea-loop-rotate", "Rotate Loops",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(LoopPass)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MemorySSAWrapperPass)
INITIALIZE_PASS_END(SeaLoopRotateLegacyPass, "sea-loop-rotate", "Rotate Loops",
                    false, false)

Pass *llvm_seahorn::createLoopRotatePass(int MaxHeaderSize, bool PrepareForLTO) {
  return new SeaLoopRotateLegacyPass(MaxHeaderSize, PrepareForLTO);
}