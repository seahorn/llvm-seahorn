#ifndef LLVM_SEAHORN_SCEV_UTILS_H
#define LLVM_SEAHORN_SCEV_UTILS_H

namespace llvm {
class SCEV;
} // namespace llvm

namespace llvm_seahorn {
bool seaSCEVContainsMul(const llvm::SCEV *Expr);
} // namespace llvm_seahorn

#endif // LLVM_SEAHORN_SCEV_UTILS_H
