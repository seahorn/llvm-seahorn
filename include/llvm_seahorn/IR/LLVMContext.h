#ifndef __LLVM_SEAHORN_IR_LLVMCONTEXT_H_
#define __LLVM_SEAHORN_IR_LLVMCONTEXT_H_

namespace llvm_seahorn {
// In llvm-12 this is a static const member of LLVMContext.
// See llvm/include/llvm/IR/FixedMetadataKinds.def
class LLVMContext {
public:
  static unsigned const MD_annotation;
};
} // namespace llvm_seahorn

#endif
