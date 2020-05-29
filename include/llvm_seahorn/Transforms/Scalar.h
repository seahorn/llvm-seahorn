#ifndef __LLVM_SEAHORN_TRANSFORMS_SCALAR__H_
#define __LLVM_SEAHORN_TRANSFORMS_SCALAR__H_

namespace llvm {
class FunctionPass;
class Pass;
} // namespace llvm

namespace llvm_seahorn {
// llvm::FunctionPass *createInstructionCombiningPass(bool ExpensiveCombines =
// true); llvm::Pass* createNondetInitPass (); llvm::Pass*
// createDeadNondetElimPass ();
llvm::Pass *createIndVarSimplifyPass();
llvm::Pass *createFakeLatchExitPass();
llvm::Pass *createLoopRotatePass(int MaxHeaderSize = -1);
} // namespace llvm_seahorn

#endif
