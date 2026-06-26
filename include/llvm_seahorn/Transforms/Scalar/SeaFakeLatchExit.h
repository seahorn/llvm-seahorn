//===- SeaFakeLatchExit.h - Fake latch exit (new PM) ------------*- C++ -*-===//
//
// SeaHorn pass: give every loop whose latch ends in an *unconditional* branch a
// fake, statically-always-taken exit edge. The latch branch
//   latch: br label %succ
// becomes
//   latch: br i1 true, label %succ, label %fake_latch_exit
//   fake_latch_exit: unreachable
// so the loop gains a structural exit block. Run it LAST in a pipeline:
// simplifycfg/instcombine fold `br i1 true` back to an unconditional branch.
//
//===----------------------------------------------------------------------===//

#ifndef SEA_LLVM_TRANSFORMS_SCALAR_SEAFAKELATCHEXIT_H
#define SEA_LLVM_TRANSFORMS_SCALAR_SEAFAKELATCHEXIT_H

#include "llvm/IR/PassManager.h"

namespace llvm_seahorn {

class SeaFakeLatchExitPass
    : public llvm::PassInfoMixin<SeaFakeLatchExitPass> {
public:
  llvm::PreservedAnalyses run(llvm::Function &F,
                              llvm::FunctionAnalysisManager &AM);
  static llvm::StringRef name() { return "SeaFakeLatchExitPass"; }
};

} // namespace llvm_seahorn

#endif // SEA_LLVM_TRANSFORMS_SCALAR_SEAFAKELATCHEXIT_H
