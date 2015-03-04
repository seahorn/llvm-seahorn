#ifndef __LLVM_SEAHORN_TRANSFORMS_SCALAR__H_
#define __LLVM_SEAHORN_TRANSFORMS_SCALAR__H_

namespace llvm {class FunctionPass;}
namespace llvm_seahorn
{
  llvm::FunctionPass *createInstructionCombiningPass();
}
#endif
