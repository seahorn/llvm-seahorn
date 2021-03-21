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

#include "llvm_seahorn/IR/LLVMContext.h"
#include "llvm_seahorn/Transforms/IPO/Annotation2Metadata.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm_seahorn/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/IPO.h"
#include "llvm_seahorn/Transforms/IPO.h"

using namespace llvm;

#define DEBUG_TYPE "annotation2metadata"

const char *llvm_seahorn::LLVMContext::MD_annotation = "sea.annotations";

// In llvm-12 this is Instruction::addAnnotationMetadata.
// See llvm/lib/IR/Metadata.cpp.
static void addAnnotationMetadata(Instruction &I, StringRef Name) {
  MDBuilder MDB(I.getContext());

  auto *Existing = I.getMetadata(llvm_seahorn::LLVMContext::MD_annotation);
  SmallVector<Metadata *, 4> Names;
  bool AppendName = true;
  if (Existing) {
    auto *Tuple = cast<MDTuple>(Existing);
    for (auto &N : Tuple->operands()) {
      if (cast<MDString>(N.get())->getString() == Name)
        AppendName = false;
      Names.push_back(N.get());
    }
  }
  if (AppendName)
    Names.push_back(MDB.createString(Name));

  MDNode *MD = MDTuple::get(I.getContext(), Names);
  I.setMetadata(llvm_seahorn::LLVMContext::MD_annotation, MD);
}

static bool convertAnnotation2Metadata(Module &M) {
  // Run convertAnnotations2Metadata unconditionally.
  /*
   *if (!OptimizationRemarkEmitter::allowExtraAnalysis(M.getContext(),
   *                                                   "annotation-remarks"))
   *  return false;
   */

  auto *Annotations = M.getGlobalVariable("llvm.global.annotations");
  auto *C = dyn_cast_or_null<Constant>(Annotations);
  if (!C || C->getNumOperands() != 1)
    return false;

  C = cast<Constant>(C->getOperand(0));

  for (auto &Op : C->operands()) {
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
    auto *Bitcast = dyn_cast<ConstantExpr>(OpC->getOperand(0));
    if (!Bitcast || Bitcast->getOpcode() != Instruction::BitCast)
      continue;
    auto *Fn = dyn_cast<Function>(Bitcast->getOperand(0));
    if (!Fn)
      continue;

    for (auto &I : instructions(Fn))
      addAnnotationMetadata(I, StrData->getAsCString());
  }
  return true;
}

namespace {
struct Annotation2MetadataLegacy : public ModulePass {
  static char ID;

  Annotation2MetadataLegacy() : ModulePass(ID) {
    initializeAnnotation2MetadataLegacyPass(*PassRegistry::getPassRegistry());
  }

  bool runOnModule(Module &M) override { return convertAnnotation2Metadata(M); }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
};

} // end anonymous namespace

char Annotation2MetadataLegacy::ID = 0;

INITIALIZE_PASS_BEGIN(Annotation2MetadataLegacy, DEBUG_TYPE,
                      "Annotation2Metadata", false, false)
INITIALIZE_PASS_END(Annotation2MetadataLegacy, DEBUG_TYPE,
                    "Annotation2Metadata", false, false)

ModulePass *llvm_seahorn::createAnnotation2MetadataLegacyPass() {
  return new Annotation2MetadataLegacy();
}

PreservedAnalyses llvm_seahorn::Annotation2MetadataPass::run(Module &M,
                                                             ModuleAnalysisManager &AM) {
  convertAnnotation2Metadata(M);
  return PreservedAnalyses::all();
}
