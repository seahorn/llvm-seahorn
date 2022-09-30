//===- LoopUnroll.cpp - Loop unroller pass --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass implements a simple loop unroller.  It works best when loops have
// been canonicalized by the -indvars pass, allowing it to determine the trip
// counts of loops easily.
//===----------------------------------------------------------------------===//

// AG: Port of LoopUnroll from LLVM to give seahorn control over unrolling for
// BMC

#include "llvm/Transforms/Scalar/LoopUnrollPass.h"
#include "llvm_seahorn/InitializePasses.h"
#include "llvm_seahorn/Transforms/Scalar.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/LazyBlockFrequencyInfo.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/LoopUnrollAnalyzer.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/LoopPeel.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/SizeOpts.h"
#include "llvm/Transforms/Utils/UnrollLoop.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <string>
#include <tuple>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "sea-loop-unroll"

static LoopUnrollResult
tryToUnrollLoop(Loop *L, DominatorTree &DT, LoopInfo *LI, ScalarEvolution &SE,
                const TargetTransformInfo &TTI, AssumptionCache &AC,
                OptimizationRemarkEmitter &ORE, BlockFrequencyInfo *BFI,
                ProfileSummaryInfo *PSI, bool PreserveLCSSA, int OptLevel,
                bool ForgetAllSCEV, Optional<unsigned> ProvidedCount,
                Optional<bool> ProvidedUpperBound,
                Optional<bool> ProvidedAllowPeeling,
                Optional<unsigned> ProvidedFullUnrollMaxCount) {
  LLVM_DEBUG(dbgs() << "Loop Unroll: F["
                    << L->getHeader()->getParent()->getName() << "] Loop %"
                    << L->getHeader()->getName() << "\n");
  TransformationMode TM = hasUnrollTransformation(L);
  if (TM & TM_Disable)
    return LoopUnrollResult::Unmodified;
  if (!L->isLoopSimplifyForm()) {
    LLVM_DEBUG(
        dbgs() << "  Not unrolling loop which is not in loop-simplify form.\n");
    return LoopUnrollResult::Unmodified;
  }

  bool OptForSize = L->getHeader()->getParent()->hasOptSize();
  unsigned NumInlineCandidates;
  bool NotDuplicatable;
  bool Convergent;
  TargetTransformInfo::UnrollingPreferences UP = gatherUnrollingPreferences(
      L, SE, TTI, BFI, PSI, OptLevel,
      std::numeric_limits<unsigned>::max() /* Threshold*/, ProvidedCount,
      false /* UserAllowPartial */, false /* UserRuntime */, ProvidedUpperBound,
      ProvidedFullUnrollMaxCount);
  TargetTransformInfo::PeelingPreferences PP =
      gatherPeelingPreferences(L, SE, TTI, ProvidedAllowPeeling,
                               false /* UserAllowProfileBasedPeeling */,
                               false /* UnrollingSpecficValues */);

  // Exit early if unrolling is disabled. For OptForSize, we pick the loop size
  // as threshold later on.
  if (UP.Threshold == 0 && (!UP.Partial || UP.PartialThreshold == 0) &&
      !OptForSize)
    return LoopUnrollResult::Unmodified;

  SmallPtrSet<const Value *, 32> EphValues;
  CodeMetrics::collectEphemeralValues(L, &AC, EphValues);

  unsigned LoopSize =
      ApproximateLoopSize(L, NumInlineCandidates, NotDuplicatable, Convergent,
                          TTI, EphValues, UP.BEInsns);
  if (NotDuplicatable) {
    LLVM_DEBUG(dbgs() << "  Not unrolling loop which contains non-duplicatable"
                      << " instructions.\n");
    return LoopUnrollResult::Unmodified;
  }

  if (NumInlineCandidates != 0) {
    LLVM_DEBUG(dbgs() << "  Not unrolling loop with inlinable calls.\n");
    return LoopUnrollResult::Unmodified;
  }

  // Find the smallest exact trip count for any exit. This is an upper bound
  // on the loop trip count, but an exit at an earlier iteration is still
  // possible. An unroll by the smallest exact trip count guarantees that all
  // brnaches relating to at least one exit can be eliminated. This is unlike
  // the max trip count, which only guarantees that the backedge can be broken.
  unsigned TripCount = 0;
  unsigned TripMultiple = 1;
  SmallVector<BasicBlock *, 8> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);
  for (BasicBlock *ExitingBlock : ExitingBlocks)
    if (unsigned TC = SE.getSmallConstantTripCount(L, ExitingBlock))
      if (!TripCount || TC < TripCount)
        TripCount = TripMultiple = TC;

  if (!TripCount) {
    // If no exact trip count is known, determine the trip multiple of either
    // the loop latch or the single exiting block.
    // TODO: Relax for multiple exits.
  BasicBlock *ExitingBlock = L->getLoopLatch();
  if (!ExitingBlock || !L->isLoopExiting(ExitingBlock))
    ExitingBlock = L->getExitingBlock();
  if (ExitingBlock)
    TripMultiple = SE.getSmallConstantTripMultiple(L, ExitingBlock);
  }

  // If the loop contains a convergent operation, the prelude we'd add
  // to do the first few instructions before we hit the unrolled loop
  // is unsafe -- it adds a control-flow dependency to the convergent
  // operation.  Therefore restrict remainder loop (try unrolling without).
  //
  // TODO: This is quite conservative.  In practice, convergent_op()
  // is likely to be called unconditionally in the loop.  In this
  // case, the program would be ill-formed (on most architectures)
  // unless n were the same on all threads in a thread group.
  // Assuming n is the same on all threads, any kind of unrolling is
  // safe.  But currently llvm's notion of convergence isn't powerful
  // enough to express this.
  if (Convergent)
    UP.AllowRemainder = false;

  // Try to find the trip count upper bound if we cannot find the exact trip
  // count.
  unsigned MaxTripCount = 0;
  bool MaxOrZero = false;
  if (!TripCount) {
    MaxTripCount = SE.getSmallConstantMaxTripCount(L);
    MaxOrZero = SE.isBackedgeTakenCountMaxOrZero(L);
  }

  // computeUnrollCount() decides whether it is beneficial to use upper bound to
  // fully unroll the loop.
  bool UseUpperBound = false;
  bool IsCountSetExplicitly = computeUnrollCount(
      L, TTI, DT, LI, SE, EphValues, &ORE, TripCount, MaxTripCount, MaxOrZero,
      TripMultiple, LoopSize, UP, PP, UseUpperBound);
  if (!UP.Count) {
    LLVM_DEBUG(dbgs() << "  Not unrolling loop without known unroll count\n");
    return LoopUnrollResult::Unmodified;
  }

  if (PP.PeelCount) {
    assert(UP.Count == 1 && "Cannot perform peel and unroll in the same step");
    LLVM_DEBUG(dbgs() << "PEELING loop %" << L->getHeader()->getName()
                      << " with iteration count " << PP.PeelCount << "!\n");
    ORE.emit([&]() {
      return OptimizationRemark(DEBUG_TYPE, "Peeled", L->getStartLoc(),
                                L->getHeader())
             << " peeled loop by " << ore::NV("PeelCount", PP.PeelCount)
             << " iterations";
    });

    if (peelLoop(L, PP.PeelCount, LI, &SE, &DT, &AC, PreserveLCSSA)) {
      simplifyLoopAfterUnroll(L, true, LI, &SE, &DT, &AC, &TTI);
      // If the loop was peeled, we already "used up" the profile information
      // we had, so we don't want to unroll or peel again.
      if (PP.PeelProfiledIterations)
        L->setLoopAlreadyUnrolled();
      return LoopUnrollResult::PartiallyUnrolled;
    }
    return LoopUnrollResult::Unmodified;
  }
  // At this point, UP.Runtime indicates that run-time unrolling is allowed.
  // However, we only want to actually perform it if we don't know the trip
  // count and the unroll count doesn't divide the known trip multiple.
  // TODO: This decision should probably be pushed up into
  // computeUnrollCount().
  UP.Runtime &= TripCount == 0 && TripMultiple % UP.Count != 0;

  // Save loop properties before it is transformed.
  MDNode *OrigLoopID = L->getLoopID();

  // Unroll the loop.
  Loop *RemainderLoop = nullptr;
  LoopUnrollResult UnrollResult = UnrollLoop(
      L,
      {UP.Count, UP.Force, UP.Runtime, UP.AllowExpensiveTripCount,
       UP.UnrollRemainder, ForgetAllSCEV},
      LI, &SE, &DT, &AC, &TTI, &ORE, PreserveLCSSA, &RemainderLoop);
  if (UnrollResult == LoopUnrollResult::Unmodified)
    return LoopUnrollResult::Unmodified;

  if (RemainderLoop) {
    Optional<MDNode *> RemainderLoopID =
        makeFollowupLoopID(OrigLoopID, {LLVMLoopUnrollFollowupAll,
                                        LLVMLoopUnrollFollowupRemainder});
    if (RemainderLoopID.hasValue())
      RemainderLoop->setLoopID(RemainderLoopID.getValue());
  }

  if (UnrollResult != LoopUnrollResult::FullyUnrolled) {
    Optional<MDNode *> NewLoopID =
        makeFollowupLoopID(OrigLoopID, {LLVMLoopUnrollFollowupAll,
                                        LLVMLoopUnrollFollowupUnrolled});
    if (NewLoopID.hasValue()) {
      L->setLoopID(NewLoopID.getValue());

      // Do not setLoopAlreadyUnrolled if loop attributes have been specified
      // explicitly.
      return UnrollResult;
    }
  }

  // If loop has an unroll count pragma or unrolled by explicitly set count
  // mark loop as unrolled to prevent unrolling beyond that requested.
  if (UnrollResult != LoopUnrollResult::FullyUnrolled && IsCountSetExplicitly)
    L->setLoopAlreadyUnrolled();

  return UnrollResult;
}

namespace {

class SeaLoopUnroll : public LoopPass {
public:
  static char ID; // Pass ID, replacement for typeid

  int OptLevel;

  /// If false, when SCEV is invalidated, only forget everything in the
  /// top-most loop (call forgetTopMostLoop), of the loop being processed.
  /// Otherwise, forgetAllLoops and rebuild when needed next.
  bool ForgetAllSCEV;

  Optional<unsigned> ProvidedCount;
  Optional<bool> ProvidedAllowPartial;
  Optional<bool> ProvidedUpperBound;
  Optional<bool> ProvidedAllowPeeling;
  Optional<unsigned> ProvidedFullUnrollMaxCount;

  SeaLoopUnroll(int OptLevel = 2, bool ForgetAllSCEV = false,
                Optional<unsigned> Count = None,
                Optional<bool> UpperBound = None,
                Optional<bool> AllowPeeling = None,
                Optional<unsigned> ProvidedFullUnrollMaxCount = None)
      : LoopPass(ID), OptLevel(OptLevel), ForgetAllSCEV(ForgetAllSCEV),
        ProvidedCount(std::move(Count)), ProvidedUpperBound(UpperBound),
        ProvidedAllowPeeling(AllowPeeling),
        ProvidedFullUnrollMaxCount(ProvidedFullUnrollMaxCount) {
    initializeSeaLoopUnrollPass(*PassRegistry::getPassRegistry());
  }

  bool runOnLoop(Loop *L, LPPassManager &LPM) override {
    if (skipLoop(L))
      return false;

    Function &F = *L->getHeader()->getParent();

    auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    LoopInfo *LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    ScalarEvolution &SE = getAnalysis<ScalarEvolutionWrapperPass>().getSE();
    const TargetTransformInfo &TTI =
        getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
    auto &AC = getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);
    // For the old PM, we can't use OptimizationRemarkEmitter as an analysis
    // pass.  Function analyses need to be preserved across loop
    // transformations but ORE cannot be preserved (see comment before the
    // pass definition).
    OptimizationRemarkEmitter ORE(&F);
    bool PreserveLCSSA = mustPreserveAnalysisID(LCSSAID);

    LoopUnrollResult Result = tryToUnrollLoop(
        L, DT, LI, SE, TTI, AC, ORE, nullptr, nullptr, PreserveLCSSA, OptLevel,
        ForgetAllSCEV, ProvidedCount, ProvidedUpperBound, ProvidedAllowPeeling,
        ProvidedFullUnrollMaxCount);

    if (Result == LoopUnrollResult::FullyUnrolled)
      LPM.markLoopAsDeleted(*L);

    return Result != LoopUnrollResult::Unmodified;
  }

  /// This transformation requires natural loop information & requires that
  /// loop preheaders be inserted into the CFG...
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
    // FIXME: Loop passes are required to preserve domtree, and for now we
    // just recreate dom info if anything gets unrolled.
    getLoopAnalysisUsage(AU);
  }
};
} // namespace

char SeaLoopUnroll::ID = 0;

INITIALIZE_PASS_BEGIN(SeaLoopUnroll, "sea-loop-unroll", "Sea Unroll loops",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(LoopPass)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_END(SeaLoopUnroll, "sea-loop-unroll", "Sea Unroll loops", false,
                    false)

Pass *llvm_seahorn::createSeaLoopUnrollPass(int OptLevel, bool ForgetAllSCEV,
                                            int Count, int AllowPartial,
                                            int UpperBound, int AllowPeeling) {
  // TODO: It would make more sense for this function to take the optionals
  // directly, but that's dangerous since it would silently break out of tree
  // callers.
  return new SeaLoopUnroll(
      OptLevel, ForgetAllSCEV, Count == -1 ? None : Optional<unsigned>(Count),
      UpperBound == -1 ? None : Optional<bool>(UpperBound),
      AllowPeeling == -1 ? None : Optional<bool>(AllowPeeling));
}
