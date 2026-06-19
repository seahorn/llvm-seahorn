//===- sea_loop_extract_driver.cpp ---------------------------------------===//
// Standalone driver that exercises the SeaHorn loop-extract pass
// (createSeaLoopExtractorPass / replaceFnBodyWithND) directly via the library
// API -- without registering the pass in seaopt.
//
// Usage:  sea_loop_extract_driver input.ll
// Runs SeaLoopExtractor over the module, verifies the result, and prints the
// transformed IR to stdout. Loops are extracted into functions whose bodies are
// replaced with non-deterministic (verifier.nondet.*) stubs.
//===----------------------------------------------------------------------===//

#include "llvm_seahorn/Transforms/IPO.h"

#include "llvm/Pass.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/InitializePasses.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

int main(int argc, char **argv) {
  if (argc < 2) {
    errs() << "usage: " << argv[0] << " <input.ll>\n";
    return 1;
  }

  LLVMContext Ctx;
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseIRFile(argv[1], Err, Ctx);
  if (!M) {
    Err.print(argv[0], errs());
    return 1;
  }

  // The legacy PassManager schedules the pass's required analyses
  // (BreakCriticalEdges / DominatorTree / LoopInfo / LoopSimplify), so the
  // relevant pass groups must be registered.
  PassRegistry &R = *PassRegistry::getPassRegistry();
  initializeCore(R);
  initializeAnalysis(R);
  initializeTransformUtils(R);
  initializeScalarOpts(R);
  initializeIPO(R);

  legacy::PassManager PM;
  PM.add(llvm_seahorn::createSeaLoopExtractorPass());
  PM.run(*M);

  if (verifyModule(*M, &errs())) {
    errs() << "error: SeaLoopExtractor produced invalid IR\n";
    return 2;
  }

  M->print(outs(), nullptr);
  return 0;
}
