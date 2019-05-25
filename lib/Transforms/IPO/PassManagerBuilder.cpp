//===- PassManagerBuilder.cpp - Build Standard Pass -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the PassManagerBuilder class, which is used to set up a
// "standard" optimization sequence suitable for languages like C and C++.
//
//===----------------------------------------------------------------------===//

#include "llvm_seahorn/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/CFLAndersAliasAnalysis.h"
#include "llvm/Analysis/CFLSteensAliasAnalysis.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/ScopedNoAliasAA.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TypeBasedAliasAnalysis.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/ForceFunctionAttrs.h"
#include "llvm/Transforms/IPO/FunctionAttrs.h"
#include "llvm/Transforms/IPO/InferFunctionAttrs.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Vectorize.h"
#include "llvm_seahorn/Transforms/Scalar.h"

using namespace llvm_seahorn;
using namespace llvm;

static cl::opt<bool>
NeverTrue ("never-true", cl::Hidden, cl::init (false));

static cl::opt<bool>
EnableIndVar("enable-indvar", cl::Hidden,
             cl::desc("Enable indvar pass"),
             cl::init (true));

static cl::opt<bool>
EnableLoopIdiom("enable-loop-idiom", cl::Hidden,
                cl::desc("Enable loop-idiom pass"),
                cl::init (true));

static cl::opt<bool>
EnableNondet("enable-nondet-init", cl::Hidden,
             cl::desc("Enable non-deterministic initialization of all alloca"),
             cl::init (true));

static cl::opt<bool>
RunLoopVectorization("vectorize-loops", cl::Hidden,
                     cl::desc("Run the Loop vectorization passes"));

static cl::opt<bool>
RunSLPVectorization("vectorize-slp", cl::Hidden,
                    cl::desc("Run the SLP vectorization passes"));

static cl::opt<bool>
UseGVNAfterVectorization("use-gvn-after-vectorization",
  cl::init(false), cl::Hidden,
  cl::desc("Run GVN instead of Early CSE after vectorization passes"));

static cl::opt<bool> ExtraVectorizerPasses(
    "extra-vectorizer-passes", cl::init(false), cl::Hidden,
    cl::desc("Run cleanup optimization passes after vectorization."));

static cl::opt<bool> UseNewSROA("use-new-sroa",
  cl::init(true), cl::Hidden,
  cl::desc("Enable the new, experimental SROA pass"));

static cl::opt<bool>
RunLoopRerolling("reroll-loops", cl::Hidden,
                 cl::desc("Run the loop rerolling pass"));

static cl::opt<bool> RunNewGVN("enable-newgvn", cl::init(false), cl::Hidden,
                               cl::desc("Run the NewGVN pass"));

static cl::opt<bool>
RunFloat2Int("float-to-int", cl::Hidden, cl::init(true),
             cl::desc("Run the float2int (float demotion) pass"));

static cl::opt<bool>
RunSLPAfterLoopVectorization("run-slp-after-loop-vectorization",
  cl::init(true), cl::Hidden,
  cl::desc("Run the SLP vectorizer (and BB vectorizer) after the Loop "
           "vectorizer instead of before"));

// Experimental option to use CFL-AA
enum class CFLAAType { None, Steensgaard, Andersen, Both };
static cl::opt<CFLAAType>
    UseCFLAA("use-cfl-aa", cl::init(CFLAAType::None), cl::Hidden,
             cl::desc("Enable the new, experimental CFL alias analysis"),
             cl::values(clEnumValN(CFLAAType::None, "none", "Disable CFL-AA"),
                        clEnumValN(CFLAAType::Steensgaard, "steens",
                                   "Enable unification-based CFL-AA"),
                        clEnumValN(CFLAAType::Andersen, "anders",
                                   "Enable inclusion-based CFL-AA"),
                        clEnumValN(CFLAAType::Both, "both",
                                   "Enable both variants of CFL-AA")));

static cl::opt<bool>
EnableMLSM("mlsm", cl::init(true), cl::Hidden,
           cl::desc("Enable motion of merged load and store"));

static cl::opt<bool> EnableLoopInterchange(
    "sea-enable-loopinterchange", cl::init(false), cl::Hidden,
    cl::desc("Enable the new, experimental LoopInterchange Pass"));

static cl::opt<bool> EnableLoopDistribute(
    "sea-enable-loop-distribute", cl::init(false), cl::Hidden,
    cl::desc("Enable the new, experimental LoopDistribution Pass"));

static cl::opt<bool> EnableNonLTOGlobalsModRef(
    "sea-enable-non-lto-gmr", cl::init(true), cl::Hidden,
    cl::desc(
        "Enable the GlobalsModRef AliasAnalysis outside of the LTO pipeline."));

static cl::opt<bool> EnableLoopLoadElim(
    "sea-enable-loop-load-elim", cl::init(false), cl::Hidden,
    cl::desc("Enable the new, experimental LoopLoadElimination Pass"));

namespace llvm_seahorn {
  
PassManagerBuilder::PassManagerBuilder() {
    OptLevel = 2;
    SizeLevel = 0;
    LibraryInfo = nullptr;
    Inliner = nullptr;
    DisableUnitAtATime = false;
    DisableUnrollLoops = false;
    SLPVectorize = RunSLPVectorization;
    LoopVectorize = RunLoopVectorization;
    RerollLoops = RunLoopRerolling;
    NewGVN = RunNewGVN;    
    DisableGVNLoadPRE = false;
    VerifyInput = false;
    VerifyOutput = false;
    MergeFunctions = false;
    PrepareForLTO = false;
}

PassManagerBuilder::~PassManagerBuilder() {
  delete LibraryInfo;
  delete Inliner;
}

/// Set of global extensions, automatically added as part of the standard set.
static ManagedStatic<SmallVector<std::pair<PassManagerBuilder::ExtensionPointTy,
   PassManagerBuilder::ExtensionFn>, 8> > GlobalExtensions;

void PassManagerBuilder::addGlobalExtension(
    PassManagerBuilder::ExtensionPointTy Ty,
    PassManagerBuilder::ExtensionFn Fn) {
  GlobalExtensions->push_back(std::make_pair(Ty, Fn));
}

void PassManagerBuilder::addExtension(ExtensionPointTy Ty, ExtensionFn Fn) {
  Extensions.push_back(std::make_pair(Ty, Fn));
}

void PassManagerBuilder::addExtensionsToPM(ExtensionPointTy ETy,
                                           legacy::PassManagerBase &PM) const {
  for (unsigned i = 0, e = GlobalExtensions->size(); i != e; ++i)
    if ((*GlobalExtensions)[i].first == ETy)
      (*GlobalExtensions)[i].second(*this, PM);
  for (unsigned i = 0, e = Extensions.size(); i != e; ++i)
    if (Extensions[i].first == ETy)
      Extensions[i].second(*this, PM);
}

void PassManagerBuilder::addInitialAliasAnalysisPasses(
    legacy::PassManagerBase &PM) const {

  switch (UseCFLAA) {
  case CFLAAType::Steensgaard:
    PM.add(createCFLSteensAAWrapperPass());
    break;
  case CFLAAType::Andersen:
    PM.add(createCFLAndersAAWrapperPass());
    break;
  case CFLAAType::Both:
    PM.add(createCFLSteensAAWrapperPass());
    PM.add(createCFLAndersAAWrapperPass());
    break;
  default:
    break;
  }
  
  // Add TypeBasedAliasAnalysis before BasicAliasAnalysis so that
  // BasicAliasAnalysis wins if they disagree. This is intended to help
  // support "obvious" type-punning idioms.
  PM.add(createTypeBasedAAWrapperPass());
  PM.add(createScopedNoAliasAAWrapperPass());
}

void PassManagerBuilder::populateFunctionPassManager(
    legacy::FunctionPassManager &FPM) {
  addExtensionsToPM(EP_EarlyAsPossible, FPM);

  // Add LibraryInfo if we have some.
  if (LibraryInfo)
    FPM.add(new TargetLibraryInfoWrapperPass(*LibraryInfo));

  if (OptLevel == 0) return;

  addInitialAliasAnalysisPasses(FPM);

  FPM.add(createCFGSimplificationPass());
  FPM.add(createSROAPass());
  if (!EnableNondet) {
    // -- this pass might mess things up if there is undefined
    // -- behavior that we want to treat as non-deterministic
    FPM.add(createEarlyCSEPass());
  }
  FPM.add(createLowerExpectIntrinsicPass());
}

void PassManagerBuilder::populateModulePassManager(
    legacy::PassManagerBase &MPM) {
  // Allow forcing function attributes as a debugging and tuning aid.
  MPM.add(createForceFunctionAttrsLegacyPass());

  if (EnableNondet) {
    // -- Turn undef into nondet (undef are created by SROA when it calls mem2reg)
    MPM.add (llvm_seahorn::createNondetInitPass ());
    // -- Moved here from populateFunctionPassManager() since we have
    // -- to run nondet-init after sroa and before anything else
    //MPM.add(createEarlyCSEPass());
  }
  
  if (NeverTrue)
    MPM.add (llvm_seahorn::createFakeLatchExitPass ());
  
  // If all optimizations are disabled, just run the always-inline pass and,
  // if enabled, the function merging pass.
  if (OptLevel == 0) {
    if (Inliner) {
      MPM.add(Inliner);
      Inliner = nullptr;
    }

    // FIXME: The BarrierNoopPass is a HACK! The inliner pass above implicitly
    // creates a CGSCC pass manager, but we don't want to add extensions into
    // that pass manager. To prevent this we insert a no-op module pass to reset
    // the pass manager to get the same behavior as EP_OptimizerLast in non-O0
    // builds. The function merging pass is 
    if (MergeFunctions)
      MPM.add(createMergeFunctionsPass());
    else if (!GlobalExtensions->empty() || !Extensions.empty())
      MPM.add(createBarrierNoopPass());

    addExtensionsToPM(EP_EnabledOnOptLevel0, MPM);
    return;
  }

  // Add LibraryInfo if we have some.
  if (LibraryInfo)
    MPM.add(new TargetLibraryInfoWrapperPass(*LibraryInfo));

  addInitialAliasAnalysisPasses(MPM);

  if (!DisableUnitAtATime) {
    // Infer attributes about declarations if possible.
    MPM.add(createInferFunctionAttrsLegacyPass());

    addExtensionsToPM(EP_ModuleOptimizerEarly, MPM);

    MPM.add(createIPSCCPPass());              // IP SCCP
    MPM.add(createGlobalOptimizerPass());     // Optimize out global vars
    // Promote any localized global vars
    MPM.add(createPromoteMemoryToRegisterPass());

    if (EnableNondet) {
      // -- Turn undef into nondet (undef are created by mem2reg)
      MPM.add (llvm_seahorn::createNondetInitPass ());
    }
    
    MPM.add(createDeadArgEliminationPass());  // Dead argument elimination

    MPM.add(llvm_seahorn::createInstructionCombiningPass());// Clean up after IPCP & DAE    
    addExtensionsToPM(EP_Peephole, MPM);
    MPM.add(createCFGSimplificationPass());   // Clean up after IPCP & DAE
  }

  if (EnableNonLTOGlobalsModRef)
    // We add a module alias analysis pass here. In part due to bugs in the
    // analysis infrastructure this "works" in that the analysis stays alive
    // for the entire SCC pass run below.
    MPM.add(createGlobalsAAWrapperPass());

  // Start of CallGraph SCC passes.
  if (!DisableUnitAtATime)
    MPM.add(createPruneEHPass());             // Remove dead EH info
  if (Inliner) {
    MPM.add(Inliner);
    Inliner = nullptr;
  }
  if (!DisableUnitAtATime)
    MPM.add(createPostOrderFunctionAttrsLegacyPass());    
  if (OptLevel > 2)
    MPM.add(createArgumentPromotionPass());   // Scalarize uninlined fn args

  // Start of function pass.
  // Break up aggregate allocas, using SSAUpdater.
  MPM.add(createSROAPass());

  if (EnableNondet) {
    // -- Turn undef into nondet (undef are created by SROA when it calls mem2reg)
    MPM.add (llvm_seahorn::createNondetInitPass ());
  }

  MPM.add(createEarlyCSEPass());              // Catch trivial redundancies
  MPM.add(createJumpThreadingPass());         // Thread jumps.
  MPM.add(createCorrelatedValuePropagationPass()); // Propagate conditionals
  MPM.add(createCFGSimplificationPass());     // Merge & remove BBs
  MPM.add(llvm_seahorn::createInstructionCombiningPass());  // Combine silly seq's
  addExtensionsToPM(EP_Peephole, MPM);

  if (EnableNondet) {
    // eliminate unused calls to verifier.nondet() functions
    MPM.add (llvm_seahorn::createDeadNondetElimPass ());
  }
  
  MPM.add(createTailCallEliminationPass()); // Eliminate tail calls
  MPM.add(createCFGSimplificationPass());     // Merge & remove BBs
  MPM.add(createReassociatePass());           // Reassociate expressions
  // Rotate Loop - disable header duplication at -Oz
  MPM.add(createLoopRotatePass(SizeLevel == 2 ? 0 : -1));
  MPM.add(createLICMPass());                  // Hoist loop invariants
  MPM.add(createLoopUnswitchPass(SizeLevel || OptLevel < 3));
  MPM.add(createCFGSimplificationPass());

  MPM.add(llvm_seahorn::createInstructionCombiningPass());
  if (EnableIndVar)
    MPM.add(llvm_seahorn::createIndVarSimplifyPass());        // Canonicalize indvars
  if (EnableLoopIdiom)
    MPM.add(createLoopIdiomPass());             // Recognize idioms like memset.
  
  MPM.add(createLoopDeletionPass());          // Delete dead loops
  if (EnableLoopInterchange) {
    MPM.add(createLoopInterchangePass()); // Interchange loops
    MPM.add(createCFGSimplificationPass());
  }
  if (!DisableUnrollLoops)
    MPM.add(createSimpleLoopUnrollPass());    // Unroll small loops
  addExtensionsToPM(EP_LoopOptimizerEnd, MPM);

  if (OptLevel > 1) {
    if (EnableMLSM)
      MPM.add(createMergedLoadStoreMotionPass()); // Merge ld/st in diamonds
    MPM.add(NewGVN ? createNewGVNPass()
                   : createGVNPass(DisableGVNLoadPRE)); // Remove redundancies
  }
  
  MPM.add(createMemCpyOptPass());             // Remove memcpy / form memset
  if (EnableNondet) {
    // -- Turn undef into nondet (undef are created by MemCpy)
    MPM.add (llvm_seahorn::createNondetInitPass ());
  }
  
  MPM.add(createSCCPPass());                  // Constant prop with SCCP

  // Delete dead bit computations (instcombine runs after to fold away the dead
  // computations, and then ADCE will run later to exploit any new DCE
  // opportunities that creates).
  MPM.add(createBitTrackingDCEPass());        // Delete dead bit computations

  // Run instcombine after redundancy elimination to exploit opportunities
  // opened up by them.
  MPM.add(llvm_seahorn::createInstructionCombiningPass());  
  addExtensionsToPM(EP_Peephole, MPM);
  MPM.add(createJumpThreadingPass());         // Thread jumps
  MPM.add(createCorrelatedValuePropagationPass());
  MPM.add(createDeadStoreEliminationPass());  // Delete dead stores
  MPM.add(createLICMPass());

  addExtensionsToPM(EP_ScalarOptimizerLate, MPM);

  if (EnableNondet) {
    // eliminate unused calls to verifier.nondet() functions
    MPM.add (llvm_seahorn::createDeadNondetElimPass ());
  }
  
  if (RerollLoops)
    MPM.add(createLoopRerollPass());
  if (!RunSLPAfterLoopVectorization) {
    if (SLPVectorize)
      MPM.add(createSLPVectorizerPass());   // Vectorize parallel scalar chains.
  }

  MPM.add(createAggressiveDCEPass());         // Delete dead instructions
  MPM.add(createCFGSimplificationPass()); // Merge & remove BBs
  MPM.add(llvm_seahorn::createInstructionCombiningPass());  // Clean up after everything.
  addExtensionsToPM(EP_Peephole, MPM);

  // FIXME: This is a HACK! The inliner pass above implicitly creates a CGSCC
  // pass manager that we are specifically trying to avoid. To prevent this
  // we must insert a no-op module pass to reset the pass manager.
  MPM.add(createBarrierNoopPass());

  if (!DisableUnitAtATime)
    MPM.add(createReversePostOrderFunctionAttrsPass());

  if (!DisableUnitAtATime && OptLevel > 1 && !PrepareForLTO) {
    // Remove avail extern fns and globals definitions if we aren't
    // compiling an object file for later LTO. For LTO we want to preserve
    // these so they are eligible for inlining at link-time. Note if they
    // are unreferenced they will be removed by GlobalDCE later, so
    // this only impacts referenced available externally globals.
    // Eventually they will be suppressed during codegen, but eliminating
    // here enables more opportunity for GlobalDCE as it may make
    // globals referenced by available external functions dead
    // and saves running remaining passes on the eliminated functions.
    MPM.add(createEliminateAvailableExternallyPass());
  }

  if (EnableNonLTOGlobalsModRef)
    // We add a fresh GlobalsModRef run at this point. This is particularly
    // useful as the above will have inlined, DCE'ed, and function-attr
    // propagated everything. We should at this point have a reasonably minimal
    // and richly annotated call graph. By computing aliasing and mod/ref
    // information for all local globals here, the late loop passes and notably
    // the vectorizer will be able to use them to help recognize vectorizable
    // memory operations.
    //
    // Note that this relies on a bug in the pass manager which preserves
    // a module analysis into a function pass pipeline (and throughout it) so
    // long as the first function pass doesn't invalidate the module analysis.
    // Thus both Float2Int and LoopRotate have to preserve AliasAnalysis for
    // this to work. Fortunately, it is trivial to preserve AliasAnalysis
    // (doing nothing preserves it as it is required to be conservatively
    // correct in the face of IR changes).
    MPM.add(createGlobalsAAWrapperPass());

  if (RunFloat2Int)
    MPM.add(createFloat2IntPass());

  addExtensionsToPM(EP_VectorizerStart, MPM);

  // Re-rotate loops in all our loop nests. These may have fallout out of
  // rotated form due to GVN or other transformations, and the vectorizer relies
  // on the rotated form. Disable header duplication at -Oz.
  MPM.add(createLoopRotatePass(SizeLevel == 2 ? 0 : -1));

  // Distribute loops to allow partial vectorization.  I.e. isolate dependences
  // into separate loop that would otherwise inhibit vectorization.
  if (EnableLoopDistribute)
    MPM.add(createLoopDistributePass());

  if (LoopVectorize) {
    MPM.add(createLoopVectorizePass(DisableUnrollLoops, LoopVectorize));
  }

  // Eliminate loads by forwarding stores from the previous iteration to loads
  // of the current iteration.
  if (EnableLoopLoadElim)
    MPM.add(createLoopLoadEliminationPass());

  // FIXME: Because of #pragma vectorize enable, the passes below are always
  // inserted in the pipeline, even when the vectorizer doesn't run (ex. when
  // on -O1 and no #pragma is found). Would be good to have these two passes
  // as function calls, so that we can only pass them when the vectorizer
  // changed the code.
  MPM.add(llvm_seahorn::createInstructionCombiningPass());  
  if (OptLevel > 1 && ExtraVectorizerPasses) {
    // At higher optimization levels, try to clean up any runtime overlap and
    // alignment checks inserted by the vectorizer. We want to track correllated
    // runtime checks for two inner loops in the same outer loop, fold any
    // common computations, hoist loop-invariant aspects out of any outer loop,
    // and unswitch the runtime checks if possible. Once hoisted, we may have
    // dead (or speculatable) control flows or more combining opportunities.
    MPM.add(createEarlyCSEPass());
    MPM.add(createCorrelatedValuePropagationPass());
    MPM.add(llvm_seahorn::createInstructionCombiningPass());      
    MPM.add(createLICMPass());
    MPM.add(createLoopUnswitchPass(SizeLevel || OptLevel < 3));
    MPM.add(createCFGSimplificationPass());
    MPM.add(llvm_seahorn::createInstructionCombiningPass());          
  }

  if (RunSLPAfterLoopVectorization) {
    if (SLPVectorize) {
      MPM.add(createSLPVectorizerPass());   // Vectorize parallel scalar chains.
      if (OptLevel > 1 && ExtraVectorizerPasses) {
        MPM.add(createEarlyCSEPass());
      }
    }
  }

  addExtensionsToPM(EP_Peephole, MPM);
  MPM.add(createCFGSimplificationPass());
  MPM.add(llvm_seahorn::createInstructionCombiningPass());        

  if (!DisableUnrollLoops) {
    MPM.add(createLoopUnrollPass());    // Unroll small loops

    // LoopUnroll may generate some redundency to cleanup.
    MPM.add(llvm_seahorn::createInstructionCombiningPass());            

    // Runtime unrolling will introduce runtime check in loop prologue. If the
    // unrolled loop is a inner loop, then the prologue will be inside the
    // outer loop. LICM pass can help to promote the runtime check out if the
    // checked value is loop invariant.
    MPM.add(createLICMPass());
  }

  // After vectorization and unrolling, assume intrinsics may tell us more
  // about pointer alignments.
  MPM.add(createAlignmentFromAssumptionsPass());

  if (!DisableUnitAtATime) {
    // FIXME: We shouldn't bother with this anymore.
    MPM.add(createStripDeadPrototypesPass()); // Get rid of dead prototypes

    // GlobalOpt already deletes dead functions and globals, at -O2 try a
    // late pass of GlobalDCE.  It is capable of deleting dead cycles.
    if (OptLevel > 1) {
      MPM.add(createGlobalDCEPass());         // Remove dead fns and globals.
      MPM.add(createConstantMergePass());     // Merge dup global constants
    }
  }

  if (MergeFunctions)
    MPM.add(createMergeFunctionsPass());

  addExtensionsToPM(EP_OptimizerLast, MPM);
}

void PassManagerBuilder::addLTOOptimizationPasses(legacy::PassManagerBase &PM) {
  // Remove unused virtual tables to improve the quality of code generated by
  // whole-program devirtualization and bitset lowering.
  PM.add(createGlobalDCEPass());
  
  // Provide AliasAnalysis services for optimizations.
  addInitialAliasAnalysisPasses(PM);

  // Allow forcing function attributes as a debugging and tuning aid.
  PM.add(createForceFunctionAttrsLegacyPass());

  // Infer attributes about declarations if possible.
  PM.add(createInferFunctionAttrsLegacyPass());

  // Propagate constants at call sites into the functions they call.  This
  // opens opportunities for globalopt (and inlining) by substituting function
  // pointers passed as arguments to direct uses of functions.
  PM.add(createIPSCCPPass());

  // Now that we internalized some globals, see if we can hack on them!
  PM.add(createPostOrderFunctionAttrsLegacyPass());
  PM.add(createReversePostOrderFunctionAttrsPass());
  PM.add(createGlobalOptimizerPass());
  // Promote any localized global vars.
  PM.add(createPromoteMemoryToRegisterPass());

  if (EnableNondet) {
    // -- Turn undef into nondet (undef are created by mem2reg)
    PM.add (llvm_seahorn::createNondetInitPass ());
  }
  
  // Linking modules together can lead to duplicated global constants, only
  // keep one copy of each constant.
  PM.add(createConstantMergePass());

  // Remove unused arguments from functions.
  PM.add(createDeadArgEliminationPass());

  // Reduce the code after globalopt and ipsccp.  Both can open up significant
  // simplification opportunities, and both can propagate functions through
  // function pointers.  When this happens, we often have to resolve varargs
  // calls, etc, so let instcombine do this.
  PM.add(llvm_seahorn::createInstructionCombiningPass());
  addExtensionsToPM(EP_Peephole, PM);

  // Inline small functions
  bool RunInliner = Inliner;
  if (RunInliner) {
    PM.add(Inliner);
    Inliner = nullptr;
  }

  PM.add(createPruneEHPass());   // Remove dead EH info.

  // Optimize globals again if we ran the inliner.
  if (RunInliner)
    PM.add(createGlobalOptimizerPass());
  PM.add(createGlobalDCEPass()); // Remove dead functions.

  // If we didn't decide to inline a function, check to see if we can
  // transform it to pass arguments by value instead of by reference.
  PM.add(createArgumentPromotionPass());

  // The IPO passes may leave cruft around.  Clean up after them.
  PM.add(llvm_seahorn::createInstructionCombiningPass());              
  addExtensionsToPM(EP_Peephole, PM);
  PM.add(createJumpThreadingPass());

  // Break up allocas
  PM.add(createSROAPass());

  if (EnableNondet) {
    // -- Turn undef into nondet (undef are created by SROA when it calls mem2reg)
    PM.add (llvm_seahorn::createNondetInitPass ());
  }
  
  // Run a few AA driven optimizations here and now, to cleanup the code.
  PM.add(createPostOrderFunctionAttrsLegacyPass()); // Add nocapture.
  PM.add(createGlobalsAAWrapperPass()); // IP alias analysis.

  PM.add(createLICMPass());                 // Hoist loop invariants.
  if (EnableMLSM)
    PM.add(createMergedLoadStoreMotionPass()); // Merge ld/st in diamonds.
  PM.add(NewGVN ? createNewGVNPass()
                : createGVNPass(DisableGVNLoadPRE)); // Remove redundancies.
  PM.add(createMemCpyOptPass());            // Remove dead memcpys.

  if (EnableNondet) {
    // -- Turn undef into nondet (undef are created by MemCpy)
    PM.add (llvm_seahorn::createNondetInitPass ());
  }
  
  // Nuke dead stores.
  PM.add(createDeadStoreEliminationPass());

  if (EnableNondet) {
    // eliminate unused calls to verifier.nondet() functions
    PM.add (llvm_seahorn::createDeadNondetElimPass ());
  }

  if (EnableIndVar) {
   // More loops are countable; try to optimize them.
    PM.add(llvm_seahorn::createIndVarSimplifyPass());
  }
  
  PM.add(createLoopDeletionPass());
  if (EnableLoopInterchange)
    PM.add(createLoopInterchangePass());

  if (LoopVectorize) {
    PM.add(createLoopVectorizePass(true, LoopVectorize));
  }

  // Now that we've optimized loops (in particular loop induction variables),
  // we may have exposed more scalar opportunities. Run parts of the scalar
  // optimizer again at this point.
  PM.add(llvm_seahorn::createInstructionCombiningPass()); // Initial cleanup 
  PM.add(createCFGSimplificationPass()); // if-convert
  PM.add(createSCCPPass()); // Propagate exposed constants
  PM.add(llvm_seahorn::createInstructionCombiningPass()); // Clean up again
  PM.add(createBitTrackingDCEPass());

  // More scalar chains could be vectorized due to more alias information
  if (RunSLPAfterLoopVectorization)
    if (SLPVectorize)
      PM.add(createSLPVectorizerPass()); // Vectorize parallel scalar chains.

  // After vectorization, assume intrinsics may tell us more about pointer
  // alignments.
  PM.add(createAlignmentFromAssumptionsPass());

  // Cleanup and simplify the code after the scalar optimizations.
  PM.add(llvm_seahorn::createInstructionCombiningPass());
  addExtensionsToPM(EP_Peephole, PM);

  PM.add(createJumpThreadingPass());
}

void PassManagerBuilder::addLateLTOOptimizationPasses(
    legacy::PassManagerBase &PM) {
  // Delete basic blocks, which optimization passes may have killed.
  PM.add(createCFGSimplificationPass());

  // Drop bodies of available externally objects to improve GlobalDCE.
  PM.add(createEliminateAvailableExternallyPass());

  // Now that we have optimized the program, discard unreachable functions.
  PM.add(createGlobalDCEPass());

  // FIXME: this is profitable (for compiler time) to do at -O0 too, but
  // currently it damages debug info.
  if (MergeFunctions)
    PM.add(createMergeFunctionsPass());
}

void PassManagerBuilder::populateLTOPassManager(legacy::PassManagerBase &PM) {
  if (LibraryInfo)
    PM.add(new TargetLibraryInfoWrapperPass(*LibraryInfo));

  if (VerifyInput)
    PM.add(createVerifierPass());

  if (OptLevel != 0)
    addLTOOptimizationPasses(PM);
  else {
    // The whole-program-devirt pass needs to run at -O0 because only it knows
    // about the llvm.type.checked.load intrinsic: it needs to both lower the
    // intrinsic itself and handle it in the summary.
    PM.add(createWholeProgramDevirtPass(ExportSummary, nullptr));
  }
  
  // Create a function that performs CFI checks for cross-DSO calls with targets
  // in the current module.
  PM.add(createCrossDSOCFIPass());

  // Lower bit sets to globals. This pass supports Clang's control flow
  // integrity mechanisms (-fsanitize=cfi*) and needs to run at link time if CFI
  // is enabled. The pass does nothing if CFI is disabled.
  PM.add(createLowerTypeTestsPass(ExportSummary, nullptr));  

  if (OptLevel != 0)
    addLateLTOOptimizationPasses(PM);

  if (VerifyOutput)
    PM.add(createVerifierPass());
}

// inline PassManagerBuilder *unwrap(LLVMPassManagerBuilderRef P) {
//     return reinterpret_cast<PassManagerBuilder*>(P);
// }

// inline LLVMPassManagerBuilderRef wrap(PassManagerBuilder *P) {
//   return reinterpret_cast<LLVMPassManagerBuilderRef>(P);
// }

// LLVMPassManagerBuilderRef LLVMPassManagerBuilderCreate() {
//   PassManagerBuilder *PMB = new PassManagerBuilder();
//   return wrap(PMB);
// }

// void LLVMPassManagerBuilderDispose(LLVMPassManagerBuilderRef PMB) {
//   PassManagerBuilder *Builder = unwrap(PMB);
//   delete Builder;
// }

// void
// LLVMPassManagerBuilderSetOptLevel(LLVMPassManagerBuilderRef PMB,
//                                   unsigned OptLevel) {
//   PassManagerBuilder *Builder = unwrap(PMB);
//   Builder->OptLevel = OptLevel;
// }

// void
// LLVMPassManagerBuilderSetSizeLevel(LLVMPassManagerBuilderRef PMB,
//                                    unsigned SizeLevel) {
//   PassManagerBuilder *Builder = unwrap(PMB);
//   Builder->SizeLevel = SizeLevel;
// }

// void
// LLVMPassManagerBuilderSetDisableUnitAtATime(LLVMPassManagerBuilderRef PMB,
//                                             LLVMBool Value) {
//   PassManagerBuilder *Builder = unwrap(PMB);
//   Builder->DisableUnitAtATime = Value;
// }

// void
// LLVMPassManagerBuilderSetDisableUnrollLoops(LLVMPassManagerBuilderRef PMB,
//                                             LLVMBool Value) {
//   PassManagerBuilder *Builder = unwrap(PMB);
//   Builder->DisableUnrollLoops = Value;
// }

// void
// LLVMPassManagerBuilderSetDisableSimplifyLibCalls(LLVMPassManagerBuilderRef PMB,
//                                                  LLVMBool Value) {
//   // NOTE: The simplify-libcalls pass has been removed.
// }

// void
// LLVMPassManagerBuilderUseInlinerWithThreshold(LLVMPassManagerBuilderRef PMB,
//                                               unsigned Threshold) {
//   PassManagerBuilder *Builder = unwrap(PMB);
//   Builder->Inliner = createFunctionInliningPass(Threshold);
// }

// void
// LLVMPassManagerBuilderPopulateFunctionPassManager(LLVMPassManagerBuilderRef PMB,
//                                                   LLVMPassManagerRef PM) {
//   PassManagerBuilder *Builder = unwrap(PMB);
//   legacy::FunctionPassManager *FPM = unwrap<legacy::FunctionPassManager>(PM);
//   Builder->populateFunctionPassManager(*FPM);
// }

// void
// LLVMPassManagerBuilderPopulateModulePassManager(LLVMPassManagerBuilderRef PMB,
//                                                 LLVMPassManagerRef PM) {
//   PassManagerBuilder *Builder = unwrap(PMB);
//   legacy::PassManagerBase *MPM = unwrap(PM);
//   Builder->populateModulePassManager(*MPM);
// }

// void LLVMPassManagerBuilderPopulateLTOPassManager(LLVMPassManagerBuilderRef PMB,
//                                                   LLVMPassManagerRef PM,
//                                                   LLVMBool Internalize,
//                                                   LLVMBool RunInliner) {
//   PassManagerBuilder *Builder = unwrap(PMB);
//   legacy::PassManagerBase *LPM = unwrap(PM);

//   // A small backwards compatibility hack. populateLTOPassManager used to take
//   // an RunInliner option.
//   if (RunInliner && !Builder->Inliner)
//     Builder->Inliner = createFunctionInliningPass();

//   Builder->populateLTOPassManager(*LPM);
// }

}
