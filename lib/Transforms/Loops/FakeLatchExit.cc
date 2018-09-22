#include "llvm/Analysis/LoopPass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"

using namespace llvm;

namespace
{
  class FakeLatchExit : public LoopPass
  {
  public:
    static char ID;
    
    FakeLatchExit () : LoopPass (ID) {}

    bool runOnLoop (Loop *L, LPPassManager &LPM) override
    {
      LoopInfo *LI = &getAnalysis<LoopInfoWrapperPass> ().getLoopInfo();
      
      BasicBlock *latch = L->getLoopLatch ();

      // -- no latch
      if (!latch) return false;

      
      BranchInst *bi = dyn_cast<BranchInst> (latch->getTerminator ());
      // -- latch already conditional
      if (!bi || bi->isConditional ()) return false;
      
      assert (bi->isUnconditional ());
      
      // -- create dummy block with unreachable instruction
      LLVMContext &ctx = latch->getParent ()->getContext ();
      BasicBlock *dummy = BasicBlock::Create (ctx, "fake_latch_exit",
                                              latch->getParent ());
      new UnreachableInst (ctx, dummy);
      
      
      // -- br i1 true, label %latch, label %fake_latch_exit
      BranchInst *newBi = BranchInst::Create (bi->getSuccessor (0),
                                              dummy,
                                              ConstantInt::getTrue (ctx), bi);
      newBi->setDebugLoc (bi->getDebugLoc ());
      bi->eraseFromParent ();
      
      return true;
    }

    void getAnalysisUsage (AnalysisUsage &AU) const override
    {
      AU.addRequired<LoopInfoWrapperPass>();
      AU.addPreserved<LoopInfoWrapperPass>();
      // This pass does not preserve dominator tree!
      // AU.setPreservesAll ();
    }
  };

  char FakeLatchExit::ID = 0; 
}

namespace llvm_seahorn
{
  llvm::Pass *createFakeLatchExitPass () {return new FakeLatchExit ();}
}

static RegisterPass<FakeLatchExit>
X("fake-latch-exit","Insert fake latch exits");

