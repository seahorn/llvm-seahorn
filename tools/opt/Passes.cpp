//===- Passes.cpp - Parsing, selection, and running of passes -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides the infrastructure to parse and build a custom pass
/// manager based on a commandline flag. It also provides helpers to aid in
/// analyzing, debugging, and testing pass structures.
///
//===----------------------------------------------------------------------===//

#include "Passes.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

namespace {

/// \brief No-op module pass which does nothing.
struct NoOpModulePass {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) { return PreservedAnalyses::all(); }
  static StringRef name() { return "NoOpModulePass"; }
};

/// \brief No-op module analysis.
struct NoOpModuleAnalysis : public AnalysisInfoMixin<NoOpModuleAnalysis> {
    struct Result {};
    Result run(Module &, ModuleAnalysisManager &) { return Result(); }
    static StringRef name() { return "NoOpModuleAnalysis"; }
    
private:
    friend AnalysisInfoMixin<NoOpModuleAnalysis>;
    static AnalysisKey Key;
};                                                                                                                  

/// \brief No-op CGSCC pass which does nothing.
struct NoOpCGSCCPass {
  PreservedAnalyses run(LazyCallGraph::SCC &C, CGSCCAnalysisManager &,
                        LazyCallGraph &, CGSCCUpdateResult &UR) {
    return PreservedAnalyses::all();
  }
  static StringRef name() { return "NoOpCGSCCPass"; }
  static AnalysisKey Key;
};

/// \brief No-op CGSCC analysis.
struct NoOpCGSCCAnalysis : public AnalysisInfoMixin<NoOpCGSCCPass> {
  struct Result {};
  Result run(LazyCallGraph::SCC &, CGSCCAnalysisManager &, LazyCallGraph &G) { return Result(); }
  static StringRef name() { return "NoOpCGSCCAnalysis"; }

private:
  friend AnalysisInfoMixin<NoOpCGSCCAnalysis>;
  static AnalysisKey Key;
};


/// \brief No-op function pass which does nothing.
struct NoOpFunctionPass {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) { return PreservedAnalyses::all(); }
  static StringRef name() { return "NoOpFunctionPass"; }
};

/// \brief No-op function analysis.
struct NoOpFunctionAnalysis : public AnalysisInfoMixin<NoOpFunctionAnalysis>{
  struct Result {};
  Result run(Function &, FunctionAnalysisManager &) { return Result(); }
  static StringRef name() { return "NoOpFunctionAnalysis"; }
 
private:
  friend AnalysisInfoMixin<NoOpFunctionAnalysis>;
  static AnalysisKey Key;
};

AnalysisKey NoOpModuleAnalysis::Key;
AnalysisKey NoOpCGSCCAnalysis::Key;
AnalysisKey NoOpFunctionAnalysis::Key;

} // End anonymous namespace.

void llvm::registerModuleAnalyses(ModuleAnalysisManager &MAM) {
#define MODULE_ANALYSIS(NAME, CREATE_PASS) \
  MAM.registerPass([&] { return CREATE_PASS; });
#include "PassRegistry.def"
}



void llvm::registerCGSCCAnalyses(CGSCCAnalysisManager &CGAM) {
#define CGSCC_ANALYSIS(NAME, CREATE_PASS) \
  CGAM.registerPass([&] { return CREATE_PASS; });
#include "PassRegistry.def"
}

void llvm::registerFunctionAnalyses(FunctionAnalysisManager &FAM) {
#define FUNCTION_ANALYSIS(NAME, CREATE_PASS) \
  FAM.registerPass([&] { return CREATE_PASS; });
#include "PassRegistry.def"
}

#ifndef NDEBUG
static bool isModulePassName(StringRef Name) {
#define MODULE_PASS(NAME, CREATE_PASS) if (Name == NAME) return true;
#define MODULE_ANALYSIS(NAME, CREATE_PASS)                                     \
  if (Name == "require<" NAME ">" || Name == "invalidate<" NAME ">")           \
    return true;
#include "PassRegistry.def"

  return false;
}
#endif

static bool isCGSCCPassName(StringRef Name) {
#define CGSCC_PASS(NAME, CREATE_PASS) if (Name == NAME) return true;
#define CGSCC_ANALYSIS(NAME, CREATE_PASS)                                      \
  if (Name == "require<" NAME ">" || Name == "invalidate<" NAME ">")           \
    return true;
#include "PassRegistry.def"

  return false;
}

static bool isFunctionPassName(StringRef Name) {
#define FUNCTION_PASS(NAME, CREATE_PASS) if (Name == NAME) return true;
#define FUNCTION_ANALYSIS(NAME, CREATE_PASS)                                   \
  if (Name == "require<" NAME ">" || Name == "invalidate<" NAME ">")           \
    return true;
#include "PassRegistry.def"

  return false;
}

static bool parseModulePassName(ModulePassManager &MPM, StringRef Name) {
#define MODULE_PASS(NAME, CREATE_PASS)                                         \
  if (Name == NAME) {                                                          \
    MPM.addPass(CREATE_PASS);                                                  \
    return true;                                                               \
  }
#define MODULE_ANALYSIS(NAME, CREATE_PASS)                                     \
  if (Name == "require<" NAME ">") {                                           \
    MPM.addPass( RequireAnalysisPass<std::remove_reference<decltype(CREATE_PASS)>::type, Module>()); \
    return true;                                                               \
  }                                                                            \
  if (Name == "invalidate<" NAME ">") {                                        \
    MPM.addPass(InvalidateAnalysisPass<std::remove_reference<decltype(CREATE_PASS)>::type>()); \
    return true;                                                               \
  }
#include "PassRegistry.def"

  return false;
}

static bool parseCGSCCPassName(CGSCCPassManager &CGPM, StringRef Name) {
#define CGSCC_PASS(NAME, CREATE_PASS)                                          \
  if (Name == NAME) {                                                          \
    CGPM.addPass(CREATE_PASS);                                                 \
    return true;                                                               \
  }
#define CGSCC_ANALYSIS(NAME, CREATE_PASS)                                      \
  if (Name == "require<" NAME ">") {                                           \
    CGPM.addPass(RequireAnalysisPass<std::remove_reference<decltype(CREATE_PASS)>::type, \
		 LazyCallGraph::SCC, CGSCCAnalysisManager, LazyCallGraph &, CGSCCUpdateResult &>()); \
    return true;                                                               \
  }                                                                            \
  if (Name == "invalidate<" NAME ">") {                                        \
    CGPM.addPass(InvalidateAnalysisPass<decltype(CREATE_PASS)>());             \
    return true;                                                               \
  }
#include "PassRegistry.def"

  return false;
}

static bool parseFunctionPassName(FunctionPassManager &FPM, StringRef Name) {
#define FUNCTION_PASS(NAME, CREATE_PASS)                                       \
  if (Name == NAME) {                                                          \
    FPM.addPass(CREATE_PASS);                                                  \
    return true;                                                               \
  }
#define FUNCTION_ANALYSIS(NAME, CREATE_PASS)                                   \
  if (Name == "require<" NAME ">") {                                           \
    FPM.addPass(RequireAnalysisPass<std::remove_reference<decltype(CREATE_PASS)>::type, Function>()); \
    return true;                                                               \
  }                                                                            \
  if (Name == "invalidate<" NAME ">") {                                        \
    FPM.addPass(InvalidateAnalysisPass<decltype(CREATE_PASS)>());              \
    return true;                                                               \
  }
#include "PassRegistry.def"

  return false;
}

static bool parseFunctionPassPipeline(FunctionPassManager &FPM,
                                      StringRef &PipelineText,
                                      bool VerifyEachPass, bool DebugLogging) {
  for (;;) {
    // Parse nested pass managers by recursing.
    if (PipelineText.startswith("function(")) {
      FunctionPassManager NestedFPM(DebugLogging);

      // Parse the inner pipeline inte the nested manager.
      PipelineText = PipelineText.substr(strlen("function("));
      if (!parseFunctionPassPipeline(NestedFPM, PipelineText, VerifyEachPass,
                                     DebugLogging) ||
          PipelineText.empty())
        return false;
      assert(PipelineText[0] == ')');
      PipelineText = PipelineText.substr(1);

      // Add the nested pass manager with the appropriate adaptor.
      FPM.addPass(std::move(NestedFPM));
    } else {
      // Otherwise try to parse a pass name.
      size_t End = PipelineText.find_first_of(",)");
      if (!parseFunctionPassName(FPM, PipelineText.substr(0, End)))
        return false;
      if (VerifyEachPass)
        FPM.addPass(VerifierPass());

      PipelineText = PipelineText.substr(End);
    }

    if (PipelineText.empty() || PipelineText[0] == ')')
      return true;

    assert(PipelineText[0] == ',');
    PipelineText = PipelineText.substr(1);
  }
}

static bool parseCGSCCPassPipeline(CGSCCPassManager &CGPM,
                                   StringRef &PipelineText, bool VerifyEachPass,
                                   bool DebugLogging) {
  for (;;) {
    // Parse nested pass managers by recursing.
    if (PipelineText.startswith("cgscc(")) {
      CGSCCPassManager NestedCGPM(DebugLogging);

      // Parse the inner pipeline into the nested manager.
      PipelineText = PipelineText.substr(strlen("cgscc("));
      if (!parseCGSCCPassPipeline(NestedCGPM, PipelineText, VerifyEachPass,
                                  DebugLogging) ||
          PipelineText.empty())
        return false;
      assert(PipelineText[0] == ')');
      PipelineText = PipelineText.substr(1);

      // Add the nested pass manager with the appropriate adaptor.
      CGPM.addPass(std::move(NestedCGPM));
    } else if (PipelineText.startswith("function(")) {
      FunctionPassManager NestedFPM(DebugLogging);

      // Parse the inner pipeline inte the nested manager.
      PipelineText = PipelineText.substr(strlen("function("));
      if (!parseFunctionPassPipeline(NestedFPM, PipelineText, VerifyEachPass,
                                     DebugLogging) ||
          PipelineText.empty())
        return false;
      assert(PipelineText[0] == ')');
      PipelineText = PipelineText.substr(1);

      // Add the nested pass manager with the appropriate adaptor.
      CGPM.addPass(createCGSCCToFunctionPassAdaptor(std::move(NestedFPM)));
    } else {
      // Otherwise try to parse a pass name.
      size_t End = PipelineText.find_first_of(",)");
      if (!parseCGSCCPassName(CGPM, PipelineText.substr(0, End)))
        return false;
      // FIXME: No verifier support for CGSCC passes!

      PipelineText = PipelineText.substr(End);
    }

    if (PipelineText.empty() || PipelineText[0] == ')')
      return true;

    assert(PipelineText[0] == ',');
    PipelineText = PipelineText.substr(1);
  }
}

static bool parseModulePassPipeline(ModulePassManager &MPM,
                                    StringRef &PipelineText,
                                    bool VerifyEachPass, bool DebugLogging) {
  for (;;) {
    // Parse nested pass managers by recursing.
    if (PipelineText.startswith("module(")) {
      ModulePassManager NestedMPM(DebugLogging);

      // Parse the inner pipeline into the nested manager.
      PipelineText = PipelineText.substr(strlen("module("));
      if (!parseModulePassPipeline(NestedMPM, PipelineText, VerifyEachPass,
                                   DebugLogging) ||
          PipelineText.empty())
        return false;
      assert(PipelineText[0] == ')');
      PipelineText = PipelineText.substr(1);

      // Now add the nested manager as a module pass.
      MPM.addPass(std::move(NestedMPM));
    } else if (PipelineText.startswith("cgscc(")) {
      CGSCCPassManager NestedCGPM(DebugLogging);

      // Parse the inner pipeline inte the nested manager.
      PipelineText = PipelineText.substr(strlen("cgscc("));
      if (!parseCGSCCPassPipeline(NestedCGPM, PipelineText, VerifyEachPass,
                                  DebugLogging) ||
          PipelineText.empty())
        return false;
      assert(PipelineText[0] == ')');
      PipelineText = PipelineText.substr(1);

      // Add the nested pass manager with the appropriate adaptor.
      MPM.addPass(
          createModuleToPostOrderCGSCCPassAdaptor(std::move(NestedCGPM)));
    } else if (PipelineText.startswith("function(")) {
      FunctionPassManager NestedFPM(DebugLogging);

      // Parse the inner pipeline inte the nested manager.
      PipelineText = PipelineText.substr(strlen("function("));
      if (!parseFunctionPassPipeline(NestedFPM, PipelineText, VerifyEachPass,
                                     DebugLogging) ||
          PipelineText.empty())
        return false;
      assert(PipelineText[0] == ')');
      PipelineText = PipelineText.substr(1);

      // Add the nested pass manager with the appropriate adaptor.
      MPM.addPass(createModuleToFunctionPassAdaptor(std::move(NestedFPM)));
    } else {
      // Otherwise try to parse a pass name.
      size_t End = PipelineText.find_first_of(",)");
      if (!parseModulePassName(MPM, PipelineText.substr(0, End)))
        return false;
      if (VerifyEachPass)
        MPM.addPass(VerifierPass());

      PipelineText = PipelineText.substr(End);
    }

    if (PipelineText.empty() || PipelineText[0] == ')')
      return true;

    assert(PipelineText[0] == ',');
    PipelineText = PipelineText.substr(1);
  }
}

// Primary pass pipeline description parsing routine.
// FIXME: Should this routine accept a TargetMachine or require the caller to
// pre-populate the analysis managers with target-specific stuff?
bool llvm::parsePassPipeline(ModulePassManager &MPM, StringRef PipelineText,
                             bool VerifyEachPass, bool DebugLogging) {
  // By default, try to parse the pipeline as-if it were within an implicit
  // 'module(...)' pass pipeline. If this will parse at all, it needs to
  // consume the entire string.
  if (parseModulePassPipeline(MPM, PipelineText, VerifyEachPass, DebugLogging))
    return PipelineText.empty();

  // This isn't parsable as a module pipeline, look for the end of a pass name
  // and directly drop down to that layer.
  StringRef FirstName =
      PipelineText.substr(0, PipelineText.find_first_of(",)"));
  assert(!isModulePassName(FirstName) &&
         "Already handled all module pipeline options.");

  // If this looks like a CGSCC pass, parse the whole thing as a CGSCC
  // pipeline.
  if (isCGSCCPassName(FirstName)) {
    CGSCCPassManager CGPM(DebugLogging);
    if (!parseCGSCCPassPipeline(CGPM, PipelineText, VerifyEachPass,
                                DebugLogging) ||
        !PipelineText.empty())
      return false;
    MPM.addPass(createModuleToPostOrderCGSCCPassAdaptor(std::move(CGPM)));
    return true;
  }

  // Similarly, if this looks like a Function pass, parse the whole thing as
  // a Function pipelien.
  if (isFunctionPassName(FirstName)) {
    FunctionPassManager FPM(DebugLogging);
    if (!parseFunctionPassPipeline(FPM, PipelineText, VerifyEachPass,
                                   DebugLogging) ||
        !PipelineText.empty())
      return false;
    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
    return true;
  }

  return false;
}
