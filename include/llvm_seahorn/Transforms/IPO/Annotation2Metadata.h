//===- Annotation2Metadata.h - Add !annotation metadata. --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// New pass manager pass to convert @llvm.global.annotations to !annotation
// metadata.
//
//===----------------------------------------------------------------------===//

#ifndef SEAHORN_LLVM_TRANSFORMS_IPO_ANNOTATION2METADATA_H
#define SEAHORN_LLVM_TRANSFORMS_IPO_ANNOTATION2METADATA_H

#include "llvm/IR/PassManager.h"

namespace llvm {
class Module;
}

namespace llvm_seahorn {
struct Annotation2MetadataPass : public llvm::PassInfoMixin<Annotation2MetadataPass> {
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
};
} // end llvm_seahorn

#endif // SEAHORN_LLVM_TRANSFORMS_IPO_ANNOTATION2METADATA_H
