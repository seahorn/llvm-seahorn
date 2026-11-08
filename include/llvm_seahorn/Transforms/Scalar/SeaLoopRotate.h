// SPDX-FileCopyrightText: 2026 The llvm-seahorn Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once
/// New pass manager version of SeaHorn's loop-rotation pass.
#include "llvm/Transforms/Scalar/LoopPassManager.h"

namespace llvm_seahorn {

class SeaLoopRotatePass : public llvm::PassInfoMixin<SeaLoopRotatePass> {
public:
  SeaLoopRotatePass(int MaxHeaderSize = -1) {}
  llvm::PreservedAnalyses run(llvm::Loop &L, llvm::LoopAnalysisManager &AM,
                              llvm::LoopStandardAnalysisResults &AR,
                              llvm::LPMUpdater &U);
};

} // namespace llvm_seahorn
