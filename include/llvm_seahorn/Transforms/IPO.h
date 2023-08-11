#ifndef __LLVM_SEAHORN_TRANSFORMS_IPO__H_
#define __LLVM_SEAHORN_TRANSFORMS_IPO__H_

namespace llvm {
class ModulePass;
} // namespace llvm

namespace llvm_seahorn {
llvm::ModulePass *createSeaAnnotation2MetadataLegacyPass();
llvm::ModulePass *createSeaLoopExtractorPass();
llvm::ModulePass *createSeaSingleLoopExtractorPass();
} // namespace llvm_seahorn

#endif
