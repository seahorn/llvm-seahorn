//===-- Annotation2Metadata.cpp - Add !annotation metadata. ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Add !annotation metadata for entries in @llvm.global.anotations, generated
// using __attribute__((annotate("_name"))) on functions in Clang.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/Annotation2Metadata.h"
#include "llvm_seahorn/InitializePasses.h"
#include "llvm_seahorn/Transforms/IPO.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"

using namespace llvm;

#define DEBUG_TYPE "sea-annotation2metadata"


static bool convertAnnotation2Metadata(Module &M) {
#if 0 /* SEAHORN DISABLE */
  // Only add !annotation metadata if the corresponding remarks pass is also
  // enabled.
  if (!OptimizationRemarkEmitter::allowExtraAnalysis(M.getContext(),
                                                     "annotation-remarks"))
    return false;
#endif

  auto *Annotations = M.getGlobalVariable("llvm.global.annotations");
  auto *C = dyn_cast_or_null<Constant>(Annotations);
  if (!C || C->getNumOperands() != 1)
    return false;

  C = cast<Constant>(C->getOperand(0));

  // Iterate over all entries in C and attach !annotation metadata to suitable
  // entries.
  for (auto &Op : C->operands()) {
    // Look at the operands to check if we can use the entry to generate
    // !annotation metadata.
    auto *OpC = dyn_cast<ConstantStruct>(&Op);
    if (!OpC || OpC->getNumOperands() != 4)
      continue;
    auto *StrGEP = dyn_cast<ConstantExpr>(OpC->getOperand(1));
    if (!StrGEP || StrGEP->getNumOperands() < 2)
      continue;
    auto *StrC = dyn_cast<GlobalValue>(StrGEP->getOperand(0));
    if (!StrC)
      continue;
    auto *StrData = dyn_cast<ConstantDataSequential>(StrC->getOperand(0));
    if (!StrData)
      continue;
    // Look through bitcast.
    auto *Bitcast = dyn_cast<ConstantExpr>(OpC->getOperand(0));
    if (!Bitcast || Bitcast->getOpcode() != Instruction::BitCast)
      continue;
    auto *Fn = dyn_cast<Function>(Bitcast->getOperand(0));
    if (!Fn)
      continue;

    // Add annotation to all instructions in the function.
    for (auto &I : instructions(Fn))
      I.addAnnotationMetadata(StrData->getAsCString());
  }
  return true;
}

namespace {
struct SeaAnnotation2MetadataLegacy : public ModulePass {
  static char ID;

  SeaAnnotation2MetadataLegacy() : ModulePass(ID) {
    initializeSeaAnnotation2MetadataLegacyPass(*PassRegistry::getPassRegistry());
  }

  bool runOnModule(Module &M) override { return convertAnnotation2Metadata(M); }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
};

} // end anonymous namespace

char SeaAnnotation2MetadataLegacy::ID = 0;

INITIALIZE_PASS_BEGIN(SeaAnnotation2MetadataLegacy, DEBUG_TYPE,
                      "SeaAnnotation2Metadata", false, false)
INITIALIZE_PASS_END(SeaAnnotation2MetadataLegacy, DEBUG_TYPE,
                    "SeaAnnotation2Metadata", false, false)

ModulePass *llvm_seahorn::createSeaAnnotation2MetadataLegacyPass() {
  return new SeaAnnotation2MetadataLegacy();
}

