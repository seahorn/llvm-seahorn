//===- InstCombine.h - InstCombine pass -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides the primary interface to the instcombine pass. This pass
/// is suitable for use in the new pass manager. For a pass that works with the
/// legacy pass manager, use \c createSeaInstructionCombiningPass().
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTCOMBINE_INSTCOMBINE_H
#define LLVM_TRANSFORMS_INSTCOMBINE_INSTCOMBINE_H

#include "llvm_seahorn/InitializePasses.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

#define DEBUG_TYPE "sea-instcombine"
#include "llvm/Transforms/Utils/InstructionWorklist.h"
#undef DEBUG_TYPE

namespace llvm_seahorn {
using namespace llvm;

class SeaInstCombinePass : public PassInfoMixin<SeaInstCombinePass> {
  InstructionWorklist Worklist;
  const unsigned MaxIterations;
  const bool AvoidBv;
  const bool AvoidUnsignedICmp;
  const bool AvoidIntToPtr;
  const bool AvoidAliasing;
  const bool AvoidDisequalities;

public:
  static StringRef name() { return "SeaInstCombinePass"; }

  explicit SeaInstCombinePass(
			      bool AvoidBv = true,
			      bool AvoidUnsignedICmp = true,
			      bool AvoidIntToPtr = true,
			      bool AvoidAliasing = true,
			      bool AvoidDisequalities = false);
  explicit SeaInstCombinePass(unsigned MaxIterations,
			      bool AvoidBv,
			      bool AvoidUnsignedICmp,
			      bool AvoidIntToPtr,
			      bool AvoidAliasing,
			      bool AvoidDisequalities);

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// The legacy pass manager's instcombine pass.
///
/// This is a basic whole-function wrapper around the instcombine utility. It
/// will try to combine all instructions in the function.
class SeaInstructionCombiningPass : public FunctionPass {
  InstructionWorklist Worklist;

  const unsigned MaxIterations;
  const bool AvoidBv;
  const bool AvoidUnsignedICmp;
  const bool AvoidIntToPtr;
  const bool AvoidAliasing;
  const bool AvoidDisequalities;

public:
  static char ID; // Pass identification, replacement for typeid

  explicit SeaInstructionCombiningPass(
				       bool AvoidBv = true,
				       bool AvoidUnsignedICmp = true,
				       bool AvoidIntToPtr = true,
				       bool AvoidAliasing = true,
				       bool AvoidDisequalities = false);
  explicit SeaInstructionCombiningPass(
				       unsigned MaxIterations,
				       bool AvoidBv,
				       bool AvoidUnsignedICmp,
				       bool AvoidIntToPtr,
				       bool AvoidAliasing,
				       bool AvoidDisequalities);

  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnFunction(Function &F) override;
};

//===----------------------------------------------------------------------===//
//
// InstructionCombining - Combine instructions to form fewer, simple
// instructions. This pass does not modify the CFG, and has a tendency to make
// instructions dead, so a subsequent DCE pass is useful.
//
// This pass combines things like:
//    %Y = add int 1, %X
//    %Z = add int 1, %Y
// into:
//    %Z = add int 2, %X
//
void initializeInstCombine(llvm::PassRegistry &Registry);
}

llvm::FunctionPass *createSeaInstructionCombiningPass();
llvm::FunctionPass *createSeaInstructionCombiningPass(unsigned MaxIterations,
						      bool AvoidBv,
						      bool AvoidUnsignedICmp,
						      bool AvoidIntToPtr,
						      bool AvoidAliasing,
						      bool AvoidDisequalities);
#endif
