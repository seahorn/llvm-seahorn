//===- InstCombine.h - InstCombine pass -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides the primary interface to the instcombine pass. This pass
/// is suitable for use in the new pass manager. For a pass that works with the
/// legacy pass manager, please look for \c createInstructionCombiningPass() in
/// Scalar.h.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SEAHORN_TRANSFORMS_INSTCOMBINE_INSTCOMBINE_H
#define LLVM_SEAHORN_TRANSFORMS_INSTCOMBINE_INSTCOMBINE_H

#include "InstCombineWorklist.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"


namespace llvm_seahorn {

class InstCombinePass : public llvm::PassInfoMixin<InstCombinePass> {
  InstCombineWorklist Worklist;
  bool ExpensiveCombines;

public:
  static llvm::StringRef name() { return "InstCombinePass"; }

  explicit InstCombinePass(bool ExpensiveCombines = true)
      : ExpensiveCombines(ExpensiveCombines) {}

  llvm::PreservedAnalyses run(llvm::Function &F, llvm::FunctionAnalysisManager &AM);
};

/// \brief The legacy pass manager's instcombine pass.
///
/// This is a basic whole-function wrapper around the instcombine utility. It
/// will try to combine all instructions in the function.
class SeaInstructionCombiningPass : public llvm::FunctionPass {
  InstCombineWorklist Worklist;
  const bool ExpensiveCombines;

public:
  static char ID; // Pass identification, replacement for typeid

  SeaInstructionCombiningPass(bool ExpensiveCombines = true)
    : llvm::FunctionPass(ID), ExpensiveCombines(ExpensiveCombines) {
    //initializeInstructionCombiningPassPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(llvm::AnalysisUsage &AU) const override;
  bool runOnFunction(llvm::Function &F) override;
};
  
}

#endif
