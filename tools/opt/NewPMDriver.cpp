//===- NewPMDriver.cpp - Driver for opt with new PM -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file is just a split of the code that logically belongs in opt.cpp but
/// that includes the new pass manager headers.
///
//===----------------------------------------------------------------------===//

#include "NewPMDriver.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Bitcode/BitcodeWriterPass.h"
#include "llvm/IRPrinter/IRPrintingPasses.h"
#include <optional>
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/IPO/ThinLTOBitcodeWriter.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizer.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Utils/Debugify.h"
#include "llvm_seahorn/Transforms/InstCombine/SeaInstCombine.h"
#include "llvm_seahorn/Transforms/Scalar/SeaFakeLatchExit.h"
// Passes used to build SeaHorn's own -O pipeline (option C: construct the
// pipeline with the new pass-creation API rather than patching default<O#>).
#include "llvm/Passes/OptimizationLevel.h"
// --- passes for the dev15-faithful -O# pipeline (new-PM transcription of
// --- llvm-seahorn's forked legacy PassManagerBuilder) ---
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Transforms/IPO/CalledValuePropagation.h"
#include "llvm/Transforms/IPO/Inliner.h"
#include "llvm/Transforms/IPO/ConstantMerge.h"
#include "llvm/Transforms/IPO/DeadArgumentElimination.h"
#include "llvm/Transforms/IPO/FunctionAttrs.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "llvm/Transforms/IPO/GlobalOpt.h"
#include "llvm/Transforms/IPO/InferFunctionAttrs.h"
#include "llvm/Transforms/IPO/SCCP.h"
#include "llvm/Transforms/Scalar/ADCE.h"
#include "llvm/Transforms/Scalar/BDCE.h"
#include "llvm/Transforms/Scalar/CorrelatedValuePropagation.h"
#include "llvm/Transforms/Scalar/DeadStoreElimination.h"
#include "llvm/Transforms/Scalar/DivRemPairs.h"
#include "llvm/Transforms/Scalar/EarlyCSE.h"
#include "llvm/Transforms/Scalar/Float2Int.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/IndVarSimplify.h"
#include "llvm/Transforms/Scalar/JumpThreading.h"
#include "llvm/Transforms/Scalar/LICM.h"
#include "llvm/Transforms/Scalar/LoopDeletion.h"
#include "llvm/Transforms/Scalar/LoopInstSimplify.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Scalar/LoopRotation.h"
#include "llvm/Transforms/Scalar/LoopSimplifyCFG.h"
#include "llvm/Transforms/Scalar/LoopSink.h"
#include "llvm/Transforms/Scalar/LoopUnrollPass.h"
#include "llvm/Transforms/Scalar/LowerConstantIntrinsics.h"
#include "llvm/Transforms/Scalar/MemCpyOptimizer.h"
#include "llvm/Transforms/Scalar/MergedLoadStoreMotion.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Scalar/SCCP.h"
#include "llvm/Transforms/Scalar/SROA.h"
#include "llvm/Transforms/Scalar/SimpleLoopUnswitch.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include "llvm/Transforms/Scalar/SpeculativeExecution.h"
#include "llvm/Transforms/Scalar/TailRecursionElimination.h"
#include "llvm/Transforms/Utils/LibCallsShrinkWrap.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"

using namespace llvm;
using namespace opt_tool;

namespace llvm {
cl::opt<bool> DebugifyEach(
    "debugify-each",
    cl::desc("Start each pass with debugify and end it with check-debugify"));

cl::opt<std::string>
    DebugifyExport("debugify-export",
                   cl::desc("Export per-pass debugify statistics to this file"),
                   cl::value_desc("filename"));

cl::opt<bool> VerifyEachDebugInfoPreserve(
    "verify-each-debuginfo-preserve",
    cl::desc("Start each pass with collecting and end it with checking of "
             "debug info preservation."));

cl::opt<std::string>
    VerifyDIPreserveExport("verify-di-preserve-export",
                   cl::desc("Export debug info preservation failures into "
                            "specified (JSON) file (should be abs path as we use"
                            " append mode to insert new JSON objects)"),
                   cl::value_desc("filename"), cl::init(""));

} // namespace llvm

enum class DebugLogging { None, Normal, Verbose, Quiet };

static cl::opt<DebugLogging> DebugPM(
    "debug-pass-manager", cl::Hidden, cl::ValueOptional,
    cl::desc("Print pass management debugging information"),
    cl::init(DebugLogging::None),
    cl::values(
        clEnumValN(DebugLogging::Normal, "", ""),
        clEnumValN(DebugLogging::Quiet, "quiet",
                   "Skip printing info about analyses"),
        clEnumValN(
            DebugLogging::Verbose, "verbose",
            "Print extra information about adaptors and pass managers")));

// This flag specifies a textual description of the alias analysis pipeline to
// use when querying for aliasing information. It only works in concert with
// the "passes" flag above.
static cl::opt<std::string>
    AAPipeline("aa-pipeline",
               cl::desc("A textual description of the alias analysis "
                        "pipeline for handling managed aliasing queries"),
               cl::Hidden, cl::init("default"));

/// {{@ These options accept textual pipeline descriptions which will be
/// inserted into default pipelines at the respective extension points
static cl::opt<std::string> PeepholeEPPipeline(
    "passes-ep-peephole",
    cl::desc("A textual description of the function pass pipeline inserted at "
             "the Peephole extension points into default pipelines"),
    cl::Hidden);
static cl::opt<std::string> LateLoopOptimizationsEPPipeline(
    "passes-ep-late-loop-optimizations",
    cl::desc(
        "A textual description of the loop pass pipeline inserted at "
        "the LateLoopOptimizations extension point into default pipelines"),
    cl::Hidden);
static cl::opt<std::string> LoopOptimizerEndEPPipeline(
    "passes-ep-loop-optimizer-end",
    cl::desc("A textual description of the loop pass pipeline inserted at "
             "the LoopOptimizerEnd extension point into default pipelines"),
    cl::Hidden);
static cl::opt<std::string> ScalarOptimizerLateEPPipeline(
    "passes-ep-scalar-optimizer-late",
    cl::desc("A textual description of the function pass pipeline inserted at "
             "the ScalarOptimizerLate extension point into default pipelines"),
    cl::Hidden);
static cl::opt<std::string> CGSCCOptimizerLateEPPipeline(
    "passes-ep-cgscc-optimizer-late",
    cl::desc("A textual description of the cgscc pass pipeline inserted at "
             "the CGSCCOptimizerLate extension point into default pipelines"),
    cl::Hidden);
static cl::opt<std::string> VectorizerStartEPPipeline(
    "passes-ep-vectorizer-start",
    cl::desc("A textual description of the function pass pipeline inserted at "
             "the VectorizerStart extension point into default pipelines"),
    cl::Hidden);
static cl::opt<std::string> PipelineStartEPPipeline(
    "passes-ep-pipeline-start",
    cl::desc("A textual description of the module pass pipeline inserted at "
             "the PipelineStart extension point into default pipelines"),
    cl::Hidden);
static cl::opt<std::string> PipelineEarlySimplificationEPPipeline(
    "passes-ep-pipeline-early-simplification",
    cl::desc("A textual description of the module pass pipeline inserted at "
             "the EarlySimplification extension point into default pipelines"),
    cl::Hidden);
static cl::opt<std::string> OptimizerEarlyEPPipeline(
    "passes-ep-optimizer-early",
    cl::desc("A textual description of the module pass pipeline inserted at "
             "the OptimizerEarly extension point into default pipelines"),
    cl::Hidden);
static cl::opt<std::string> OptimizerLastEPPipeline(
    "passes-ep-optimizer-last",
    cl::desc("A textual description of the module pass pipeline inserted at "
             "the OptimizerLast extension point into default pipelines"),
    cl::Hidden);
static cl::opt<std::string> FullLinkTimeOptimizationEarlyEPPipeline(
    "passes-ep-full-link-time-optimization-early",
    cl::desc("A textual description of the module pass pipeline inserted at "
             "the FullLinkTimeOptimizationEarly extension point into default "
             "pipelines"),
    cl::Hidden);
static cl::opt<std::string> FullLinkTimeOptimizationLastEPPipeline(
    "passes-ep-full-link-time-optimization-last",
    cl::desc("A textual description of the module pass pipeline inserted at "
             "the FullLinkTimeOptimizationLast extension point into default "
             "pipelines"),
    cl::Hidden);

// Individual pipeline tuning options.
extern cl::opt<bool> DisableLoopUnrolling;

namespace llvm {
extern cl::opt<PGOKind> PGOKindFlag;
extern cl::opt<std::string> ProfileFile;
extern cl::opt<CSPGOKind> CSPGOKindFlag;
extern cl::opt<std::string> CSProfileGenFile;
extern cl::opt<bool> DisableBasicAA;
extern cl::opt<bool> PrintPipelinePasses;
} // namespace llvm

static cl::opt<std::string>
    ProfileRemappingFile("profile-remapping-file",
                         cl::desc("Path to the profile remapping file."),
                         cl::Hidden);
static cl::opt<bool> DebugInfoForProfiling(
    "new-pm-debug-info-for-profiling", cl::init(false), cl::Hidden,
    cl::desc("Emit special debug info to enable PGO profile generation."));
static cl::opt<bool> PseudoProbeForProfiling(
    "new-pm-pseudo-probe-for-profiling", cl::init(false), cl::Hidden,
    cl::desc("Emit pseudo probes to enable PGO profile generation."));
/// @}}

template <typename PassManagerT>
bool tryParsePipelineText(PassBuilder &PB,
                          const cl::opt<std::string> &PipelineOpt) {
  if (PipelineOpt.empty())
    return false;

  // Verify the pipeline is parseable:
  PassManagerT PM;
  if (auto Err = PB.parsePassPipeline(PM, PipelineOpt)) {
    errs() << "Could not parse -" << PipelineOpt.ArgStr
           << " pipeline: " << toString(std::move(Err))
           << "... I'm going to ignore it.\n";
    return false;
  }
  return true;
}

/// If one of the EPPipeline command line options was given, register callbacks
/// for parsing and inserting the given pipeline
static void registerEPCallbacks(PassBuilder &PB) {
  if (tryParsePipelineText<FunctionPassManager>(PB, PeepholeEPPipeline))
    PB.registerPeepholeEPCallback(
        [&PB](FunctionPassManager &PM, OptimizationLevel Level) {
          ExitOnError Err("Unable to parse PeepholeEP pipeline: ");
          Err(PB.parsePassPipeline(PM, PeepholeEPPipeline));
        });
  if (tryParsePipelineText<LoopPassManager>(PB,
                                            LateLoopOptimizationsEPPipeline))
    PB.registerLateLoopOptimizationsEPCallback(
        [&PB](LoopPassManager &PM, OptimizationLevel Level) {
          ExitOnError Err("Unable to parse LateLoopOptimizationsEP pipeline: ");
          Err(PB.parsePassPipeline(PM, LateLoopOptimizationsEPPipeline));
        });
  if (tryParsePipelineText<LoopPassManager>(PB, LoopOptimizerEndEPPipeline))
    PB.registerLoopOptimizerEndEPCallback(
        [&PB](LoopPassManager &PM, OptimizationLevel Level) {
          ExitOnError Err("Unable to parse LoopOptimizerEndEP pipeline: ");
          Err(PB.parsePassPipeline(PM, LoopOptimizerEndEPPipeline));
        });
  if (tryParsePipelineText<FunctionPassManager>(PB,
                                                ScalarOptimizerLateEPPipeline))
    PB.registerScalarOptimizerLateEPCallback(
        [&PB](FunctionPassManager &PM, OptimizationLevel Level) {
          ExitOnError Err("Unable to parse ScalarOptimizerLateEP pipeline: ");
          Err(PB.parsePassPipeline(PM, ScalarOptimizerLateEPPipeline));
        });
  if (tryParsePipelineText<CGSCCPassManager>(PB, CGSCCOptimizerLateEPPipeline))
    PB.registerCGSCCOptimizerLateEPCallback(
        [&PB](CGSCCPassManager &PM, OptimizationLevel Level) {
          ExitOnError Err("Unable to parse CGSCCOptimizerLateEP pipeline: ");
          Err(PB.parsePassPipeline(PM, CGSCCOptimizerLateEPPipeline));
        });
  if (tryParsePipelineText<FunctionPassManager>(PB, VectorizerStartEPPipeline))
    PB.registerVectorizerStartEPCallback(
        [&PB](FunctionPassManager &PM, OptimizationLevel Level) {
          ExitOnError Err("Unable to parse VectorizerStartEP pipeline: ");
          Err(PB.parsePassPipeline(PM, VectorizerStartEPPipeline));
        });
  if (tryParsePipelineText<ModulePassManager>(PB, PipelineStartEPPipeline))
    PB.registerPipelineStartEPCallback(
        [&PB](ModulePassManager &PM, OptimizationLevel) {
          ExitOnError Err("Unable to parse PipelineStartEP pipeline: ");
          Err(PB.parsePassPipeline(PM, PipelineStartEPPipeline));
        });
  if (tryParsePipelineText<ModulePassManager>(
          PB, PipelineEarlySimplificationEPPipeline))
    PB.registerPipelineEarlySimplificationEPCallback(
        [&PB](ModulePassManager &PM, OptimizationLevel) {
          ExitOnError Err("Unable to parse EarlySimplification pipeline: ");
          Err(PB.parsePassPipeline(PM, PipelineEarlySimplificationEPPipeline));
        });
  if (tryParsePipelineText<ModulePassManager>(PB, OptimizerEarlyEPPipeline))
    PB.registerOptimizerEarlyEPCallback(
        [&PB](ModulePassManager &PM, OptimizationLevel) {
          ExitOnError Err("Unable to parse OptimizerEarlyEP pipeline: ");
          Err(PB.parsePassPipeline(PM, OptimizerEarlyEPPipeline));
        });
  if (tryParsePipelineText<ModulePassManager>(PB, OptimizerLastEPPipeline))
    PB.registerOptimizerLastEPCallback(
        [&PB](ModulePassManager &PM, OptimizationLevel) {
          ExitOnError Err("Unable to parse OptimizerLastEP pipeline: ");
          Err(PB.parsePassPipeline(PM, OptimizerLastEPPipeline));
        });
  if (tryParsePipelineText<ModulePassManager>(
          PB, FullLinkTimeOptimizationEarlyEPPipeline))
    PB.registerFullLinkTimeOptimizationEarlyEPCallback(
        [&PB](ModulePassManager &PM, OptimizationLevel) {
          ExitOnError Err(
              "Unable to parse FullLinkTimeOptimizationEarlyEP pipeline: ");
          Err(PB.parsePassPipeline(PM,
                                   FullLinkTimeOptimizationEarlyEPPipeline));
        });
  if (tryParsePipelineText<ModulePassManager>(
          PB, FullLinkTimeOptimizationLastEPPipeline))
    PB.registerFullLinkTimeOptimizationLastEPCallback(
        [&PB](ModulePassManager &PM, OptimizationLevel) {
          ExitOnError Err(
              "Unable to parse FullLinkTimeOptimizationLastEP pipeline: ");
          Err(PB.parsePassPipeline(PM, FullLinkTimeOptimizationLastEPPipeline));
        });
}

#if 0 /*  SEAHORN REMOVE */
#define HANDLE_EXTENSION(Ext)                                                  \
  llvm::PassPluginLibraryInfo get##Ext##PluginInfo();
#include "llvm/Support/Extension.def"
#endif

// SEAHORN: optionally append sea-fake-latch-exit at the end of the sea -O
// pipeline. Off by default, mirroring dev15 (where it sat behind the
// always-false `sea-never-true` guard): the fake `br i1 true` exit is only
// meaningful as the very last step, since simplifycfg/instcombine fold it away.
static cl::opt<bool>
    SeaFakeLatchExitInO("seaopt-fake-latch-exit", cl::Hidden, cl::init(false),
                        cl::desc("Append sea-fake-latch-exit to the sea -O "
                                 "pipeline (gives unconditional-latch loops a "
                                 "fake always-taken exit edge)"));

// SEAHORN: run IndVarSimplify in the sea -O# pipeline (dev15's
// --seaopt-enable-indvar, same name, opposite default). Off by default so a
// bare `seaopt -O#` keeps assume-bounded loops intact for SeaHorn's
// unroll/cut-loops stage. The sea driver passes =true on its bounded (BMC)
// flows, where the fold is welcome: exit-value rewriting soundly summarizes a
// summarizable loop, and a non-summarizable survivor is handled by the BMC
// VC-gen mode (unify-assumes + dataflow + coi + gsa).
static cl::opt<bool>
    SeaEnableIndVar("seaopt-enable-indvar", cl::Hidden, cl::init(false),
                    cl::desc("Enable IndVarSimplify in the sea -O# pipeline"));

// SEAHORN: SeaHorn's -O# pipeline, a new-PM transcription of llvm-seahorn's
// forked legacy PassManagerBuilder (lib/Transforms/IPO/PassManagerBuilder.cpp).
// That dev15 pipeline is known to clean SeaHorn's IR (e.g. PromoteMemcpy
// field-copies, via GlobalsAA + GVN/MemCpyOpt/DSE) yet keep loops intact for
// SeaHorn's own -sea-loop-unroll/cut-loops/--assert-on-backedge machinery.
// Differences from stock default<O#>, both deliberate: stock InstCombine ->
// SeaInstCombine, and loop unrolling/vectorization are omitted (SeaHorn drives
// unrolling itself; full O3's loop-unroll erases the backedge and breaks bounded
// loop verification). `-passes=default<O#>` remains the untouched stock hatch.
static SimplifyCFGOptions seaSimplifyCFGSwitch() {
  return SimplifyCFGOptions().convertSwitchRangeToICmp(true);
}

// dev15 addFunctionSimplificationPasses (OptLevel>=2), minus loop unrolling.
static void seaAddFunctionSimplification(FunctionPassManager &FPM,
                                         OptimizationLevel Level) {
  FPM.addPass(SROAPass(SROAOptions::ModifyCFG));
  FPM.addPass(EarlyCSEPass(/*UseMemorySSA=*/true));
  FPM.addPass(SpeculativeExecutionPass(/*OnlyIfDivergentTarget=*/true));
  FPM.addPass(JumpThreadingPass());
  FPM.addPass(CorrelatedValuePropagationPass());
  FPM.addPass(SimplifyCFGPass(seaSimplifyCFGSwitch()));
  FPM.addPass(llvm_seahorn::SeaInstCombinePass());
  FPM.addPass(LibCallsShrinkWrapPass());
  FPM.addPass(TailCallElimPass());
  FPM.addPass(SimplifyCFGPass(seaSimplifyCFGSwitch()));
  FPM.addPass(ReassociatePass());

  LoopPassManager LPM1;
  LPM1.addPass(LoopInstSimplifyPass());
  LPM1.addPass(LoopSimplifyCFGPass());
  LPM1.addPass(LICMPass(LICMOptions()));
  LPM1.addPass(LoopRotatePass());
  LPM1.addPass(LICMPass(LICMOptions()));
  LPM1.addPass(SimpleLoopUnswitchPass(/*NonTrivial=*/Level ==
                                      OptimizationLevel::O3));
  FPM.addPass(createFunctionToLoopPassAdaptor(
      std::move(LPM1), /*UseMemorySSA=*/true, /*UseBlockFrequencyInfo=*/true));
  FPM.addPass(SimplifyCFGPass(seaSimplifyCFGSwitch()));
  FPM.addPass(llvm_seahorn::SeaInstCombinePass());

  // dev15 ran { LoopIdiom, IndVarSimplify, LoopDeletion } here, then a loop
  // unroller, with LoopIdiom and IndVarSimplify behind flags the sea driver
  // turned off. LoopIdiom stays omitted (it rewrites loops into memset/memcpy
  // intrinsics opsem would then have to model). IndVarSimplify is gated by
  // --seaopt-enable-indvar (default off): its exit-value rewriting summarizes
  // an __VERIFIER_assume-bounded loop into its SCEV closed form (e.g.
  // c_final = smax(c,limit)) and deletes the loop -- taking it away from
  // SeaHorn's -sea-loop-unroll/cut-loops stage. The fold itself is sound, so
  // the sea driver enables it on bounded (BMC) flows, whose full VC-gen mode
  // handles any loop that survives.
  LoopPassManager LPM2;
  if (SeaEnableIndVar)
    LPM2.addPass(IndVarSimplifyPass());
  LPM2.addPass(LoopDeletionPass());
  FPM.addPass(createFunctionToLoopPassAdaptor(std::move(LPM2)));
  FPM.addPass(LoopUnrollPass(LoopUnrollOptions(Level.getSpeedupLevel())));

  FPM.addPass(SROAPass(SROAOptions::ModifyCFG));
  FPM.addPass(MergedLoadStoreMotionPass());
  FPM.addPass(GVNPass());
  FPM.addPass(SCCPPass());
  FPM.addPass(BDCEPass());
  FPM.addPass(llvm_seahorn::SeaInstCombinePass());
  FPM.addPass(JumpThreadingPass());
  FPM.addPass(CorrelatedValuePropagationPass());
  FPM.addPass(ADCEPass());
  FPM.addPass(MemCpyOptPass());
  FPM.addPass(DSEPass());
  FPM.addPass(createFunctionToLoopPassAdaptor(
      LICMPass(LICMOptions()), /*UseMemorySSA=*/true,
      /*UseBlockFrequencyInfo=*/true));
  FPM.addPass(SimplifyCFGPass(
      SimplifyCFGOptions().hoistCommonInsts(true).sinkCommonInsts(true)));
  FPM.addPass(llvm_seahorn::SeaInstCombinePass());
}

static void buildSeaPipeline(ModulePassManager &MPM, OptimizationLevel Level) {
  if (Level == OptimizationLevel::O0) {
    MPM.addPass(createModuleToFunctionPassAdaptor(PromotePass()));
    return;
  }

  // ---- module-level setup (dev15 populateModulePassManager) ----
  MPM.addPass(InferFunctionAttrsPass());
  MPM.addPass(IPSCCPPass());
  MPM.addPass(CalledValuePropagationPass());
  MPM.addPass(GlobalOptPass());
  MPM.addPass(createModuleToFunctionPassAdaptor(PromotePass()));
  MPM.addPass(DeadArgumentEliminationPass());
  {
    FunctionPassManager FPM;
    FPM.addPass(llvm_seahorn::SeaInstCombinePass());
    FPM.addPass(SimplifyCFGPass(seaSimplifyCFGSwitch()));
    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
  }
  // Run the function simplification nested in the CGSCC inliner, exactly as
  // dev15's populateModulePassManager did (MPM.add(Inliner) then
  // addFunctionSimplificationPasses). The inliner is load-bearing for cleanup:
  // it inlines residual callees that hold SeaHorn's PromoteMemcpy structs, after
  // which SROA/GVN can scalarize and forward the field-copies. Without it
  // push_back/push_front stay ~40x slow (SROA alone can't crack the structs).
  {
    ModuleInlinerWrapperPass MIWP(
        getInlineParams(Level.getSpeedupLevel(), Level.getSizeLevel()));
    // GlobalsAA gives GVN/MemCpyOpt the alias info to forward field-copies.
    MIWP.addModulePass(RequireAnalysisPass<GlobalsAA, Module>());
    CGSCCPassManager &CG = MIWP.getPM();
    CG.addPass(PostOrderFunctionAttrsPass());
    FunctionPassManager FPM;
    seaAddFunctionSimplification(FPM, Level);
    CG.addPass(createCGSCCToFunctionPassAdaptor(std::move(FPM)));
    MPM.addPass(std::move(MIWP));
  }

  MPM.addPass(ReversePostOrderFunctionAttrsPass());
  MPM.addPass(RequireAnalysisPass<GlobalsAA, Module>());

  // ---- late cleanup (dev15 tail; loop vectorization omitted) ----
  {
    FunctionPassManager FPM;
    FPM.addPass(Float2IntPass());
    FPM.addPass(LowerConstantIntrinsicsPass());
    LoopPassManager LPM;
    LPM.addPass(LoopRotatePass());
    FPM.addPass(createFunctionToLoopPassAdaptor(std::move(LPM)));
    FPM.addPass(llvm_seahorn::SeaInstCombinePass());
    FPM.addPass(LoopSinkPass());
    FPM.addPass(DivRemPairsPass());
    FPM.addPass(SimplifyCFGPass(seaSimplifyCFGSwitch()));
    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
  }
  MPM.addPass(GlobalDCEPass());
  MPM.addPass(ConstantMergePass());

  // Optional: give unconditional-latch loops a fake exit, as the very last step.
  if (SeaFakeLatchExitInO) {
    FunctionPassManager Late;
    Late.addPass(llvm_seahorn::SeaFakeLatchExitPass());
    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(Late)));
  }
}

// Map seaopt's "default<O#>" pipeline string (built in opt.cpp for -O#) to an
// OptimizationLevel for buildSeaPipeline.
static std::optional<OptimizationLevel> seaParseOptLevel(StringRef P) {
  return llvm::StringSwitch<std::optional<OptimizationLevel>>(P)
      .Case("default<O0>", OptimizationLevel::O0)
      .Case("default<O1>", OptimizationLevel::O1)
      .Case("default<O2>", OptimizationLevel::O2)
      .Case("default<O3>", OptimizationLevel::O3)
      .Case("default<Os>", OptimizationLevel::Os)
      .Case("default<Oz>", OptimizationLevel::Oz)
      .Default(std::nullopt);
}

bool llvm::runPassPipeline(StringRef Arg0, Module &M, TargetMachine *TM,
                           TargetLibraryInfoImpl *TLII, ToolOutputFile *Out,
                           ToolOutputFile *ThinLTOLinkOut,
                           ToolOutputFile *OptRemarkFile,
                           StringRef PassPipeline, ArrayRef<StringRef> Passes,
                           ArrayRef<PassPlugin> PassPlugins,
                           OutputKind OK, VerifierKind VK,
                           bool ShouldPreserveAssemblyUseListOrder,
                           bool ShouldPreserveBitcodeUseListOrder,
                           bool EmitSummaryIndex, bool EmitModuleHash,
                           bool EnableDebugify, bool VerifyDIPreserve,
                           bool SeaCustomizeOPipeline) {
  bool VerifyEachPass = VK == VK_VerifyEachPass;

  std::optional<PGOOptions> P;
  switch (PGOKindFlag) {
  case InstrGen:
    P = PGOOptions(ProfileFile, "", "", PGOOptions::IRInstr);
    break;
  case InstrUse:
    P = PGOOptions(ProfileFile, "", ProfileRemappingFile, PGOOptions::IRUse);
    break;
  case SampleUse:
    P = PGOOptions(ProfileFile, "", ProfileRemappingFile,
                   PGOOptions::SampleUse);
    break;
  case NoPGO:
    if (DebugInfoForProfiling || PseudoProbeForProfiling)
      P = PGOOptions("", "", "", PGOOptions::NoAction, PGOOptions::NoCSAction,
                     DebugInfoForProfiling, PseudoProbeForProfiling);
    else
      P = std::nullopt;
  }
  if (CSPGOKindFlag != NoCSPGO) {
    if (P && (P->Action == PGOOptions::IRInstr ||
              P->Action == PGOOptions::SampleUse))
      errs() << "CSPGOKind cannot be used with IRInstr or SampleUse";
    if (CSPGOKindFlag == CSInstrGen) {
      if (CSProfileGenFile.empty())
        errs() << "CSInstrGen needs to specify CSProfileGenFile";
      if (P) {
        P->CSAction = PGOOptions::CSIRInstr;
        P->CSProfileGenFile = CSProfileGenFile;
      } else
        P = PGOOptions("", CSProfileGenFile, ProfileRemappingFile,
                       PGOOptions::NoAction, PGOOptions::CSIRInstr);
    } else /* CSPGOKindFlag == CSInstrUse */ {
      if (!P)
        errs() << "CSInstrUse needs to be together with InstrUse";
      P->CSAction = PGOOptions::CSIRUse;
    }
  }
  if (TM)
    TM->setPGOOption(P);

  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  PassInstrumentationCallbacks PIC;
  PrintPassOptions PrintPassOpts;
  PrintPassOpts.Verbose = DebugPM == DebugLogging::Verbose;
  PrintPassOpts.SkipAnalyses = DebugPM == DebugLogging::Quiet;
  StandardInstrumentations SI(M.getContext(), DebugPM != DebugLogging::None, VerifyEachPass,
                              PrintPassOpts);
  SI.registerCallbacks(PIC, &FAM);
  DebugifyEachInstrumentation Debugify;
  DebugifyStatsMap DIStatsMap;
  DebugInfoPerPass DebugInfoBeforePass;
  if (DebugifyEach) {
    Debugify.setDIStatsMap(DIStatsMap);
    Debugify.setDebugifyMode(DebugifyMode::SyntheticDebugInfo);
    Debugify.registerCallbacks(PIC);
  } else if (VerifyEachDebugInfoPreserve) {
    Debugify.setDebugInfoBeforePass(DebugInfoBeforePass);
    Debugify.setDebugifyMode(DebugifyMode::OriginalDebugInfo);
    Debugify.setOrigDIVerifyBugsReportFilePath(
      VerifyDIPreserveExport);
    Debugify.registerCallbacks(PIC);
  }

  PipelineTuningOptions PTO;
  // LoopUnrolling defaults on to true and DisableLoopUnrolling is initialized
  // to false above so we shouldn't necessarily need to check whether or not the
  // option has been enabled.
  PTO.LoopUnrolling = !DisableLoopUnrolling;
  PassBuilder PB(TM, PTO, P, &PIC);
  registerEPCallbacks(PB);

  // For any loaded plugins, let them register pass builder callbacks.
  for (auto &PassPlugin : PassPlugins)
    PassPlugin.registerPassBuilderCallbacks(PB);

  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        AddressSanitizerOptions Opts;
        if (Name == "asan-pipeline") {
          MPM.addPass(AddressSanitizerPass(Opts));
          return true;
        }
        // SeaHorn's InstCombine as a new-PM function pass. The default ctor
        // reads the seaopt-instcombine-avoid-* flags; AA comes from the
        // pipeline's AAManager, so no -tbaa -basic-aa scheduling is needed.
        if (Name == "sea-instcombine") {
          MPM.addPass(createModuleToFunctionPassAdaptor(
              llvm_seahorn::SeaInstCombinePass()));
          return true;
        }
        return false;
      });

  // Same pass, registered at the function level so it can be named inside a
  // `function(...)` pipeline string (e.g. -passes=sea-instcombine, used by the
  // sea_instcombine / sea_transforms lit tests). seaopt's own -O# pipeline adds
  // SeaInstCombinePass directly (see buildSeaPipeline), so it does not rely on
  // this callback.
  PB.registerPipelineParsingCallback(
      [](StringRef Name, FunctionPassManager &FPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name == "sea-instcombine") {
          FPM.addPass(llvm_seahorn::SeaInstCombinePass());
          return true;
        }
        if (Name == "sea-fake-latch-exit") {
          FPM.addPass(llvm_seahorn::SeaFakeLatchExitPass());
          return true;
        }
        return false;
      });

#if 0 /*  SEAHORN REMOVED */
#define HANDLE_EXTENSION(Ext)                                                  \
  get##Ext##PluginInfo().RegisterPassBuilderCallbacks(PB);
#include "llvm/Support/Extension.def"
#endif

  // Specially handle the alias analysis manager so that we can register
  // a custom pipeline of AA passes with it.
  AAManager AA;
  if (Passes.empty()) {
    // SeaHorn's -O# pipeline needs globals-aa in the AAManager so GVN/MemCpyOpt
    // forward PromoteMemcpy struct field-copies (this is what dev15's legacy
    // createGlobalsAAWrapperPass provided). The stock "default" AA pipeline
    // omits globals-aa, which made push_back/push_front ~40x slower.
    if (auto Err = PB.parseAAPipeline(AA, AAPipeline)) {
      errs() << Arg0 << ": " << toString(std::move(Err)) << "\n";
      return false;
    }
  }

  // For compatibility with the legacy PM AA pipeline.
  // AAResultsWrapperPass by default provides basic-aa in the legacy PM
  // unless -disable-basic-aa is specified.
  // TODO: remove this once tests implicitly requiring basic-aa use -passes= and
  // -aa-pipeline=basic-aa.
  if (!Passes.empty() && !DisableBasicAA) {
    if (auto Err = PB.parseAAPipeline(AA, "basic-aa")) {
      errs() << Arg0 << ": " << toString(std::move(Err)) << "\n";
      return false;
    }
  }

  // For compatibility with legacy pass manager.
  // Alias analyses are not specially specified when using the legacy PM.
  for (auto PassName : Passes) {
    if (false /*isAAPassName*/) {
      if (auto Err = PB.parseAAPipeline(AA, PassName)) {
        errs() << Arg0 << ": " << toString(std::move(Err)) << "\n";
        return false;
      }
    }
  }

  // Register the AA manager first so that our version is the one used.
  FAM.registerPass([&] { return std::move(AA); });
  // Register our TargetLibraryInfoImpl.
  FAM.registerPass([&] { return TargetLibraryAnalysis(*TLII); });

  // Register all the basic analyses with the managers.
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  ModulePassManager MPM;
  if (VK > VK_NoVerifier)
    MPM.addPass(VerifierPass());
  if (EnableDebugify)
    MPM.addPass(NewPMDebugifyPass());
  if (VerifyDIPreserve)
    MPM.addPass(NewPMDebugifyPass(DebugifyMode::OriginalDebugInfo, "",
                                  &DebugInfoBeforePass));

  // Add passes according to the -passes options.
  if (!PassPipeline.empty()) {
    assert(Passes.empty() &&
           "PassPipeline and Passes should not both contain passes");
    if (SeaCustomizeOPipeline) {
      // seaopt's -O# builds SeaHorn's own new-PM pipeline (sea-instcombine in
      // place of stock instcombine) -- see buildSeaPipeline. opt.cpp passes the
      // requested level as the "default<O#>" string.
      auto Level = seaParseOptLevel(PassPipeline);
      if (!Level) {
        errs() << Arg0 << ": sea -O pipeline: unexpected level '" << PassPipeline
               << "'\n";
        return false;
      }
      buildSeaPipeline(MPM, *Level);
    } else if (auto Err = PB.parsePassPipeline(MPM, PassPipeline)) {
      errs() << Arg0 << ": " << toString(std::move(Err)) << "\n";
      return false;
    }
  }
  // Add passes specified using the legacy PM syntax (i.e. not using
  // -passes). This should be removed later when such support has been
  // deprecated, i.e. when all lit tests running opt (and not using
  // -enable-new-pm=0) have been updated to use -passes.
  for (auto PassName : Passes) {
    std::string ModifiedPassName(PassName.begin(), PassName.end());
    if (false /*isAnalysisPassName*/)
      ModifiedPassName = "require<" + ModifiedPassName + ">";
    // FIXME: These translations are supposed to be removed when lit tests that
    // use these names have been updated to use the -passes syntax (and when the
    // support for using the old syntax to specify passes is considered as
    // deprecated for the new PM).
    if (ModifiedPassName == "early-cse-memssa")
      ModifiedPassName = "early-cse<memssa>";
    else if (ModifiedPassName == "post-inline-ee-instrument")
      ModifiedPassName = "ee-instrument<post-inline>";
    else if (ModifiedPassName == "loop-extract-single")
      ModifiedPassName = "loop-extract<single>";
    else if (ModifiedPassName == "lower-matrix-intrinsics-minimal")
      ModifiedPassName = "lower-matrix-intrinsics<minimal>";
    if (auto Err = PB.parsePassPipeline(MPM, ModifiedPassName)) {
      errs() << Arg0 << ": " << toString(std::move(Err)) << "\n";
      return false;
    }
  }

  if (VK > VK_NoVerifier)
    MPM.addPass(VerifierPass());
  if (EnableDebugify)
    MPM.addPass(NewPMCheckDebugifyPass(false, "", &DIStatsMap));
  if (VerifyDIPreserve)
    MPM.addPass(NewPMCheckDebugifyPass(
        false, "", nullptr, DebugifyMode::OriginalDebugInfo, &DebugInfoBeforePass,
        VerifyDIPreserveExport));

  // Add any relevant output pass at the end of the pipeline.
  switch (OK) {
  case OK_NoOutput:
    break; // No output pass needed.
  case OK_OutputAssembly:
    MPM.addPass(
        PrintModulePass(Out->os(), "", ShouldPreserveAssemblyUseListOrder));
    break;
  case OK_OutputBitcode:
    MPM.addPass(BitcodeWriterPass(Out->os(), ShouldPreserveBitcodeUseListOrder,
                                  EmitSummaryIndex, EmitModuleHash));
    break;
  case OK_OutputThinLTOBitcode:
    MPM.addPass(ThinLTOBitcodeWriterPass(
        Out->os(), ThinLTOLinkOut ? &ThinLTOLinkOut->os() : nullptr));
    break;
  }

  // Before executing passes, print the final values of the LLVM options.
  cl::PrintOptionValues();

  // Print a textual, '-passes=' compatible, representation of pipeline if
  // requested.
  if (PrintPipelinePasses) {
    MPM.printPipeline(outs(), [&PIC](StringRef ClassName) {
      auto PassName = PIC.getPassNameForClassName(ClassName);
      return PassName.empty() ? ClassName : PassName;
    });
    outs() << "\n";
    return true;
  }

  // Now that we have all of the passes ready, run them.
  MPM.run(M, MAM);

  // Declare success.
  if (OK != OK_NoOutput) {
    Out->keep();
    if (OK == OK_OutputThinLTOBitcode && ThinLTOLinkOut)
      ThinLTOLinkOut->keep();
  }

  if (OptRemarkFile)
    OptRemarkFile->keep();

  if (DebugifyEach && !DebugifyExport.empty())
    exportDebugifyStats(DebugifyExport, Debugify.getDebugifyStatsMap());

  return true;
}

void llvm::printPasses(raw_ostream &OS) {
  PassBuilder PB;
  PB.printPassNames(OS);
}
