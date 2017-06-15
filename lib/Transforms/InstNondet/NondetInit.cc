/**
SeaHorn Verification Framework
Copyright (c) 2015 Carnegie Mellon University.
All Rights Reserved.

THIS SOFTWARE IS PROVIDED "AS IS," WITH NO WARRANTIES
WHATSOEVER. CARNEGIE MELLON UNIVERSITY EXPRESSLY DISCLAIMS TO THE
FULLEST EXTENT PERMITTEDBY LAW ALL EXPRESS, IMPLIED, AND STATUTORY
WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND
NON-INFRINGEMENT OF PROPRIETARY RIGHTS.

Released under a modified BSD license, please see license.txt for full
terms.

DM-0002198
*/

#define DEBUG_TYPE "nondet"

#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Compiler.h"
#include "llvm/IR/PassManager.h"

#include "llvm/IR/IRBuilder.h"

#include "llvm/ADT/Statistic.h"
#include "boost/format.hpp"

#include <map>
#include <forward_list>

using namespace llvm;
STATISTIC(NumReplaced, "Number of undef made nondet");
STATISTIC(NumKilled, "Number of nondet calls killed");

namespace llvm_seahorn
{
  class NondetInit : public ModulePass 
  {

  private:
    /** map for nondet functions */
    DenseMap<const Type*, Constant*> m_ndfn;
    Module* m;
    
    Constant* getNondetFn (Type *type)
    {
      Constant* res = m_ndfn [type];
      if (res == NULL)
	{
	  res = m->getOrInsertFunction 
	    (boost::str 
	     (boost::format ("verifier.nondet.%d") % m_ndfn.size ()), 
             type, NULL);
	  
	  // -- say that f does not access memory will make llvm
	  // -- assume that all calls to it return the same value
	  // if (Function *f = dyn_cast<Function>(res))
          // {
	  //   // f->setDoesNotAccessMemory (true);
          //   // f->setDoesNotAlias (0);
          // }
	  m_ndfn[type] = res;
	}
      return res;
    }

  public:
    static char ID;
    NondetInit() : ModulePass(ID), m(NULL) {}    

    
    virtual bool runOnModule(Module &M) 
    {
      
      m = &M;
      bool Changed = false;

      //Iterate over all functions, basic blocks and instructions.
      for (Module::iterator FI = M.begin(), E = M.end(); FI != E; ++FI)
	Changed |= runOnFunction (*FI);
	  
      return Changed;
    }
    
    virtual void releaseMemory () {m_ndfn.clear ();}
    

    bool runOnFunction (Function &F)
    {
      bool Changed = false;
      for (BasicBlock &b : F)
        for (User &u : b)
	  {
            // phi-node
	    if (PHINode* phi = dyn_cast<PHINode> (&u))
	      {
		for (unsigned i = 0; i < phi->getNumIncomingValues (); i++)
		  {
		    if (UndefValue *uv = 
			dyn_cast<UndefValue> (phi->getIncomingValue (i)))
		      {
			Constant *ndf = getNondetFn (uv->getType ());
			IRBuilder<> Builder (F.getContext ());
			Builder.SetInsertPoint (&F.getEntryBlock (), 
						F.getEntryBlock ().begin ());
			phi->setIncomingValue (i, Builder.CreateCall (ndf));
                        ++NumReplaced;
			Changed = true;
		      }
		  }
		
		continue;
	      }

	    // -- the normal case
	    for (unsigned i = 0; i < u.getNumOperands (); i++)
	      {
		if (UndefValue *uv = dyn_cast<UndefValue> (u.getOperand (i)))
		  {
		    Constant *ndf = getNondetFn (uv->getType ());
		    IRBuilder<> Builder (F.getContext ());
		    Builder.SetInsertPoint (&F.getEntryBlock (), 
					    F.getEntryBlock ().begin ());
		    u.setOperand (i, Builder.CreateCall (ndf));
                    ++NumReplaced;
		    Changed = true;
		  }
	      }
	  }
      return Changed;
    }
    

    virtual void getAnalysisUsage (AnalysisUsage &AU) const 
    {AU.setPreservesAll ();}
    
  };
  
  char NondetInit::ID = 0;
}

namespace llvm_seahorn
{
  class KillUnusedNondet : public FunctionPass
  {

  public:
    static char ID;
    KillUnusedNondet() : FunctionPass(ID) {}    


    bool runOnFunction (Function &F)
    {
      std::forward_list<CallInst*> toerase;
      
      for (Function::iterator b = F.begin(), be = F.end(); b != be; ++b)
	for (BasicBlock::iterator it = b->begin(), ie = b->end(); 
	     it != ie; ++it) 
	  {
	    User* u = &(*it);
	    // -- looking for empty users
	    if (!u->use_empty ()) continue;
	    
	    if (CallInst *ci = dyn_cast<CallInst> (u))
	      {
		Function *f = ci->getCalledFunction ();
		if (f == NULL) continue;
		
		if (f->getName ().startswith ("verifier.nondet"))
                {
		  toerase.push_front (ci);
                  ++NumKilled;
                }
	      }
	  }
      
      
      for (CallInst *ci : toerase) ci->eraseFromParent ();
      return !toerase.empty ();
    }
    
    virtual void getAnalysisUsage (AnalysisUsage &AU) const 
    {AU.setPreservesAll ();}
    
  };
  
  char KillUnusedNondet::ID = 0;
  
  llvm::Pass* createNondetInitPass () {return new NondetInit ();}
  llvm::Pass* createDeadNondetElimPass () {return new KillUnusedNondet ();}
}

static RegisterPass<llvm_seahorn::NondetInit> 
X("nondet-init", 
  "Non-deterministic initialization" 
  "of all alloca.");

static RegisterPass<llvm_seahorn::KillUnusedNondet> 
Y("kill-nondet",
  "Remove unused nondet calls.");

