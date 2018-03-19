#include "llvm_seahorn/Loops/SeaSCEVUtils.h"

#include "llvm/Analysis/ScalarEvolutionAliasAnalysis.h"

namespace llvm_seahorn {

bool seaSCEVContainsMul(const llvm::SCEV *Expr) {
  using namespace llvm;
  assert(Expr);

  if (const auto *M = dyn_cast<SCEVMulExpr>(Expr)) {
    // If there is more than one non-constant SCEV subexpression we consider
    // multiplication costly for *verification*.
    if (std::count_if(M->op_begin(), M->op_end(), [](const SCEV *C) {
          return isa<SCEVConstant>(C);
        }) < M->getNumOperands() - 1)
      return true;
  }

  if (const auto *Cast = dyn_cast<SCEVCastExpr>(Expr))
    return seaSCEVContainsMul(Cast->getOperand());

  if (const auto *Nary = dyn_cast<SCEVNAryExpr>(Expr)) {
    for (const auto *Op : Nary->operands())
      if (seaSCEVContainsMul(Op))
        return true;

    return false;
  }

  if (const auto *Div = dyn_cast<SCEVUDivExpr>(Expr)) {
    if (seaSCEVContainsMul(Div->getLHS()))
      return true;
    if (seaSCEVContainsMul(Div->getRHS()))
      return true;
  }

  return false;
}

} // namespace llvm_seahorn
