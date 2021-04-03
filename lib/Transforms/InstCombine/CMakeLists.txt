add_llvm_library(SeaInstCombine DISABLE_LLVM_LINK_LLVM_DYLIB
  InstructionCombining.cpp
  InstCombineAddSub.cpp
  InstCombineAtomicRMW.cpp
  InstCombineAndOrXor.cpp
  InstCombineCalls.cpp
  InstCombineCasts.cpp
  InstCombineCompares.cpp
  InstCombineLoadStoreAlloca.cpp
  InstCombineMulDivRem.cpp
  InstCombineNegator.cpp
  InstCombinePHI.cpp
  InstCombineSelect.cpp
  InstCombineShifts.cpp
  InstCombineSimplifyDemanded.cpp
  InstCombineVectorOps.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms/InstCombine

  DEPENDS
  intrinsics_gen
  )
