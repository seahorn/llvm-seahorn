namespace {
struct CallGraphPrinter : public ModulePass {
  static char ID; // Pass ID, replacement for typeid
  CallGraphPrinter() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequiredTransitive<CallGraphWrapperPass>();
  }
  bool runOnModule(Module &M) override {
    getAnalysis<CallGraphWrapperPass>().print(errs(), &M);
    return false;
  }
};
} // namespace

char CallGraphPrinter::ID = 0;
static RegisterPass<CallGraphPrinter> P2("sea-print-callgraph",
                                         "Print a call graph");
