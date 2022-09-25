set(LLVM_LINK_COMPONENTS
  AllTargetsAsmParsers
  AllTargetsCodeGens
  AllTargetsDescs
  AllTargetsInfos
  AggressiveInstCombine
  Analysis
  AsmParser
  BitWriter
  CodeGen
  Core
  Coroutines
  Extensions
  IPO
  IRReader
  InstCombine
  Instrumentation
  MC
  ObjCARCOpts
  Remarks
  ScalarOpts
  Support
  Target
  TransformUtils
  Vectorize
  Passes
  )

add_llvm_executable(seaopt
  AnalysisWrappers.cpp
  BreakpointPrinter.cpp
  GraphPrinters.cpp
  NewPMDriver.cpp
  PassPrinters.cpp
  PrintSCC.cpp
  opt.cpp

  # linking dynamic llvm library messes library include order
  DISABLE_LLVM_LINK_LLVM_DYLIB

  DEPENDS
  intrinsics_gen
  SUPPORT_PLUGINS
  )
export_executable_symbols_for_plugins(seaopt)

# insert our libraries at the front
target_link_libraries(seaopt PRIVATE SeaLlvmIpo SeaInstCombine SeaLoops)

install(TARGETS seaopt RUNTIME DESTINATION bin)

if(LLVM_BUILD_EXAMPLES)
    target_link_libraries(seaopt PRIVATE ExampleIRTransforms)
endif(LLVM_BUILD_EXAMPLES)
