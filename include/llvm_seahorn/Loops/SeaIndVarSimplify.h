//===- IndVarSimplify.h - Induction Variable Simplification -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides the interface for the Induction Variable
// Simplification pass.
//
//===----------------------------------------------------------------------===//

#ifndef SEA_LLVM_TRANSFORMS_SCALAR_INDVARSIMPLIFY_H
#define SEA_LLVM_TRANSFORMS_SCALAR_INDVARSIMPLIFY_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class Loop;
class LPMUpdater;

class SeaIndVarSimplifyPass : public PassInfoMixin<SeaIndVarSimplifyPass> {
public:
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);
};

} // end namespace llvm

#endif // SEA_LLVM_TRANSFORMS_SCALAR_INDVARSIMPLIFY_H
