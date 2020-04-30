add_llvm_library(SeaLoops DISABLE_LLVM_LINK_LLVM_DYLIB
  IndVarSimplify.cc
  FakeLatchExit.cc
  SeaSCEVUtils.cc
)
