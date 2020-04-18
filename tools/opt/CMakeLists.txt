set(LLVM_LINK_COMPONENTS
  ${LLVM_TARGETS_TO_BUILD}
  AgressiveInstCombine
  Analysis
  BitWriter
  CodeGen
  Core
  Coroutines
  IPO
  IRReader
  InstCombine
  Instrumentation
  MC
  ObjCARCOpts
  ScalarOpts
  Support
  Target
  TransformUtils
  Vectorize
  Passes
  )

# Support plugins.
set(LLVM_NO_DEAD_STRIP 1)

add_llvm_executable(seaopt
  AnalysisWrappers.cpp
  CallGraphPrinterWrapper.cc
  BreakpointPrinter.cpp
  Debugify.cpp
  GraphPrinters.cpp
  NewPMDriver.cpp
  PassPrinters.cpp
  PrintSCC.cpp
  opt.cpp

  DEPENDS
  intrinsics_gen
  )
# TODO re-enable when ready
target_link_libraries(seaopt PRIVATE SeaLlvmIpo)
#target_link_libraries (seaopt SeaLlvmIpo SeaInstCombine SeaInstNondet SeaLoops)

set_target_properties(seaopt PROPERTIES ENABLE_EXPORTS 1)
install(TARGETS seaopt RUNTIME DESTINATION bin)



if(WITH_POLLY AND LINK_POLLY_INTO_TOOLS)
  target_link_libraries(opt PRIVATE Polly)
endif(WITH_POLLY AND LINK_POLLY_INTO_TOOLS)
