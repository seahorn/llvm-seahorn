#include "llvm/IR/PassManager.h"
#ifndef __LLVM_SEAHORN_TRANSFORMS_IPO__H_
#define __LLVM_SEAHORN_TRANSFORMS_IPO__H_

namespace llvm {
class ModulePass;
} // namespace llvm

namespace llvm_seahorn {
llvm::ModulePass *createSeaAnnotation2MetadataLegacyPass();

class SeaAnnotation2MetadataPass
    : public llvm::PassInfoMixin<SeaAnnotation2MetadataPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &);
};
llvm::ModulePass *createSeaLoopExtractorPass();
llvm::ModulePass *createSeaSingleLoopExtractorPass();
} // namespace llvm_seahorn

#endif
