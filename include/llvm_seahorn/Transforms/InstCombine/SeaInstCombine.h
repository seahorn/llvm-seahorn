//===- InstCombine.h - InstCombine pass -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides the primary interface to SeaHorn's instcombine pass.
/// It is a new-PM pass, registered in seaopt as `-passes=sea-instcombine`.
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

  // Default ctor: reads the seaopt-instcombine-avoid-* CLI flags so the new-PM
  // `-passes=sea-instcombine` matches the legacy `-sea-instcombine` default
  // (SeaHorn behavior ON; pass the avoid-*=0 flags for stock LLVM behavior).
  SeaInstCombinePass();
  explicit SeaInstCombinePass(
			      bool AvoidBv,
			      bool AvoidUnsignedICmp,
			      bool AvoidIntToPtr,
			      bool AvoidAliasing,
			      bool AvoidDisequalities);
  explicit SeaInstCombinePass(unsigned MaxIterations,
			      bool AvoidBv,
			      bool AvoidUnsignedICmp,
			      bool AvoidIntToPtr,
			      bool AvoidAliasing,
			      bool AvoidDisequalities);

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

}
#endif
